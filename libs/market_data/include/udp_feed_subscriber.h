#pragma once
#include "market_dispatcher.h"
#include <atomic>

/**
 * @class UDPFeedSubscriber
 * @brief Subscribes to live market data ticks over a raw UDP socket.
 *
 * Runs a background loop that binds to a port and continuously receives UDP
 * datagrams. Each datagram is cast to a binary Tick struct and pushed to the
 * MarketDispatcher.
 */
class UDPFeedSubscriber {

public:
  /**
   * @brief Binds to the given port and begins receiving data.
   * @param port UDP port to listen on.
   * @param dispatcher Reference to the dispatcher for routing ticks.
   */
  void start(int port, MarketDispatcher &dispatcher);

  /**
   * @brief Signals the listening loop to stop.
   */
  void stop();

private:
  std::atomic_bool running_;
};
