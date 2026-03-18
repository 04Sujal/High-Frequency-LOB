#include <iostream>
#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <cstdint>

// --- TIER 1 STRUCTURES ---
struct Order {
    uint64_t id;
    int64_t price; // Fixed-point (e.g., $100.25 -> 1002500)
    uint32_t qty;
    bool isBuy;
    bool active;

    Order() : id(0), price(0), qty(0), isBuy(true), active(false) {}
};

class OrderBook {
private:
    static const int MAX_ORDERS = 100000;
    std::vector<Order> pool;
    std::vector<int> freeSlots;

    // Price -> Vector of Indices into 'pool'
    std::map<int64_t, std::vector<int>, std::greater<int64_t>> bids;
    std::map<int64_t, std::vector<int>> asks;

public:
    OrderBook() {
        pool.resize(MAX_ORDERS); // This allocates 3.2MB on the Heap safely
        for (int i = MAX_ORDERS - 1; i >= 0; --i) {
            freeSlots.push_back(i);
        }
    }

    int acquireOrder() {
        if (freeSlots.empty()) return -1;
        int idx = freeSlots.back();
        freeSlots.pop_back();
        return idx;
    }

    void addOrder(uint64_t id, int64_t price, uint32_t qty, bool isBuy) {
        // 1. Try matching first (simplified for this snippet)
        uint32_t remainingQty = qty;

        // 2. If not fully matched, add to pool
        int idx = acquireOrder();
        if (idx != -1) {
            pool[idx].id = id;
            pool[idx].price = price;
            pool[idx].qty = remainingQty;
            pool[idx].isBuy = isBuy;
            pool[idx].active = true;

            if (isBuy) bids[price].push_back(idx);
            else asks[price].push_back(idx);
            
            std::cout << "Added " << (isBuy ? "BUY" : "SELL") << " ID:" << id << " @ " << price << std::endl;
        }
    }

    // High-performance "walk" through the book
    void printTop() {
        if (!bids.empty()) std::cout << "Best Bid: " << bids.begin()->first << std::endl;
        if (!asks.empty()) std::cout << "Best Ask: " << asks.begin()->first << std::endl;
        else std::cout << "No Asks in book." << std::endl;
    }
};

// --- FAST PARSING UTILITY ---
int64_t fastParsePrice(const std::string& s) {
    int64_t res = 0;
    for (char c : s) {
        if (c == '.') continue;
        res = res * 10 + (c - '0');
    }
    return res;
}

int main() {
    std::cout << "--- ENGINE STARTING ---" << std::endl;
    OrderBook lob;
    
    // Simulate incoming normalized data
    lob.addOrder(1, fastParsePrice("100.25"), 100, true);
    lob.addOrder(2, fastParsePrice("100.30"), 50, false);
    
    lob.printTop();
    std::cout << "--- ENGINE SHUTTING DOWN ---" << std::endl;
    return 0;
}