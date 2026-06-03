#pragma once

#include "position_manager.h"
#include "tick.h"
#include "vol_surface.h"
#include <map>
#include <string>
/**
 * @class RiskEngine
 * @brief Computes mark-to-market portfolio value and Value at Risk (VaR).
 *
 * This engine acts as a central sink for both executions (via PositionManager)
 * and live market prices. It periodically evaluates the portfolio to compute
 * unrealised P&L, realised P&L, and risk metrics like VaR.
 */
class RiskEngine {

public:
  RiskEngine(PositionManager &positionManager, VolatilitySurface &volSurface);
  
  /**
   * @brief Caches the latest tick price for a given symbol.
   * @param tick The latest market data tick.
   */
  void onTick(const Tick &);
  
  /**
   * @brief Prints a snapshot of current positions and their unrealised/realised P&L.
   */
  void runReport() const;
  
  /**
   * @brief Computes and prints the 95% 1-day Value at Risk (VaR) for the portfolio.
   */
  void computeVar() const;

private:
  PositionManager &positionManager_;
  VolatilitySurface &volSurface_;
  
  // Cache of the latest observed market price for each symbol
  std::map<std::string, double> latestPrices_;
};
