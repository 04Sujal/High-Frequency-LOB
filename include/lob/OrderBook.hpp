#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace lob {

using OrderId = std::uint64_t;
using Price = std::int64_t;
using Quantity = std::uint32_t;

enum class Side { Buy, Sell };

enum class Status {
    Accepted,
    NotFound,
    DuplicateOrderId,
    InvalidOrder,
    PriceOutOfRange,
    BookFull
};

struct BookConfig {
    std::size_t max_orders = 100'000;
    Price min_price = 0;
    Price max_price = 1'000'000;
    Price tick_size = 1;
};

struct Trade {
    OrderId aggressing_order_id = 0;
    OrderId resting_order_id = 0;
    Side aggressor_side = Side::Buy;
    Price price = 0;
    Quantity quantity = 0;
};

struct OrderResult {
    Status status = Status::Accepted;
    OrderId order_id = 0;
    Quantity filled_quantity = 0;
    Quantity resting_quantity = 0;
    std::size_t trade_count = 0;
    bool trade_buffer_exhausted = false;
    bool rested = false;
    bool priority_preserved = false;
};

struct CancelResult {
    Status status = Status::Accepted;
    OrderId order_id = 0;
    Side side = Side::Buy;
    Price price = 0;
    Quantity canceled_quantity = 0;
};

struct LevelSnapshot {
    Price price = 0;
    Quantity total_quantity = 0;
    std::uint32_t order_count = 0;
};

struct TopOfBook {
    std::optional<LevelSnapshot> bid;
    std::optional<LevelSnapshot> ask;
};

struct OrderView {
    OrderId id = 0;
    Side side = Side::Buy;
    Price price = 0;
    Quantity quantity = 0;
};

class OrderBook {
public:
    explicit OrderBook(BookConfig config = {});

    [[nodiscard]] OrderResult add_limit_order(
        OrderId id,
        Side side,
        Price price,
        Quantity quantity,
        std::span<Trade> trades = {});

    [[nodiscard]] OrderResult add_market_order(
        OrderId id,
        Side side,
        Quantity quantity,
        std::span<Trade> trades = {});

    [[nodiscard]] CancelResult cancel_order(OrderId id);

    // Preserves priority only when reducing quantity at the same price.
    // Price changes and quantity increases are implemented as cancel/replace.
    [[nodiscard]] OrderResult modify_order(
        OrderId id,
        Price new_price,
        Quantity new_quantity,
        std::span<Trade> trades = {});

    [[nodiscard]] TopOfBook top_of_book() const;
    [[nodiscard]] std::optional<LevelSnapshot> best_bid() const;
    [[nodiscard]] std::optional<LevelSnapshot> best_ask() const;
    [[nodiscard]] std::size_t depth(Side side, std::span<LevelSnapshot> out) const;
    [[nodiscard]] std::optional<OrderView> get_order(OrderId id) const;
    [[nodiscard]] std::size_t active_order_count() const noexcept;
    [[nodiscard]] const BookConfig& config() const noexcept;

private:
    static constexpr std::int32_t npos = -1;

    struct PriceLevel {
        std::int32_t head = npos;
        std::int32_t tail = npos;
        Quantity total_quantity = 0;
        std::uint32_t order_count = 0;
    };

    struct OrderNode {
        OrderId id = 0;
        Side side = Side::Buy;
        Price price = 0;
        Quantity quantity = 0;
        std::int32_t prev = npos;
        std::int32_t next = npos;
        bool active = false;
    };

    class FixedOrderIndex {
    public:
        explicit FixedOrderIndex(std::size_t max_entries);

        [[nodiscard]] bool insert(OrderId id, std::int32_t index);
        [[nodiscard]] std::int32_t find(OrderId id) const;
        [[nodiscard]] bool erase(OrderId id);
        [[nodiscard]] std::size_t size() const noexcept;

    private:
        enum class SlotState : std::uint8_t { Empty, Occupied, Tombstone };

        struct Slot {
            OrderId id = 0;
            std::int32_t index = npos;
            SlotState state = SlotState::Empty;
        };

        [[nodiscard]] static std::uint64_t hash(OrderId id) noexcept;

        std::vector<Slot> table_;
        std::size_t max_entries_ = 0;
        std::size_t size_ = 0;
        std::size_t mask_ = 0;
    };

    [[nodiscard]] std::optional<std::size_t> price_to_index(Price price) const;
    [[nodiscard]] Price index_to_price(std::size_t index) const;
    [[nodiscard]] std::vector<PriceLevel>& levels(Side side);
    [[nodiscard]] const std::vector<PriceLevel>& levels(Side side) const;
    [[nodiscard]] bool crosses(Side side, Price limit_price) const;
    [[nodiscard]] std::int32_t best_index(Side side) const;
    void set_best_index(Side side, std::int32_t index);
    void update_best_after_add(Side side, std::size_t index);
    void refresh_best_from(Side side, std::int32_t start_index);

    [[nodiscard]] std::int32_t acquire_slot();
    void release_slot(std::int32_t index);
    [[nodiscard]] bool rest_order(OrderId id, Side side, Price price, Quantity quantity);
    void detach_order(std::int32_t index);
    void emit_trade(const Trade& trade, std::span<Trade> out, OrderResult& result) const;
    void match(
        OrderId aggressing_id,
        Side aggressor_side,
        std::optional<Price> limit_price,
        Quantity& remaining_quantity,
        std::span<Trade> trades,
        OrderResult& result);

    BookConfig config_;
    std::vector<PriceLevel> bid_levels_;
    std::vector<PriceLevel> ask_levels_;
    std::vector<OrderNode> orders_;
    std::vector<std::int32_t> free_slots_;
    FixedOrderIndex order_index_;
    std::int32_t best_bid_ = npos;
    std::int32_t best_ask_ = npos;
    std::size_t active_orders_ = 0;
};

[[nodiscard]] const char* to_string(Side side) noexcept;
[[nodiscard]] const char* to_string(Status status) noexcept;

} // namespace lob
