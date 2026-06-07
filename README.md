# High-Frequency-LOB

A small C++20 limit order book and matching engine project focused on correctness, readable low-latency-oriented design, and honest benchmarking.

This is not a production exchange gateway or a complete HFT system. It is a compact implementation intended to demonstrate price-time priority matching, fixed-point prices, bounded memory, and basic latency measurement.

## Architecture

- `include/lob/OrderBook.hpp`: public order book API and data types.
- `src/OrderBook.cpp`: matching engine implementation.
- `src/main.cpp`: minimal demo program.
- `include/lob/SpscQueue.hpp`: bounded single-producer/single-consumer queue.
- `examples/SpscIngestion.cpp`: producer/consumer ingestion example using the SPSC queue.
- `tests/OrderBookTests.cpp`: dependency-free test runner.
- `benchmarks/Benchmark.cpp`: synthetic add/cancel/match and mixed-workload microbenchmark.
- `.github/workflows/ci.yml`: GitHub Actions build/test workflow for Linux and Windows MinGW.

The book uses:

- fixed-point integer prices (`int64_t`) and integer quantities;
- a configured bounded price range mapped directly to array-indexed price levels;
- FIFO linked lists per price level for price-time priority;
- preallocated order storage with a free list;
- a preallocated open-addressed order ID index for cancel/modify lookup;
- caller-provided trade buffers via `std::span<Trade>` to avoid allocating trade records in the matching path.

The matching engine itself is single-threaded by design. The SPSC queue is kept as a separate ingestion example so the book remains deterministic and easy to test.

## Supported Functionality

- Add limit orders.
- Add market orders.
- Match marketable orders against the opposite side.
- Preserve FIFO priority at the same price.
- Enforce price priority across levels.
- Support partial fills and full fills.
- Cancel resting orders by order ID.
- Modify resting orders by order ID:
  - same-price quantity reductions preserve priority;
  - price changes and quantity increases are handled as cancel/replace.
- Query top of book.
- Query bid/ask depth.
- Emit trade records with aggressing order ID, resting order ID, side, price, and quantity.

## Correctness Testing

The tests cover targeted scenarios and randomized differential checks:

- FIFO priority at the same price.
- Price priority across levels.
- Partial and full fills.
- Cancel existing and missing orders.
- Modify behavior, including priority-preserving quantity reduction and cancel/replace.
- Top-of-book and depth updates after adds, cancels, and fills.
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

If CMake is installed:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=C:\msys64\mingw64\bin\g++.exe
cmake --build build
ctest --test-dir build
```

## Run

```powershell
.\build\lob_demo.exe
.\build\lob_tests.exe
.\build\lob_benchmark.exe 100000
.\build\lob_spsc_ingestion.exe
```

## Benchmark

Local run from this workspace:

- Date: 2026-06-07
- OS: Microsoft Windows 11 Home Single Language
- CPU: 12th Gen Intel(R) Core(TM) i5-1240P
- Compiler: MSYS2 MinGW g++ 14.1.0
- Hardware threads reported by benchmark: 16
- Build flags: `-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic`
- Operations per case: 100,000

These are synthetic microbenchmarks using `std::chrono::steady_clock` around each operation. They include timing overhead and are not end-to-end exchange latency measurements. The mixed workload uses a preloaded book and then applies roughly 45% limit adds, 20% cancels, 15% modifies, and 20% market orders.

| Case | Operations | Throughput/s | Median ns/op | p99 ns/op |
| --- | ---: | ---: | ---: | ---: |
| Resting limit add | 100,000 | 2,679,391 | 200 | 1,200 |
| Cancel resting order | 100,000 | 3,480,318 | 200 | 1,000 |
| Market order match | 100,000 | 2,094,319 | 300 | 1,400 |
| Mixed workload | 100,000 | 1,256,019 | 200 | 1,200 |

## Limitations

- Single-threaded matching engine; the SPSC ingestion example is demonstrative and not a real market data gateway.
- No persistence, recovery, market data replay, or FIX/ITCH/OUCH protocol support.
- Price range is configured up front and must fit in the array-indexed level model.
- The order ID index is fixed-capacity and optimized for bounded in-memory tests.
- Benchmarks are synthetic and not comparable to production HFT systems.
- No CPU pinning or real-time OS tuning is applied in the benchmark.

## Next Steps

- Add CSV benchmark output for easier run-to-run comparison.
- Add repeated benchmark runs with min/median/max summaries.
- Add a simple market-data replay format for deterministic scenario files.
- Add optional thread affinity controls for benchmark experiments on supported platforms.
