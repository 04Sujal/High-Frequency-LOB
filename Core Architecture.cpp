#include <iostream>
#include <map>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>

// --- TYPES & STRUCTURES ---
enum class Side : char { BUY = '1', SELL = '2' };

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
        if (matchBids) {
            auto it = bids.begin();
            while (it != bids.end() && qty > 0) {
                if (it->first < price) break; // Bid price too low
                executeAtLevel(it, bids, qty);
                if (it->second.empty()) it = bids.erase(it);
                else ++it;
            }
        } else {
            auto it = asks.begin();
            while (it != asks.end() && qty > 0) {
                if (it->first > price) break; // Ask price too high
                executeAtLevel(it, asks, qty);
                if (it->second.empty()) it = asks.erase(it);
                else ++it;
            }
        }
    }

    // Helper to keep the code clean
    template<typename T>
    void executeAtLevel(T& it, auto& map, int& qty) {
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
    }

    void addOrder(uint64_t id, double price, int qty, bool isBuy) {
        std::cout << (isBuy ? "GATEWAY-BUY " : "GATEWAY-SELL ") << qty << " @ $" << price << " (ID:" << id << ")" << std::endl;
        
        int remainingQty = qty;
        if (isBuy) {
            matchAggressiveOrder(price, remainingQty, false); 
            if (remainingQty > 0) {
                auto& list = bids[price];
                list.push_back(std::make_unique<Order>(id, price, remainingQty, isBuy));
                orderLookup[id] = std::prev(list.end());
            }
        } else {
            matchAggressiveOrder(price, remainingQty, true); 
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

// --- SIMULATION / EVENT LOOP ---
int main() {
    OrderBook lob;

    // Simulated "Network Feed" of strings (FIX-style)
    std::vector<std::string> rawPrices = {"100.10", "100.20", "99.90", "100.15"};
    std::vector<std::string> rawSides = {"2", "2", "1", "1"}; // 1=Buy, 2=Sell
    std::vector<int> rawQtys = {50, 100, 50, 60};

    std::cout << "--- STARTING HFT MATCHING ENGINE & GATEWAY ---" << std::endl;

    for (size_t i = 0; i < rawPrices.size(); ++i) {
        // --- PARSING STAGE ---
        double p = std::stod(rawPrices[i]);
        bool isBuy = (rawSides[i] == "1");
        
        // --- EXECUTION STAGE ---
        lob.addOrder(i + 100, p, rawQtys[i], isBuy);
        
        std::cout << "Current Mid-Price: $" << lob.getMidPrice() << "\n" << std::endl;
    }

    std::cout << "--- SIMULATION COMPLETE ---" << std::endl;
    return 0;
}