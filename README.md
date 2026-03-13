# High-Frequency Limit Order Book (LOB)

A high-performance matching engine core written in C++20, optimized for low-latency market data processing and order execution.

## Key Features

* **Price-Time Priority:** Implements a standard matching algorithm using `std::map` (Price) and `std::list` (Time).
* **Zero-Copy Logic:** Designed to minimize memory allocations during the "Hot Path" of order matching.
* **Order Tracking:** $O(1)$ order cancellation and modification via an internal `std::unordered_map` lookup.
* **Smart Analytics:** Real-time calculation of **Mid-Price** and **Order Book Imbalance** to detect market trends.
* **Memory Efficiency:** Utilizes `std::unique_ptr` for safe, automated memory management without the overhead of raw pointers.

## Technical Implementation

* **Header-Only Core:** The engine is encapsulated in a highly portable `OrderBook` class.
* **Dirty-Bit Caching:** Mid-price calculations are cached and only recalculated when the "Best Bid/Ask" is affected, saving CPU cycles.
* **Data Structures:** * `std::map`: Manages price levels with logarithmic search complexity ($O(\log N)$).
    * `std::list`: Handles the queue of orders at each price level to maintain strict FIFO time priority.

## Future Optimizations

* **Lock-Free Logging:** Implementation of a ring-buffer for asynchronous trade recording.
* **Bit-Packing:** Compressing the `Order` struct to fit within single cache lines (64 bytes).
* **SIMD Support:** Utilizing vector instructions for faster volume aggregation.
