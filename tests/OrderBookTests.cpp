#include "lob/OrderBook.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view expression, int line) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

lob::OrderBook make_book() {
    return lob::OrderBook({.max_orders = 256, .min_price = 90, .max_price = 110, .tick_size = 1});
}

class ReferenceBook {
public:
    struct RefOrder {
        lob::OrderId id = 0;
        lob::Side side = lob::Side::Buy;
        lob::Price price = 0;
        lob::Quantity quantity = 0;
    };

    explicit ReferenceBook(lob::BookConfig config) : config_(config) {}

    lob::OrderResult add_limit_order(
        lob::OrderId id,
        lob::Side side,
        lob::Price price,
        lob::Quantity quantity,
        std::vector<lob::Trade>& trades) {
        lob::OrderResult result;
        result.order_id = id;

        if (id == 0 || quantity == 0) {
            result.status = lob::Status::InvalidOrder;
            return result;
        }
        if (!valid_price(price)) {
            result.status = lob::Status::PriceOutOfRange;
            return result;
        }
        if (orders_.contains(id)) {
            result.status = lob::Status::DuplicateOrderId;
            return result;
        }

        auto remaining = quantity;
        match(id, side, price, remaining, trades, result);

        if (remaining > 0) {
            rest_order({id, side, price, remaining});
            result.rested = true;
            result.resting_quantity = remaining;
        }

        result.status = lob::Status::Accepted;
        return result;
    }

    lob::OrderResult add_market_order(
        lob::OrderId id,
        lob::Side side,
        lob::Quantity quantity,
        std::vector<lob::Trade>& trades) {
        lob::OrderResult result;
        result.order_id = id;

        if (id == 0 || quantity == 0) {
            result.status = lob::Status::InvalidOrder;
            return result;
        }
        if (orders_.contains(id)) {
            result.status = lob::Status::DuplicateOrderId;
            return result;
        }

        auto remaining = quantity;
        match(id, side, {}, remaining, trades, result);
        result.status = lob::Status::Accepted;
        return result;
    }

    lob::CancelResult cancel_order(lob::OrderId id) {
        lob::CancelResult result;
        result.order_id = id;

        const auto found = orders_.find(id);
        if (found == orders_.end()) {
            result.status = lob::Status::NotFound;
            return result;
        }

        const RefOrder order = found->second;
        result.side = order.side;
        result.price = order.price;
        result.canceled_quantity = order.quantity;
        erase_order(id);
        result.status = lob::Status::Accepted;
        return result;
    }

    lob::OrderResult modify_order(
        lob::OrderId id,
        lob::Price new_price,
        lob::Quantity new_quantity,
        std::vector<lob::Trade>& trades) {
        lob::OrderResult result;
        result.order_id = id;

        const auto found = orders_.find(id);
        if (found == orders_.end()) {
            result.status = lob::Status::NotFound;
            return result;
        }

        if (new_quantity == 0) {
            const auto cancel = cancel_order(id);
            result.status = cancel.status;
            result.priority_preserved = true;
            return result;
        }

        if (!valid_price(new_price)) {
            result.status = lob::Status::PriceOutOfRange;
            return result;
        }

        const auto current = found->second;
        if (new_price == current.price && new_quantity <= current.quantity) {
            update_order_quantity(id, new_quantity);
            result.status = lob::Status::Accepted;
            result.rested = true;
            result.resting_quantity = new_quantity;
            result.priority_preserved = true;
            return result;
        }

        const auto side = current.side;
        erase_order(id);
        result = add_limit_order(id, side, new_price, new_quantity, trades);
        result.priority_preserved = false;
        return result;
    }

    std::optional<lob::LevelSnapshot> best_bid() const {
        for (const auto& [price, orders] : bids_) {
            if (!orders.empty()) {
                return snapshot(price, orders);
            }
        }
        return std::nullopt;
    }

