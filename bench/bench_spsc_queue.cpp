// Benchmarks for the lock-free SPSC ring buffer.
//
// INTERPRETING RESULTS
// The single-threaded round-trip benchmark (BM_RoundTrip) measures pure
// throughput with no OS scheduling or cache-coherence traffic — a lower
// bound on the achievable throughput. The actual hot-path performance
// will be slightly worse due to the producer and consumer running on
// different cores (cache-line invalidation on tail_ / head_).
//
// The two-thread benchmark (BM_TwoThread_Throughput) measures real-world
// cross-core throughput. On a modern x86 system expect ~40–80 ns one-way
// latency (one cache-line transfer between cores via L3).
//
// HOW TO RUN
//   ./build/bench/bench_spsc_queue
//   ./build/bench/bench_spsc_queue --benchmark_filter=BM_TwoThread

#include <benchmark/benchmark.h>
#include "spsc_queue.h"
#include "tick.h"
#include <atomic>
#include <cstring>
#include <thread>

static Tick make_tick(const char* sym, double price) noexcept {
    Tick t{};
    strncpy(t.symbol, sym, sizeof(t.symbol) - 1);
    t.last = price;
    t.bid  = price - 0.01;
    t.ask  = price + 0.01;
    return t;
}

// ── 1. Single-Thread Round-Trip ────────────────────────────────────────────
// Push one item then immediately pop it — no cross-core coherence traffic.
// This is the absolute floor: measures memcpy + two atomic ops only.
// Expected: ~10–20 ns per round-trip.
static void BM_RoundTrip(benchmark::State& state) {
    SpscQueue<Tick> q(4096);
    const Tick tick = make_tick("AAPL", 150.0);
    Tick out{};

    for (auto _ : state) {
        q.push(tick);
        q.pop(out);
        benchmark::DoNotOptimize(out.last);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("single-thread push+pop baseline");
}
BENCHMARK(BM_RoundTrip)->MinTime(1.0);

// ── 2. Push-Only Throughput (producer side) ────────────────────────────────
// Measures how fast the producer can fill the queue when the consumer is absent.
// Tests pure write throughput to the ring buffer.
static void BM_PushOnly(benchmark::State& state) {
    SpscQueue<Tick> q(1'000'000);
    const Tick tick = make_tick("MSFT", 300.0);

    for (auto _ : state) {
        // Drain at the start of each batch to keep space available.
        state.PauseTiming();
        Tick drain;
        while (q.pop(drain)) {}
        state.ResumeTiming();

        benchmark::DoNotOptimize(q.push(tick));
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("producer push-only");
}
BENCHMARK(BM_PushOnly)->MinTime(1.0);

// ── 3. Two-Thread Throughput ───────────────────────────────────────────────
// True producer/consumer across two OS threads. Measures real-world throughput
// including cache-coherence protocol (MESI): the core holding tail_'s cache
// line must invalidate it in the consumer's cache on every push, and vice-versa.
//
// Google Benchmark runs all thread_index values simultaneously. Thread 0 = producer,
// Thread 1 = consumer. They share the static queue below.
//
// Expected: 40–100 ns/item on the same socket; up to 200+ ns cross-socket (NUMA).
static SpscQueue<int64_t> s_shared_queue{65536};
static std::atomic<bool>  s_producer_done{false};

static void BM_TwoThread_Throughput(benchmark::State& state) {
    if (state.thread_index() == 0) {
        // Producer
        s_producer_done.store(false, std::memory_order_relaxed);
        for (auto _ : state) {
            int64_t val = 42;
            while (!s_shared_queue.push(val)) {} // spin if full
        }
        s_producer_done.store(true, std::memory_order_release);

    } else {
        // Consumer: drain until producer signals done
        int64_t val;
        uint64_t consumed = 0;
        for (auto _ : state) {
            while (!s_shared_queue.pop(val)) {} // spin if empty
            benchmark::DoNotOptimize(val);
            ++consumed;
        }
        // Drain any remaining items after the state loop ends
        while (!s_producer_done.load(std::memory_order_acquire) ||
               s_shared_queue.pop(val)) {}

        state.counters["items_consumed"] = benchmark::Counter(
            static_cast<double>(consumed),
            benchmark::Counter::kIsRate
        );
    }
}
BENCHMARK(BM_TwoThread_Throughput)
    ->Threads(2)
    ->MinTime(2.0)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

// ── 4. Queue Capacity Sensitivity ─────────────────────────────────────────
// Does queue capacity (and therefore the backing vector size) affect per-op cost?
// Larger queues = more memory = potentially more TLB misses on the first access.
// After warm-up the working set should fit in L3 for any sane capacity.
static void BM_CapacitySweep(benchmark::State& state) {
    const int cap = static_cast<int>(state.range(0));
    SpscQueue<Tick> q(static_cast<size_t>(cap));
    const Tick tick = make_tick("GOOGL", 140.0);
    Tick out{};

    for (auto _ : state) {
        q.push(tick);
        q.pop(out);
        benchmark::DoNotOptimize(out.last);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CapacitySweep)
    ->RangeMultiplier(8)
    ->Range(64, 1'000'000)
    ->MinTime(0.5);

BENCHMARK_MAIN();
