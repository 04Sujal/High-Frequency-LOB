# High-Frequency Limit Order Book (LOB) & Gateway

A high-performance matching engine and data ingestion gateway written in C++20. Optimized for low-latency financial trading environments.

## Key Features
* **Price-Time Priority:** Standard FIFO matching algorithm using `std::map` and `std::list`.
* **Order Entry Gateway:** Logic for parsing string-based market data (FIX-style) into binary engine formats.
* **$O(1)$ Cancellation:** Internal `std::unordered_map` stores iterators for instant order removal.
* **Smart Analytics:** Real-time cached calculation of Mid-Price and Order Book Imbalance.
* **Modern Memory Management:** Uses `std::unique_ptr` and Move Semantics to ensure zero memory leaks and minimal copies.

## Technical Implementation
* **Zero-Copy Ingestion:** Designed to minimize data duplication as orders move from the gateway to the engine.
* **Efficient Caching:** Utilizes "Dirty-Bits" to ensure Mid-Price is only recalculated when the top-of-book changes.
* **Type Safety:** Implements `enum class` for Side identification, reducing memory footprint to 1 byte.

## Future Optimizations
* **Fast Parsing:** Replace `std::stod` with `std::from_chars` for non-allocating numeric conversion.
* **Binary Protocols:** Integration of binary feed handlers (e.g., NASDAQ ITCH) to bypass string parsing latency.
* **Kernel Bypass:** Future support for Solarflare/Mellanox specialized networking to reduce TCP overhead.
