#include "spsc_queue.h"

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Basic correctness
// ─────────────────────────────────────────────────────────────────────────────
TEST(SpscQueueTest, PushAndPopRoundTrip) {
    SpscQueue<int> q(16);
    EXPECT_TRUE(q.push(42));
    int v = 0;
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 42);
}

TEST(SpscQueueTest, PopFromEmptyReturnsFalse) {
    SpscQueue<int> q(16);
    int v = 0;
    EXPECT_FALSE(q.pop(v));
    EXPECT_EQ(v, 0);  // untouched
}

TEST(SpscQueueTest, SizeReflectsContents) {
    SpscQueue<int> q(16);
    EXPECT_EQ(q.size(), 0u);
    q.push(1);
    EXPECT_EQ(q.size(), 1u);
    q.push(2);
    EXPECT_EQ(q.size(), 2u);
    int v;
    q.pop(v);
    EXPECT_EQ(q.size(), 1u);
}

TEST(SpscQueueTest, CapacityMatchesConstructorArg) {
    SpscQueue<int> q(100);
    EXPECT_EQ(q.capacity(), 100u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Full queue
// The ring buffer wastes one slot to distinguish full from empty:
// a queue of capacity N can hold at most N-1 items before push() returns false.
// ─────────────────────────────────────────────────────────────────────────────
TEST(SpscQueueTest, PushReturnsFalseWhenFull) {
    SpscQueue<int> q(4);  // holds 3 items
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));
    EXPECT_FALSE(q.push(4));  // full
}

TEST(SpscQueueTest, PopAfterFullMakesRoomForNextPush) {
    SpscQueue<int> q(4);
    q.push(1); q.push(2); q.push(3);
    EXPECT_FALSE(q.push(99));

    int v;
    q.pop(v);  // drain one slot

    EXPECT_TRUE(q.push(99));  // now there's room
}

// ─────────────────────────────────────────────────────────────────────────────
// FIFO ordering — the most fundamental correctness property of a queue
// ─────────────────────────────────────────────────────────────────────────────
TEST(SpscQueueTest, FIFOOrdering) {
    SpscQueue<int> q(64);
    for (int i = 0; i < 20; ++i) q.push(i);

    for (int i = 0; i < 20; ++i) {
        int v = -1;
        ASSERT_TRUE(q.pop(v));
        EXPECT_EQ(v, i);
    }
}

TEST(SpscQueueTest, FIFOPreservedOverWrapAround) {
    // The ring wraps around modulo capacity. Verify ordering survives wrapping.
    SpscQueue<int> q(8);  // capacity 8, holds 7
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 5; ++i) q.push(round * 100 + i);
        for (int i = 0; i < 5; ++i) {
            int v;
            ASSERT_TRUE(q.pop(v));
            EXPECT_EQ(v, round * 100 + i);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-threaded producer / consumer
//
// The SPSC contract: exactly ONE thread calls push(), exactly ONE calls pop().
// These tests verify correctness under concurrent use — the scenario the queue
// is designed for. If the atomics or memory ordering were wrong, the consumer
// would see stale, partial, or out-of-order writes.
// ─────────────────────────────────────────────────────────────────────────────
TEST(SpscQueueTest, ProducerConsumerThroughput) {
    constexpr int kMessages = 1'000'000;
    SpscQueue<int> q(65'536);

    int received = 0;

    std::thread producer([&] {
        for (int i = 0; i < kMessages; ++i)
            while (!q.push(i)) {}   // retry if full
    });

    std::thread consumer([&] {
        int v;
        while (received < kMessages)
            if (q.pop(v)) ++received;
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(received, kMessages);
}

TEST(SpscQueueTest, ProducerConsumerOrderPreserved) {
    // Record every popped value and verify strict ordering.
    constexpr int kMessages = 100'000;
    SpscQueue<int> q(65'536);
    std::vector<int> results;
    results.reserve(kMessages);

    std::thread producer([&] {
        for (int i = 0; i < kMessages; ++i)
            while (!q.push(i)) {}
    });

    std::thread consumer([&] {
        int v;
        while (static_cast<int>(results.size()) < kMessages)
            if (q.pop(v)) results.push_back(v);
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(static_cast<int>(results.size()), kMessages);
    for (int i = 0; i < kMessages; ++i)
        EXPECT_EQ(results[i], i) << "ordering broken at index " << i;
}

TEST(SpscQueueTest, ZeroDroppedUnderLoad) {
    // Every pushed item must be consumed exactly once.
    constexpr int kMessages = 500'000;
    SpscQueue<int> q(65'536);
    std::atomic<int64_t> sum_sent{0}, sum_received{0};

    std::thread producer([&] {
        for (int i = 1; i <= kMessages; ++i) {
            while (!q.push(i)) {}
            sum_sent.fetch_add(i, std::memory_order_relaxed);
        }
    });

    std::thread consumer([&] {
        int v, count = 0;
        while (count < kMessages) {
            if (q.pop(v)) {
                sum_received.fetch_add(v, std::memory_order_relaxed);
                ++count;
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(sum_sent.load(), sum_received.load());
}
