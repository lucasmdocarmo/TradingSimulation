#include "market_dispatcher.h"

#include <gtest/gtest.h>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a Tick with just a symbol set.
// ─────────────────────────────────────────────────────────────────────────────
static Tick makeTick(const char* symbol,
                     double bid = 0.0, double ask = 0.0,
                     double last = 0.0, int64_t ts = 0) {
    Tick t{};
    strncpy(t.symbol, symbol, sizeof(t.symbol) - 1);
    t.bid = bid; t.ask = ask; t.last = last; t.timestamp_ns = ts;
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
// Routing
// ─────────────────────────────────────────────────────────────────────────────
TEST(MarketDispatcherTest, DispatchesToCorrectSymbol) {
    MarketDispatcher d;
    int aapl = 0, msft = 0;

    d.subscribe("AAPL", [&](Tick) { ++aapl; });
    d.subscribe("MSFT", [&](Tick) { ++msft; });

    d.dispatch(makeTick("AAPL"));

    EXPECT_EQ(aapl, 1);
    EXPECT_EQ(msft, 0);
}

TEST(MarketDispatcherTest, UnknownSymbolDoesNotFireCallback) {
    MarketDispatcher d;
    d.subscribe("AAPL", [](Tick) { FAIL() << "should not be called"; });

    d.dispatch(makeTick("TSLA"));  // TSLA not subscribed
}

TEST(MarketDispatcherTest, MultipleSymbolsRoutedIndependently) {
    MarketDispatcher d;
    int a = 0, b = 0, c = 0;

    d.subscribe("AAPL",   [&](Tick) { ++a; });
    d.subscribe("MSFT",   [&](Tick) { ++b; });
    d.subscribe("GBPUSD", [&](Tick) { ++c; });

    d.dispatch(makeTick("MSFT"));
    d.dispatch(makeTick("GBPUSD"));
    d.dispatch(makeTick("GBPUSD"));

    EXPECT_EQ(a, 0);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(c, 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiple subscribers per symbol (fan-out)
// The dispatcher must call ALL registered callbacks for a symbol, not just one.
// ─────────────────────────────────────────────────────────────────────────────
TEST(MarketDispatcherTest, MultipleSubscribersSameSymbol) {
    MarketDispatcher d;
    int count = 0;

    d.subscribe("AAPL", [&](Tick) { ++count; });
    d.subscribe("AAPL", [&](Tick) { ++count; });
    d.subscribe("AAPL", [&](Tick) { ++count; });

    d.dispatch(makeTick("AAPL"));

    EXPECT_EQ(count, 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick data integrity
// The callback must receive the exact Tick that was dispatched — no mutation
// or truncation through the uint64_t symbol-key routing layer.
// ─────────────────────────────────────────────────────────────────────────────
TEST(MarketDispatcherTest, TickFieldsPassedThrough) {
    MarketDispatcher d;
    Tick received{};

    d.subscribe("AAPL", [&](Tick t) { received = t; });

    const Tick sent = makeTick("AAPL", 149.99, 150.01, 150.00, 987654321LL);
    d.dispatch(sent);

    EXPECT_DOUBLE_EQ(received.bid,          149.99);
    EXPECT_DOUBLE_EQ(received.ask,          150.01);
    EXPECT_DOUBLE_EQ(received.last,         150.00);
    EXPECT_EQ       (received.timestamp_ns, 987654321LL);
    EXPECT_STREQ    (received.symbol,       "AAPL");
}

// ─────────────────────────────────────────────────────────────────────────────
// subscriberCount
// ─────────────────────────────────────────────────────────────────────────────
TEST(MarketDispatcherTest, SubscriberCountZeroByDefault) {
    MarketDispatcher d;
    EXPECT_EQ(d.subscriberCount("AAPL"), 0);
}

TEST(MarketDispatcherTest, SubscriberCountIncrements) {
    MarketDispatcher d;
    d.subscribe("AAPL", [](Tick) {});
    d.subscribe("AAPL", [](Tick) {});
    EXPECT_EQ(d.subscriberCount("AAPL"), 2);
    EXPECT_EQ(d.subscriberCount("MSFT"), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Empty symbol guard
// subscribe("") should be silently ignored — no crash, no phantom callbacks.
// ─────────────────────────────────────────────────────────────────────────────
TEST(MarketDispatcherTest, EmptySymbolSubscribeIgnored) {
    MarketDispatcher d;
    d.subscribe("", [](Tick) { FAIL() << "should not be called"; });
    EXPECT_EQ(d.subscriberCount(""), 0);

    // Dispatching a zero-symbol tick also must not crash.
    Tick t{};
    d.dispatch(t);
}
