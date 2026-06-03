#pragma once

#include <cstdint>

enum class OrderType { BUY, SELL };

// alignas(64) places the whole struct on one cache line (64 bytes on x86/ARM).
// No std::string: fixed-size char arrays make this a Plain Old Data (POD) type.
//
// Why POD matters here:
//   1. The FIX reader constructs Orders by value and pushes them into the pool.
//      POD means the compiler can use memcpy for copies — no constructor overhead.
//   2. ObjectPool<Order> pre-fills the slab with raw memory; POD types require
//      no constructor call on alloc(), saving a branch per order.
//   3. `next` is an intrusive linked-list pointer used by PriceLevel queues.
//      Intrusive means we borrow a field inside the existing allocation instead
//      of heap-allocating a separate list node — zero extra memory per enqueued order.
//
// Size: id[16] + symbol[8] + price(8) + order_type(4) + quantity(4) +
//       arrival_ns(8) + next*(8) + pad(8) = 64 bytes.
struct alignas(64) Order {
    char      id[16];
    char      symbol[8];
    double    price;
    OrderType order_type;
    int32_t   quantity;
    int64_t   arrival_ns;
    Order*    next;         // intrusive link — used by PriceLevel FIFO queue
};
