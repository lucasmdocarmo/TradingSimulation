#pragma once

#include "tick.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Pub-Sub routing layer for market data ticks.
//
// PATTERN: OBSERVER / PUB-SUB
// The dispatcher decouples the source of market data (UDP socket) from its
// consumers (Risk Engine). The UDP subscriber knows nothing about who is
// listening; the Risk Engine knows nothing about where ticks come from.
// They communicate only through the dispatcher via registered callbacks.
//
// This is the Observer pattern: subscribers register callbacks, the dispatcher
// notifies all of them when an event (a tick) arrives.
//
// DATA STRUCTURE
// std::unordered_map<symbol, vector<callbacks>> — O(1) average lookup by symbol.
// Multiple callbacks per symbol are supported (e.g. both a risk engine and a
// strategy could subscribe to AAPL ticks independently).
//
// HOT PATH NOTE
// dispatch() is called on the UDP receive thread for every incoming tick.
// The std::function callbacks and the temporary std::string key construction
// add some overhead. In a production system, the dispatcher would use a flat
// symbol table (array indexed by a numeric instrument ID) to eliminate the map
// lookup entirely. See market_dispatcher.cpp for the inline explanation.
class MarketDispatcher {
public:
    // Registers a callback for a specific symbol (e.g. "AAPL").
    // Not thread-safe — call only at startup before threads are running.
    void subscribe(const std::string& symbol, const std::function<void(Tick)>& callback);

    // Routes an incoming tick to all callbacks registered for tick.symbol.
    // Called on the hot UDP receive thread — no blocking, no I/O.
    void dispatch(const Tick& tick);

    int subscriberCount(const std::string& symbol) const;

private:
    std::unordered_map<std::string, std::vector<std::function<void(Tick)>>> listCallback;
};
