#pragma once

#include "market_dispatcher.h"

#include <atomic>

// Raw UDP socket listener for binary market data.
//
// WHY UDP (NOT TCP)?
// TCP guarantees delivery and ordering via acknowledgements and retransmission.
// These guarantees have a cost: round-trip latency per ACK, head-of-line blocking
// when a packet is lost, and TCP's congestion control algorithm adding jitter.
//
// In market data, a stale tick is worthless — if we miss tick #42 and receive
// tick #43, we do NOT want to wait for #42 to be retransmitted. We want #43 NOW.
// UDP delivers each packet immediately with no retransmission, no connection
// overhead, and no buffering. Missing packets are simply dropped, which is the
// correct behaviour for real-time price feeds.
//
// Real exchanges (CME, NASDAQ) use UDP multicast for their market data feeds
// (e.g. CME MDP 3.0, OPRA) for exactly this reason.
//
// BINARY PROTOCOL
// The wire format is sizeof(Tick) bytes of raw binary data — the exact in-memory
// representation of the Tick struct. This works correctly because:
//   1. Tick is a POD (Plain Old Data) type with no pointers or virtual functions
//   2. Both sender (udp_tick_simulator) and receiver run on the same machine
//      with the same ABI (byte order, struct layout, alignment)
// For cross-machine or cross-language use, explicit serialisation (FlatBuffers,
// SBE, Protobuf) would be required to handle endianness and padding differences.
class UDPFeedSubscriber {
public:
    // Binds to `port`, enters a blocking receive loop, and dispatches each
    // decoded Tick to the MarketDispatcher. Runs until stop() is called.
    void start(int port, MarketDispatcher& dispatcher);

    // Sets the running_ flag to false, causing the receive loop to exit
    // on its next iteration. Called from the main thread on SIGINT.
    void stop();

private:
    std::atomic_bool running_{false};
};
