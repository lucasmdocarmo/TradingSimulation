#pragma once

#include <boost/lockfree/queue.hpp>
#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────────
// MpmcQueue — Multiple-Producer Multiple-Consumer lock-free queue.
//
// WHEN TO USE MPMC INSTEAD OF SPSC
// Our SpscQueue is strictly single-producer / single-consumer. It is faster
// than MPMC (no CAS loops, just two relaxed atomic loads/stores) but cannot
// be used when more than one thread writes or reads.
//
// Scenario that requires MPMC here:
//   Primary FIX session ──┐
//                         ├──► MpmcQueue<Order> ──► OrderBook thread
//   Backup  FIX session ──┘
//
// Two FIX threads (primary + failover) each push Orders concurrently. The
// OrderBook thread pops and matches. SPSC can't handle two producers.
//
// HOW boost::lockfree::queue WORKS
// Each slot in the queue is a node in a Michael-Scott lock-free linked list.
// Producers use a CAS (compare-and-swap) on the tail pointer; consumers on
// the head. The ABA problem (a value being changed from A → B → A while a
// thread's CAS sees only A) is avoided via a "counted pointer" trick where
// the top bits of the pointer hold a monotonically increasing version number.
//
// COST COMPARED TO SPSC
//   SPSC  push/pop: ~3–5 ns  (one acquire-load + one release-store each)
//   MPMC  push/pop: ~15–40 ns (one CAS loop per operation, plus ABA guard)
//
// For the hot path (one FIX reader, one OrderBook), SPSC is always better.
// MPMC is only worth the overhead when you genuinely have multiple producers.
//
// T REQUIREMENTS
// T must be trivially constructable and destructable (Boost requirement).
// Our Order, Tick, and Execution POD types all satisfy this.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
class MpmcQueue {
public:
    // capacity is rounded up to the next power of 2 by Boost internally.
    explicit MpmcQueue(std::size_t capacity) : queue_(capacity) {}

    // Thread-safe for any number of concurrent producers.
    // Returns true if the item was enqueued; false if the queue is full.
    bool push(const T& item) noexcept { return queue_.push(item); }

    // Thread-safe for any number of concurrent consumers.
    // Returns true and writes to `out` if an item was available; false if empty.
    bool pop(T& out) noexcept { return queue_.pop(out); }

    // Approximate — may be stale by the time the caller reads it.
    bool empty() const noexcept { return queue_.empty(); }

private:
    boost::lockfree::queue<T> queue_;
};
