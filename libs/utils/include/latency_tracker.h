#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string>
#include <iostream>

// Returns the current monotonic clock in nanoseconds.
// steady_clock (not high_resolution_clock) is mandatory for latency measurement
// because it is guaranteed never to go backwards. On some platforms,
// high_resolution_clock aliases system_clock, which can be adjusted by NTP.
inline int64_t now_ns() noexcept {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

// ─────────────────────────────────────────────────────────────────────────────
// Lock-Free Power-of-2 Latency Histogram
//
// DESIGN
// 64 buckets indexed by floor(log2(sample_ns)):
//   Bucket  0: [0,       1) ns     sub-nanosecond (clock overhead)
//   Bucket  9: [256,   512) ns     typical SPSC push
//   Bucket 10: [512,  1024) ns     sub-microsecond order match
//   Bucket 20: [524µs, 1ms)        cold-path drain
//   Bucket 30: [536ms,  1s)        GC / OS scheduling hiccup
//
// All writes use memory_order_relaxed — atomic increments with no ordering
// fence. The histogram is purely observational; the cold-path reader tolerates
// reading slightly stale data without any correctness impact.
//
// THROUGHPUT
// record() costs ~5–10 ns: one __builtin_clzll (1 cycle), one fetch_add (~5 ns),
// two CAS loops for min/max (usually one iteration each).
//
// PERCENTILES
// Walking 64 buckets to find a percentile takes ~64 loads — cheap enough for
// a periodic report called every few seconds.
//
// JITTER (as reported here)
// Defined as p99 − p50 (the "latency spread").
// p50 is your typical operation. p99 is your worst 1-in-100 operation.
// A wide spread means the system behaves inconsistently — GC pauses, cache
// misses, OS preemption, or lock contention can all widen the spread.
// In HFT the target is sub-microsecond jitter on the hot path.
// ─────────────────────────────────────────────────────────────────────────────
class LatencyTracker {
    static constexpr int BUCKETS = 64;

    static int bucket_of(uint64_t ns) noexcept {
        if (ns == 0) return 0;
        // __builtin_clzll: count leading zeros in a 64-bit int (one CPU instruction).
        // 63 - clz(n) = floor(log2(n)). Fast and branch-free.
        const int b = 63 - __builtin_clzll(ns);
        return b < BUCKETS ? b : BUCKETS - 1;
    }

public:
    LatencyTracker() noexcept { reset(); }

    void record(int64_t ns) noexcept {
        if (ns < 0) return;
        if (ns == 0) ns = 1; // sub-clock-resolution ops still need a bucket
        const uint64_t uns = static_cast<uint64_t>(ns);

        buckets_[bucket_of(uns)].fetch_add(1, std::memory_order_relaxed);
        count_.fetch_add(1,  std::memory_order_relaxed);
        sum_ns_.fetch_add(ns, std::memory_order_relaxed);

        // Atomic max: CAS loop. Typically one iteration (most samples are not a new max).
        int64_t cur = max_ns_.load(std::memory_order_relaxed);
        while (ns > cur &&
               !max_ns_.compare_exchange_weak(cur, ns, std::memory_order_relaxed)) {}

        // Atomic min: same pattern, initialised to INT64_MAX.
        cur = min_ns_.load(std::memory_order_relaxed);
        while (ns < cur &&
               !min_ns_.compare_exchange_weak(cur, ns, std::memory_order_relaxed)) {}
    }

    void reset() noexcept {
        for (auto& b : buckets_) b.store(0, std::memory_order_relaxed);
        count_.store(0, std::memory_order_relaxed);
        sum_ns_.store(0, std::memory_order_relaxed);
        max_ns_.store(0, std::memory_order_relaxed);
        min_ns_.store(std::numeric_limits<int64_t>::max(), std::memory_order_relaxed);
    }

    // Returns the smallest observed value in bucket b (= 2^(b-1) ns, or 0 for b=0).
    // Percentile walks buckets until the cumulative count >= target fraction.
    int64_t percentile(double pct) const noexcept {
        const uint64_t total = count_.load(std::memory_order_relaxed);
        if (total == 0) return 0;
        const auto target = static_cast<uint64_t>(static_cast<double>(total) * pct / 100.0);
        uint64_t cum = 0;
        for (int b = 0; b < BUCKETS; ++b) {
            cum += buckets_[b].load(std::memory_order_relaxed);
            if (cum >= target)
                return (b == 0) ? 1LL : (1LL << b);
        }
        return max_ns_.load(std::memory_order_relaxed);
    }

    uint64_t count() const noexcept { return count_.load(std::memory_order_relaxed); }
    int64_t  max()   const noexcept { return max_ns_.load(std::memory_order_relaxed); }
    int64_t  min()   const noexcept {
        const int64_t v = min_ns_.load(std::memory_order_relaxed);
        return (v == std::numeric_limits<int64_t>::max()) ? 0LL : v;
    }
    double mean() const noexcept {
        const uint64_t n = count_.load(std::memory_order_relaxed);
        return n ? static_cast<double>(sum_ns_.load(std::memory_order_relaxed)) / n : 0.0;
    }

    // Formats a nanosecond value as "123 ns", "4.56 µs", or "7.89 ms".
    static std::string fmt_ns(int64_t ns) {
        if (ns < 1'000)
            return std::to_string(ns) + " ns";
        if (ns < 1'000'000) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f µs", ns / 1000.0);
            return buf;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f ms", ns / 1'000'000.0);
        return buf;
    }

    void report(const std::string& name, std::ostream& os = std::cout) const {
        const uint64_t n = count_.load(std::memory_order_relaxed);
        if (n == 0) { os << "  " << name << ": no samples yet\n"; return; }

        const int64_t p50    = percentile(50.0);
        const int64_t p75    = percentile(75.0);
        const int64_t p95    = percentile(95.0);
        const int64_t p99    = percentile(99.0);
        const int64_t p999   = percentile(99.9);
        const int64_t p9999  = percentile(99.99);
        const int64_t jitter = p99 - p50;

        os << "  " << name << "  (" << n << " samples)\n"
           << "    p50="    << fmt_ns(p50)
           << "   p75="    << fmt_ns(p75)
           << "   p95="    << fmt_ns(p95)
           << "   p99="    << fmt_ns(p99)   << "\n"
           << "    p99.9="  << fmt_ns(p999)
           << "  p99.99=" << fmt_ns(p9999)
           << "   max="   << fmt_ns(max())
           << "   mean="  << fmt_ns(static_cast<int64_t>(mean())) << "\n"
           << "    jitter(p99-p50)=" << fmt_ns(jitter)
           << "   min=" << fmt_ns(min()) << "\n";
    }

private:
    // Separate cache lines for each counter to prevent false sharing when
    // two hot-path threads record into different trackers simultaneously.
    alignas(64) std::array<std::atomic<uint64_t>, BUCKETS> buckets_;
    alignas(64) std::atomic<uint64_t> count_;
    alignas(64) std::atomic<int64_t>  sum_ns_;
    alignas(64) std::atomic<int64_t>  max_ns_;
    alignas(64) std::atomic<int64_t>  min_ns_;
};

// RAII scope timer — records elapsed nanoseconds into a LatencyTracker on destruction.
// Usage:  { ScopedLatency timer(myTracker); expensive_work(); }
struct ScopedLatency {
    LatencyTracker& tracker;
    int64_t         start;
    explicit ScopedLatency(LatencyTracker& t) noexcept
        : tracker(t), start(now_ns()) {}
    ~ScopedLatency() noexcept { tracker.record(now_ns() - start); }
    ScopedLatency(const ScopedLatency&) = delete;
    ScopedLatency& operator=(const ScopedLatency&) = delete;
};
