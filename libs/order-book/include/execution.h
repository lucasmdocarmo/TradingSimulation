#pragma once
#include <chrono>
#include <string>

enum Side { BUY, SELL };
struct Execution {
  std::string buy_id;
  std::string sell_id;
  std::string symbol;
  double trade_price;
  double quantity;
  Side side;
  std::chrono::system_clock::time_point arrival;
};
