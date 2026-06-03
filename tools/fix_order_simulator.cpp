#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

struct SimSymbol {
  std::string symbol;
  double currentPrice;
  double vol;
};

int main() {
  std::vector<SimSymbol> symbols = {
      {"AAPL", 150.00, 0.10}, {"MSFT", 300.00, 0.15},  {"GOOGL", 140.00, 0.10},
      {"TSLA", 200.00, 0.20}, {"ESZ4", 4500.00, 1.00}, {"GBPUSD", 1.25, 0.002},
  };

  std::mt19937 rng(42);
  std::normal_distribution<double> noise(0.0, 1.0);
  std::uniform_int_distribution<int> dist_sym(0, (int)symbols.size() - 1);
  std::uniform_int_distribution<int> dist_qty(10, 100);
  std::uniform_int_distribution<int> dist_side(0, 1);

  std::ofstream out("config/orders.fix");
  if (!out.is_open()) {
    std::cerr << "Could not open config/orders.fix\n";
    return 1;
  }

  for (int i = 1; i <= 10000; ++i) {
    SimSymbol &sym = symbols[dist_sym(rng)];

    sym.currentPrice += noise(rng) * sym.vol;
    if (sym.currentPrice < 0.01)
      sym.currentPrice = 0.01;

    int side = dist_side(rng);
    int qty = dist_qty(rng);

    out << "11=ORD" << std::setw(5) << std::setfill('0') << i << "|35=D"
        << "|55=" << sym.symbol << "|54=" << (side == 0 ? "1" : "2")
        << "|44=" << std::fixed << std::setprecision(2) << sym.currentPrice
        << "|38=" << qty << "\n";
  }

  out.close();
  std::cout << "Generated 10,000 orders to config/orders.fix\n";
  return 0;
}
