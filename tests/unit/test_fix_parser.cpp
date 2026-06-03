#include "fix_session_reader.h"

#include <gtest/gtest.h>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// parseFixLine() is public so we can unit-test the parser without touching
// the mmap or file layer. This tests the zero-copy FIX tag parser in isolation.
// ─────────────────────────────────────────────────────────────────────────────
class FixParserTest : public ::testing::Test {
protected:
    FixSessionReader reader;

    Order parse(const char* line) {
        return reader.parseFixLine(line, std::strlen(line));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Full message
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(FixParserTest, ParsesFullNOS) {
    // FIX New Order Single (35=D): every field present.
    // Tag 11 = ClOrdID, 35 = MsgType, 55 = Symbol, 54 = Side, 44 = Price, 38 = Qty.
    Order o = parse("11=ORD001|35=D|55=AAPL|54=1|44=150.00|38=100");

    EXPECT_STREQ(o.id,     "ORD001");
    EXPECT_STREQ(o.symbol, "AAPL");
    EXPECT_EQ(o.order_type, OrderType::BUY);
    EXPECT_DOUBLE_EQ(o.price,    150.00);
    EXPECT_EQ(o.quantity, 100);
}

TEST_F(FixParserTest, ParsesSellOrder) {
    Order o = parse("11=ORD002|55=MSFT|54=2|44=300.00|38=50");
    EXPECT_EQ(o.order_type, OrderType::SELL);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fields
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(FixParserTest, OrderIdCopied) {
    Order o = parse("11=MYORDER42|55=X|54=1|44=1.00|38=1");
    EXPECT_STREQ(o.id, "MYORDER42");
}

TEST_F(FixParserTest, SymbolCopied) {
    Order o = parse("11=X|55=GBPUSD|54=1|44=1.25|38=1");
    EXPECT_STREQ(o.symbol, "GBPUSD");
}

TEST_F(FixParserTest, PriceParsedCorrectly) {
    Order o = parse("11=X|55=AAPL|54=1|44=149.99|38=1");
    EXPECT_NEAR(o.price, 149.99, 0.001);
}

TEST_F(FixParserTest, QuantityParsedCorrectly) {
    Order o = parse("11=X|55=AAPL|54=1|44=100.00|38=5000");
    EXPECT_EQ(o.quantity, 5000);
}

TEST_F(FixParserTest, LargeQuantity) {
    Order o = parse("11=X|55=AAPL|54=1|44=100.00|38=65535");
    EXPECT_EQ(o.quantity, 65535);
}

// ─────────────────────────────────────────────────────────────────────────────
// Side parsing: tag 54 — '1' = BUY, anything else = SELL
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(FixParserTest, Side1IsBuy) {
    Order o = parse("11=X|55=AAPL|54=1|44=100.00|38=1");
    EXPECT_EQ(o.order_type, OrderType::BUY);
}

TEST_F(FixParserTest, Side2IsSell) {
    Order o = parse("11=X|55=AAPL|54=2|44=100.00|38=1");
    EXPECT_EQ(o.order_type, OrderType::SELL);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tag order independence
// FIX is a positional tag-value protocol but tags can appear in any order.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(FixParserTest, TagsInAnyOrder) {
    Order o = parse("38=200|44=99.50|54=2|55=TSLA|11=REV001");
    EXPECT_STREQ(o.id,     "REV001");
    EXPECT_STREQ(o.symbol, "TSLA");
    EXPECT_EQ(o.order_type, OrderType::SELL);
    EXPECT_NEAR(o.price, 99.50, 0.001);
    EXPECT_EQ(o.quantity, 200);
}

// ─────────────────────────────────────────────────────────────────────────────
// Resilience — missing / truncated fields
// The parser must not crash on malformed input; missing values default to zero.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(FixParserTest, MissingPriceDefaultsToZero) {
    Order o = parse("11=X|55=AAPL|54=1|38=100");
    EXPECT_DOUBLE_EQ(o.price, 0.0);
}

TEST_F(FixParserTest, MissingQuantityDefaultsToZero) {
    Order o = parse("11=X|55=AAPL|54=1|44=100.00");
    EXPECT_EQ(o.quantity, 0);
}

TEST_F(FixParserTest, EmptyLineNocrash) {
    // Zero-length line should produce a default-constructed Order without UB.
    Order o = reader.parseFixLine("", 0);
    EXPECT_EQ(o.quantity, 0);
    EXPECT_DOUBLE_EQ(o.price, 0.0);
}

TEST_F(FixParserTest, SymbolTruncatedToSevenChars) {
    // Order::symbol is char[8] — max 7 usable chars + null terminator.
    Order o = parse("11=X|55=VERYLONGSYMBOL|54=1|44=1.00|38=1");
    EXPECT_EQ(std::strlen(o.symbol), 7u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Arrival timestamp
// The arrival_ns must be set before parsing begins — it represents the T0 for
// E2E latency measurement and must always be a positive wall-clock value.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(FixParserTest, ArrivalNsIsPositive) {
    Order o = parse("11=X|55=AAPL|54=1|44=100.00|38=1");
    EXPECT_GT(o.arrival_ns, 0LL);
}
