#include "lob/OrderBook.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Request {
    lob::OrderId id = 0;
    lob::Side side = lob::Side::Buy;
    lob::Price price = 0;
    lob::Quantity quantity = 0;
};

struct Summary {
    std::string_view name;
    std::size_t operations = 0;
    double throughput_ops_per_sec = 0.0;
    std::uint64_t median_ns = 0;
    std::uint64_t p99_ns = 0;
};

class ActiveOrderTracker {
public:
    void add(lob::OrderId id) {
        if (positions_.contains(id)) {
            return;
        }
        positions_[id] = ids_.size();
        ids_.push_back(id);
    }

    void remove(lob::OrderId id) {
        const auto found = positions_.find(id);
        if (found == positions_.end()) {
            return;
        }

        const auto pos = found->second;
        const auto moved = ids_.back();
        ids_[pos] = moved;
        positions_[moved] = pos;
        ids_.pop_back();
        positions_.erase(found);
    }

    [[nodiscard]] bool empty() const noexcept {
        return ids_.empty();
    }

    [[nodiscard]] lob::OrderId random_id(std::mt19937_64& rng) const {
        return ids_[static_cast<std::size_t>(rng() % ids_.size())];
    }

private:
    std::vector<lob::OrderId> ids_;
    std::unordered_map<lob::OrderId, std::size_t> positions_;
};

std::uint64_t ns_since(Clock::time_point start, Clock::time_point end) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

Summary summarize(std::string_view name, std::vector<std::uint64_t>& samples, std::uint64_t wall_ns) {
    std::sort(samples.begin(), samples.end());
    const auto median = samples[samples.size() / 2];
    const auto p99 = samples[static_cast<std::size_t>(static_cast<double>(samples.size() - 1) * 0.99)];
    const double seconds = static_cast<double>(wall_ns) / 1'000'000'000.0;
    return Summary{name, samples.size(), static_cast<double>(samples.size()) / seconds, median, p99};
}

