#include "lob/OrderBook.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace lob {

namespace {

std::size_t next_power_of_two(std::size_t value) {
    std::size_t result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

} // namespace

OrderBook::FixedOrderIndex::FixedOrderIndex(std::size_t max_entries)
    : table_(next_power_of_two(std::max<std::size_t>(2, max_entries * 2))),
      max_entries_(max_entries),
      mask_(table_.size() - 1) {}

std::uint64_t OrderBook::FixedOrderIndex::hash(OrderId id) noexcept {
    std::uint64_t x = id + 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

bool OrderBook::FixedOrderIndex::insert(OrderId id, std::int32_t index) {
    if (size_ >= max_entries_) {
        return false;
    }

    std::size_t first_tombstone = table_.size();
    const std::size_t start = hash(id) & mask_;

    for (std::size_t probe = 0; probe < table_.size(); ++probe) {
        Slot& slot = table_[(start + probe) & mask_];
        if (slot.state == SlotState::Occupied) {
            if (slot.id == id) {
                return false;
            }
            continue;
        }

        if (slot.state == SlotState::Tombstone) {
            if (first_tombstone == table_.size()) {
                first_tombstone = (start + probe) & mask_;
            }
            continue;
        }

        Slot& target = first_tombstone == table_.size() ? slot : table_[first_tombstone];
        target.id = id;
        target.index = index;
        target.state = SlotState::Occupied;
        ++size_;
        return true;
    }

    if (first_tombstone != table_.size()) {
        Slot& target = table_[first_tombstone];
        target.id = id;
        target.index = index;
        target.state = SlotState::Occupied;
        ++size_;
        return true;
    }

    return false;
}

std::int32_t OrderBook::FixedOrderIndex::find(OrderId id) const {
    const std::size_t start = hash(id) & mask_;
    for (std::size_t probe = 0; probe < table_.size(); ++probe) {
        const Slot& slot = table_[(start + probe) & mask_];
        if (slot.state == SlotState::Empty) {
            return npos;
        }
        if (slot.state == SlotState::Occupied && slot.id == id) {
            return slot.index;
        }
    }
    return npos;
}

bool OrderBook::FixedOrderIndex::erase(OrderId id) {
    const std::size_t start = hash(id) & mask_;
    for (std::size_t probe = 0; probe < table_.size(); ++probe) {
        Slot& slot = table_[(start + probe) & mask_];
        if (slot.state == SlotState::Empty) {
            return false;
        }
        if (slot.state == SlotState::Occupied && slot.id == id) {
            slot.state = SlotState::Tombstone;
            slot.index = npos;
            --size_;
            return true;
        }
    }
    return false;
}

std::size_t OrderBook::FixedOrderIndex::size() const noexcept {
    return size_;
}

OrderBook::OrderBook(BookConfig config)
    : config_(config),
      order_index_(config.max_orders) {
    if (config_.max_orders == 0 || config_.tick_size <= 0 || config_.min_price > config_.max_price) {
        throw std::invalid_argument("invalid order book configuration");
    }

    const auto range = static_cast<std::uint64_t>((config_.max_price - config_.min_price) / config_.tick_size) + 1;
    if (range > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::invalid_argument("price range is too large for indexed levels");
    }

    bid_levels_.resize(static_cast<std::size_t>(range));
    ask_levels_.resize(static_cast<std::size_t>(range));
    orders_.resize(config_.max_orders);
    free_slots_.reserve(config_.max_orders);
    for (std::int32_t i = static_cast<std::int32_t>(config_.max_orders) - 1; i >= 0; --i) {
        free_slots_.push_back(i);
    }
}

OrderResult OrderBook::add_limit_order(
    OrderId id,
    Side side,
    Price price,
    Quantity quantity,
    std::span<Trade> trades) {
    OrderResult result;
    result.order_id = id;

    if (id == 0 || quantity == 0) {
        result.status = Status::InvalidOrder;
        return result;
    }
    if (!price_to_index(price).has_value()) {
        result.status = Status::PriceOutOfRange;
        return result;
    }
    if (order_index_.find(id) != npos) {
        result.status = Status::DuplicateOrderId;
        return result;
    }

    Quantity remaining = quantity;
    match(id, side, price, remaining, trades, result);

    if (remaining > 0) {
        if (!rest_order(id, side, price, remaining)) {
            result.status = Status::BookFull;
            return result;
        }
        result.rested = true;
        result.resting_quantity = remaining;
    }

    result.status = Status::Accepted;
    return result;
}

OrderResult OrderBook::add_market_order(
    OrderId id,
    Side side,
    Quantity quantity,
    std::span<Trade> trades) {
    OrderResult result;
    result.order_id = id;

    if (id == 0 || quantity == 0) {
        result.status = Status::InvalidOrder;
        return result;
    }
    if (order_index_.find(id) != npos) {
        result.status = Status::DuplicateOrderId;
        return result;
    }

    Quantity remaining = quantity;
    match(id, side, std::nullopt, remaining, trades, result);
    result.status = Status::Accepted;
    return result;
}

CancelResult OrderBook::cancel_order(OrderId id) {
    CancelResult result;
    result.order_id = id;

    const std::int32_t index = order_index_.find(id);
    if (index == npos) {
        result.status = Status::NotFound;
        return result;
    }

    const OrderNode node = orders_[index];
    result.side = node.side;
    result.price = node.price;
    result.canceled_quantity = node.quantity;

    detach_order(index);
    [[maybe_unused]] const bool erased = order_index_.erase(id);
    release_slot(index);
    --active_orders_;

    result.status = Status::Accepted;
    return result;
}

OrderResult OrderBook::modify_order(
    OrderId id,
    Price new_price,
    Quantity new_quantity,
    std::span<Trade> trades) {
    OrderResult result;
    result.order_id = id;

    const std::int32_t index = order_index_.find(id);
    if (index == npos) {
        result.status = Status::NotFound;
        return result;
    }

    if (new_quantity == 0) {
        const CancelResult cancel = cancel_order(id);
        result.status = cancel.status;
        result.priority_preserved = true;
        return result;
    }

    if (!price_to_index(new_price).has_value()) {
        result.status = Status::PriceOutOfRange;
        return result;
    }

    OrderNode& node = orders_[index];
    if (new_price == node.price && new_quantity <= node.quantity) {
        auto& level = levels(node.side)[*price_to_index(node.price)];
        level.total_quantity -= node.quantity - new_quantity;
        node.quantity = new_quantity;
        result.status = Status::Accepted;
        result.rested = true;
        result.resting_quantity = new_quantity;
        result.priority_preserved = true;
        return result;
    }

    const Side side = node.side;
    const CancelResult cancel = cancel_order(id);
    if (cancel.status != Status::Accepted) {
        result.status = cancel.status;
        return result;
    }

    result = add_limit_order(id, side, new_price, new_quantity, trades);
    result.priority_preserved = false;
    return result;
}

TopOfBook OrderBook::top_of_book() const {
    return TopOfBook{best_bid(), best_ask()};
}

std::optional<LevelSnapshot> OrderBook::best_bid() const {
    if (best_bid_ == npos) {
        return std::nullopt;
    }
    const PriceLevel& level = bid_levels_[static_cast<std::size_t>(best_bid_)];
    return LevelSnapshot{index_to_price(static_cast<std::size_t>(best_bid_)), level.total_quantity, level.order_count};
}

std::optional<LevelSnapshot> OrderBook::best_ask() const {
    if (best_ask_ == npos) {
        return std::nullopt;
    }
    const PriceLevel& level = ask_levels_[static_cast<std::size_t>(best_ask_)];
    return LevelSnapshot{index_to_price(static_cast<std::size_t>(best_ask_)), level.total_quantity, level.order_count};
}

std::size_t OrderBook::depth(Side side, std::span<LevelSnapshot> out) const {
    const auto& book_levels = levels(side);
    std::size_t written = 0;

    if (side == Side::Buy) {
        for (std::int32_t i = best_bid_; i >= 0 && written < out.size(); --i) {
            const PriceLevel& level = book_levels[static_cast<std::size_t>(i)];
            if (level.order_count > 0) {
                out[written++] = LevelSnapshot{index_to_price(static_cast<std::size_t>(i)), level.total_quantity, level.order_count};
            }
        }
        return written;
    }

    for (std::int32_t i = best_ask_; i != npos && i < static_cast<std::int32_t>(book_levels.size()) && written < out.size(); ++i) {
        const PriceLevel& level = book_levels[static_cast<std::size_t>(i)];
        if (level.order_count > 0) {
            out[written++] = LevelSnapshot{index_to_price(static_cast<std::size_t>(i)), level.total_quantity, level.order_count};
        }
    }
    return written;
}

std::optional<OrderView> OrderBook::get_order(OrderId id) const {
    const std::int32_t index = order_index_.find(id);
    if (index == npos) {
        return std::nullopt;
    }
    const OrderNode& node = orders_[index];
    return OrderView{node.id, node.side, node.price, node.quantity};
}

std::size_t OrderBook::active_order_count() const noexcept {
    return active_orders_;
}

const BookConfig& OrderBook::config() const noexcept {
    return config_;
}

std::optional<std::size_t> OrderBook::price_to_index(Price price) const {
    if (price < config_.min_price || price > config_.max_price) {
        return std::nullopt;
    }
    const Price offset = price - config_.min_price;
    if (offset % config_.tick_size != 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(offset / config_.tick_size);
}

Price OrderBook::index_to_price(std::size_t index) const {
    return config_.min_price + static_cast<Price>(index) * config_.tick_size;
}

std::vector<OrderBook::PriceLevel>& OrderBook::levels(Side side) {
    return side == Side::Buy ? bid_levels_ : ask_levels_;
}

const std::vector<OrderBook::PriceLevel>& OrderBook::levels(Side side) const {
    return side == Side::Buy ? bid_levels_ : ask_levels_;
}

bool OrderBook::crosses(Side side, Price limit_price) const {
    if (side == Side::Buy) {
        return best_ask_ != npos && index_to_price(static_cast<std::size_t>(best_ask_)) <= limit_price;
    }
    return best_bid_ != npos && index_to_price(static_cast<std::size_t>(best_bid_)) >= limit_price;
}

std::int32_t OrderBook::best_index(Side side) const {
    return side == Side::Buy ? best_bid_ : best_ask_;
}

void OrderBook::set_best_index(Side side, std::int32_t index) {
    if (side == Side::Buy) {
        best_bid_ = index;
    } else {
        best_ask_ = index;
    }
}

void OrderBook::update_best_after_add(Side side, std::size_t index) {
    const auto signed_index = static_cast<std::int32_t>(index);
    if (side == Side::Buy) {
        if (best_bid_ == npos || signed_index > best_bid_) {
            best_bid_ = signed_index;
        }
        return;
    }

    if (best_ask_ == npos || signed_index < best_ask_) {
        best_ask_ = signed_index;
    }
}

void OrderBook::refresh_best_from(Side side, std::int32_t start_index) {
    const auto& book_levels = levels(side);
    if (book_levels.empty()) {
        set_best_index(side, npos);
        return;
    }

    if (side == Side::Buy) {
        for (std::int32_t i = std::min<std::int32_t>(start_index, static_cast<std::int32_t>(book_levels.size()) - 1); i >= 0; --i) {
            if (book_levels[static_cast<std::size_t>(i)].order_count > 0) {
                best_bid_ = i;
                return;
            }
        }
        best_bid_ = npos;
        return;
    }

    const std::int32_t begin = std::max<std::int32_t>(0, start_index);
    for (std::int32_t i = begin; i < static_cast<std::int32_t>(book_levels.size()); ++i) {
        if (book_levels[static_cast<std::size_t>(i)].order_count > 0) {
            best_ask_ = i;
            return;
        }
    }
    best_ask_ = npos;
}

std::int32_t OrderBook::acquire_slot() {
    if (free_slots_.empty()) {
        return npos;
    }

    const std::int32_t index = free_slots_.back();
    free_slots_.pop_back();
    return index;
}

void OrderBook::release_slot(std::int32_t index) {
    orders_[static_cast<std::size_t>(index)] = OrderNode{};
    free_slots_.push_back(index);
}

bool OrderBook::rest_order(OrderId id, Side side, Price price, Quantity quantity) {
    const std::int32_t order_index = acquire_slot();
    if (order_index == npos) {
        return false;
    }

    const std::optional<std::size_t> price_index = price_to_index(price);
    if (!price_index.has_value()) {
        release_slot(order_index);
        return false;
    }

    OrderNode& node = orders_[static_cast<std::size_t>(order_index)];
    node = OrderNode{id, side, price, quantity, npos, npos, true};

    PriceLevel& level = levels(side)[*price_index];
    if (level.tail != npos) {
        OrderNode& old_tail = orders_[static_cast<std::size_t>(level.tail)];
        old_tail.next = order_index;
        node.prev = level.tail;
    } else {
        level.head = order_index;
    }
    level.tail = order_index;
    level.total_quantity += quantity;
    ++level.order_count;

    if (!order_index_.insert(id, order_index)) {
        detach_order(order_index);
        release_slot(order_index);
        return false;
    }

    ++active_orders_;
    update_best_after_add(side, *price_index);
    return true;
}

void OrderBook::detach_order(std::int32_t index) {
    OrderNode& node = orders_[static_cast<std::size_t>(index)];
    auto& level = levels(node.side)[*price_to_index(node.price)];

    if (node.prev != npos) {
        orders_[static_cast<std::size_t>(node.prev)].next = node.next;
    } else {
        level.head = node.next;
    }

    if (node.next != npos) {
        orders_[static_cast<std::size_t>(node.next)].prev = node.prev;
    } else {
        level.tail = node.prev;
    }

    level.total_quantity -= node.quantity;
    --level.order_count;
    node.active = false;
    node.prev = npos;
    node.next = npos;

    if (level.order_count == 0) {
        const auto removed_index = static_cast<std::int32_t>(*price_to_index(node.price));
        if (node.side == Side::Buy && best_bid_ == removed_index) {
            refresh_best_from(Side::Buy, removed_index - 1);
        } else if (node.side == Side::Sell && best_ask_ == removed_index) {
            refresh_best_from(Side::Sell, removed_index + 1);
        }
    }
}

void OrderBook::emit_trade(const Trade& trade, std::span<Trade> out, OrderResult& result) const {
    if (result.trade_count < out.size()) {
        out[result.trade_count] = trade;
    } else {
        result.trade_buffer_exhausted = true;
    }
    ++result.trade_count;
}

void OrderBook::match(
    OrderId aggressing_id,
    Side aggressor_side,
    std::optional<Price> limit_price,
    Quantity& remaining_quantity,
    std::span<Trade> trades,
    OrderResult& result) {
    const Side resting_side = aggressor_side == Side::Buy ? Side::Sell : Side::Buy;

    while (remaining_quantity > 0 && best_index(resting_side) != npos) {
        const std::int32_t level_index = best_index(resting_side);
        const Price resting_price = index_to_price(static_cast<std::size_t>(level_index));
        if (limit_price.has_value() && !crosses(aggressor_side, *limit_price)) {
            return;
        }

        PriceLevel& level = levels(resting_side)[static_cast<std::size_t>(level_index)];
        const std::int32_t resting_index = level.head;
        OrderNode& resting_order = orders_[static_cast<std::size_t>(resting_index)];
        const Quantity fill_quantity = std::min(remaining_quantity, resting_order.quantity);

        remaining_quantity -= fill_quantity;
        resting_order.quantity -= fill_quantity;
        level.total_quantity -= fill_quantity;
        result.filled_quantity += fill_quantity;

        emit_trade(
            Trade{aggressing_id, resting_order.id, aggressor_side, resting_price, fill_quantity},
            trades,
            result);

        if (resting_order.quantity == 0) {
            const OrderId resting_id = resting_order.id;
            detach_order(resting_index);
            [[maybe_unused]] const bool erased = order_index_.erase(resting_id);
            release_slot(resting_index);
            --active_orders_;
        }
    }
}

const char* to_string(Side side) noexcept {
    return side == Side::Buy ? "buy" : "sell";
}

const char* to_string(Status status) noexcept {
    switch (status) {
    case Status::Accepted:
        return "accepted";
    case Status::NotFound:
        return "not_found";
    case Status::DuplicateOrderId:
        return "duplicate_order_id";
    case Status::InvalidOrder:
        return "invalid_order";
    case Status::PriceOutOfRange:
        return "price_out_of_range";
    case Status::BookFull:
        return "book_full";
    }
    return "unknown";
}

} // namespace lob
