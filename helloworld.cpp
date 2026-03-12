#include <iostream>
#include <list>
#include <map>
#include <unordered_map>
#include <memory>
#include <vector>

// 1. Fixed-point price (e.g., 150.25 -> 15025)
using Price = int64_t;
using OrderID = uint64_t;

struct Order {
    OrderID id;
    Price price;
    int qty;
    bool isBuy;

    Order(OrderID i, Price p, int q, bool b) 
        : id(i), price(p), qty(q), isBuy(b) {}
};

class OrderBook {
private:
    // Bids: Sorted Descending (High to Low)
    std::map<Price, std::list<std::unique_ptr<Order>>, std::greater<Price>> bids;
    // Asks: Sorted Ascending (Low to High)
    std::map<Price, std::list<std::unique_ptr<Order>>> asks;
    
    // Quick Lookup: Map ID to the iterator in the specific list
    using OrderIter = std::list<std::unique_ptr<Order>>::iterator;
    std::unordered_map<OrderID, OrderIter> orderLookup;

public:
    void addOrder(OrderID id, Price price, int qty, bool isBuy) {
        if (isBuy) {
            auto& level = bids[price];
            level.push_back(std::make_unique<Order>(id, price, qty, isBuy));
            orderLookup[id] = std::prev(level.end());
            std::cout << "[LOB] Added Bid " << id << ": " << qty << " @ " << price << "\n";
        } else {
            auto& level = asks[price];
            level.push_back(std::make_unique<Order>(id, price, qty, isBuy));
            orderLookup[id] = std::prev(level.end());
            std::cout << "[LOB] Added Ask " << id << ": " << qty << " @ " << price << "\n";
        }
    }

    void cancelOrder(OrderID id) {
        auto it = orderLookup.find(id);
        if (it == orderLookup.end()) return;

        OrderIter listIt = it->second;
        Price p = (*listIt)->price;
        bool isBuy = (*listIt)->isBuy;

        if (isBuy) bids[p].erase(listIt);
        else asks[p].erase(listIt);

        orderLookup.erase(it);
        std::cout << "[LOB] Canceled " << id << "\n";
    }
};

int main() {
    OrderBook lob;
    lob.addOrder(101, 15025, 100, true);
    lob.addOrder(103, 15030, 200, true);
    lob.cancelOrder(101);
    return 0;
}