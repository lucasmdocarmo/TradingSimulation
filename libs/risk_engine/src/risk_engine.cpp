#include "risk_engine.h"

#include <cmath>
#include <iostream>
#include <string>

RiskEngine::RiskEngine(PositionManager& pm, VolatilitySurface& vs)
    : positionManager_(pm), volSurface_(vs) {}

void RiskEngine::onTick(const Tick& tick) {
    // tick.symbol is char[8]; construct std::string once for the map key.
    // This is on the cold path (draining the SPSC queue), so the allocation
    // overhead is acceptable — unlike the hot UDP receive path.
    latestPrices_[std::string(tick.symbol)] = tick.last;
}

void RiskEngine::runReport() const {
    std::cout << "------------ Risk Report ------------\n";

    for (const auto& [symbol, pos] : positionManager_.all()) {
        const auto it = latestPrices_.find(symbol);
        const double currentPrice = (it != latestPrices_.end())
            ? it->second : pos.averageEntryPrice;

        const double unrealisedPnl = (currentPrice - pos.averageEntryPrice) * pos.quantity;

        std::cout << symbol
                  << "  qty="           << pos.quantity
                  << "  price="         << currentPrice
                  << "  unrealisedPnl=" << unrealisedPnl
                  << "  realisedPnl="   << pos.realisedPnl << "\n";
    }
    std::cout << "------------------------------------------\n";
}

void RiskEngine::computeVar() const {
    double vaR = 0.0;

    for (const auto& [symbol, pos] : positionManager_.all()) {
        const auto it = latestPrices_.find(symbol);
        const double currentPrice = (it != latestPrices_.end())
            ? it->second : pos.averageEntryPrice;

        const double annualVol = volSurface_.getVol(currentPrice, 30);
        // Annualised → daily volatility: σ_daily = σ_annual / √252
        const double dailyVol = annualVol / std::sqrt(252.0);

        // Parametric VaR component for this position.
        // Assumes normal returns; no correlation between assets (conservative upper bound).
        vaR += pos.quantity * currentPrice * dailyVol;
    }

    // 1.645 is the z-score for a 95% one-tailed confidence interval under N(0,1).
    vaR *= 1.645;
    std::cout << "VaR (95%, 1-day): $" << vaR << "\n";
}
