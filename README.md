# High-Frequency-LOB

A compact C++20 limit order book and matching engine focused on price-time priority,
bounded memory, correctness tests, and honest synthetic benchmarking.

This is not a production exchange gateway or a complete HFT stack. It is a focused
systems project intended to demonstrate the core mechanics behind an order-driven
market: matching, cancels, modifies, top-of-book/depth queries, deterministic storage,
and basic latency measurement.

## Highlights

- Price-time priority matching for limit and market orders.
- Partial and full fills with trade records containing aggressing/resting order IDs.
- Cancel-by-ID and modify-by-ID support.
- Same-price quantity reductions preserve priority; price changes and quantity
  increases are handled as cancel/replace.
- `int64_t` fixed-point prices and integer quantities.
- Configured bounded price range mapped to array-indexed price levels.
- FIFO linked lists per price level.
- Preallocated order storage with a free list.
- Preallocated open-addressed order ID index for cancel/modify lookup.
- Caller-provided `std::span<Trade>` buffers to avoid matching-path trade allocation.
- Separate bounded SPSC queue example for ingestion experiments.
- Dependency-free test runner with randomized differential checks.
- GitHub Actions CI for Linux and Windows MinGW.

## Repository Layout

```text
include/lob/OrderBook.hpp      Public order book API and data types
include/lob/SpscQueue.hpp      Bounded single-producer/single-consumer queue
src/OrderBook.cpp              Matching engine implementation
src/main.cpp                   Minimal demo
tests/OrderBookTests.cpp       Dependency-free correctness tests
benchmarks/Benchmark.cpp       Synthetic add/cancel/match/mixed benchmark
examples/SpscIngestion.cpp     Producer/consumer ingestion example
.github/workflows/ci.yml       Linux and Windows CI
```

## Supported Functionality

- Add limit orders.
- Add market orders.
- Match marketable orders against the opposite side.
- Preserve FIFO priority at the same price.
- Enforce price priority across levels.
- Support partial fills and full fills.
- Cancel resting orders by order ID.
- Modify resting orders by order ID.
- Query top of book.
- Query bid/ask depth.
- Emit trade records with side, price, quantity, aggressing order ID, and resting order ID.

## Correctness Testing

The tests cover:

- FIFO priority at the same price.
- Price priority across levels.
- Partial and full fills.
- Existing and missing-order cancels.
- Priority-preserving quantity reduction.
- Cancel/replace behavior on price changes and quantity increases.
- Top-of-book and depth updates after adds, cancels, modifies, and fills.
- Empty-book edge cases.
- Randomized operation streams checked against an independent slower reference book.

## Build

PowerShell with MSYS2 MinGW `g++`:

```powershell
New-Item -ItemType Directory -Force build

& 'C:\msys64\mingw64\bin\g++.exe' -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude src\OrderBook.cpp src\main.cpp -o build\lob_demo.exe

& 'C:\msys64\mingw64\bin\g++.exe' -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude src\OrderBook.cpp tests\OrderBookTests.cpp -o build\lob_tests.exe

& 'C:\msys64\mingw64\bin\g++.exe' -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Iinclude src\OrderBook.cpp benchmarks\Benchmark.cpp -o build\lob_benchmark.exe

& 'C:\msys64\mingw64\bin\g++.exe' -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude src\OrderBook.cpp examples\SpscIngestion.cpp -o build\lob_spsc_ingestion.exe
```

With CMake:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=C:\msys64\mingw64\bin\g++.exe
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

```powershell
.\build\lob_demo.exe
.\build\lob_tests.exe
.\build\lob_benchmark.exe 100000
.\build\lob_spsc_ingestion.exe
```

Expected test output:

```text
All order book tests passed
```

## Benchmark

Local benchmark environment:

- Date: 2026-06-07
- OS: Microsoft Windows 11 Home Single Language
- CPU: 12th Gen Intel(R) Core(TM) i5-1240P
- Compiler: MSYS2 MinGW `g++ 14.1.0`
- Hardware threads reported by benchmark: 16
- Build flags: `-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic`
- Operations per case: 100,000

These are synthetic microbenchmarks using `std::chrono::steady_clock` around each
operation. They include timing overhead and are not end-to-end exchange latency
measurements.

Across four local runs:

| Case | Median throughput/s | Median latency | p99 range |
| --- | ---: | ---: | ---: |
| Resting limit add | 5.7M | 100-200 ns/op | 400-600 ns/op |
| Cancel resting order | 7.3M | 100-200 ns/op | 200-500 ns/op |
| Market order match | 5.5M | 100-200 ns/op | 300-700 ns/op |
| Mixed workload | 2.1M | 200 ns/op | 500-700 ns/op |

## Design Notes

The matching engine is intentionally single-threaded. The SPSC queue is kept as a
separate ingestion example so the book remains deterministic and easier to test. This
matches the design goal of isolating the correctness-critical matching path from
transport and threading concerns.

## Limitations

- No persistence, recovery, market data replay, or FIX/ITCH/OUCH protocol support.
- Price range is configured up front and must fit the array-indexed level model.
- The order ID index is fixed-capacity and optimized for bounded in-memory tests.
- Benchmarks are synthetic and not comparable to production HFT systems.
- No CPU pinning or real-time OS tuning is applied in the benchmark.

## Next Steps

- Add CSV benchmark output and repeated-run summaries.
- Add a `std::map`/`std::deque` baseline for comparison.
- Add a deterministic market-data replay format.
- Report memory footprint for different book configurations.
- Add optional thread affinity controls for benchmark experiments.