    std::optional<lob::LevelSnapshot> best_ask() const {
        for (const auto& [price, orders] : asks_) {
            if (!orders.empty()) {
                return snapshot(price, orders);
            }
        }
        return std::nullopt;
    }

    std::vector<lob::LevelSnapshot> depth(lob::Side side, std::size_t max_levels) const {
        std::vector<lob::LevelSnapshot> levels;
        levels.reserve(max_levels);

        if (side == lob::Side::Buy) {
            for (const auto& [price, orders] : bids_) {
                if (!orders.empty()) {
                    levels.push_back(snapshot(price, orders));
                }
                if (levels.size() == max_levels) {
                    break;
                }
            }
            return levels;
        }

        for (const auto& [price, orders] : asks_) {
            if (!orders.empty()) {
                levels.push_back(snapshot(price, orders));
            }
            if (levels.size() == max_levels) {
                break;
            }
        }
        return levels;
    }

    const std::unordered_map<lob::OrderId, RefOrder>& orders() const {
        return orders_;
    }

private:
    using BidMap = std::map<lob::Price, std::deque<RefOrder>, std::greater<>>;
    using AskMap = std::map<lob::Price, std::deque<RefOrder>>;

    bool valid_price(lob::Price price) const {
        return price >= config_.min_price
            && price <= config_.max_price
            && (price - config_.min_price) % config_.tick_size == 0;
    }

    void rest_order(const RefOrder& order) {
        if (order.side == lob::Side::Buy) {
            bids_[order.price].push_back(order);
        } else {
            asks_[order.price].push_back(order);
        }
        orders_[order.id] = order;
    }

    void erase_order(lob::OrderId id) {
        const auto found = orders_.find(id);
        if (found == orders_.end()) {
            return;
        }

        if (found->second.side == lob::Side::Buy) {
            auto level = bids_.find(found->second.price);
            if (level != bids_.end()) {
                erase_from_level(bids_, level, id);
            }
        } else {
            auto level = asks_.find(found->second.price);
            if (level != asks_.end()) {
                erase_from_level(asks_, level, id);
            }
        }
        orders_.erase(found);
    }

    void update_order_quantity(lob::OrderId id, lob::Quantity quantity) {
        auto& order = orders_.at(id);
        auto& queue = order.side == lob::Side::Buy ? bids_.at(order.price) : asks_.at(order.price);
        for (auto& resting : queue) {
            if (resting.id == id) {
                resting.quantity = quantity;
                break;
            }
        }
        order.quantity = quantity;
    }

    template <typename Levels>
    void erase_from_level(Levels& levels, typename Levels::iterator level, lob::OrderId id) {
        auto& queue = level->second;
        queue.erase(std::remove_if(queue.begin(), queue.end(), [id](const RefOrder& order) {
            return order.id == id;
        }), queue.end());
        if (queue.empty()) {
            levels.erase(level);
        }
    }

    static lob::LevelSnapshot snapshot(lob::Price price, const std::deque<RefOrder>& orders) {
        lob::Quantity total = 0;
        for (const auto& order : orders) {
            total += order.quantity;
        }
        return lob::LevelSnapshot{price, total, static_cast<std::uint32_t>(orders.size())};
    }

    template <typename Levels>
    void remove_front(Levels& levels, typename Levels::iterator level, const RefOrder& order) {
        auto& queue = level->second;
        queue.pop_front();
        orders_.erase(order.id);
        if (queue.empty()) {
            levels.erase(level);
        }
    }

    void match(
        lob::OrderId aggressing_id,
        lob::Side aggressor_side,
        std::optional<lob::Price> limit_price,
        lob::Quantity& remaining_quantity,
        std::vector<lob::Trade>& trades,
        lob::OrderResult& result) {
        if (aggressor_side == lob::Side::Buy) {
            while (remaining_quantity > 0 && !asks_.empty()) {
                auto level = asks_.begin();
                if (limit_price.has_value() && level->first > *limit_price) {
                    return;
                }
                fill_front(aggressing_id, aggressor_side, remaining_quantity, trades, result, asks_, level);
            }
            return;
        }

        while (remaining_quantity > 0 && !bids_.empty()) {
            auto level = bids_.begin();
            if (limit_price.has_value() && level->first < *limit_price) {
                return;
            }
            fill_front(aggressing_id, aggressor_side, remaining_quantity, trades, result, bids_, level);
        }
    }

