// FIX Order Simulator
//
// PURPOSE
// Generates a file of synthetic FIX New Order Single messages (config/orders.fix)
// for use by the FixSessionReader during simulation. In a live system, these
// messages would arrive over a TCP FIX session from a broker or OMS (Order
// Management System). Here we generate them in advance so the simulation is
// deterministic and self-contained.
//
// GENERATED FORMAT
// Each line is one FIX order:
//   11=ORD00001|35=D|55=AAPL|54=1|44=150.00|38=30
//
// MARKET SIMULATION
// Prices evolve using a random walk (Brownian motion): each tick adds
// Gaussian noise scaled by the symbol's volatility parameter. This is the
// same model used in the Black-Scholes option pricing formula — the log-
// returns of a stock are assumed to be normally distributed. Here we add
// the noise directly to the price (arithmetic random walk) for simplicity.

#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

struct SimSymbol {
    std::string symbol;
    double      currentPrice;
    double      vol;  // per-step price noise std deviation (in $)
};

int main() {
    std::vector<SimSymbol> symbols = {
        {"AAPL",   150.00, 0.10},
        {"MSFT",   300.00, 0.15},
        {"GOOGL",  140.00, 0.10},
        {"TSLA",   200.00, 0.20},
        {"ESZ4",  4500.00, 1.00},
        {"GBPUSD",   1.25, 0.002},
    };

    // Seed 42 makes the output deterministic across runs — useful for
    // debugging because the same order sequence reproduces the same P&L.
    std::mt19937                       rng(42);
    std::normal_distribution<double>   noise(0.0, 1.0);
    std::uniform_int_distribution<int> dist_sym(0, static_cast<int>(symbols.size()) - 1);
    std::uniform_int_distribution<int> dist_qty(10, 100);  // order size: 10–100 units
    std::uniform_int_distribution<int> dist_side(0, 1);    // 0 = Buy, 1 = Sell

    std::ofstream out("config/orders.fix");
    if (!out.is_open()) {
        std::cerr << "Could not open config/orders.fix\n";
        return 1;
    }

    for (int i = 1; i <= 10000; ++i) {
        SimSymbol& sym = symbols[static_cast<std::size_t>(dist_sym(rng))];

        // Random walk: price += N(0, vol). Floor at $0.01 to prevent negative prices.
        sym.currentPrice += noise(rng) * sym.vol;
        if (sym.currentPrice < 0.01) sym.currentPrice = 0.01;

        const int  side = dist_side(rng);
        const int  qty  = dist_qty(rng);

        // Write one FIX New Order Single line.
        // std::setw + std::setfill produce zero-padded IDs: ORD00001, ORD00002, ...
        out << "11=ORD" << std::setw(5) << std::setfill('0') << i
            << "|35=D"                        // 35=D: New Order Single message type
            << "|55=" << sym.symbol            // 55: symbol
            << "|54=" << (side == 0 ? "1" : "2") // 54: 1=Buy 2=Sell
            << "|44=" << std::fixed << std::setprecision(2) << sym.currentPrice
            << "|38=" << qty << "\n";
    }

    out.close();
    std::cout << "Generated 10,000 orders to config/orders.fix\n";
    return 0;
}
