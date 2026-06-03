#pragma once
#include "tick.h"
#include <functional>
#include <string>

/**
 * @class MarketDispatcher
 * @brief Implements a Pub-Sub routing layer for market data ticks.
 *
 * Allows consumers (e.g. RiskEngine) to subscribe to price updates for
 * specific instruments. When a tick arrives, it invokes all callbacks
 * registered for that symbol.
 */
class MarketDispatcher {
public:
  /**
   * @brief Registers a callback function for a specific symbol.
   */
  void subscribe(const std::string &symbol,
                 const std::function<void(Tick)> &callback);

  /**
   * @brief Routes an incoming tick to all subscribed listeners.
   */
  void dispatch(const Tick &tick);

  /**
   * @brief Returns the number of active subscriptions for a symbol.
   */
  int subscriberCount(const std::string &symbol) const;

private:
  // Maps a symbol to a list of callback functions
  std::unordered_map<std::string, std::vector<std::function<void(Tick)>>>
      listCallback;
};