    template <typename Levels>
    void fill_front(
        lob::OrderId aggressing_id,
        lob::Side aggressor_side,
        lob::Quantity& remaining_quantity,
        std::vector<lob::Trade>& trades,
        lob::OrderResult& result,
        Levels& levels,
        typename Levels::iterator level) {
        auto& resting = level->second.front();
        const auto fill_quantity = std::min(remaining_quantity, resting.quantity);

        remaining_quantity -= fill_quantity;
        resting.quantity -= fill_quantity;
        orders_.at(resting.id).quantity = resting.quantity;
        result.filled_quantity += fill_quantity;
        ++result.trade_count;
        trades.push_back(lob::Trade{aggressing_id, resting.id, aggressor_side, level->first, fill_quantity});

        if (resting.quantity == 0) {
            const auto filled_order = resting;
            remove_front(levels, level, filled_order);
        }
    }

    lob::BookConfig config_;
    BidMap bids_;
    AskMap asks_;
    std::unordered_map<lob::OrderId, RefOrder> orders_;
};

void compare_optional_level(
    const std::optional<lob::LevelSnapshot>& actual,
    const std::optional<lob::LevelSnapshot>& expected) {
    CHECK(actual.has_value() == expected.has_value());
    if (!actual.has_value() || !expected.has_value()) {
        return;
    }
    CHECK(actual->price == expected->price);
    CHECK(actual->total_quantity == expected->total_quantity);
    CHECK(actual->order_count == expected->order_count);
}

void compare_result(
    const lob::OrderResult& actual,
    const std::vector<lob::Trade>& actual_trades,
    const lob::OrderResult& expected,
    const std::vector<lob::Trade>& expected_trades) {
    CHECK(actual.status == expected.status);
    CHECK(actual.filled_quantity == expected.filled_quantity);
    CHECK(actual.resting_quantity == expected.resting_quantity);
    CHECK(actual.trade_count == expected.trade_count);
    CHECK(actual.rested == expected.rested);
    CHECK(actual.priority_preserved == expected.priority_preserved);
    CHECK(actual.trade_buffer_exhausted == false);
    CHECK(actual_trades.size() == expected_trades.size());

    const auto count = std::min(actual_trades.size(), expected_trades.size());
    for (std::size_t i = 0; i < count; ++i) {
        CHECK(actual_trades[i].aggressing_order_id == expected_trades[i].aggressing_order_id);
        CHECK(actual_trades[i].resting_order_id == expected_trades[i].resting_order_id);
        CHECK(actual_trades[i].aggressor_side == expected_trades[i].aggressor_side);
        CHECK(actual_trades[i].price == expected_trades[i].price);
        CHECK(actual_trades[i].quantity == expected_trades[i].quantity);
    }
}

void compare_cancel(const lob::CancelResult& actual, const lob::CancelResult& expected) {
    CHECK(actual.status == expected.status);
    CHECK(actual.order_id == expected.order_id);
    if (expected.status == lob::Status::Accepted) {
        CHECK(actual.side == expected.side);
        CHECK(actual.price == expected.price);
        CHECK(actual.canceled_quantity == expected.canceled_quantity);
    }
}

