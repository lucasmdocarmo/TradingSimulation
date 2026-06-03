#pragma once

#include "execution.h"
#include "position.h"

#include <string>
#include <unordered_map>

// Central P&L ledger for the entire portfolio.
//
// OWNERSHIP MODEL
// The PositionManager is owned exclusively by the cold-path thread (main thread).
// It is NEVER accessed from the hot-path threads. State updates arrive via
// Execution objects popped from the lock-free execQueue SPSC — the hot execution
// thread only pushes to that queue and never touches PositionManager directly.
//
// This single-threaded ownership is the key design decision that eliminates all
// locking on the accounting path. There are no mutexes, no atomics, no shared
// state — the PositionManager just processes events from a queue it alone reads.
//
// STATE MACHINE PER SYMBOL
// Each symbol has a Position that transitions through states:
//
//   Flat (qty=0)
//     │ BUY fill  → Long (qty>0):  VWAP updated, no realised P&L
//     │ SELL fill → Short (qty<0): averageEntryPrice = fill price
//
//   Long (qty>0)
//     │ SELL (partial) → Long (smaller): realised P&L += (fillPrice - entry) * qty
//     │ SELL (full)    → Flat: realised P&L += (fillPrice - entry) * fullQty
//     │ SELL (excess)  → Short: close long, open short with remainder
//     │ BUY            → Long (larger): VWAP updated
//
//   Short (qty<0)
//     │ BUY (partial) → Short (smaller): realised P&L += (entry - fillPrice) * qty
//     │ BUY (full)    → Flat
//     │ BUY (excess)  → Long: cover short, open long with remainder
//     │ SELL          → Short (larger): VWAP updated
class PositionManager {
public:
    // Updates position state from a newly matched execution.
    // Called only from the cold path after popping from execQueue.
    void onExecution(const Execution& exec);

    // Point-in-time lookup — throws if symbol not found.
    Position getPosition(const std::string& symbol) const;

    // Read-only view of all positions — used by RiskEngine for reporting.
    const std::unordered_map<std::string, Position>& all() const;

private:
    std::unordered_map<std::string, Position> positions_;
};
