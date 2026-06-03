#pragma once

#include <string>

// Snapshot of the current portfolio state for one instrument.
//
// QUANTITY SIGN CONVENTION
// Positive quantity = net long (we own the asset, profit if price rises).
// Negative quantity = net short (we owe the asset, profit if price falls).
// Zero = flat (no exposure).
//
// AVERAGE ENTRY PRICE (VWAP)
// When multiple fills build a position over time, the average entry price is
// calculated as a Volume-Weighted Average Price (VWAP):
//   newAvg = (existingQty * oldAvg + newQty * newPrice) / (existingQty + newQty)
// This is updated in PositionManager::onExecution for every partial fill.
//
// REALISED vs UNREALISED P&L
// Realised P&L: profit/loss locked in from trades that have been closed.
//   Long:  realisedPnl += (salePrice - entryPrice) * qty
//   Short: realisedPnl += (entryPrice - coverPrice) * qty
// This number only increases/decreases when we trade.
//
// Unrealised P&L: the current mark-to-market value of the open position.
//   unrealisedPnl = (currentMarketPrice - averageEntryPrice) * quantity
// This fluctuates with every tick and is computed on-the-fly in RiskEngine.
// It is stored here for convenience but is NOT persisted — it's always
// recomputed from the latest tick price.
struct Position {
    std::string symbol;
    double      quantity;            // net units held (positive = long, negative = short)
    double      averageEntryPrice;   // VWAP of all fills building this position
    double      realisedPnl;         // cumulative locked-in profit/loss
    double      unrealisedPnl;       // current mark-to-market (refreshed by RiskEngine)
};