void compare_book_state(const lob::OrderBook& actual, const ReferenceBook& expected) {
    compare_optional_level(actual.best_bid(), expected.best_bid());
    compare_optional_level(actual.best_ask(), expected.best_ask());
    CHECK(actual.active_order_count() == expected.orders().size());

    std::array<lob::LevelSnapshot, 16> actual_depth{};
    const auto expected_bids = expected.depth(lob::Side::Buy, actual_depth.size());
    const auto actual_bid_count = actual.depth(lob::Side::Buy, actual_depth);
    CHECK(actual_bid_count == expected_bids.size());
    for (std::size_t i = 0; i < std::min<std::size_t>(actual_bid_count, expected_bids.size()); ++i) {
        CHECK(actual_depth[i].price == expected_bids[i].price);
        CHECK(actual_depth[i].total_quantity == expected_bids[i].total_quantity);
        CHECK(actual_depth[i].order_count == expected_bids[i].order_count);
    }

    const auto expected_asks = expected.depth(lob::Side::Sell, actual_depth.size());
    const auto actual_ask_count = actual.depth(lob::Side::Sell, actual_depth);
    CHECK(actual_ask_count == expected_asks.size());
    for (std::size_t i = 0; i < std::min<std::size_t>(actual_ask_count, expected_asks.size()); ++i) {
        CHECK(actual_depth[i].price == expected_asks[i].price);
        CHECK(actual_depth[i].total_quantity == expected_asks[i].total_quantity);
        CHECK(actual_depth[i].order_count == expected_asks[i].order_count);
    }

    for (const auto& [id, ref_order] : expected.orders()) {
        const auto actual_order = actual.get_order(id);
        CHECK(actual_order.has_value());
        if (actual_order.has_value()) {
            CHECK(actual_order->id == ref_order.id);
            CHECK(actual_order->side == ref_order.side);
            CHECK(actual_order->price == ref_order.price);
            CHECK(actual_order->quantity == ref_order.quantity);
        }
    }
}

void fifo_priority_at_same_price() {
    auto book = make_book();
    std::array<lob::Trade, 4> trades{};

    CHECK(book.add_limit_order(1, lob::Side::Sell, 100, 10, trades).status == lob::Status::Accepted);
    CHECK(book.add_limit_order(2, lob::Side::Sell, 100, 10, trades).status == lob::Status::Accepted);

    const auto result = book.add_market_order(3, lob::Side::Buy, 15, trades);
    CHECK(result.status == lob::Status::Accepted);
    CHECK(result.filled_quantity == 15);
    CHECK(result.trade_count == 2);
    CHECK(trades[0].resting_order_id == 1);
    CHECK(trades[0].quantity == 10);
    CHECK(trades[1].resting_order_id == 2);
    CHECK(trades[1].quantity == 5);

    const auto remaining = book.get_order(2);
    CHECK(remaining.has_value());
    CHECK(remaining->quantity == 5);
}

void price_priority_across_levels() {
    auto book = make_book();
    std::array<lob::Trade, 4> trades{};

    CHECK(book.add_limit_order(1, lob::Side::Sell, 101, 10, trades).status == lob::Status::Accepted);
    CHECK(book.add_limit_order(2, lob::Side::Sell, 100, 10, trades).status == lob::Status::Accepted);

    const auto result = book.add_market_order(3, lob::Side::Buy, 15, trades);
    CHECK(result.trade_count == 2);
    CHECK(trades[0].price == 100);
    CHECK(trades[0].resting_order_id == 2);
    CHECK(trades[1].price == 101);
    CHECK(trades[1].resting_order_id == 1);
}

void partial_fill_leaves_resting_quantity() {
    auto book = make_book();
    std::array<lob::Trade, 4> trades{};

    CHECK(book.add_limit_order(1, lob::Side::Sell, 100, 10, trades).status == lob::Status::Accepted);
    const auto result = book.add_limit_order(2, lob::Side::Buy, 100, 4, trades);

    CHECK(result.filled_quantity == 4);
    CHECK(result.resting_quantity == 0);
    CHECK(result.trade_count == 1);
    CHECK(book.get_order(1)->quantity == 6);
    CHECK(book.best_ask()->total_quantity == 6);
}

