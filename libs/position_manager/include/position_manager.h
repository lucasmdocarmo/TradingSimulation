#pragma once
#include "execution.h"
#include "position.h"
#include <string>
#include <unordered_map>
/**
 * @class PositionManager
 * @brief Core ledger for tracking portfolio state.
 *
 * Maintains the current positions, average entry prices, and realised P&L
 * across all traded instruments. It processes incoming executions and applies
 * standard accounting rules for long and short positions.
 */
class PositionManager {

public:
  /**
   * @brief Updates the portfolio state based on a new execution.
   * @param exec The incoming execution (symbol, quantity, price, side).
   */
  void onExecution(const Execution &exec);

  /**
   * @brief Retrieves the current position for a specific symbol.
   */
  Position getPosition(const std::string &symbol) const;

  /**
   * @brief Returns a read-only view of all current positions.
   */
  const std::unordered_map<std::string, Position> &all() const;

private:
  // Maps a symbol (e.g. "AAPL") to its current Position state.
  std::unordered_map<std::string, Position> positions_;
};
