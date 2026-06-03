#include "market_dispatcher.h"

#include <string>

void MarketDispatcher::subscribe(const std::string& symbol,
                                 const std::function<void(Tick)>& callback) {
    if (!symbol.empty())
        listCallback[symbol].push_back(callback);
}

void MarketDispatcher::dispatch(const Tick& tick) {
    // std::cout removed from here intentionally.
    // A std::cout call acquires an internal mutex and may perform a write() syscall,
    // adding microseconds of jitter to every tick on the hot UDP receive path.
    // Logging belongs on the cold path; the hot path should only push to queues.

    // tick.symbol is char[8]; std::string(tick.symbol) constructs a temporary key.
    // This involves a small heap allocation on each tick dispatch — a known cost.
    // Future improvement: replace std::unordered_map<std::string,...> with a
    // fixed-symbol lookup table (flat array + memcmp) to eliminate the allocation.
    const auto it = listCallback.find(std::string(tick.symbol));
    if (it == listCallback.end()) return;

    for (const auto& cb : it->second)
        cb(tick);
}

int MarketDispatcher::subscriberCount(const std::string& symbol) const {
    const auto it = listCallback.find(symbol);
    if (it == listCallback.end()) return 0;
    return static_cast<int>(it->second.size());
}
