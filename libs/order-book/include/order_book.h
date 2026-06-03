#pragma once

#include "execution.h"
#include "order.h"
#include <functional>
#include <map>
#include <queue>

#include <unordered_map>
class OrderBook {
public:
  void acceptNewOrder(const Order &order);
  void matchOrders(const std::string &symbol);
  double bestBid(const std::string &symbol);
  double bestAsk(const std::string &symbol);
  Execution buildExecution(int &tradeQty, const Order &ask, const Order &bid,
                           const std::string &symbol);
  void setExecutionCallback(const std::function<void(Execution)> callback) {
    onExecution_ = callback;
  }

private:
  std::unordered_map<std::string,
                     std::map<double, std::queue<Order>, std::greater<double>>>
      bids_;

  std::unordered_map<std::string, std::map<double, std::queue<Order>>> asks_;
  std::function<void(Execution)> onExecution_;
};
