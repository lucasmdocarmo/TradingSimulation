#include "market_dispatcher.h"
#include "iostream"
#include <functional>
#include <string>
void MarketDispatcher::subscribe(const std::string &symbol,
                                 const std::function<void(Tick)> &callback) {
  if (!symbol.empty()) {
    // Add the callback to the list of listeners for this symbol
    listCallback[symbol].push_back(callback);
  }
};

void MarketDispatcher::dispatch(const Tick &tick) {
  std::cout << tick.symbol << " bid=" << tick.bid << " ask=" << tick.ask
            << "\n";
            
  // Find all listeners registered for this tick's symbol
  auto it = listCallback.find(tick.symbol);
  if (it == listCallback.end())
    return; // No listeners for this symbol
    
  // Invoke each registered callback synchronously
  for (auto &callback : it->second) {
    callback(tick);
  }
}

int MarketDispatcher::subscriberCount(const std::string &symbol) const {
  auto it = listCallback.find(symbol);
  if (it == listCallback.end())
    return 0;
  return it->second.size();
}
