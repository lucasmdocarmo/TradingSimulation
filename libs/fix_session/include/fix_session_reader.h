#pragma once

#include "order.h"

#include <atomic>
#include <functional>
#include <string>

// FIX Protocol Session Reader
//
// WHAT IS FIX?
// FIX (Financial Information eXchange) is the universal messaging standard for
// electronic trading, used by virtually every exchange and broker globally.
// A FIX "New Order Single" message (type 35=D) is a pipe-delimited string of
// tag=value pairs that describes a trade order:
//
//   11=ORD00001|35=D|55=AAPL|54=1|44=150.00|38=50
//
//   Tag 11 (ClOrdID):  client order ID = "ORD00001"
//   Tag 35 (MsgType):  message type = D (New Order Single)
//   Tag 55 (Symbol):   instrument = "AAPL"
//   Tag 54 (Side):     1 = Buy, 2 = Sell
//   Tag 44 (Price):    limit price = $150.00
//   Tag 38 (OrderQty): quantity = 50 shares
//
// In production, FIX messages are sent over persistent TCP sessions with
// sequence numbers, heartbeats, and session-level state (login/logout/resend).
// This implementation is a simplified file-based reader that simulates the
// order flow by reading pre-generated messages from a flat file.
//
// IMPLEMENTATION
// Uses mmap() to map the orders.fix file into virtual address space, then
// parses tag-value pairs in-place — no heap allocation per line. See the
// .cpp file for the full mmap + MADV_SEQUENTIAL explanation.
class FixSessionReader {
public:
    // Opens and mmap's `filepath`, parses each line as a FIX order, and
    // invokes the registered callback for each valid order. Runs to end-of-file.
    void start(const std::string& filepath);

    // Signals the read loop to stop. May leave the file partially read.
    void stop();

    // Registers the callback invoked for every successfully parsed order.
    // The Order struct is passed by value — it is a small POD (64 bytes) and
    // the callback typically just pushes it into the OrderBook immediately.
    void setOrderCallback(const std::function<void(Order)>& callback);

    // In-place zero-copy parser — public so benchmarks can call it directly
    // without going through the full mmap session lifecycle.
    Order parseFixLine(const char* line, std::size_t len);

private:

    std::atomic_bool           running_{false};
    std::function<void(Order)> onOrder_;
};
