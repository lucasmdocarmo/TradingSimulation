# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
# Configure (from repo root)
cmake -S . -B build

# Build
cmake --build build

# Run
./build/Test
```

CLion pre-configures `cmake-build-debug` (Debug) and `cmake-build-release` (Release) automatically.

## Project

- **Standard:** C++20
- **Entry point:** `main.cpp`
- **Target:** single executable `Test`

## Learning Context

This is a structured C++ learning project. Every interaction should treat teaching as a first-class concern:

- **Explain everything:** every keyword, syntax construct, namespace, library, header, template parameter, compiler flag, and CMake directive should be explained when introduced — what it is, what it does, and why it exists.
- **Verify understanding:** after introducing a concept, reinforce it with a concrete example and point out common mistakes or misconceptions around it.
- **Show idiomatic patterns:** always demonstrate the modern, correct C++ way — not just "something that works." Call out when a pattern is idiomatic vs. a workaround.
- **Annotate trade-offs:** for any design or implementation choice, explain what was given up and what was gained (e.g. performance vs. safety, heap vs. stack, virtual dispatch vs. templates).

### Focus Areas

The learning path covers the following domains — explanations and examples should be grounded in these contexts:

| Domain | Topics |
|---|---|
| **Advanced C++** | move semantics, perfect forwarding, RAII, templates, concepts, ranges, coroutines, constexpr |
| **Memory & Low Latency** | stack vs heap, allocators, memory pools, cache lines, false sharing, alignment, `mmap`, huge pages |
| **HPC / HFT** | hot path analysis, branch prediction, SIMD, lock-free data structures, wait-free queues, CPU pinning, NUMA |
| **OOP & Design Patterns** | SOLID, CRTP, policy-based design, type erasure, factory, strategy, observer in a C++ context |
| **Build & Tooling** | CMake targets, linking, sanitizers (ASan/TSan/UBSan), compiler flags (`-O2/-O3`, `-march`, LTO), profiling (perf, valgrind, vtune) |
| **Compilers** | compilation pipeline, ODR, TUs, inline, LTO, PGO, undefined behavior |
| **Third-party Libraries** | Crow (HTTP), Boost (asio, intrusive, lockfree), Folly (F14, MPMC queue, hazard pointers), QuickFIX / FIX protocol |
| **Networking & RDMA** | RDMA / ibverbs, kernel bypass, DPDK concepts, zero-copy I/O |
| **Order Book & Market Data** | in-memory order book architecture, level-2 data, price-time priority, latency-critical data paths |
| **Performance Analysis** | flame graphs, perf stat/record, cache miss analysis, latency histograms, benchmarking with Google Benchmark |