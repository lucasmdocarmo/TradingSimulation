#include "metrics_server.h"

// Boost.Beast / Asio headers live only in this .cpp — hidden from the public API
// by the PIMPL in metrics_server.h.
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <chrono>
#include <sstream>
#include <thread>

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace ws    = beast::websocket;
using     tcp   = asio::ip::tcp;

// ─────────────────────────────────────────────────────────────────────────────
// MetricsServer::Impl — hidden implementation.
// ─────────────────────────────────────────────────────────────────────────────
struct MetricsServer::Impl {
    uint16_t                         port_;
    std::function<MetricsSnapshot()> snapshotFn_;
    std::atomic<bool>                running_{false};
    asio::io_context                 ioc_;
    std::unique_ptr<tcp::acceptor>   acceptor_;
    std::thread                      acceptThread_;

    Impl(uint16_t port, std::function<MetricsSnapshot()> fn)
        : port_(port), snapshotFn_(std::move(fn)) {}

    // ── JSON serialisation (no third-party JSON library needed) ─────────────
    // Produces a compact JSON object with every field in MetricsSnapshot.
    // Called on the per-client thread once per second — not performance-critical.
    static std::string toJson(const MetricsSnapshot& s) {
        std::ostringstream o;
        o << "{"
          << "\"tick_p50\":"            << s.tick_p50            << ","
          << "\"tick_p99\":"            << s.tick_p99            << ","
          << "\"tick_p999\":"           << s.tick_p999           << ","
          << "\"tick_max\":"            << s.tick_max            << ","
          << "\"tick_count\":"          << s.tick_count          << ","
          << "\"match_p50\":"           << s.match_p50           << ","
          << "\"match_p99\":"           << s.match_p99           << ","
          << "\"match_p999\":"          << s.match_p999          << ","
          << "\"match_max\":"           << s.match_max           << ","
          << "\"match_count\":"         << s.match_count         << ","
          << "\"dropped_ticks\":"       << s.dropped_ticks       << ","
          << "\"dropped_execs\":"       << s.dropped_execs       << ","
          << "\"tick_queue_depth\":"    << s.tick_queue_depth    << ","
          << "\"exec_queue_depth\":"    << s.exec_queue_depth    << ","
          << "\"tick_queue_capacity\":" << s.tick_queue_capacity << ","
          << "\"exec_queue_capacity\":" << s.exec_queue_capacity
          << "}";
        return o.str();
    }

    // ── Per-client thread ────────────────────────────────────────────────────
    // Performs the WebSocket handshake, then broadcasts a JSON snapshot every
    // second until the client disconnects or the server is stopped.
    //
    // WHY A THREAD PER CLIENT?
    // Synchronous Beast is the simplest correct design for a low-traffic
    // monitoring endpoint (we expect 1–3 dashboard connections at a time).
    // For production use with many clients, replace with an async strand or
    // a single-threaded event loop — but that adds significant complexity for
    // no meaningful gain at this scale.
    void serveClient(tcp::socket sock) {
        try {
            ws::stream<tcp::socket> wss{std::move(sock)};

            // Perform the HTTP → WebSocket upgrade handshake.
            // Beast handles the "Upgrade: websocket" / "101 Switching Protocols"
            // exchange automatically.
            wss.accept();

            while (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (!running_) break;

                const std::string json = toJson(snapshotFn_());

                beast::error_code ec;
                // wss.write() sends a single WebSocket text frame.
                // The browser / wscat client receives it as a complete message.
                wss.write(asio::buffer(json), ec);
                if (ec) break;  // client disconnected
            }

            beast::error_code ec;
            wss.close(ws::close_code::normal, ec);
        } catch (...) {
            // Any error (bad handshake, broken pipe) — silently terminate the
            // thread rather than crashing the whole server.
        }
    }

    // ── Accept loop ──────────────────────────────────────────────────────────
    // Blocks on acceptor_->accept() until a client connects. When stop() is
    // called, acceptor_->close() causes the pending accept() to return with
    // operation_aborted, the loop checks `!running_` and exits cleanly.
    void acceptLoop() {
        while (running_) {
            tcp::socket sock{ioc_};
            boost::system::error_code ec;
            acceptor_->accept(sock, ec);

            if (ec || !running_) break;

            // Detach: the client thread runs independently.
            // If the server stops while a client is connected, the next
            // wss.write() call will fail and the thread will exit on its own.
            std::thread([this, s = std::move(sock)]() mutable {
                serveClient(std::move(s));
            }).detach();
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────
MetricsServer::MetricsServer(uint16_t port,
                             std::function<MetricsSnapshot()> snapshotFn)
    : impl_(std::make_unique<Impl>(port, std::move(snapshotFn))) {}

MetricsServer::~MetricsServer() { stop(); }

void MetricsServer::start() {
    impl_->running_ = true;

    // Create the acceptor: binds to 0.0.0.0:<port> and starts listening.
    // reuse_address allows restarting the process immediately after it exits
    // without waiting for the OS TIME_WAIT period to expire.
    impl_->acceptor_ = std::make_unique<tcp::acceptor>(
        impl_->ioc_,
        tcp::endpoint{tcp::v4(), impl_->port_});
    impl_->acceptor_->set_option(asio::socket_base::reuse_address(true));

    impl_->acceptThread_ = std::thread([this] { impl_->acceptLoop(); });
}

void MetricsServer::stop() {
    if (!impl_->running_.exchange(false)) return;  // already stopped

    if (impl_->acceptor_) {
        boost::system::error_code ec;
        // Closing the acceptor unblocks the pending accept() call in acceptLoop().
        // The call returns with operation_aborted; we check !running_ and exit.
        impl_->acceptor_->close(ec);
    }

    if (impl_->acceptThread_.joinable())
        impl_->acceptThread_.join();
}
