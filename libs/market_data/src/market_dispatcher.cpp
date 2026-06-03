#include "market_dispatcher.h"

void MarketDispatcher::subscribe(const std::string& symbol,
                                 const std::function<void(Tick)>& callback) {
    if (!symbol.empty()) {
        // Pad to 8 bytes so sym_key() always reads a consistent value.
        char sym8[8] = {};
        const std::size_t n = std::min(symbol.size(), sizeof(sym8));
        std::memcpy(sym8, symbol.c_str(), n);
        callbacks_[sym_key(sym8)].push_back(callback);
    }
}

void MarketDispatcher::dispatch(const Tick& tick) {
    // sym_key() is a single memcpy + implicit load — no heap, no hash over bytes.
    // tick.symbol is char[8] so memcpy always reads exactly 8 bytes (zero-padded).
    const auto it = callbacks_.find(sym_key(tick.symbol));
    if (it == callbacks_.end()) return;

    for (const auto& cb : it->second)
        cb(tick);
}

int MarketDispatcher::subscriberCount(const std::string& symbol) const {
    char sym8[8] = {};
    const std::size_t n = std::min(symbol.size(), sizeof(sym8));
    std::memcpy(sym8, symbol.c_str(), n);
    const auto it = callbacks_.find(sym_key(sym8));
    return (it == callbacks_.end()) ? 0 : static_cast<int>(it->second.size());
}
