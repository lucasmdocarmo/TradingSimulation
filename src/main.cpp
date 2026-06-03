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
#include "../libs/utils/include/latency_tracker.h"

#include <atomic>
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

    // ── Metrics ──────────────────────────────────────────────────────────────
    // tickHotLatency:  time from tick generation (timestamp_ns set by sender)
    //                  to the moment it is pushed onto the SPSC queue.
    //                  Measures end-to-end UDP delivery + dispatch overhead.
    //
    // matchLatency:    exec.arrival_ns - exec.order_arrival_ns.
    //                  The aggressor order's FIX parse timestamp to the moment
    //                  the matching engine fires the Execution. True E2E latency.
    //
    // Both trackers use memory_order_relaxed atomics — safe to write from the
    // hot threads and read from the cold path without a synchronisation fence.
    LatencyTracker tickHotLatency;
    LatencyTracker matchLatency;

    // Dropped-event counters: incremented when the SPSC queue is full and push()
    // returns false. A non-zero count means the hot path is producing faster than
    // the cold path can drain — the queue needs to be sized larger or the cold
    // path needs to drain faster.
    std::atomic<uint64_t> droppedTicks{0};
    std::atomic<uint64_t> droppedExecs{0};

    // Lock-free SPSC queues — the only synchronisation between hot and cold paths.
    // 100,000 slots × sizeof(Tick/Execution): each queue occupies ~4–6 MB.
    // Sized generously to absorb bursts; the cold path drains in a tight spin loop.
    SpscQueue<Tick>      tickQueue(100'000);
    SpscQueue<Execution> execQueue(100'000);

    // ── HOT PATH 1: UDP Market Data ─────────────────────────────────────────
    MarketDispatcher dispatcher;

    // Record tick hot-path latency on every push. The Tick::timestamp_ns was set
    // by the simulator using steady_clock, so now_ns() - timestamp_ns gives the
    // true one-way delivery latency from sender to queue entry.
    auto tickCallback = [&](Tick t) {
        tickHotLatency.record(now_ns() - t.timestamp_ns);
        if (!tickQueue.push(t)) [[unlikely]]
            droppedTicks.fetch_add(1, std::memory_order_relaxed);
    };

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
        orderBook.setExecutionCallback([&](Execution exec) {
            if (!execQueue.push(exec)) [[unlikely]]
                droppedExecs.fetch_add(1, std::memory_order_relaxed);
        });

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

    // ── COLD PATH: Risk + Metrics ─────────────────────────────────────────────
    PositionManager positionManager;
    RiskEngine      riskEngine(positionManager, volSurface);

    // Cold-path drain statistics (single-threaded, no atomics needed).
    uint64_t totalDrainCycles  = 0;
    uint64_t maxTickBatch      = 0;   // largest burst of ticks drained in one cycle
    uint64_t maxExecBatch      = 0;   // largest burst of executions in one cycle
    uint64_t maxTickQueueDepth = 0;   // peak observed SPSC fill level for ticks
    uint64_t maxExecQueueDepth = 0;   // peak observed SPSC fill level for executions

    auto lastReportTime = std::chrono::steady_clock::now();

    while (running) {
        ++totalDrainCycles;

        // Sample queue depth before draining — this is the "queue pressure" metric.
        // High pressure (depth close to capacity) means the hot path is outrunning
        // the cold path and the queue is at risk of dropping events.
        const auto tickDepth = tickQueue.size();
        const auto execDepth = execQueue.size();
        if (tickDepth > maxTickQueueDepth) maxTickQueueDepth = tickDepth;
        if (execDepth > maxExecQueueDepth) maxExecQueueDepth = execDepth;

        Tick t;
        uint64_t tickBatch = 0;
        while (tickQueue.pop(t)) {
            riskEngine.onTick(t);
            ++tickBatch;
        }
        if (tickBatch > maxTickBatch) maxTickBatch = tickBatch;

        Execution e;
        uint64_t execBatch = 0;
        while (execQueue.pop(e)) {
            // E2E matching latency: from the moment the FIX parser stamped the
            // order (order_arrival_ns) to the moment the execution was generated.
            matchLatency.record(e.arrival_ns - e.order_arrival_ns);
            positionManager.onExecution(e);
            ++execBatch;
        }
        if (execBatch > maxExecBatch) maxExecBatch = execBatch;

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastReportTime).count() >= 5) {

            // ── Metrics Dashboard ───────────────────────────────────────────
            std::cout << "\n\033[1m═══ Metrics Dashboard ═══════════════════════════════════════\033[0m\n";

            tickHotLatency.report("tick hot-path (UDP → queue)");
            matchLatency.report  ("E2E match      (parse → exec)");

            const double tickFill = 100.0 * static_cast<double>(maxTickQueueDepth)
                                           / static_cast<double>(tickQueue.capacity());
            const double execFill = 100.0 * static_cast<double>(maxExecQueueDepth)
                                           / static_cast<double>(execQueue.capacity());

            std::cout << "  queue pressure (peak fill since last report):\n"
                      << "    tick queue: " << maxTickQueueDepth << " / "
                      << tickQueue.capacity() << "  (" << tickFill << "%)\n"
                      << "    exec queue: " << maxExecQueueDepth << " / "
                      << execQueue.capacity() << "  (" << execFill << "%)\n";

            std::cout << "  dropped events:  ticks=" << droppedTicks.load(std::memory_order_relaxed)
                      << "   execs=" << droppedExecs.load(std::memory_order_relaxed) << "\n";

            std::cout << "  cold-path drain: cycles=" << totalDrainCycles
                      << "   max_tick_batch=" << maxTickBatch
                      << "   max_exec_batch=" << maxExecBatch << "\n";

            // Reset per-report-window stats (latency histograms + pressure peaks).
            // Totals (droppedTicks/Execs, totalDrainCycles) are cumulative from startup.
            tickHotLatency.reset();
            matchLatency.reset();
            maxTickQueueDepth = 0;
            maxExecQueueDepth = 0;

            // ── Risk Report ─────────────────────────────────────────────────
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
