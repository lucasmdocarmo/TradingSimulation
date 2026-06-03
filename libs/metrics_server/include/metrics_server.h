#pragma once

#include <cstdint>
#include <functional>
#include <memory>

// Snapshot of all hot-path metrics collected at a point in time.
// Plain-old-data — safe to copy across thread boundaries.
struct MetricsSnapshot {
    // Tick hot-path latency (ns): UDP receive → SPSC queue push
    int64_t  tick_p50,  tick_p99,  tick_p999,  tick_max;
    uint64_t tick_count;

    // End-to-end match latency (ns): FIX parse timestamp → Execution generated
    int64_t  match_p50, match_p99, match_p999, match_max;
    uint64_t match_count;

    // Queue health
    uint64_t dropped_ticks, dropped_execs;
    uint64_t tick_queue_depth, exec_queue_depth;
    uint64_t tick_queue_capacity, exec_queue_capacity;
};

// WebSocket metrics server.
//
// WHY PIMPL?
// The implementation uses Boost.Beast and Boost.Asio, which pull in hundreds of
// headers. Hiding them behind a PIMPL (Pointer to IMPLementation) boundary means
// every TU that includes metrics_server.h compiles without seeing Boost — keeping
// compile times fast and avoiding macro pollution in the rest of the codebase.
//
// HOW IT WORKS
// start() spawns a background accept thread. For each WebSocket client that
// connects, a detached thread calls snapshotFn() every second and broadcasts
// the JSON result as a WebSocket text frame. stop() closes the acceptor socket,
// unblocking the accept() call and letting the thread exit cleanly.
//
// CONNECT FROM BROWSER / WSCAT
//   wscat -c ws://localhost:9001
//   websocat ws://localhost:9001
class MetricsServer {
public:
    explicit MetricsServer(uint16_t port,
                           std::function<MetricsSnapshot()> snapshotFn);
    ~MetricsServer();

    void start();
    void stop();

    MetricsServer(const MetricsServer&)            = delete;
    MetricsServer& operator=(const MetricsServer&) = delete;

private:
    // Impl is defined only in metrics_server.cpp — no Boost headers leak here.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
