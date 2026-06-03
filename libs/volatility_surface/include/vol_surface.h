#pragma once

#include <map>
#include <string>

// Volatility Surface — a 2D interpolation table: strike × expiry → implied vol
//
// WHAT IS IMPLIED VOLATILITY?
// Options are priced using models like Black-Scholes, which take a volatility
// input σ (sigma). In reality, σ is not constant — it varies by strike price
// and time to expiry. The "implied volatility surface" captures this by storing
// the market-observed σ for every (strike, expiry) coordinate. A 3D plot of
// this data typically forms a "smile" or "skew" shape.
//
// HOW IT IS USED HERE
// The Risk Engine calls getVol(currentPrice, 30) to get the 30-day implied
// volatility at the current mark price. This σ is then used to compute the
// daily volatility (σ / √252) for the parametric VaR calculation.
//
// DATA STRUCTURE
// std::map<double, std::map<int, double>>: outer key = strike, inner key = expiry days.
// Both maps are sorted, enabling lower_bound() interpolation — if the exact
// strike/expiry is not in the surface, we snap to the nearest available point.
//
// TRADE-OFF
// std::map uses a Red-Black tree: O(log n) lookup with pointer-chasing (poor
// cache locality). This is acceptable here because getVol() is called on the
// cold path (risk reporting every 5 seconds), not the hot path. A future
// improvement would replace this with a sorted std::vector + binary search
// for better cache locality.
class VolatilitySurface {
public:
    // Parses a CSV with columns: strike, expiry_days, implied_vol
    // Example row: 150,30,0.25 — AAPL 30-day vol at $150 strike is 25%.
    void loadFromCSV(const std::string& path);

    // Returns the nearest implied vol for a given strike and expiry in days.
    // Uses lower_bound() on both axes — snaps up if the exact value isn't found.
    double getVol(double strike, int expiry);

private:
    // Outer key: strike price.  Inner key: days to expiry.  Value: annual σ.
    std::map<double, std::map<int, double>> volatilities_;
};