Summary benchmark_adds(std::size_t operations) {
    lob::OrderBook book({.max_orders = operations + 16, .min_price = 9'000, .max_price = 11'000, .tick_size = 1});
    std::vector<Request> requests;
    requests.reserve(operations);
    for (std::size_t i = 0; i < operations; ++i) {
        requests.push_back(Request{static_cast<lob::OrderId>(i + 1), lob::Side::Buy, 9'900 - static_cast<lob::Price>(i % 100), 1});
    }

    std::array<lob::Trade, 4> trades{};
    std::vector<std::uint64_t> samples;
    samples.reserve(operations);

    const auto wall_start = Clock::now();
    for (const auto& request : requests) {
        const auto start = Clock::now();
        const auto result = book.add_limit_order(request.id, request.side, request.price, request.quantity, trades);
        const auto end = Clock::now();
        if (result.status != lob::Status::Accepted) {
            std::cerr << "add benchmark rejected order " << request.id << '\n';
            break;
        }
        samples.push_back(ns_since(start, end));
    }
    const auto wall_end = Clock::now();

    return summarize("resting limit add", samples, ns_since(wall_start, wall_end));
}

Summary benchmark_cancels(std::size_t operations) {
    lob::OrderBook book({.max_orders = operations + 16, .min_price = 9'000, .max_price = 11'000, .tick_size = 1});
    std::array<lob::Trade, 4> trades{};
    for (std::size_t i = 0; i < operations; ++i) {
        const auto result = book.add_limit_order(static_cast<lob::OrderId>(i + 1), lob::Side::Buy, 9'900 - static_cast<lob::Price>(i % 100), 1, trades);
        if (result.status != lob::Status::Accepted) {
            std::cerr << "cancel benchmark preload rejected order " << (i + 1) << '\n';
            break;
        }
    }

    std::vector<lob::OrderId> ids(operations);
    std::iota(ids.begin(), ids.end(), 1);
    std::rotate(ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(operations / 3), ids.end());

    std::vector<std::uint64_t> samples;
    samples.reserve(operations);

    const auto wall_start = Clock::now();
    for (const auto id : ids) {
        const auto start = Clock::now();
        const auto result = book.cancel_order(id);
        const auto end = Clock::now();
        if (result.status != lob::Status::Accepted) {
            std::cerr << "cancel benchmark missed order " << id << '\n';
            break;
        }
        samples.push_back(ns_since(start, end));
    }
    const auto wall_end = Clock::now();

    return summarize("cancel resting order", samples, ns_since(wall_start, wall_end));
}

Summary benchmark_matches(std::size_t operations) {
    lob::OrderBook book({.max_orders = operations + 16, .min_price = 9'000, .max_price = 11'000, .tick_size = 1});
    std::array<lob::Trade, 4> trades{};
    for (std::size_t i = 0; i < operations; ++i) {
        const auto result = book.add_limit_order(static_cast<lob::OrderId>(i + 1), lob::Side::Sell, 10'000 + static_cast<lob::Price>(i % 10), 1, trades);
        if (result.status != lob::Status::Accepted) {
            std::cerr << "match benchmark preload rejected order " << (i + 1) << '\n';
            break;
        }
    }

    std::vector<std::uint64_t> samples;
    samples.reserve(operations);

    const auto wall_start = Clock::now();
    for (std::size_t i = 0; i < operations; ++i) {
        const auto start = Clock::now();
        const auto result = book.add_market_order(static_cast<lob::OrderId>(operations + 1 + i), lob::Side::Buy, 1, trades);
        const auto end = Clock::now();
        if (result.status != lob::Status::Accepted || result.filled_quantity != 1) {
            std::cerr << "match benchmark failed at operation " << i << '\n';
            break;
        }
        samples.push_back(ns_since(start, end));
    }
    const auto wall_end = Clock::now();

    return summarize("market order match", samples, ns_since(wall_start, wall_end));
}

void refresh_active_orders(
    lob::OrderBook& book,
    ActiveOrderTracker& active,
    lob::OrderId submitted_id,
    const lob::OrderResult& result,
    const std::array<lob::Trade, 64>& trades) {
    for (std::size_t i = 0; i < result.trade_count && i < trades.size(); ++i) {
        const auto resting_id = trades[i].resting_order_id;
        if (!book.get_order(resting_id).has_value()) {
            active.remove(resting_id);
        }
    }

    if (book.get_order(submitted_id).has_value()) {
        active.add(submitted_id);
    } else {
        active.remove(submitted_id);
    }
}

Summary benchmark_mixed_workload(std::size_t operations) {
    const std::size_t preload_orders = std::max<std::size_t>(1'000, operations / 2);
    lob::OrderBook book({.max_orders = operations + preload_orders + 1'024, .min_price = 9'000, .max_price = 11'000, .tick_size = 1});
    ActiveOrderTracker active;
    std::array<lob::Trade, 64> trades{};
    lob::OrderId next_id = 1;

    for (std::size_t i = 0; i < preload_orders; ++i) {
        const auto side = (i & 1U) == 0 ? lob::Side::Buy : lob::Side::Sell;
        const auto price = side == lob::Side::Buy
            ? 9'950 - static_cast<lob::Price>(i % 50)
            : 10'050 + static_cast<lob::Price>(i % 50);
        const auto id = next_id++;
        const auto result = book.add_limit_order(id, side, price, 1 + static_cast<lob::Quantity>(i % 5), trades);
        if (result.status == lob::Status::Accepted && result.rested) {
            active.add(id);
        }
    }

    std::mt19937_64 rng(20'270'607);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<int> qty_dist(1, 10);
    std::uniform_int_distribution<int> price_offset_dist(0, 70);
    std::vector<std::uint64_t> samples;
    samples.reserve(operations);

    const auto wall_start = Clock::now();
    for (std::size_t i = 0; i < operations; ++i) {
        trades = {};
        const int op = active.empty() ? 0 : op_dist(rng);

        if (op < 45) {
            const auto side = (rng() & 1U) == 0 ? lob::Side::Buy : lob::Side::Sell;
            const auto price = side == lob::Side::Buy
                ? 9'980 - static_cast<lob::Price>(price_offset_dist(rng))
                : 10'020 + static_cast<lob::Price>(price_offset_dist(rng));
            const auto id = next_id++;

            const auto start = Clock::now();
            const auto result = book.add_limit_order(id, side, price, static_cast<lob::Quantity>(qty_dist(rng)), trades);
            const auto end = Clock::now();
            if (result.status != lob::Status::Accepted) {
                std::cerr << "mixed benchmark add failed at operation " << i << '\n';
                break;
            }
            samples.push_back(ns_since(start, end));
            refresh_active_orders(book, active, id, result, trades);
        } else if (op < 65) {
            const auto id = active.random_id(rng);

            const auto start = Clock::now();
            const auto result = book.cancel_order(id);
            const auto end = Clock::now();
            if (result.status != lob::Status::Accepted) {
                std::cerr << "mixed benchmark cancel failed at operation " << i << '\n';
                break;
            }
            samples.push_back(ns_since(start, end));
            active.remove(id);
        } else if (op < 80) {
            const auto id = active.random_id(rng);
            const auto existing = book.get_order(id);
            if (!existing.has_value()) {
                active.remove(id);
                continue;
            }
            const auto new_price = existing->price + static_cast<lob::Price>((static_cast<int>(rng() % 3) - 1));
            const auto new_quantity = static_cast<lob::Quantity>(qty_dist(rng));

            const auto start = Clock::now();
            const auto result = book.modify_order(id, new_price, new_quantity, trades);
            const auto end = Clock::now();
            if (result.status != lob::Status::Accepted && result.status != lob::Status::PriceOutOfRange) {
                std::cerr << "mixed benchmark modify failed at operation " << i << '\n';
                break;
            }
            samples.push_back(ns_since(start, end));
            refresh_active_orders(book, active, id, result, trades);
        } else {
            const auto side = (rng() & 1U) == 0 ? lob::Side::Buy : lob::Side::Sell;
            const auto id = next_id++;

            const auto start = Clock::now();
            const auto result = book.add_market_order(id, side, static_cast<lob::Quantity>(qty_dist(rng)), trades);
            const auto end = Clock::now();
            if (result.status != lob::Status::Accepted) {
                std::cerr << "mixed benchmark market order failed at operation " << i << '\n';
                break;
            }
            samples.push_back(ns_since(start, end));
            refresh_active_orders(book, active, id, result, trades);
        }
    }
    const auto wall_end = Clock::now();

    return summarize("mixed workload", samples, ns_since(wall_start, wall_end));
}

void print_summary(const Summary& summary) {
    std::cout << std::left << std::setw(24) << summary.name
              << std::right << std::setw(12) << summary.operations
              << std::setw(18) << static_cast<std::uint64_t>(summary.throughput_ops_per_sec)
              << std::setw(14) << summary.median_ns
              << std::setw(12) << summary.p99_ns << '\n';
}

} // namespace

int main(int argc, char** argv) {
    std::size_t operations = 100'000;
    if (argc > 1) {
        operations = static_cast<std::size_t>(std::stoull(argv[1]));
    }

    std::cout << "compiler=" << __VERSION__ << '\n';
    std::cout << "hardware_threads=" << std::thread::hardware_concurrency() << '\n';
    std::cout << "operations_per_case=" << operations << '\n';
    std::cout << '\n';
    std::cout << std::left << std::setw(24) << "case"
              << std::right << std::setw(12) << "ops"
              << std::setw(18) << "throughput/s"
              << std::setw(14) << "median ns"
              << std::setw(12) << "p99 ns" << '\n';

    print_summary(benchmark_adds(operations));
    print_summary(benchmark_cancels(operations));
    print_summary(benchmark_matches(operations));
    print_summary(benchmark_mixed_workload(operations));

    return 0;
}
