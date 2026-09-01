# HFT Order Book — C++20

A high-performance limit order book and matching engine implemented in modern C++20.

The goal of this project is to build a realistic low-latency trading engine while studying the systems programming concepts used in High-Frequency Trading (HFT).

## Features

* Limit order book
* BID / ASK management
* Price-time priority
* FIFO matching
* Order insertion
* Order cancellation
* Order modification
* Partial execution
* Full execution
* Multi-level matching
* Best bid / best ask
* Spread calculation
* Mid-price calculation
* Stress testing
* Performance benchmarking

## Architecture

```text
Incoming Order
      │
      ▼
┌───────────────┐
│ Matching      │
│ Engine        │
└───────┬───────┘
        │
   ┌────┴────┐
   ▼         ▼
 BIDS       ASKS
   │         │
   └────┬────┘
        ▼
    Trade /
    Resting Order
```

## Project Structure

```text
taha_arc_cpp/
├── include/
│   ├── orderbook.hpp
│   ├── order.hpp
│   └── types.hpp
│
├── src/
│   └── orderbook.cpp
│
├── tests/
│   └── test_orderbook.cpp
│
├── benchmarks/
│   └── benchmark.cpp
│
├── CMakeLists.txt
├── README.md
└── .gitignore
```

## Technologies

* C++20
* STL
* CMake
* Linux / WSL2
* Git / GitHub

## Performance

Initial benchmark:

| Operation           |         Result |
| ------------------- | -------------: |
| Orders added        |        100,000 |
| Average add latency |       ~0.43 µs |
| Add throughput      | ~2.3M orders/s |

Benchmarks are performed in optimized builds using:

```bash
g++ -std=c++20 -O3 -march=native -DNDEBUG
```

Further optimization will focus on:

* Cache locality
* Memory allocation
* Data structures
* Branch prediction
* CPU efficiency
* Object lifetime
* Memory pools
* Lock-free programming
* Multithreading

## Roadmap

### Phase 1 — Matching Engine

* [x] Order representation
* [x] Add order
* [x] Cancel order
* [x] Modify order
* [x] Price-time priority
* [x] Order matching
* [x] Partial fills
* [x] Multi-level matching

### Phase 2 — Testing

* [x] Unit tests
* [x] Assertion-based tests
* [ ] Randomized testing
* [ ] Large-scale stress testing
* [ ] Edge-case testing

### Phase 3 — Performance

* [x] Initial benchmark
* [ ] Benchmark each operation independently
* [ ] Nanosecond latency measurements
* [ ] Allocation profiling
* [ ] Cache analysis
* [ ] `perf` profiling

### Phase 4 — Low Latency

* [ ] Memory pool
* [ ] Custom allocators
* [ ] Cache-friendly data structures
* [ ] Reduce dynamic allocations
* [ ] Branch optimization
* [ ] CPU affinity
* [ ] Lock-free structures

### Phase 5 — HFT Infrastructure

* [ ] Market data feed
* [ ] Trade event system
* [ ] Order gateway
* [ ] Risk checks
* [ ] Multithreaded architecture
* [ ] Lock-free queues
* [ ] Replay engine

## Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j$(nproc)
```

## Run Tests

```bash
./tests
```

## Run Benchmark

```bash
./benchmark
```

## Objective

This project is designed as a deep C++ systems project focused on:

* Modern C++
* Data structures
* Algorithms
* Linux systems programming
* CPU architecture
* Memory management
* Concurrency
* Low-latency engineering
* HFT system design

The long-term objective is to evolve this project from a basic matching engine into a realistic low-latency trading system.

