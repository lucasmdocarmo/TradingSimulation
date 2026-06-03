// Benchmarks for the FIX protocol parser.
//
// WHAT WE ARE MEASURING
// The zero-copy in-place parser (parseFixLine) that operates directly on
// mmap'd bytes. No std::string allocation, no stream objects, just pointer
// arithmetic and memchr scans.
//
// COMPARISON BASELINE
// The old std::stringstream-based parser allocated and destroyed multiple
// std::string objects per line. On a modern allocator, this adds ~200–500 ns
// per order. The new parser should be under 100 ns per line.
//
// HOW TO RUN
//   ./build/bench/bench_fix_parser
//   ./build/bench/bench_fix_parser --benchmark_filter=BM_ParseLine

#include <benchmark/benchmark.h>
#include "fix_session_reader.h"
#include <cstring>
#include <vector>
#include <string>

// Representative FIX New Order Single lines covering each field type.
// These are stored as string literals — benchmark accesses them like mmap'd memory.
static constexpr const char* kLines[] = {
    "11=ORD00001|35=D|55=AAPL|54=1|44=150.00|38=50",
    "11=ORD00002|35=D|55=MSFT|54=2|44=299.75|38=100",
    "11=ORD00003|35=D|55=ESZ4|54=1|44=4498.50|38=5",
    "11=ORD00004|35=D|55=GBPUSD|54=2|44=1.2501|38=1000",
    "11=ORD00005|35=D|55=TSLA|54=1|44=201.30|38=25",
};
static constexpr int kNumLines = static_cast<int>(sizeof(kLines) / sizeof(kLines[0]));

// ── 1. Single Line Parse ───────────────────────────────────────────────────
// Parses one representative FIX line repeatedly.
// Tests: memchr scan speed, field extraction, atof/integer parse.
// Expected: 50–150 ns per line.
static void BM_ParseLine(benchmark::State& state) {
    FixSessionReader reader;
    const char* line = kLines[0];
    const std::size_t len = strlen(line);

    for (auto _ : state) {
        Order o = reader.parseFixLine(line, len);
        benchmark::DoNotOptimize(o.price);
        benchmark::DoNotOptimize(o.quantity);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("one FIX NOS line, no alloc");
}
BENCHMARK(BM_ParseLine)->MinTime(1.0);

// ── 2. Round-Robin Across 5 Lines ─────────────────────────────────────────
// Cycles through different symbols and sides to exercise all tag branches.
// Also tests branch-predictor behaviour when tag dispatch varies each call.
static void BM_ParseLine_RoundRobin(benchmark::State& state) {
    FixSessionReader reader;
    int idx = 0;
    for (auto _ : state) {
        const char* line = kLines[idx % kNumLines];
        Order o = reader.parseFixLine(line, strlen(line));
        benchmark::DoNotOptimize(o.price);
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("5 distinct symbols round-robin");
}
BENCHMARK(BM_ParseLine_RoundRobin)->MinTime(1.0);

// ── 3. Throughput: Parse N Lines (simulates file replay) ──────────────────
// Builds an in-memory buffer of N FIX lines (like a mmap'd file) and measures
// total parse throughput. Larger N exercises prefetcher and L1/L2 behaviour.
static void BM_ParseFile_InMemory(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    FixSessionReader reader;

    // Build a contiguous buffer of N FIX lines separated by '\n'
    std::string buf;
    buf.reserve(static_cast<size_t>(N) * 50);
    for (int i = 0; i < N; ++i) {
        buf += kLines[i % kNumLines];
        buf += '\n';
    }

    int64_t total_bytes = 0;
    for (auto _ : state) {
        const char* p   = buf.data();
        const char* end = p + buf.size();
        int parsed = 0;
        while (p < end) {
            const char* nl = static_cast<const char*>(memchr(p, '\n', static_cast<size_t>(end - p)));
            if (!nl) nl = end;
            Order o = reader.parseFixLine(p, static_cast<std::size_t>(nl - p));
            benchmark::DoNotOptimize(o.price);
            p = nl + 1;
            ++parsed;
        }
        benchmark::DoNotOptimize(parsed);
        total_bytes += static_cast<int64_t>(buf.size());
    }
    state.SetBytesProcessed(total_bytes);
    state.SetItemsProcessed(state.iterations() * N);
    state.SetComplexityN(N);
}
BENCHMARK(BM_ParseFile_InMemory)
    ->RangeMultiplier(10)
    ->Range(10, 100'000)
    ->Complexity(benchmark::oN);

BENCHMARK_MAIN();
