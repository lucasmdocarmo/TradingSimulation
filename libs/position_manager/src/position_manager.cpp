#include "position_manager.h"
#include <cstddef>

void PositionManager::onExecution(const Execution &exec) {
  Position &position = positions_[exec.symbol];
  if (position.symbol.empty()) {
    position.symbol = exec.symbol;
  }

  // Handle a BUY execution
  if (exec.side == Side::BUY) {
    if (position.quantity < 0) {
      // We are currently net short. Buying covers this short.
      // Calculate how much of the short is being covered.
      double coverQty = std::min((double)exec.quantity, -position.quantity);
      
      // Realised P&L on a short: (Entry Price - Exit Price) * Quantity Covered
      position.realisedPnl +=
          coverQty * (position.averageEntryPrice - exec.trade_price);
          
      position.quantity += exec.quantity;
      if (position.quantity > 0) {
        // The buy order was larger than our short position. We flipped to net long.
        // The remaining quantity forms a new long position at the execution price.
        position.averageEntryPrice = exec.trade_price;
      }
      // Note: If we are still short (quantity <= 0), averageEntryPrice stays as the original entry.
    } else {
      // We are long or flat, and adding to the long position.
      // Calculate the new Volume-Weighted Average Price (VWAP).
      double newAvg = ((position.quantity * position.averageEntryPrice) +
                       ((double)exec.quantity * exec.trade_price)) /
                      (position.quantity + exec.quantity);
      position.quantity += exec.quantity;
      position.averageEntryPrice = newAvg;
    }
  }

  // Handle a SELL execution
  if (exec.side == Side::SELL) {
    if (position.quantity > 0) {
      // We are currently net long. Selling closes the long.
      if ((double)exec.quantity >= position.quantity) {
        // The sell order is larger than our long position.
        // Close entire long, realise P&L, and open a short with remainder.
        double remainder = exec.quantity - position.quantity;
        
        // Realised P&L on a long: (Exit Price - Entry Price) * Quantity Closed
        position.realisedPnl +=
            position.quantity * (exec.trade_price - position.averageEntryPrice);
            
        position.quantity = -remainder; // Flip to net short
        
        // The new entry price for the short is the current execution price.
        position.averageEntryPrice = (remainder > 0) ? exec.trade_price : 0;
      } else {
        // Partial close of the long position. Average entry price remains unchanged.
        position.realisedPnl +=
            exec.quantity * (exec.trade_price - position.averageEntryPrice);
        position.quantity -= exec.quantity;
      }
    } else if (position.quantity == 0) {
      // We are flat. Open a new short position.
      position.quantity = -(double)exec.quantity;
      position.averageEntryPrice = exec.trade_price;
    } else {
      // We are already short, and selling more adds to the short position.
      // Calculate new VWAP for the combined short position.
      double existingShort = -position.quantity;
      double totalShort = existingShort + exec.quantity;
      position.averageEntryPrice = (existingShort * position.averageEntryPrice +
                                    exec.quantity * exec.trade_price) /
                                   totalShort;
      position.quantity -= exec.quantity;
    }
  }
};
Position PositionManager::getPosition(const std::string &symbol) const {
  return positions_.at(symbol);
}

const std::unordered_map<std::string, Position> &PositionManager::all() const {
  return positions_;
}
