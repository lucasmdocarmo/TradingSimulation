#include "position_manager.h"

#include <gtest/gtest.h>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Helper
// ─────────────────────────────────────────────────────────────────────────────
static Execution makeExec(const char* symbol, double qty,
                          double price, Side side) {
    Execution e{};
    strncpy(e.symbol, symbol, sizeof(e.symbol) - 1);
    e.quantity    = qty;
    e.trade_price = price;
    e.side        = side;
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────
// Long position
// ─────────────────────────────────────────────────────────────────────────────
TEST(PositionManagerTest, OpenLong) {
    PositionManager pm;
    pm.onExecution(makeExec("AAPL", 100, 150.00, Side::BUY));

    const auto& pos = pm.getPosition("AAPL");
    EXPECT_DOUBLE_EQ(pos.quantity,          100.0);
    EXPECT_DOUBLE_EQ(pos.averageEntryPrice, 150.00);
    EXPECT_DOUBLE_EQ(pos.realisedPnl,       0.0);
}

TEST(PositionManagerTest, AddToLongUpdatesVWAP) {
    PositionManager pm;
    pm.onExecution(makeExec("AAPL", 100, 150.00, Side::BUY));
    pm.onExecution(makeExec("AAPL", 100, 152.00, Side::BUY));

    const auto& pos = pm.getPosition("AAPL");
    EXPECT_DOUBLE_EQ(pos.quantity, 200.0);
    EXPECT_NEAR(pos.averageEntryPrice, 151.00, 0.01);  // (150+152)/2
    EXPECT_DOUBLE_EQ(pos.realisedPnl, 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Short position
// ─────────────────────────────────────────────────────────────────────────────
TEST(PositionManagerTest, OpenShort) {
    PositionManager pm;
    pm.onExecution(makeExec("TSLA", 50, 200.00, Side::SELL));

    const auto& pos = pm.getPosition("TSLA");
    EXPECT_DOUBLE_EQ(pos.quantity,          -50.0);
    EXPECT_DOUBLE_EQ(pos.averageEntryPrice, 200.00);
    EXPECT_DOUBLE_EQ(pos.realisedPnl,       0.0);
}

TEST(PositionManagerTest, AddToShortUpdatesVWAP) {
    PositionManager pm;
    pm.onExecution(makeExec("TSLA", 50, 200.00, Side::SELL));
    pm.onExecution(makeExec("TSLA", 50, 198.00, Side::SELL));

    const auto& pos = pm.getPosition("TSLA");
    EXPECT_DOUBLE_EQ(pos.quantity, -100.0);
    EXPECT_NEAR(pos.averageEntryPrice, 199.00, 0.01);  // (200+198)/2
}

// ─────────────────────────────────────────────────────────────────────────────
// Realised P&L
// ─────────────────────────────────────────────────────────────────────────────
TEST(PositionManagerTest, CloseLongProfitable) {
    // Buy 100 @ 150, sell 100 @ 151 → P&L = 100 × $1 = $100
    PositionManager pm;
    pm.onExecution(makeExec("AAPL", 100, 150.00, Side::BUY));
    pm.onExecution(makeExec("AAPL", 100, 151.00, Side::SELL));

    const auto& pos = pm.getPosition("AAPL");
    EXPECT_DOUBLE_EQ(pos.quantity, 0.0);
    EXPECT_NEAR(pos.realisedPnl, 100.0, 0.01);
}

TEST(PositionManagerTest, CloseLongAtLoss) {
    // Buy 100 @ 150, sell 100 @ 148 → P&L = 100 × -$2 = -$200
    PositionManager pm;
    pm.onExecution(makeExec("AAPL", 100, 150.00, Side::BUY));
    pm.onExecution(makeExec("AAPL", 100, 148.00, Side::SELL));

    const auto& pos = pm.getPosition("AAPL");
    EXPECT_DOUBLE_EQ(pos.quantity, 0.0);
    EXPECT_NEAR(pos.realisedPnl, -200.0, 0.01);
}

TEST(PositionManagerTest, CoverShortProfitable) {
    // Sell 100 @ 200 (short), buy back @ 195 → P&L = 100 × $5 = $500
    PositionManager pm;
    pm.onExecution(makeExec("TSLA", 100, 200.00, Side::SELL));
    pm.onExecution(makeExec("TSLA", 100, 195.00, Side::BUY));

    const auto& pos = pm.getPosition("TSLA");
    EXPECT_DOUBLE_EQ(pos.quantity, 0.0);
    EXPECT_NEAR(pos.realisedPnl, 500.0, 0.01);
}

TEST(PositionManagerTest, PartialCloseLocksInPnl) {
    // Buy 200 @ 150, sell 100 @ 152 → realised = 100 × $2 = $200; 100 still long
    PositionManager pm;
    pm.onExecution(makeExec("AAPL", 200, 150.00, Side::BUY));
    pm.onExecution(makeExec("AAPL", 100, 152.00, Side::SELL));

    const auto& pos = pm.getPosition("AAPL");
    EXPECT_DOUBLE_EQ(pos.quantity, 100.0);
    EXPECT_NEAR(pos.realisedPnl, 200.0, 0.01);
    EXPECT_NEAR(pos.averageEntryPrice, 150.00, 0.01);  // entry unchanged
}

// ─────────────────────────────────────────────────────────────────────────────
// Position flip (long → short, short → long)
// A sell order that exceeds the long position closes the long and opens a short
// in one atomic operation — important for correct VWAP on the new position.
// ─────────────────────────────────────────────────────────────────────────────
TEST(PositionManagerTest, LongToShortFlip) {
    // Long 100 @ 150, then sell 150 → close 100 long + open 50 short @ 155
    PositionManager pm;
    pm.onExecution(makeExec("AAPL", 100, 150.00, Side::BUY));
    pm.onExecution(makeExec("AAPL", 150, 155.00, Side::SELL));

    const auto& pos = pm.getPosition("AAPL");
    EXPECT_DOUBLE_EQ(pos.quantity, -50.0);
    EXPECT_NEAR(pos.averageEntryPrice, 155.00, 0.01);  // new short opens at 155
    EXPECT_NEAR(pos.realisedPnl, 500.0, 0.01);         // 100 × (155−150)
}

TEST(PositionManagerTest, ShortToLongFlip) {
    // Short 100 @ 200, then buy 150 @ 190 → cover 100 short + open 50 long
    PositionManager pm;
    pm.onExecution(makeExec("TSLA", 100, 200.00, Side::SELL));
    pm.onExecution(makeExec("TSLA", 150, 190.00, Side::BUY));

    const auto& pos = pm.getPosition("TSLA");
    EXPECT_DOUBLE_EQ(pos.quantity, 50.0);
    EXPECT_NEAR(pos.averageEntryPrice, 190.00, 0.01);
    EXPECT_NEAR(pos.realisedPnl, 1000.0, 0.01);  // 100 × (200−190)
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-symbol isolation
// ─────────────────────────────────────────────────────────────────────────────
TEST(PositionManagerTest, SymbolsTrackedIndependently) {
    PositionManager pm;
    pm.onExecution(makeExec("AAPL",  100, 150.00, Side::BUY));
    pm.onExecution(makeExec("MSFT",  200, 300.00, Side::BUY));
    pm.onExecution(makeExec("GOOGL",  50, 140.00, Side::SELL));

    EXPECT_DOUBLE_EQ(pm.getPosition("AAPL").quantity,   100.0);
    EXPECT_DOUBLE_EQ(pm.getPosition("MSFT").quantity,   200.0);
    EXPECT_DOUBLE_EQ(pm.getPosition("GOOGL").quantity,  -50.0);

    EXPECT_EQ(pm.all().size(), 3u);
}

TEST(PositionManagerTest, AllReturnsAllPositions) {
    PositionManager pm;
    pm.onExecution(makeExec("AAPL", 100, 150.00, Side::BUY));
    pm.onExecution(makeExec("TSLA",  50, 200.00, Side::SELL));

    const auto& all = pm.all();
    EXPECT_EQ(all.size(), 2u);
    EXPECT_NE(all.find("AAPL"), all.end());
    EXPECT_NE(all.find("TSLA"), all.end());
}
