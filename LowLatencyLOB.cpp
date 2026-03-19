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
    static const int64_t MIN_PRICE = 10000;
    static const int64_t MAX_PRICE = 10500;
    static const size_t RANGE = 501;

    // 1. The Memory Pool (The "Rows")
    std::vector<Order> pool; 
    std::vector<int> freeSlots;

    // 2. The Flat Map (The "Price Levels")
    // Each index represents a $0.01 tick
    std::vector<int> bidLevels[RANGE]; 
    std::vector<int> askLevels[RANGE];

    size_t getIndex(int64_t price) {
        if (price < MIN_PRICE || price > MAX_PRICE) return 0; 
        return static_cast<size_t>(price - MIN_PRICE);
    }

public:
    OrderBook() {
        pool.resize(MAX_ORDERS);
        for (int i = MAX_ORDERS - 1; i >= 0; --i) freeSlots.push_back(i);
    }

    // This was missing!
    int acquireOrder() {
        if (freeSlots.empty()) return -1;
        int idx = freeSlots.back();
        freeSlots.pop_back();
        return idx;
    }

    void processOrder(const Order& incoming) {
        int poolIdx = acquireOrder(); 
        if (poolIdx == -1) return; // Pool full!

        pool[poolIdx] = incoming;
        pool[poolIdx].active = true;
        
        size_t priceIdx = getIndex(incoming.price);
        
        if (incoming.isBuy) bidLevels[priceIdx].push_back(poolIdx);
        else askLevels[priceIdx].push_back(poolIdx);

        std::cout << "[Strategy] Price " << incoming.price << " mapped to Index " << priceIdx << std::endl;
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