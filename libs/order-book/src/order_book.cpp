#include "order_book.h"
#include <algorithm>
#include <chrono>
using namespace std;
void OrderBook::acceptNewOrder(const Order &order) {
  if (order.order_type == OrderType::BUY) {
    bids_[order.symbol][order.price].push(order);
  } else {
    asks_[order.symbol][order.price].push(order);
  }
  matchOrders(order.symbol);
}

void OrderBook::matchOrders(const std::string &symbol) {

  while (true) {
    if (bids_[symbol].empty() || asks_[symbol].empty()) {
      return;
    }
    double bestBid = bids_[symbol].begin()->first;
    Order frontBidOrder = bids_[symbol].begin()->second.front();
    double bestAsk = asks_[symbol].begin()->first;
    Order frontAskOrder = asks_[symbol].begin()->second.front();

    if (bestBid < bestAsk) {
      return;
    }
    int tradeQty = min(frontAskOrder.quantity, frontBidOrder.quantity);

    Execution exec =
        buildExecution(tradeQty, frontAskOrder, frontBidOrder, symbol);
    if (onExecution_) {
      onExecution_(exec);
    };
    bids_[symbol].begin()->second.front().quantity -= tradeQty;
    asks_[symbol].begin()->second.front().quantity -= tradeQty;
    if (bids_[symbol].begin()->second.front().quantity == 0) {
      bids_[symbol].begin()->second.pop();
    }
    if (bids_[symbol].begin()->second.empty()) {
      bids_[symbol].erase(bids_[symbol].begin());
    }
    if (asks_[symbol].begin()->second.front().quantity == 0) {
      asks_[symbol].begin()->second.pop();
    }
    if (asks_[symbol].begin()->second.empty()) {
      asks_[symbol].erase(asks_[symbol].begin());
    }
  }
}

Execution OrderBook::buildExecution(int &tradeQty, const Order &ask,
                                    const Order &bid, const string &symbol) {
  Execution execution{};
  execution.buy_id = ask.id;
  execution.sell_id = bid.id;
  execution.quantity = tradeQty;
  execution.symbol = symbol;
  execution.trade_price = ask.price;
  execution.arrival = chrono::system_clock::now();

  return execution;
};
double OrderBook::bestBid(const std::string &symbol) {
  if (bids_[symbol].empty()) {
    return 0.0;
  } else {
    return bids_[symbol].begin()->first;
  }
}
double OrderBook::bestAsk(const std::string &symbol) {
  if (asks_[symbol].empty()) {
    return 0.0;
  } else {
    return asks_[symbol].begin()->first;
  }
}
