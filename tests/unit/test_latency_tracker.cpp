#include "latency_tracker.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Basic state
// ─────────────────────────────────────────────────────────────────────────────
TEST(LatencyTrackerTest, DefaultStateHasNoSamples) {
    LatencyTracker t;
    EXPECT_EQ(t.count(), 0u);
    EXPECT_EQ(t.min(),   0LL);
    EXPECT_EQ(t.max(),   0LL);
    EXPECT_DOUBLE_EQ(t.mean(), 0.0);
    EXPECT_EQ(t.percentile(50.0), 0LL);
}

// ─────────────────────────────────────────────────────────────────────────────
// record()
// ─────────────────────────────────────────────────────────────────────────────
TEST(LatencyTrackerTest, SingleSampleUpdatesAllStats) {
    LatencyTracker t;
    t.record(1000);
    EXPECT_EQ(t.count(), 1u);
    EXPECT_EQ(t.min(),   1000LL);
    EXPECT_EQ(t.max(),   1000LL);
    EXPECT_NEAR(t.mean(), 1000.0, 1.0);
}

TEST(LatencyTrackerTest, NegativeSamplesAreDropped) {
    LatencyTracker t;
    t.record(-1);
    t.record(-1000);
    EXPECT_EQ(t.count(), 0u);
}

TEST(LatencyTrackerTest, ZeroMappedToOne) {
    // A zero-ns sample is physically impossible (clock resolution > 0) but can
    // appear when two steady_clock calls land in the same tick. We map it to 1
    // so it lands in bucket 0 rather than being silently dropped.
    LatencyTracker t;
    t.record(0);
    EXPECT_EQ(t.count(), 1u);
    EXPECT_EQ(t.min(),   1LL);
}

TEST(LatencyTrackerTest, MinAndMaxTracked) {
    LatencyTracker t;
    t.record(500);
    t.record(100);
    t.record(1000);
    t.record(300);

    EXPECT_EQ(t.min(), 100LL);
    EXPECT_EQ(t.max(), 1000LL);
}

TEST(LatencyTrackerTest, MeanIsCorrect) {
    LatencyTracker t;
    t.record(100);
    t.record(200);
    t.record(300);
    EXPECT_NEAR(t.mean(), 200.0, 1.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// reset()
// ─────────────────────────────────────────────────────────────────────────────
TEST(LatencyTrackerTest, ResetClearsAllState) {
    LatencyTracker t;
    t.record(100);
    t.record(200);
    t.reset();

    EXPECT_EQ(t.count(), 0u);
    EXPECT_EQ(t.min(),   0LL);
    EXPECT_EQ(t.max(),   0LL);
    EXPECT_EQ(t.percentile(99.0), 0LL);
}

TEST(LatencyTrackerTest, RecordAfterResetWorks) {
    LatencyTracker t;
    t.record(999);
    t.reset();
    t.record(42);

    EXPECT_EQ(t.count(), 1u);
    EXPECT_EQ(t.min(), 42LL);
}

// ─────────────────────────────────────────────────────────────────────────────
// percentile()
// Percentiles walk power-of-2 buckets, so results are rounded to the nearest
// power of 2. The key property to assert is the ordering guarantee.
// ─────────────────────────────────────────────────────────────────────────────
TEST(LatencyTrackerTest, PercentileOrderingHolds) {
    LatencyTracker t;
    // Record 1,000 samples spanning several decades of latency.
    for (int i = 1; i <= 1000; ++i)
        t.record(static_cast<int64_t>(i) * 100);  // 100ns … 100µs

    const int64_t p50  = t.percentile(50.0);
    const int64_t p75  = t.percentile(75.0);
    const int64_t p95  = t.percentile(95.0);
    const int64_t p99  = t.percentile(99.0);
    const int64_t p999 = t.percentile(99.9);

    EXPECT_LE(p50,  p75);
    EXPECT_LE(p75,  p95);
    EXPECT_LE(p95,  p99);
    EXPECT_LE(p99,  p999);
    EXPECT_LE(p999, t.max());
}

TEST(LatencyTrackerTest, P100ApproachesMax) {
    LatencyTracker t;
    for (int i = 1; i <= 100; ++i)
        t.record(static_cast<int64_t>(i) * 1000);

    // p99.99 should be at or near the maximum recorded value.
    EXPECT_LE(t.percentile(99.99), t.max() * 2);
    EXPECT_GE(t.percentile(99.99), t.percentile(99.0));
}

// ─────────────────────────────────────────────────────────────────────────────
// fmt_ns()
// ─────────────────────────────────────────────────────────────────────────────
TEST(LatencyTrackerTest, FmtNsUnder1000IsNanoseconds) {
    const std::string s = LatencyTracker::fmt_ns(500);
    EXPECT_NE(s.find("ns"), std::string::npos);
}

TEST(LatencyTrackerTest, FmtNsMicroseconds) {
    const std::string s = LatencyTracker::fmt_ns(5000);
    EXPECT_NE(s.find("\xc2\xb5s"), std::string::npos);  // UTF-8 µs
}

TEST(LatencyTrackerTest, FmtNsMilliseconds) {
    const std::string s = LatencyTracker::fmt_ns(5'000'000);
    EXPECT_NE(s.find("ms"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Thread safety
// record() uses memory_order_relaxed atomics — safe for concurrent writers.
// The final count must equal the total number of records across all threads.
// ─────────────────────────────────────────────────────────────────────────────
TEST(LatencyTrackerTest, ConcurrentRecordsCountCorrectly) {
    LatencyTracker t;
    constexpr int kThreads    = 4;
    constexpr int kPerThread  = 25'000;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&t] {
            for (int j = 1; j <= kPerThread; ++j)
                t.record(static_cast<int64_t>(j));
        });
    }
    for (auto& th : threads) th.join();

    EXPECT_EQ(t.count(), static_cast<uint64_t>(kThreads * kPerThread));
}

TEST(LatencyTrackerTest, ConcurrentMaxIsCorrect) {
    LatencyTracker t;
    constexpr int kThreads = 4;
    const int64_t expected_max = 1'000'000'000LL;  // 1 second

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&t, i, expected_max] {
            // Thread 0 records the known maximum; others record smaller values.
            t.record(i == 0 ? expected_max : 1000LL);
        });
    }
    for (auto& th : threads) th.join();

    EXPECT_EQ(t.max(), expected_max);
}

// ─────────────────────────────────────────────────────────────────────────────
// ScopedLatency RAII timer
// ─────────────────────────────────────────────────────────────────────────────
TEST(LatencyTrackerTest, ScopedLatencyRecordsOnDestruction) {
    LatencyTracker t;
    {
        ScopedLatency timer(t);
        // Some work — just enough to produce a non-zero elapsed time.
        volatile int x = 0;
        for (int i = 0; i < 1000; ++i) x += i;
        (void)x;
    }
    EXPECT_EQ(t.count(), 1u);
    EXPECT_GT(t.max(),   0LL);
}
