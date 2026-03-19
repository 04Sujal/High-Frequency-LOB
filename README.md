# High-Frequency Limit Order Book (LOB) & Matching Engine

A Tier-1, ultra-low-latency matching engine and data ingestion gateway written in Modern C++. This project demonstrates industrial-grade techniques used in High-Frequency Trading (HFT) to achieve deterministic performance and sub-microsecond execution.

## 🚀 High-Performance Architecture
* **Lock-Free Concurrency:** Utilizes a **Single-Producer Single-Consumer (SPSC)** Ring Buffer with `std::atomic` memory barriers (Acquire/Release semantics) to bridge Network and Strategy threads without mutex contention.
* **Deterministic Memory (Zero-Allocation):** Implements a **Fixed-Size Memory Pool** for Order objects, eliminating runtime heap allocations (`new`/`malloc`) in the critical path to prevent OS-induced latency spikes.
* **O(1) Price-Level Discovery:** Replaced traditional $O(\log N)$ tree-based maps with a **Flat-Map (Array-based indexing)**. This allows for constant-time access to price levels by mapping financial ticks directly to memory offsets.
* **Hardware-Level Optimization:** Employs **Thread Affinity (CPU Pinning)** to lock critical execution paths to specific physical cores, maximizing L1/L2 cache hits and eliminating context-switch overhead.

## 🛠️ Core Features
* **Price-Time Priority:** Standard FIFO matching algorithm optimized for high-throughput environments.
* **Fixed-Point Math:** Uses `int64_t` for all price calculations to avoid the non-deterministic precision and performance overhead of floating-point arithmetic.
* **Zero-Copy Ingestion:** Designed to overlay binary structures directly onto network buffers using `reinterpret_cast` and `[[packed]]` structs for "wire-to-engine" speed.
* **Smart Analytics:** Real-time calculation of **Order Book Imbalance (OBI)** and Mid-Price for Alpha generation.

## 🏗️ Technical Stack
* **Language:** C++20
* **Concurrency:** Lock-free primitives (`std::atomic`), Multi-threading (`std::thread`)
* **Memory:** Custom Memory Pools, Cache-aligned structures
* **Gateway:** Zero-copy binary parsing, `std::string_view` for high-speed string slicing

## 📈 Roadmap & Alpha Generation
* **Signal Processing:** Integration of **Lasso (L1) Regularization** for sparse feature selection in market-making strategies.
* **Kernel Bypass:** Future integration of User-space networking (e.g., Solarflare OpenOnload) to bypass the Linux kernel TCP/IP stack.
* **SIMD Vectorization:** Utilizing AVX-512 instructions for parallelized book updates and signal calculation.

---
*Developed for high-performance financial engineering and low-latency systems research.*