void full_fill_removes_order() {
    auto book = make_book();
    std::array<lob::Trade, 4> trades{};

    CHECK(book.add_limit_order(1, lob::Side::Sell, 100, 10, trades).status == lob::Status::Accepted);
    const auto result = book.add_limit_order(2, lob::Side::Buy, 100, 10, trades);

    CHECK(result.filled_quantity == 10);
    CHECK(!book.get_order(1).has_value());
    CHECK(!book.best_ask().has_value());
    CHECK(book.active_order_count() == 0);
}

void cancel_existing_and_missing_order() {
    auto book = make_book();
    std::array<lob::Trade, 4> trades{};

    CHECK(book.add_limit_order(1, lob::Side::Buy, 99, 25, trades).status == lob::Status::Accepted);
    const auto canceled = book.cancel_order(1);
    CHECK(canceled.status == lob::Status::Accepted);
    CHECK(canceled.canceled_quantity == 25);
    CHECK(!book.get_order(1).has_value());
    CHECK(book.cancel_order(1).status == lob::Status::NotFound);
    CHECK(book.cancel_order(42).status == lob::Status::NotFound);
}

void modify_reduce_preserves_priority() {
    auto book = make_book();
    std::array<lob::Trade, 4> trades{};

    CHECK(book.add_limit_order(1, lob::Side::Buy, 100, 10, trades).status == lob::Status::Accepted);
    CHECK(book.add_limit_order(2, lob::Side::Buy, 100, 10, trades).status == lob::Status::Accepted);

    const auto modified = book.modify_order(1, 100, 5, trades);
    CHECK(modified.status == lob::Status::Accepted);
    CHECK(modified.priority_preserved);
    CHECK(book.get_order(1)->quantity == 5);

    const auto result = book.add_market_order(3, lob::Side::Sell, 6, trades);
    CHECK(result.trade_count == 2);
    CHECK(trades[0].resting_order_id == 1);
    CHECK(trades[0].quantity == 5);
    CHECK(trades[1].resting_order_id == 2);
    CHECK(trades[1].quantity == 1);
}

void modify_increase_cancel_replaces_at_tail() {
    auto book = make_book();
    std::array<lob::Trade, 4> trades{};

    CHECK(book.add_limit_order(1, lob::Side::Buy, 100, 5, trades).status == lob::Status::Accepted);
    CHECK(book.add_limit_order(2, lob::Side::Buy, 100, 5, trades).status == lob::Status::Accepted);

    const auto modified = book.modify_order(1, 100, 10, trades);
    CHECK(modified.status == lob::Status::Accepted);
    CHECK(!modified.priority_preserved);

    const auto result = book.add_market_order(3, lob::Side::Sell, 6, trades);
    CHECK(result.trade_count == 2);
    CHECK(trades[0].resting_order_id == 2);
    CHECK(trades[0].quantity == 5);
    CHECK(trades[1].resting_order_id == 1);
    CHECK(trades[1].quantity == 1);
}

void top_of_book_after_adds_cancels_and_fills() {
    auto book = make_book();
    std::array<lob::Trade, 4> trades{};

    CHECK(book.add_limit_order(1, lob::Side::Buy, 99, 10, trades).status == lob::Status::Accepted);
    CHECK(book.add_limit_order(2, lob::Side::Buy, 100, 10, trades).status == lob::Status::Accepted);
    CHECK(book.add_limit_order(3, lob::Side::Sell, 102, 7, trades).status == lob::Status::Accepted);
    CHECK(book.add_limit_order(4, lob::Side::Sell, 101, 7, trades).status == lob::Status::Accepted);

    auto top = book.top_of_book();
    CHECK(top.bid->price == 100);
    CHECK(top.ask->price == 101);

    CHECK(book.cancel_order(2).status == lob::Status::Accepted);
    top = book.top_of_book();
    CHECK(top.bid->price == 99);
    CHECK(top.ask->price == 101);

    CHECK(book.add_market_order(5, lob::Side::Buy, 7, trades).status == lob::Status::Accepted);
    top = book.top_of_book();
    CHECK(top.ask->price == 102);
}

