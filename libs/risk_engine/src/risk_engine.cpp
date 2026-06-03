#include "risk_engine.h"
#include "position_manager.h"
#include "tick.h"
#include "vol_surface.h"
#include <cmath>
#include <iostream>
#include <map>
RiskEngine::RiskEngine(PositionManager &positionManager,
                       VolatilitySurface &volSurface)
    : positionManager_(positionManager), volSurface_(volSurface) {}
void RiskEngine::onTick(const Tick &tick) {
  latestPrices_[tick.symbol] = tick.last;
};
void RiskEngine::runReport() const {
  std::cout << "------------ Risk Report ------------\n";

  for (const auto &[symbol, pos] : positionManager_.all()) {
    // Use the latest market price if available; otherwise, fallback to entry price
    double currentPrice = latestPrices_.count(symbol) ? latestPrices_.at(symbol)
                                                      : pos.averageEntryPrice;
                                                      
    // Unrealised P&L = (Current Market Price - Entry Price) * Quantity
    // Positive quantity (long) benefits from price rise. 
    // Negative quantity (short) benefits from price drop.
    double unrealisedPnl =
        (currentPrice - pos.averageEntryPrice) * pos.quantity;
        
    std::cout << symbol << "  qty=" << pos.quantity
              << "  price=" << currentPrice
              << "  unrealisedPnl=" << unrealisedPnl
              << "  realisedPnl=" << pos.realisedPnl << "\n";
  }
  std::cout << "------------------------------------------\n";
}
void RiskEngine::computeVar() const {
  double vaR = 0.0;
  for (const auto &[symbol, pos] : positionManager_.all()) {
    double currentPrice = latestPrices_.count(symbol) ? latestPrices_.at(symbol)
                                                      : pos.averageEntryPrice;
    
    // Lookup implied annual volatility from the surface (assuming 30 days expiry)
    double annualVol = volSurface_.getVol(currentPrice, 30);
    
    // Convert annualised volatility to daily volatility (assuming 252 trading days)
    double dailyVol = annualVol / std::sqrt(252.0);
    
    // Simple parametric VaR component for this position
    // (Note: This simplistic summation assumes perfect positive correlation 
    // between assets, which is a conservative upper-bound approach for a portfolio)
    vaR += pos.quantity * currentPrice * dailyVol;
  }
  
  // Scale the standard deviation by 1.645 to achieve a 95% one-tailed confidence interval
  vaR = vaR * 1.645;
  std::cout << "VaR (95%, 1-day): $" << vaR << "\n";
};
