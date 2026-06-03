#pragma once

#include <cstddef>
#include <vector>

// Heap-backed slab allocator.  alloc() / release() are O(1) with no heap
// traffic after construction — just pointer moves in the free-stack.
//
// Why heap (std::vector) instead of stack (std::array):
//   An ObjectPool<Order, 65536> stores 65536 * 64 bytes = 4 MB of Order objects.
//   If this lived inside an OrderBook member (which is constructed inside a thread
//   lambda), it would be placed on the thread's stack.  Default thread stacks are
//   1–8 MB on Linux/macOS — a 4 MB automatic variable would overflow immediately.
//   std::vector allocates on the heap: one large, contiguous block with exactly the
//   same cache-line access pattern as std::array, but no stack pressure.
//
// Alignment note (C++17):
//   std::allocator<T> calls operator new(size_t, std::align_val_t) for over-aligned
//   types (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__).  For alignas(64) Order,
//   this guarantees the slab base address is 64-byte aligned — same as a stack array.
//
// NOT thread-safe by design.  The pool is owned by a single hot-path thread; using
// it from multiple threads would race on `top_` with no synchronisation overhead.
template <typename T, std::size_t CAPACITY = 65536>
class ObjectPool {
public:
    ObjectPool() : slab_(CAPACITY), free_stack_(CAPACITY), top_(CAPACITY) {
        for (std::size_t i = 0; i < CAPACITY; ++i)
            free_stack_[i] = &slab_[i];
    }

    // O(1) allocation: pop a pointer from the free-stack.
    // Returns nullptr if the pool is exhausted — treat as a fatal/alert condition.
    [[nodiscard]] T* alloc() noexcept {
        if (top_ == 0) [[unlikely]] return nullptr;
        return free_stack_[--top_];
    }

    // O(1) deallocation: push the pointer back onto the free-stack.
    // The most-recently-freed slot is returned first (LIFO), keeping it hot in cache.
    void release(T* obj) noexcept {
        free_stack_[top_++] = obj;
    }

    std::size_t available() const noexcept { return top_; }
    static constexpr std::size_t capacity() noexcept { return CAPACITY; }

private:
    std::vector<T>   slab_;        // contiguous block: CAPACITY * sizeof(T) bytes
    std::vector<T*>  free_stack_;  // stack of pointers into slab_
    std::size_t      top_;
};
