#include "fix_session_reader.h"
#include "instrument_store.h"
#include "market_dispatcher.h"
#include "metrics_server.h"
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
    // tickHotLatency: fast_now_ns() - tick.timestamp_ns on the UDP hot path.
    //                 Measures one-way delivery + SPSC push overhead.
    //
    // matchLatency:   exec.arrival_ns - exec.order_arrival_ns.
    //                 Aggressor order's parse timestamp to Execution generated.
    //
    // Both use memory_order_relaxed — safe to write from hot threads and read
    // from the cold-path dashboard without a synchronisation fence.
    LatencyTracker tickHotLatency;
    LatencyTracker matchLatency;

    std::atomic<uint64_t> droppedTicks{0};
    std::atomic<uint64_t> droppedExecs{0};

    // Lock-free SPSC queues — the only synchronisation between hot and cold paths.
    SpscQueue<Tick>      tickQueue(100'000);
    SpscQueue<Execution> execQueue(100'000);

    // ── HOT PATH 1: UDP Market Data ─────────────────────────────────────────
    MarketDispatcher dispatcher;

    // fast_now_ns() replaces now_ns() here: this callback fires on every tick
    // on the UDP receive thread. We shave ~10 ns per call vs steady_clock.
    auto tickCallback = [&](Tick t) {
        tickHotLatency.record(fast_now_ns() - t.timestamp_ns);
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
    // fixReader.start() is now a template: the compiler instantiates it with the
    // exact lambda type below, inlining the orderBook.acceptNewOrder() call
    // directly into the mmap parse loop — no virtual dispatch, no std::function.
    std::thread executionThread([&]() {
        OrderBook orderBook;
        orderBook.setExecutionCallback([&](Execution exec) {
            if (!execQueue.push(exec)) [[unlikely]]
                droppedExecs.fetch_add(1, std::memory_order_relaxed);
        });

        FixSessionReader fixReader;
        fixReader.start("config/orders.fix",
                        [&](Order order) { orderBook.acceptNewOrder(order); });

        std::cout << "[exec] Pool slots remaining after FIX replay: "
                  << orderBook.poolAvailable() << " / "
                  << ObjectPool<Order, 65536>::capacity() << "\n";
    });

    // CPU affinity: pin hot-path threads to dedicated cores.
    pin_thread_to_core(udpThread,       1);
    pin_thread_to_core(executionThread, 2);

    // ── COLD PATH: Risk + Metrics ─────────────────────────────────────────────
    PositionManager positionManager;
    RiskEngine      riskEngine(positionManager, volSurface);

    // ── WebSocket Metrics Server (Boost.Beast) ────────────────────────────────
    // Broadcasts a JSON snapshot to every connected WebSocket client every second.
    //
    // Connect with:  wscat -c ws://localhost:9001
    //                websocat ws://localhost:9001
    //
    // The snapshot lambda captures the trackers and queues by reference.
    // It is called on the metrics server's per-client thread — reads are safe
    // because all LatencyTracker accessors use memory_order_relaxed atomics, and
    // SpscQueue::size() is a relaxed load on the write-side atomic.
    MetricsServer metricsServer(9001, [&]() -> MetricsSnapshot {
        return MetricsSnapshot{
            .tick_p50  = tickHotLatency.percentile(50.0),
            .tick_p99  = tickHotLatency.percentile(99.0),
            .tick_p999 = tickHotLatency.percentile(99.9),
            .tick_max  = tickHotLatency.max(),
            .tick_count = tickHotLatency.count(),

            .match_p50  = matchLatency.percentile(50.0),
            .match_p99  = matchLatency.percentile(99.0),
            .match_p999 = matchLatency.percentile(99.9),
            .match_max  = matchLatency.max(),
            .match_count = matchLatency.count(),

            .dropped_ticks = droppedTicks.load(std::memory_order_relaxed),
            .dropped_execs = droppedExecs.load(std::memory_order_relaxed),

            .tick_queue_depth    = tickQueue.size(),
            .exec_queue_depth    = execQueue.size(),
            .tick_queue_capacity = tickQueue.capacity(),
            .exec_queue_capacity = execQueue.capacity(),
        };
    });
    metricsServer.start();

    uint64_t totalDrainCycles  = 0;
    uint64_t maxTickBatch      = 0;
    uint64_t maxExecBatch      = 0;
    uint64_t maxTickQueueDepth = 0;
    uint64_t maxExecQueueDepth = 0;

    auto lastReportTime = std::chrono::steady_clock::now();

    // ── Adaptive Spin ─────────────────────────────────────────────────────────
    // If work was found (ticks or execs in the queues), loop immediately —
    // draining as fast as possible gives sub-millisecond cold-path latency.
    //
    // If the queues are empty, spin with CPU_PAUSE() for up to kSpinMax
    // iterations (~7 µs on a 3 GHz CPU at 14 cycles/PAUSE), then fall back
    // to a short sleep. This "adaptive" approach balances two extremes:
    //
    //   Pure spin: lowest latency, but burns a whole CPU core even when idle.
    //   Pure sleep: 1 ms dead time on every cycle — ~1000x worse latency.
    //
    // Adaptive: sub-10µs react time under load, ~5% CPU at idle.
    constexpr int kSpinMax = 500;
    int spin = 0;

    while (running) {
        ++totalDrainCycles;

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
            matchLatency.record(e.arrival_ns - e.order_arrival_ns);
            positionManager.onExecution(e);
            ++execBatch;
        }
        if (execBatch > maxExecBatch) maxExecBatch = execBatch;

        const bool didWork = tickBatch > 0 || execBatch > 0;
        if (didWork) {
            spin = 0;
        } else if (spin < kSpinMax) {
            CPU_PAUSE();
            ++spin;
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            spin = 0;
        }

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastReportTime).count() >= 5) {

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

            std::cout << "  metrics WebSocket: ws://localhost:9001\n";

            tickHotLatency.reset();
            matchLatency.reset();
            maxTickQueueDepth = 0;
            maxExecQueueDepth = 0;

            riskEngine.runReport();
            riskEngine.computeVar();
            lastReportTime = now;
        }
    }

    metricsServer.stop();
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
