#include "fix_session_reader.h"
#include "instrument_store.h"
#include "market_dispatcher.h"
#include "order_book.h"
#include "position_manager.h"
#include "risk_engine.h"
#include "udp_feed_subscriber.h"
#include "vol_surface.h"
#include "../libs/utils/include/spsc_queue.h"
#include "../libs/utils/include/cpu_affinity.h"

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

    VolatilitySurface volSurface;
    volSurface.loadFromCSV("config/vol_surface.csv");

    // Lock-free SPSC queues — the only synchronisation between hot and cold paths.
    // 100,000 slots × sizeof(Tick/Execution): each queue occupies ~4–6 MB.
    // Sized generously to absorb bursts; the cold path drains in a tight spin loop.
    SpscQueue<Tick>      tickQueue(100'000);
    SpscQueue<Execution> execQueue(100'000);

    // ── HOT PATH 1: UDP Market Data ─────────────────────────────────────────
    MarketDispatcher dispatcher;

    auto tickCallback = [&](Tick t) { tickQueue.push(t); };

    dispatcher.subscribe("AAPL",   tickCallback);
    dispatcher.subscribe("MSFT",   tickCallback);
    dispatcher.subscribe("GOOGL",  tickCallback);
    dispatcher.subscribe("TSLA",   tickCallback);
    dispatcher.subscribe("ESZ4",   tickCallback);
    dispatcher.subscribe("GBPUSD", tickCallback);

    UDPFeedSubscriber subscriber;
    std::thread udpThread([&]() { subscriber.start(9000, dispatcher); });

    // ── HOT PATH 2: FIX Execution ────────────────────────────────────────────
    // OrderBook is constructed inside the thread so the pool (4 MB slab) is
    // allocated on the stack of that thread's OS stack page — increasing the chance
    // it lands on NUMA-local memory for that core (on multi-socket systems).
    std::thread executionThread([&]() {
        OrderBook orderBook;
        orderBook.setExecutionCallback([&](Execution exec) { execQueue.push(exec); });

        FixSessionReader fixReader;
        fixReader.setOrderCallback([&](Order order) { orderBook.acceptNewOrder(order); });
        fixReader.start("config/orders.fix");

        std::cout << "[exec] Pool slots remaining after FIX replay: "
                  << orderBook.poolAvailable() << " / "
                  << ObjectPool<Order, 65536>::capacity() << "\n";
    });

    // CPU affinity: pin the two hot-path threads to dedicated cores.
    // Core layout: 0 = cold/main, 1 = udpThread, 2 = executionThread.
    // On macOS this is advisory; on Linux it is enforced by the scheduler.
    // Adjust core IDs based on the machine's topology (lscpu / sysctl hw.physicalcpu).
    pin_thread_to_core(udpThread,       1);
    pin_thread_to_core(executionThread, 2);

    // ── COLD PATH: Risk Reporting ─────────────────────────────────────────────
    PositionManager positionManager;
    RiskEngine      riskEngine(positionManager, volSurface);

    auto lastReportTime = std::chrono::steady_clock::now();

    while (running) {
        Tick t;
        while (tickQueue.pop(t))
            riskEngine.onTick(t);

        Execution e;
        while (execQueue.pop(e))
            positionManager.onExecution(e);

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastReportTime).count() >= 5) {
            riskEngine.runReport();
            riskEngine.computeVar();
            lastReportTime = now;
        }

        // 1 ms yield keeps cold-path CPU usage low without adding meaningful latency.
        // In a true HFT cold path, use _mm_pause() for a spin-wait that yields the
        // CPU pipeline without sleeping, achieving sub-microsecond reaction time.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    subscriber.stop();
    udpThread.join();
    executionThread.join();

    std::ofstream report("final_pnl_report.txt");
    for (const auto& [symbol, pos] : positionManager.all()) {
        report << symbol
               << "  qty="      << pos.quantity
               << "  avg="      << pos.averageEntryPrice
               << "  realised=" << pos.realisedPnl << "\n";
    }
    report.close();
    std::cout << "Final P&L report written to final_pnl_report.txt\n";

    return 0;
}
