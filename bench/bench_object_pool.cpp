// Benchmarks for ObjectPool vs heap allocation.
//
// THE KEY QUESTION
// Is the pool actually faster than new/delete in practice?
// How much does it cost when the free-stack is cold (first run after startup)?
// What happens when we batch-alloc many orders and batch-release them?
//
// HOW TO RUN
//   ./build/bench/bench_object_pool
//   ./build/bench/bench_object_pool --benchmark_filter=BM_Pool
//
// EXPECTED RESULTS (approximate, modern x86)
//   BM_Heap_NewDelete:         ~80–150 ns/op  (allocator lock + bookkeeping)
//   BM_Pool_AllocRelease:       ~3–8 ns/op    (pointer pop + push, hot in cache)
//   BM_Pool_BatchAlloc:        varies with N  (O(N), cache-dependent at large N)

#include <benchmark/benchmark.h>
#include "object_pool.h"
#include "order.h"
#include <cstring>
#include <vector>

// ── 1. Heap Baseline: new + delete ────────────────────────────────────────
// Establishes how expensive standard heap allocation is for the Order struct.
// On a contention-free single thread, glibc malloc/jemalloc is typically 80–150 ns.
// Under multi-threaded load (two threads allocating simultaneously), this can
// spike to 500+ ns due to heap lock contention.
static void BM_Heap_NewDelete(benchmark::State& state) {
    for (auto _ : state) {
        Order* o = new Order{};
        benchmark::DoNotOptimize(o);
        delete o;
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("operator new + delete (baseline)");
}
BENCHMARK(BM_Heap_NewDelete)->MinTime(1.0);

// ── 2. Pool: Alloc + Immediate Release ────────────────────────────────────
// Hot-path scenario: allocate a slot, write data, release immediately.
// The free-stack top is hot in L1 cache after the first iteration.
// This should be 10–30x faster than heap allocation.
static void BM_Pool_AllocRelease(benchmark::State& state) {
    ObjectPool<Order, 65536> pool;
    for (auto _ : state) {
        Order* o = pool.alloc();
        benchmark::DoNotOptimize(o);
        // Simulate writing an order (similar to what acceptNewOrder does)
        o->price    = 150.0;
        o->quantity = 100;
        pool.release(o);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("pool alloc + release (hot free-stack)");
}
BENCHMARK(BM_Pool_AllocRelease)->MinTime(1.0);

// ── 3. Pool Batch: Alloc N, then Release N ────────────────────────────────
// Simulates a burst of orders arriving before any matches occur (market open).
// After N allocs, the free-stack top has moved N positions back, pulling
// N different cache lines depending on the Order slab layout.
// At large N (N > L1 cache / 64 bytes = ~512 for a 32KB L1), cache misses appear.
static void BM_Pool_BatchAlloc(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    ObjectPool<Order, 65536> pool;
    std::vector<Order*> ptrs;
    ptrs.reserve(static_cast<size_t>(N));

    for (auto _ : state) {
        // Alloc N slots
        for (int i = 0; i < N; ++i) {
            Order* o = pool.alloc();
            benchmark::DoNotOptimize(o);
            ptrs.push_back(o);
        }
        // Release all N in reverse — LIFO keeps the most-recently-used slots hot
        for (int i = N - 1; i >= 0; --i)
            pool.release(ptrs[static_cast<size_t>(i)]);
        ptrs.clear();
    }
    state.SetComplexityN(N);
    state.SetItemsProcessed(state.iterations() * N * 2LL); // N allocs + N releases
}
BENCHMARK(BM_Pool_BatchAlloc)
    ->RangeMultiplier(4)
    ->Range(1, 4096)
    ->Complexity(benchmark::oN);

// ── 4. Pool Exhaustion Threshold ──────────────────────────────────────────
// Measures how close to exhaustion performance changes. At near-full capacity
// (< 64 slots remaining) the free-stack entries are at the beginning of the
// slab — those cache lines may have been evicted if we haven't touched them
// since startup. Shows the "cold tail" behaviour.
static void BM_Pool_NearExhaustion(benchmark::State& state) {
    ObjectPool<Order, 65536> pool;
    constexpr size_t LEAVE_FREE = 64; // how many slots to leave in the pool

    // Pre-allocate all but LEAVE_FREE slots
    std::vector<Order*> held;
    held.reserve(ObjectPool<Order, 65536>::capacity() - LEAVE_FREE);
    while (pool.available() > LEAVE_FREE)
        held.push_back(pool.alloc());

    for (auto _ : state) {
        Order* o = pool.alloc();
        benchmark::DoNotOptimize(o);
        pool.release(o);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("last 64 slots remaining (cold cache tail)");

    // Release held slots after benchmark completes
    for (Order* o : held) pool.release(o);
}
BENCHMARK(BM_Pool_NearExhaustion)->MinTime(1.0);

BENCHMARK_MAIN();