void depth_query_returns_best_levels_in_order() {
    auto book = make_book();
    std::array<lob::Trade, 4> trades{};
    std::array<lob::LevelSnapshot, 4> depth{};

    CHECK(book.add_limit_order(1, lob::Side::Buy, 98, 10, trades).status == lob::Status::Accepted);
    CHECK(book.add_limit_order(2, lob::Side::Buy, 100, 5, trades).status == lob::Status::Accepted);
    CHECK(book.add_limit_order(3, lob::Side::Buy, 99, 7, trades).status == lob::Status::Accepted);
    CHECK(book.add_limit_order(4, lob::Side::Sell, 103, 8, trades).status == lob::Status::Accepted);
    CHECK(book.add_limit_order(5, lob::Side::Sell, 101, 6, trades).status == lob::Status::Accepted);

    const auto bid_count = book.depth(lob::Side::Buy, depth);
    CHECK(bid_count == 3);
    CHECK(depth[0].price == 100);
    CHECK(depth[1].price == 99);
    CHECK(depth[2].price == 98);

    const auto ask_count = book.depth(lob::Side::Sell, depth);
    CHECK(ask_count == 2);
    CHECK(depth[0].price == 101);
    CHECK(depth[1].price == 103);
}

void empty_book_edge_cases() {
    auto book = make_book();
    std::array<lob::Trade, 4> trades{};
    std::array<lob::LevelSnapshot, 4> depth{};

    CHECK(!book.best_bid().has_value());
    CHECK(!book.best_ask().has_value());
    CHECK(!book.top_of_book().bid.has_value());
    CHECK(!book.top_of_book().ask.has_value());
    CHECK(book.depth(lob::Side::Buy, depth) == 0);
    CHECK(book.depth(lob::Side::Sell, depth) == 0);
    CHECK(book.cancel_order(123).status == lob::Status::NotFound);

    const auto market = book.add_market_order(1, lob::Side::Buy, 10, trades);
    CHECK(market.status == lob::Status::Accepted);
    CHECK(market.filled_quantity == 0);
    CHECK(market.trade_count == 0);
}

void rejects_invalid_duplicate_and_out_of_range_orders() {
    auto book = make_book();
    std::array<lob::Trade, 4> trades{};

    CHECK(book.add_limit_order(0, lob::Side::Buy, 100, 1, trades).status == lob::Status::InvalidOrder);
    CHECK(book.add_limit_order(1, lob::Side::Buy, 111, 1, trades).status == lob::Status::PriceOutOfRange);
    CHECK(book.add_limit_order(1, lob::Side::Buy, 100, 1, trades).status == lob::Status::Accepted);
    CHECK(book.add_limit_order(1, lob::Side::Buy, 100, 1, trades).status == lob::Status::DuplicateOrderId);
}

std::vector<lob::Trade> captured_trades(const std::array<lob::Trade, 128>& buffer, const lob::OrderResult& result) {
    std::vector<lob::Trade> trades;
    trades.reserve(result.trade_count);
    for (std::size_t i = 0; i < result.trade_count && i < buffer.size(); ++i) {
        trades.push_back(buffer[i]);
    }
    return trades;
}

std::vector<lob::OrderId> active_ids(const ReferenceBook& book) {
    std::vector<lob::OrderId> ids;
    ids.reserve(book.orders().size());
    for (const auto& [id, _] : book.orders()) {
        ids.push_back(id);
    }
    return ids;
}

lob::Side random_side(std::mt19937_64& rng) {
    return (rng() & 1U) == 0 ? lob::Side::Buy : lob::Side::Sell;
}

