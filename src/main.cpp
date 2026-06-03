#include "fix_session_reader.h"
#include "instrument_store.h"
#include "market_dispatcher.h"
#include "order_book.h"
#include "position_manager.h"
#include "risk_engine.h"
#include "udp_feed_subscriber.h"
#include "vol_surface.h"
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>

volatile sig_atomic_t running = 1;
void handleSignal(int) { running = 0; }

int main() {
  std::signal(SIGINT, handleSignal);

  InstrumentStore store;
  store.loadFromCSV("config/instruments.csv");

  OrderBook orderBook;
  PositionManager positionManager;
  VolatilitySurface volSurface;
  volSurface.loadFromCSV("config/vol_surface.csv");

  RiskEngine riskEngine(positionManager, volSurface);

  FixSessionReader fixReader;
  fixReader.setOrderCallback([&](Order order) {
    Execution exec{};
    exec.symbol = order.symbol;
    exec.quantity = order.quantity;
    exec.trade_price = order.price;
    exec.side = (order.order_type == OrderType::BUY) ? Side::BUY : Side::SELL;
    exec.arrival = std::chrono::system_clock::now();
    positionManager.onExecution(exec);
  });
  fixReader.start("config/orders.fix");
  MarketDispatcher dispatcher;
  dispatcher.subscribe("AAPL", [&](Tick t) { riskEngine.onTick(t); });
  dispatcher.subscribe("MSFT", [&](Tick t) { riskEngine.onTick(t); });
  dispatcher.subscribe("GOOGL", [&](Tick t) { riskEngine.onTick(t); });
  dispatcher.subscribe("TSLA", [&](Tick t) { riskEngine.onTick(t); });
  dispatcher.subscribe("ESZ4", [&](Tick t) { riskEngine.onTick(t); });
  dispatcher.subscribe("GBPUSD", [&](Tick t) { riskEngine.onTick(t); });

  UDPFeedSubscriber subscriber;
  std::thread udpThread([&]() { subscriber.start(9000, dispatcher); });

  while (running) {
    riskEngine.runReport();
    riskEngine.computeVar();
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }

  subscriber.stop();
  udpThread.join();

  std::ofstream report("final_pnl_report.txt");
  for (const auto &[symbol, pos] : positionManager.all()) {
    report << symbol << "  qty=" << pos.quantity
           << "  avg=" << pos.averageEntryPrice
           << "  realised=" << pos.realisedPnl << "\n";
  }
  report.close();
  std::cout << "Final P&L report written to final_pnl_report.txt\n";

  return 0;
}
