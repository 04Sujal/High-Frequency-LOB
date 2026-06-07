#include "lob/OrderBook.hpp"
#include "lob/SpscQueue.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {

enum class MessageType { AddLimit, Market, Cancel };

struct InboundMessage {
    MessageType type = MessageType::AddLimit;
    lob::OrderId id = 0;
    lob::Side side = lob::Side::Buy;
    lob::Price price = 0;
    lob::Quantity quantity = 0;
};

void spin_push(lob::SpscQueue<InboundMessage, 1024>& queue, const InboundMessage& message) {
    while (!queue.try_push(message)) {
        std::this_thread::yield();
    }
}

} // namespace

int main() {
    lob::SpscQueue<InboundMessage, 1024> queue;
    std::atomic<bool> producer_done{false};
    lob::OrderBook book({.max_orders = 4'096, .min_price = 9'000, .max_price = 11'000, .tick_size = 1});

    std::thread producer([&] {
        spin_push(queue, {MessageType::AddLimit, 1, lob::Side::Sell, 10'010, 100});
        spin_push(queue, {MessageType::AddLimit, 2, lob::Side::Sell, 10'005, 50});
        spin_push(queue, {MessageType::AddLimit, 3, lob::Side::Buy, 9'990, 80});
        spin_push(queue, {MessageType::Market, 4, lob::Side::Buy, 0, 120});
        spin_push(queue, {MessageType::Cancel, 3, lob::Side::Buy, 0, 0});
        producer_done.store(true, std::memory_order_release);
    });

    std::uint64_t accepted = 0;
    std::uint64_t trades_seen = 0;
    std::array<lob::Trade, 16> trades{};
    InboundMessage message;

    while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
        if (!queue.try_pop(message)) {
            std::this_thread::yield();
            continue;
        }

        trades = {};
        if (message.type == MessageType::AddLimit) {
            const auto result = book.add_limit_order(message.id, message.side, message.price, message.quantity, trades);
            accepted += result.status == lob::Status::Accepted ? 1 : 0;
            trades_seen += result.trade_count;
        } else if (message.type == MessageType::Market) {
            const auto result = book.add_market_order(message.id, message.side, message.quantity, trades);
            accepted += result.status == lob::Status::Accepted ? 1 : 0;
            trades_seen += result.trade_count;
        } else {
            const auto result = book.cancel_order(message.id);
            accepted += result.status == lob::Status::Accepted ? 1 : 0;
        }
    }

    producer.join();

    const auto top = book.top_of_book();
    std::cout << "accepted_messages=" << accepted << '\n';
    std::cout << "trades_seen=" << trades_seen << '\n';
    if (top.ask.has_value()) {
        std::cout << "best_ask=" << top.ask->price << " x " << top.ask->total_quantity << '\n';
    }

    return 0;
}
