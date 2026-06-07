#include "lob/OrderBook.hpp"

#include <array>
#include <iostream>

int main() {
    lob::OrderBook book({.max_orders = 1'024, .min_price = 9'500, .max_price = 10'500, .tick_size = 1});
    std::array<lob::Trade, 8> trades{};

    const auto first_add = book.add_limit_order(1, lob::Side::Sell, 10'010, 100, trades);
    const auto second_add = book.add_limit_order(2, lob::Side::Sell, 10'005, 50, trades);
    if (first_add.status != lob::Status::Accepted || second_add.status != lob::Status::Accepted) {
        std::cerr << "failed to seed demo book\n";
        return 1;
    }
    const auto result = book.add_limit_order(3, lob::Side::Buy, 10'010, 120, trades);

    std::cout << "status=" << lob::to_string(result.status)
              << " filled=" << result.filled_quantity
              << " resting=" << result.resting_quantity
              << " trades=" << result.trade_count << '\n';

    for (std::size_t i = 0; i < result.trade_count && i < trades.size(); ++i) {
        const auto& trade = trades[i];
        std::cout << "trade price=" << trade.price
                  << " qty=" << trade.quantity
                  << " resting_id=" << trade.resting_order_id << '\n';
    }

    const auto top = book.top_of_book();
    if (top.bid.has_value()) {
        std::cout << "best_bid=" << top.bid->price << " x " << top.bid->total_quantity << '\n';
    }
    if (top.ask.has_value()) {
        std::cout << "best_ask=" << top.ask->price << " x " << top.ask->total_quantity << '\n';
    }

    return 0;
}
