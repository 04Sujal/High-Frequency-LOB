#include <iostream>
#include <vector>
#include <map>
#include <atomic>
#include <thread>
#include <windows.h> // For CPU Affinity
#include <cstdint>

// --- STRUCTURES ---
struct Order {
    uint64_t id;
    int64_t price;
    uint32_t qty;
    bool isBuy;
    bool active;
    Order() : id(0), price(0), qty(0), isBuy(true), active(false) {}
};

// --- LOCK-FREE SPSC QUEUE ---
template <typename T, size_t Size>
class SPSCQueue {
private:
    T buffer[Size];
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0};
public:
    bool push(const Order& item) {
        size_t h = head.load(std::memory_order_relaxed);
        size_t nextHead = (h + 1) % Size;
        if (nextHead == tail.load(std::memory_order_acquire)) return false;
        buffer[h] = item;
        head.store(nextHead, std::memory_order_release);
        return true;
    }
    bool pop(Order& item) {
        size_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false;
        item = buffer[t];
        tail.store((t + 1) % Size, std::memory_order_release);
        return true;
    }
};

// --- THE ORDERBOOK ENGINE ---
class OrderBook {
private:
    static const int MAX_ORDERS = 100000;
    std::vector<Order> pool; 
    std::vector<int> freeSlots;
    
    // Still using Map for now (We will Flatten this in the next Phase!)
    std::map<int64_t, std::vector<int>, std::greater<int64_t>> bids;
    std::map<int64_t, std::vector<int>> asks;

public:
    OrderBook() {
        pool.resize(MAX_ORDERS);
        for (int i = MAX_ORDERS - 1; i >= 0; --i) freeSlots.push_back(i);
    }

    void processOrder(const Order& incoming) {
        int idx = freeSlots.back();
        freeSlots.pop_back();
        pool[idx] = incoming;
        pool[idx].active = true;

        if (incoming.isBuy) bids[incoming.price].push_back(idx);
        else asks[incoming.price].push_back(idx);

        std::cout << "[Strategy Thread] Processed ID: " << incoming.id << " @ " << incoming.price << std::endl;
    }
};

// --- GLOBALS FOR CONCURRENCY ---
SPSCQueue<Order, 1024> marketDataQueue;
std::atomic<bool> keepRunning{true};

void set_affinity(int core_id) {
    SetThreadAffinityMask(GetCurrentThread(), (static_cast<DWORD_PTR>(1) << core_id));
}

// --- THREAD 1: THE NETWORK SIMULATOR ---
void exchangeSimulator() {
    set_affinity(1); 
    for(int i = 0; i < 5; ++i) {
        Order o;
        o.id = i + 1; o.price = 10025; o.qty = 10; o.isBuy = true;
        while(!marketDataQueue.push(o)); // Spin-push
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate network gap
    }
    keepRunning = false;
}

// --- THREAD 2: THE MATCHING ENGINE ---
void engineProcess() {
    set_affinity(2);
    OrderBook lob;
    Order incoming;
    while(keepRunning.load() || marketDataQueue.pop(incoming)) {
        if(marketDataQueue.pop(incoming)) {
            lob.processOrder(incoming);
        }
    }
}

int main() {
    std::cout << "--- STARTING CONCURRENT ENGINE ---" << std::endl;
    std::thread netThread(exchangeSimulator);
    std::thread stratThread(engineProcess);

    netThread.join();
    stratThread.join();
    std::cout << "--- ENGINE SHUTDOWN CLEANLY ---" << std::endl;
    return 0;
}