void randomized_differential_test() {
    constexpr std::array<std::uint64_t, 5> seeds{7, 19, 101, 1'337, 20'270'607};
    constexpr int operations_per_seed = 2'000;
    const lob::BookConfig config{.max_orders = 20'000, .min_price = 95, .max_price = 105, .tick_size = 1};

    for (const auto seed : seeds) {
        lob::OrderBook actual(config);
        ReferenceBook expected(config);
        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<int> op_dist(0, 99);
        std::uniform_int_distribution<int> price_dist(94, 106);
        std::uniform_int_distribution<int> quantity_dist(0, 20);
        lob::OrderId next_id = 1;

        for (int op_index = 0; op_index < operations_per_seed; ++op_index) {
            const auto before_failures = failures;
            std::array<lob::Trade, 128> actual_buffer{};
            std::vector<lob::Trade> expected_trades;
            expected_trades.reserve(actual_buffer.size());

            auto ids = active_ids(expected);
            const int op = ids.empty() ? 0 : op_dist(rng);

            if (op < 45) {
                const bool duplicate_active_id = !ids.empty() && op_dist(rng) < 8;
                const auto id = duplicate_active_id ? ids[static_cast<std::size_t>(rng() % ids.size())] : next_id++;
                const auto side = random_side(rng);
                const auto price = static_cast<lob::Price>(price_dist(rng));
                const auto quantity = static_cast<lob::Quantity>(quantity_dist(rng));

                const auto actual_result = actual.add_limit_order(id, side, price, quantity, actual_buffer);
                const auto expected_result = expected.add_limit_order(id, side, price, quantity, expected_trades);
                compare_result(actual_result, captured_trades(actual_buffer, actual_result), expected_result, expected_trades);
            } else if (op < 65) {
                const bool duplicate_active_id = !ids.empty() && op_dist(rng) < 5;
                const auto id = duplicate_active_id ? ids[static_cast<std::size_t>(rng() % ids.size())] : next_id++;
                const auto side = random_side(rng);
                const auto quantity = static_cast<lob::Quantity>(quantity_dist(rng));

                const auto actual_result = actual.add_market_order(id, side, quantity, actual_buffer);
                const auto expected_result = expected.add_market_order(id, side, quantity, expected_trades);
                compare_result(actual_result, captured_trades(actual_buffer, actual_result), expected_result, expected_trades);
            } else if (op < 82) {
                const bool cancel_existing = !ids.empty() && op_dist(rng) < 80;
                const auto id = cancel_existing ? ids[static_cast<std::size_t>(rng() % ids.size())] : next_id + static_cast<lob::OrderId>(rng() % 1'000);

                const auto actual_result = actual.cancel_order(id);
                const auto expected_result = expected.cancel_order(id);
                compare_cancel(actual_result, expected_result);
            } else {
                const bool modify_existing = !ids.empty() && op_dist(rng) < 85;
                const auto id = modify_existing ? ids[static_cast<std::size_t>(rng() % ids.size())] : next_id + static_cast<lob::OrderId>(rng() % 1'000);
                const auto price = static_cast<lob::Price>(price_dist(rng));
                const auto quantity = static_cast<lob::Quantity>(quantity_dist(rng));

                const auto actual_result = actual.modify_order(id, price, quantity, actual_buffer);
                const auto expected_result = expected.modify_order(id, price, quantity, expected_trades);
                compare_result(actual_result, captured_trades(actual_buffer, actual_result), expected_result, expected_trades);
            }

            compare_book_state(actual, expected);
            if (failures != before_failures) {
                std::cerr << "Randomized differential test failed at seed=" << seed
                          << " op=" << op_index << '\n';
                return;
            }
        }
    }
}

} // namespace

int main() {
    fifo_priority_at_same_price();
    price_priority_across_levels();
    partial_fill_leaves_resting_quantity();
    full_fill_removes_order();
    cancel_existing_and_missing_order();
    modify_reduce_preserves_priority();
    modify_increase_cancel_replaces_at_tail();
    top_of_book_after_adds_cancels_and_fills();
    depth_query_returns_best_levels_in_order();
    empty_book_edge_cases();
    rejects_invalid_duplicate_and_out_of_range_orders();
    randomized_differential_test();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All order book tests passed\n";
    return EXIT_SUCCESS;
}
