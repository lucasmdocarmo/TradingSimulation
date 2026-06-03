#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

/**
 * @class SpscQueue
 * @brief A lock-free Single-Producer Single-Consumer ring buffer.
 *
 * Uses std::atomic with memory_order_release and memory_order_acquire 
 * to ensure safe, lock-free communication between exactly one writer
 * and one reader thread.
 * 
 * Cache line padding is applied to prevent false sharing between head and tail pointers.
 */
template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(size_t capacity) 
        : capacity_(capacity), 
          buffer_(capacity) {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    // Producer only
    bool push(const T& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) % capacity_;

        // If the queue is full, we cannot push
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[current_tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Consumer only
    bool pop(T& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        // If the queue is empty, we cannot pop
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        item = buffer_[current_head];
        head_.store((current_head + 1) % capacity_, std::memory_order_release);
        return true;
    }

private:
    const size_t capacity_;
    std::vector<T> buffer_;

    // alignas(64) forces head and tail to be on separate cache lines
    // typical L1 cache line is 64 bytes.
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
};
