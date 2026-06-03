#include "order_book.h"

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Helper
// ─────────────────────────────────────────────────────────────────────────────
static Order makeOrder(const char* id, const char* symbol,
                       double price, OrderType type, int32_t qty) {
    Order o{};
    strncpy(o.id,     id,     sizeof(o.id)     - 1);
    strncpy(o.symbol, symbol, sizeof(o.symbol) - 1);
    o.price      = price;
    o.order_type = type;
    o.quantity   = qty;
    return o;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// Owns a fresh OrderBook and accumulates all Execution callbacks into a vector.
// Each TEST_F gets a clean state — SetUp() runs before every individual test.
// ─────────────────────────────────────────────────────────────────────────────
class OrderBookTest : public ::testing::Test {
protected:
    OrderBook            book;
    std::vector<Execution> execs;

    void SetUp() override {
        book.setExecutionCallback([this](Execution e) { execs.push_back(e); });
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Resting orders — no match yet
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(OrderBookTest, RestingBidNoMatch) {
    book.acceptNewOrder(makeOrder("B1", "AAPL", 150.00, OrderType::BUY, 100));
    EXPECT_TRUE(execs.empty());
    EXPECT_DOUBLE_EQ(book.bestBid("AAPL"), 150.00);
    EXPECT_DOUBLE_EQ(book.bestAsk("AAPL"), 0.0);
}

TEST_F(OrderBookTest, RestingAskNoMatch) {
    book.acceptNewOrder(makeOrder("A1", "AAPL", 150.00, OrderType::SELL, 100));
    EXPECT_TRUE(execs.empty());
    EXPECT_DOUBLE_EQ(book.bestBid("AAPL"), 0.0);
    EXPECT_DOUBLE_EQ(book.bestAsk("AAPL"), 150.00);
}

TEST_F(OrderBookTest, SpreadNoMatch) {
    // Bid below ask — no crossing, no match.
    book.acceptNewOrder(makeOrder("B1", "AAPL", 149.00, OrderType::BUY, 100));
    book.acceptNewOrder(makeOrder("A1", "AAPL", 151.00, OrderType::SELL, 100));
    EXPECT_TRUE(execs.empty());
    EXPECT_DOUBLE_EQ(book.bestBid("AAPL"), 149.00);
    EXPECT_DOUBLE_EQ(book.bestAsk("AAPL"), 151.00);
}

// ─────────────────────────────────────────────────────────────────────────────
// Matching — correctness
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(OrderBookTest, ExactMatchClearsBook) {
    book.acceptNewOrder(makeOrder("B1", "AAPL", 150.00, OrderType::BUY,  100));
    book.acceptNewOrder(makeOrder("A1", "AAPL", 150.00, OrderType::SELL, 100));

    ASSERT_EQ(execs.size(), 1u);
    EXPECT_EQ(execs[0].quantity,    100.0);
    EXPECT_DOUBLE_EQ(execs[0].trade_price, 150.00);

    // Both orders fully consumed — book is empty.
    EXPECT_DOUBLE_EQ(book.bestBid("AAPL"), 0.0);
    EXPECT_DOUBLE_EQ(book.bestAsk("AAPL"), 0.0);
}

TEST_F(OrderBookTest, TradeAtPassivePriceWhenBuyAggresses) {
    // BUY aggressor crosses a resting ask: trade at ask.price (passive side).
    // The aggressive buyer wanted to pay up to 155 but only pays 150.
    book.acceptNewOrder(makeOrder("A1", "AAPL", 150.00, OrderType::SELL, 100));
    book.acceptNewOrder(makeOrder("B1", "AAPL", 155.00, OrderType::BUY,  100));

    ASSERT_EQ(execs.size(), 1u);
    EXPECT_DOUBLE_EQ(execs[0].trade_price, 150.00);  // not 155
}

TEST_F(OrderBookTest, PartialFillBidLarger) {
    // Bid 200, ask 100 → 100 trades, 100 bid remains.
    book.acceptNewOrder(makeOrder("B1", "AAPL", 150.00, OrderType::BUY,  200));
    book.acceptNewOrder(makeOrder("A1", "AAPL", 150.00, OrderType::SELL, 100));

    ASSERT_EQ(execs.size(), 1u);
    EXPECT_EQ(execs[0].quantity, 100.0);
    EXPECT_DOUBLE_EQ(book.bestBid("AAPL"), 150.00);  // residual bid remains
    EXPECT_DOUBLE_EQ(book.bestAsk("AAPL"), 0.0);
}

TEST_F(OrderBookTest, PartialFillAskLarger) {
    // Bid 100, ask 200 → 100 trades, 100 ask remains.
    book.acceptNewOrder(makeOrder("B1", "AAPL", 150.00, OrderType::BUY,  100));
    book.acceptNewOrder(makeOrder("A1", "AAPL", 150.00, OrderType::SELL, 200));

    ASSERT_EQ(execs.size(), 1u);
    EXPECT_EQ(execs[0].quantity, 100.0);
    EXPECT_DOUBLE_EQ(book.bestBid("AAPL"), 0.0);
    EXPECT_DOUBLE_EQ(book.bestAsk("AAPL"), 150.00);  // residual ask remains
}

// ─────────────────────────────────────────────────────────────────────────────
// Price-Time Priority (FIFO at same level)
// Orders at the same price must be matched in arrival order.
// This is the "time" part of "price-time priority" — the fundamental fairness
// rule of every exchange order book.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(OrderBookTest, PriceTimePriorityFIFO) {
    book.acceptNewOrder(makeOrder("B1", "AAPL", 150.00, OrderType::BUY, 50));
    book.acceptNewOrder(makeOrder("B2", "AAPL", 150.00, OrderType::BUY, 50));

    // One ask that can only fill one bid.
    book.acceptNewOrder(makeOrder("A1", "AAPL", 150.00, OrderType::SELL, 50));

    ASSERT_EQ(execs.size(), 1u);
    EXPECT_STREQ(execs[0].buy_id, "B1");  // B1 arrived first → matched first
}

TEST_F(OrderBookTest, BestPriceMatchedFirst) {
    // Two bids at different prices; aggressive ask should hit the higher bid.
    book.acceptNewOrder(makeOrder("B_HI", "AAPL", 151.00, OrderType::BUY, 50));
    book.acceptNewOrder(makeOrder("B_LO", "AAPL", 149.00, OrderType::BUY, 50));

    book.acceptNewOrder(makeOrder("A1", "AAPL", 149.00, OrderType::SELL, 50));

    ASSERT_EQ(execs.size(), 1u);
    EXPECT_STREQ(execs[0].buy_id, "B_HI");
    EXPECT_DOUBLE_EQ(execs[0].trade_price, 151.00);  // matched at higher bid
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-level sweep
// An aggressive order large enough to consume multiple price levels.
// Tests that the bitset correctly advances best_ask_tick through 3 levels.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(OrderBookTest, AggressiveBidSweepsMultipleLevels) {
    book.acceptNewOrder(makeOrder("A1", "AAPL", 150.00, OrderType::SELL, 100));
    book.acceptNewOrder(makeOrder("A2", "AAPL", 151.00, OrderType::SELL, 100));
    book.acceptNewOrder(makeOrder("A3", "AAPL", 152.00, OrderType::SELL, 100));

    book.acceptNewOrder(makeOrder("B1", "AAPL", 152.00, OrderType::BUY, 300));

    ASSERT_EQ(execs.size(), 3u);
    // Matched in price order: cheapest ask first.
    EXPECT_DOUBLE_EQ(execs[0].trade_price, 150.00);
    EXPECT_DOUBLE_EQ(execs[1].trade_price, 151.00);
    EXPECT_DOUBLE_EQ(execs[2].trade_price, 152.00);

    // Book fully consumed.
    EXPECT_DOUBLE_EQ(book.bestAsk("AAPL"), 0.0);
    EXPECT_DOUBLE_EQ(book.bestBid("AAPL"), 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Best-price tracking after match
// After the top level is consumed, best_bid / best_ask must fall back to the
// next occupied level. This exercises the O(1) bitset scan.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(OrderBookTest, BestBidFallsBackAfterMatch) {
    book.acceptNewOrder(makeOrder("B_HI", "AAPL", 150.00, OrderType::BUY, 100));
    book.acceptNewOrder(makeOrder("B_LO", "AAPL", 149.00, OrderType::BUY, 100));

    EXPECT_DOUBLE_EQ(book.bestBid("AAPL"), 150.00);

    // Consume the top bid level.
    book.acceptNewOrder(makeOrder("A1", "AAPL", 150.00, OrderType::SELL, 100));
    EXPECT_DOUBLE_EQ(book.bestBid("AAPL"), 149.00);  // bitset scanned to next level
}

TEST_F(OrderBookTest, BestAskRisesAfterMatch) {
    book.acceptNewOrder(makeOrder("A_LO", "AAPL", 150.00, OrderType::SELL, 100));
    book.acceptNewOrder(makeOrder("A_HI", "AAPL", 151.00, OrderType::SELL, 100));

    EXPECT_DOUBLE_EQ(book.bestAsk("AAPL"), 150.00);

    book.acceptNewOrder(makeOrder("B1", "AAPL", 150.00, OrderType::BUY, 100));
    EXPECT_DOUBLE_EQ(book.bestAsk("AAPL"), 151.00);
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol isolation
// Each symbol has its own SymbolBook. Orders for AAPL must not affect MSFT.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(OrderBookTest, DifferentSymbolsDontMatch) {
    book.acceptNewOrder(makeOrder("B1", "AAPL", 150.00, OrderType::BUY,  100));
    book.acceptNewOrder(makeOrder("A1", "MSFT", 150.00, OrderType::SELL, 100));

    EXPECT_TRUE(execs.empty());
    EXPECT_DOUBLE_EQ(book.bestBid("AAPL"), 150.00);
    EXPECT_DOUBLE_EQ(book.bestAsk("MSFT"), 150.00);
    EXPECT_DOUBLE_EQ(book.bestBid("MSFT"), 0.0);
    EXPECT_DOUBLE_EQ(book.bestAsk("AAPL"), 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution metadata
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(OrderBookTest, ExecutionAggressorSideBuy) {
    book.acceptNewOrder(makeOrder("A1", "AAPL", 150.00, OrderType::SELL, 100));
    book.acceptNewOrder(makeOrder("B1", "AAPL", 150.00, OrderType::BUY,  100));

    ASSERT_EQ(execs.size(), 1u);
    EXPECT_EQ(execs[0].side, Side::BUY);
    EXPECT_STREQ(execs[0].buy_id,  "B1");
    EXPECT_STREQ(execs[0].sell_id, "A1");
}

TEST_F(OrderBookTest, ExecutionAggressorSideSell) {
    book.acceptNewOrder(makeOrder("B1", "AAPL", 150.00, OrderType::BUY,  100));
    book.acceptNewOrder(makeOrder("A1", "AAPL", 150.00, OrderType::SELL, 100));

    ASSERT_EQ(execs.size(), 1u);
    EXPECT_EQ(execs[0].side, Side::SELL);
}

TEST_F(OrderBookTest, ExecutionSymbolCopied) {
    book.acceptNewOrder(makeOrder("B1", "GOOGL", 140.00, OrderType::BUY,  50));
    book.acceptNewOrder(makeOrder("A1", "GOOGL", 140.00, OrderType::SELL, 50));

    ASSERT_EQ(execs.size(), 1u);
    EXPECT_STREQ(execs[0].symbol, "GOOGL");
}

// ─────────────────────────────────────────────────────────────────────────────
// Pool capacity
// After all orders are processed the pool should have returned all slots.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(OrderBookTest, PoolReleasedAfterFullMatch) {
    const std::size_t before = book.poolAvailable();

    book.acceptNewOrder(makeOrder("B1", "AAPL", 150.00, OrderType::BUY,  100));
    book.acceptNewOrder(makeOrder("A1", "AAPL", 150.00, OrderType::SELL, 100));

    // Both orders matched and returned to pool.
    EXPECT_EQ(book.poolAvailable(), before);
}
