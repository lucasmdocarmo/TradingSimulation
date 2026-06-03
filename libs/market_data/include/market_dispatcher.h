#pragma once

#include "tick.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Pub-Sub routing layer for market data ticks.
//
// PATTERN: OBSERVER / PUB-SUB
// Decouples the UDP receiver from the risk engine. Neither knows about the other.
//
// HOT-PATH OPTIMIZATION: uint64_t symbol key
// The previous version used unordered_map<std::string, ...> and constructed
// a temporary std::string on every dispatch() call:
//   std::string key(tick.symbol);  ← SSO-allocated, hashed byte-by-byte
//
// The new version reinterprets char[8] as a uint64_t with a single memcpy.
// This is well-defined under [basic.types] because both types are trivially
// copyable and share the same size. Integer hashing is a single multiply +
// shift (~3 ns), vs string hashing (~8–12 ns for 4–6 char symbols).
//
// Before (every dispatch call): construct std::string → hash bytes → lookup
// After  (every dispatch call): memcpy 8 bytes → hash uint64 → lookup
class MarketDispatcher {
public:
    // Registers a callback for symbol (e.g. "AAPL"). Startup-only, not hot-path.
    void subscribe(const std::string& symbol, const std::function<void(Tick)>& callback);

    // Routes a tick to all callbacks registered for tick.symbol.
    // Called on the hot UDP receive thread — no locks, no heap allocation.
    void dispatch(const Tick& tick);

    int subscriberCount(const std::string& symbol) const;

private:
    // Reinterpret char[8] as uint64_t via memcpy — branch-free, one load.
    static uint64_t sym_key(const char* s) noexcept {
        uint64_t k = 0;
        std::memcpy(&k, s, sizeof(k));
        return k;
    }

    std::unordered_map<uint64_t, std::vector<std::function<void(Tick)>>> callbacks_;
};
