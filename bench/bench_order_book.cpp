// Benchmarks for the flat-array OrderBook matching engine.
//
// HOW TO READ GOOGLE BENCHMARK OUTPUT
//   Benchmark name               Time           CPU        Iterations
//   BM_Insert_NoMatch           48.3 ns       48.1 ns      14512890
//
//   Time  = wall-clock time per iteration (includes OS scheduling jitter)
//   CPU   = CPU time per iteration (excludes time waiting for the OS)
//   For hot-path work, CPU time is usually more informative.
//
// HOW TO RUN
//   ./build/bench/bench_order_book
//   ./build/bench/bench_order_book --benchmark_filter=BM_SweepMatch
//   ./build/bench/bench_order_book --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
//
// WHAT WE ARE MEASURING
//   1. Pure insertion (no matches) — baseline for acceptNewOrder overhead
//   2. Immediate full match — every order crosses; tests the matching loop
//   3. Sweep match — one order sweeps N resting orders; tests pop_front + pool.release at depth

#include <benchmark/benchmark.h>
#include "order_book.h"
#include <chrono>
#include <cstdio>
#include <cstring>

static Order make_order(int seq, const char* sym, double price,
                        OrderType type, int32_t qty) noexcept {
    Order o{};
    snprintf(o.id, sizeof(o.id), "O%07d", seq);
    strncpy(o.symbol, sym, sizeof(o.symbol) - 1);
    o.price       = price;
    o.order_type  = type;
    o.quantity    = qty;
    o.arrival_ns  = std::chrono::steady_clock::now().time_since_epoch().count();
    return o;
}

// ── 1. Pure Insertion, No Match ────────────────────────────────────────────
// All BUY orders at the same price, no SELLs. Tests acceptNewOrder overhead:
// pool alloc + vector index + intrusive push + best_bid update.
// Expected: ~50-100 ns per insertion (dominated by pool alloc + memory write).
//
// POOL MANAGEMENT: With a 65536-slot pool and MinTime(1.0), the benchmark
// runs millions of iterations. When the pool is almost exhausted we issue one
// large crossing SELL under PauseTiming to drain all accumulated bids back to
// the pool — excluding that sweep cost from measurements.
static void BM_Insert_NoMatch(benchmark::State& state) {
    OrderBook book;
    book.setExecutionCallback([](Execution) {}); // discard — needed for sweep
    int seq = 0;
    for (auto _ : state) {
        if (book.poolAvailable() < 2) [[unlikely]] {
            state.PauseTiming();
            // Sweep all accumulated AAPL bids back to the pool.
            // A SELL at 149.99 crosses every resting BID at 150.00.
            book.acceptNewOrder(make_order(seq++, "AAPL", 149.99,
                                           OrderType::SELL, 65536 * 10));
            state.ResumeTiming();
        }
        book.acceptNewOrder(make_order(seq++, "AAPL", 150.00, OrderType::BUY, 10));
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("pure insert, no crosses");
}
BENCHMARK(BM_Insert_NoMatch)->MinTime(1.0);

// ── 2. Immediate Full Match ────────────────────────────────────────────────
// Alternating BUY/SELL at the same price — every order immediately crosses.
// Tests the hot matching loop: front() + buildExecution + pop_front + pool.release.
// Expected: ~100-200 ns per matched pair.
static void BM_ImmediateMatch(benchmark::State& state) {
    OrderBook book;
    int seq = 0;
    for (auto _ : state) {
        const bool is_buy = (seq % 2 == 0);
        book.acceptNewOrder(make_order(
            seq++, "AAPL", 150.00,
            is_buy ? OrderType::BUY : OrderType::SELL, 100
        ));
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("alternating buy/sell — immediate match");
}
BENCHMARK(BM_ImmediateMatch)->MinTime(1.0);

// ── 3. Sweep Match (variable depth) ───────────────────────────────────────
// Pre-fill N bids at the same price level, then one large ask sweeps all of them.
// Models a large aggressive sell that clears a price level — a common market event.
//
// This is the worst-case matching scenario for the hot path: the inner while(true)
// loop in matchOrders must pop N orders from the intrusive list and release N pool
// slots. Shows how the system scales with book depth.
//
// Complexity annotation (-Complexity) asks Google Benchmark to fit a curve to
// the data and report whether behaviour is O(N), O(N log N), etc.
static void BM_SweepMatch(benchmark::State& state) {
    const int depth = static_cast<int>(state.range(0));

    for (auto _ : state) {
        state.PauseTiming(); // exclude setup from measured time
        OrderBook book;
        book.setExecutionCallback([](Execution) {}); // discard executions
        for (int i = 0; i < depth; ++i)
            book.acceptNewOrder(make_order(i, "AAPL", 150.00, OrderType::BUY, 1));
        state.ResumeTiming();

        // One crossing SELL sweeps all depth bids — this is what we time.
        book.acceptNewOrder(make_order(depth, "AAPL", 149.99, OrderType::SELL, depth));
    }
    state.SetComplexityN(depth);
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(depth));
}
BENCHMARK(BM_SweepMatch)->RangeMultiplier(4)->Range(1, 1024)->Complexity(benchmark::oN);

// ── 4. Multi-Symbol Throughput ─────────────────────────────────────────────
// Interleaved orders across 6 symbols — measures unordered_map lookup + per-symbol
// SymbolBook construction overhead. The first access to each symbol triggers a
// SymbolBook() constructor (allocates two vectors of 500K PriceLevels).
// After warm-up, this is purely the map lookup + insert path.
static void BM_MultiSymbol(benchmark::State& state) {
    static const char* symbols[] = {"AAPL","MSFT","GOOGL","TSLA","ESZ4","GBPUSD"};
    static const double prices[] = {150.0, 300.0, 140.0, 200.0, 4500.0, 1.25};

    OrderBook book;
    book.setExecutionCallback([](Execution) {});

    // Warm-up: trigger SymbolBook construction for all 6 symbols before timing.
    for (int i = 0; i < 6; ++i) {
        book.acceptNewOrder(make_order(i, symbols[i], prices[i], OrderType::BUY, 1));
    }

    int seq = 100;
    for (auto _ : state) {
        if (book.poolAvailable() < 10) [[unlikely]] {
            state.PauseTiming();
            // Sweep all resting bids across every symbol under paused timing.
            for (int i = 0; i < 6; ++i)
                book.acceptNewOrder(make_order(seq++, symbols[i],
                                               prices[i] - 0.01,
                                               OrderType::SELL, 65536));
            state.ResumeTiming();
        }
        const int sym_idx = seq % 6;
        book.acceptNewOrder(make_order(
            seq++, symbols[sym_idx], prices[sym_idx], OrderType::BUY, 1
        ));
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("6 symbols, no cross");
}
BENCHMARK(BM_MultiSymbol)->MinTime(1.0);

BENCHMARK_MAIN();
