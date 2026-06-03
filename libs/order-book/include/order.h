#pragma once
#include <chrono>
#include <string>

enum class OrderType { BUY, SELL };

struct Order {
  std::string id;
  std::string symbol;
  double price;
  OrderType order_type;
  int quantity;
  std::chrono::system_clock::time_point arrival;
};
