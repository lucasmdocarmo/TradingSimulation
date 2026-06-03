#include "position_manager.h"

#include <algorithm>
#include <string>

void PositionManager::onExecution(const Execution& exec) {
    // exec.symbol is char[8]; std::string(exec.symbol) for the unordered_map key.
    const std::string sym(exec.symbol);
    Position& position = positions_[sym];

    if (position.symbol.empty())
        position.symbol = sym;

    if (exec.side == Side::BUY) {
        if (position.quantity < 0) {
            // Currently net short — buying covers part or all of the short.
            const double coverQty = std::min(static_cast<double>(exec.quantity),
                                             -position.quantity);
            // Realised P&L on short cover: (entry − exit) × qty covered.
            position.realisedPnl += coverQty * (position.averageEntryPrice - exec.trade_price);
            position.quantity    += exec.quantity;

            if (position.quantity > 0) {
                // Buy was larger than the short — we flipped to net long.
                // The excess forms a new long at the current execution price.
                position.averageEntryPrice = exec.trade_price;
            }
        } else {
            // Flat or long — adding to the long. Update VWAP.
            const double newAvg =
                ((position.quantity * position.averageEntryPrice) +
                 (static_cast<double>(exec.quantity) * exec.trade_price)) /
                (position.quantity + exec.quantity);

            position.quantity          += exec.quantity;
            position.averageEntryPrice  = newAvg;
        }
    }

    if (exec.side == Side::SELL) {
        if (position.quantity > 0) {
            if (static_cast<double>(exec.quantity) >= position.quantity) {
                // Sell exceeds long — close the long, open a short with remainder.
                const double remainder = exec.quantity - position.quantity;
                position.realisedPnl  += position.quantity *
                                         (exec.trade_price - position.averageEntryPrice);
                position.quantity      = -remainder;
                position.averageEntryPrice = (remainder > 0) ? exec.trade_price : 0.0;
            } else {
                // Partial close of the long. Average entry price is unchanged.
                position.realisedPnl  += exec.quantity *
                                         (exec.trade_price - position.averageEntryPrice);
                position.quantity -= exec.quantity;
            }
        } else if (position.quantity == 0) {
            // Flat — open a new short position.
            position.quantity          = -static_cast<double>(exec.quantity);
            position.averageEntryPrice = exec.trade_price;
        } else {
            // Already short — adding to the short. Update VWAP.
            const double existingShort = -position.quantity;
            const double totalShort    = existingShort + exec.quantity;
            position.averageEntryPrice = (existingShort * position.averageEntryPrice +
                                          exec.quantity * exec.trade_price) / totalShort;
            position.quantity -= exec.quantity;
        }
    }
}

Position PositionManager::getPosition(const std::string& symbol) const {
    return positions_.at(symbol);
}

const std::unordered_map<std::string, Position>& PositionManager::all() const {
    return positions_;
}
