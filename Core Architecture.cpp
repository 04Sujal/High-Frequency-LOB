#include <iostream>
#include <map>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>
#include <algorithm>

// --- STRUCTURES ---
struct Order {
    uint64_t id;
    double price;
    int qty;
    bool isBuy;

    Order(uint64_t _id, double _p, int _q, bool _b) 
        : id(_id), price(_p), qty(_q), isBuy(_b) {}
};

// --- THE ENGINE ---
class OrderBook {
private:
    std::map<double, std::list<std::unique_ptr<Order>>, std::greater<double>> bids;
    std::map<double, std::list<std::unique_ptr<Order>>> asks;
    std::unordered_map<uint64_t, std::list<std::unique_ptr<Order>>::iterator> orderLookup;

    mutable double cachedMidPrice = 0.0;
    mutable bool midPriceDirty = true;

public:
    void matchAggressiveOrder(double price, int& qty, bool matchBids) {
        auto& targetMap = matchBids ? bids : asks;
        auto it = targetMap.begin();

        while (it != targetMap.end() && qty > 0) {
            // Price check: Bids must be >= price, Asks must be <= price
            if ((matchBids && it->first < price) || (!matchBids && it->first > price)) break;

            auto& priceLevel = it->second;
            while (!priceLevel.empty() && qty > 0) {
                auto& restingOrder = priceLevel.front();
                int matchQty = std::min(qty, restingOrder->qty);

                std::cout << "  [MATCH] ID:" << restingOrder->id << " Size:" << matchQty << " @ $" << it->first << std::endl;

                qty -= matchQty;
                restingOrder->qty -= matchQty;

                if (restingOrder->qty == 0) {
                    orderLookup.erase(restingOrder->id);
                    priceLevel.pop_front();
                }
            }
            if (priceLevel.empty()) it = targetMap.erase(it);
            else ++it;
        }
    }

    void addOrder(uint64_t id, double price, int qty, bool isBuy) {
        std::cout << (isBuy ? "BUY " : "SELL ") << qty << " @ $" << price << " (ID:" << id << ")" << std::endl;
        
        int remainingQty = qty;
        if (isBuy) {
            matchAggressiveOrder(price, remainingQty, false); // Match against Asks
            if (remainingQty > 0) {
                auto& list = bids[price];
                list.push_back(std::make_unique<Order>(id, price, remainingQty, isBuy));
                orderLookup[id] = std::prev(list.end());
            }
        } else {
            matchAggressiveOrder(price, remainingQty, true); // Match against Bids
            if (remainingQty > 0) {
                auto& list = asks[price];
                list.push_back(std::make_unique<Order>(id, price, remainingQty, isBuy));
                orderLookup[id] = std::prev(list.end());
            }
        }
        midPriceDirty = true;
    }

    double getMidPrice() const {
        if (midPriceDirty) {
            if (bids.empty() || asks.empty()) cachedMidPrice = 0.0;
            else cachedMidPrice = (bids.begin()->first + asks.begin()->first) / 2.0;
            midPriceDirty = false;
        }
        return cachedMidPrice;
    }
};

// --- SIMULATION ---
int main() {
    OrderBook lob;

    std::cout << "--- STARTING HFT MATCHING ENGINE ---" << std::endl;

    // 1. Add some passive selling pressure
    lob.addOrder(101, 100.10, 50, false);
    lob.addOrder(102, 100.20, 100, false);

    // 2. Add some passive buying pressure
    lob.addOrder(201, 99.90, 50, true);

    std::cout << "Current Mid-Price: $" << lob.getMidPrice() << std::endl;

    // 3. AGGRESSIVE BUY: This should "Cross" the book and trade
    std::cout << "\n--- Incoming Aggressive Buy Order ---" << std::endl;
    lob.addOrder(301, 100.15, 60, true); 

    std::cout << "\nFinal Mid-Price: $" << lob.getMidPrice() << std::endl;
    std::cout << "--- SIMULATION COMPLETE ---" << std::endl;

    return 0;
}