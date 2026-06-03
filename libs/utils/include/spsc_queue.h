#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

// Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer
//
// PURPOSE IN THIS SYSTEM
// The main challenge of the hot path is that we have two types of latency-
// critical work happening simultaneously:
//   1. Receiving and processing market data ticks (Hot Path 2, UDP thread)
//   2. Matching FIX orders in the order book (Hot Path 1, execution thread)
// Both of these produce data that the Risk Engine needs (ticks for mark-to-
// market prices, executions for position accounting). But the Risk Engine does
// expensive work (VaR, P&L reports) that we cannot do on the hot threads.
//
// The SPSC queue is the bridge: the hot threads PUSH data in nanoseconds and
// return immediately. The cold Risk Engine thread POPS and processes at its
// own pace, completely isolated from the execution latency.
//
// WHY NOT A MUTEX?
// A std::mutex blocks the calling thread until the lock is available. If the
// cold thread holds the lock while computing VaR, the hot UDP thread would
// stall — missing incoming ticks. Even a "fast" mutex can add 50-100ns of
// latency. In HFT, that latency is the difference between executing at the
// best price and missing it entirely.
//
// HOW IT WORKS WITHOUT LOCKS
// The queue uses two atomic integers: `tail_` (written only by the producer)
// and `head_` (written only by the consumer). Because each is written by
// exactly one thread, there is no data race. The memory ordering tags
// (memory_order_release / memory_order_acquire) create a "happens-before"
// relationship: every write the producer made before the tail_ store is
// guaranteed to be visible to the consumer after the head_ load. This is
// the minimal synchronisation necessary — cheaper than a full memory fence.
//
// LIMITATION
// "Single-Producer Single-Consumer" is a strict contract. If two threads
// called push() simultaneously, both would read the same tail_ value,
// compute the same next_tail, and write to the same slot — corrupting data.
// In this system, each queue has exactly one writer and one reader:
//   tickQueue:  writer = UDP thread,       reader = cold path (main thread)
//   execQueue:  writer = execution thread, reader = cold path (main thread)

template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(size_t capacity)
        : capacity_(capacity), buffer_(capacity) {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    // Called by the PRODUCER thread only.
    // Returns false (drop) if the queue is full — in real HFT, a full queue
    // is a critical alert (cold path is too slow). Here we tolerate drops
    // since this is a simulation.
    bool push(const T& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail    = (current_tail + 1) % capacity_;

        // Acquire: ensures we see the consumer's latest head_ update.
        // If queue is full (next_tail == head_), bail out immediately.
        if (next_tail == head_.load(std::memory_order_acquire))
            return false;

        buffer_[current_tail] = item;

        // Release: makes the item write above visible to the consumer
        // before the consumer can observe the updated tail_.
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Called by the CONSUMER thread only.
    // Returns false if the queue is empty.
    bool pop(T& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        // Acquire: ensures we see the producer's latest tail_ store,
        // and that the item write that preceded it is also visible.
        if (current_head == tail_.load(std::memory_order_acquire))
            return false;

        item = buffer_[current_head];

        // Release: makes the slot reusable by the producer.
        head_.store((current_head + 1) % capacity_, std::memory_order_release);
        return true;
    }

    // Approximate number of unconsumed items. Not exact under concurrent access
    // (two separate atomic loads), but accurate enough for monitoring dashboards.
    size_t size() const noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_relaxed);
        return (tail >= head) ? (tail - head) : (capacity_ - head + tail);
    }

    size_t capacity() const noexcept { return capacity_; }

private:
    const size_t      capacity_;
    std::vector<T>    buffer_;   // ring: indices wrap via modulo

    // alignas(64): head_ and tail_ are on separate cache lines.
    // Without this, both atomics would share a single 64-byte cache line.
    // When the producer writes tail_ and the consumer writes head_, the CPU
    // must transfer the entire shared cache line between cores — "false sharing".
    // Padding them to separate lines means each core writes its own line
    // without invalidating the other core's cache. This alone can halve latency.
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
};
