#pragma once

#include <cstdint>

// Scoped enum (enum class) is preferred over unscoped (enum) in modern C++ because:
//   - It does NOT implicitly convert to int — prevents the exact bug we had where
//     a zero-initialised Side was silently treated as BUY.
//   - Its enumerators are scoped: Side::BUY, not just BUY — no namespace pollution.
//   - It is NOT implicitly zero-initialised unless explicitly default-constructed,
//     making uninitialised use a compile error when the type system catches it.
enum class Side { BUY, SELL };

// POD execution record passed through the lock-free SPSC queue.
// Fixed-size char arrays eliminate heap allocation and make this trivially copyable.
//
// A single Execution captures one matched trade between a buyer and a seller.
// The `side` field records the aggressor side (the order that triggered the match),
// which the PositionManager uses to decide whether to add to or reduce a position.
struct Execution {
    char    buy_id[16];
    char    sell_id[16];
    char    symbol[8];
    double  trade_price;
    double  quantity;
    Side    side;
    int64_t arrival_ns;       // when the execution was generated (steady_clock)
    int64_t order_arrival_ns; // when the aggressor order was parsed from FIX
    // E2E matching latency = arrival_ns - order_arrival_ns
};
