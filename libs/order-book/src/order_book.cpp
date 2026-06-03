#include "order_book.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>

using namespace std;

void OrderBook::acceptNewOrder(const Order& order) {
    const int tick = priceToTick(order.price);
    if (tick < 0 || tick >= MAX_TICKS) return;

    // Allocate a slot from the pool and copy the incoming order into it.
    // This is O(1) with no heap allocation — just a pointer pop from the free-stack.
    Order* o = pool_.alloc();
    if (!o) [[unlikely]] {
        cerr << "[OrderBook] CRITICAL: order pool exhausted — order dropped\n";
        return;
    }
    *o = order;
    o->next = nullptr;

    SymbolBook& book = books_[o->symbol];

    if (o->order_type == OrderType::BUY) {
        book.bids[tick].push(o);
        if (tick > book.best_bid_tick) book.best_bid_tick = tick;
    } else {
        book.asks[tick].push(o);
        if (tick < book.best_ask_tick) book.best_ask_tick = tick;
    }

    matchOrders(o->symbol, o->order_type);
}

void OrderBook::matchOrders(const string& symbol, OrderType aggressor_side) {
    SymbolBook& book = books_[symbol];

    while (true) {
        if (book.best_bid_tick == -1 || book.best_ask_tick == MAX_TICKS)
            return;

        if (book.best_bid_tick < book.best_ask_tick)
            return;

        Order* bid = book.bids[book.best_bid_tick].front();
        Order* ask = book.asks[book.best_ask_tick].front();

        // Prefetch the next likely price levels into L1 cache while we process
        // the current match. The CPU fetches the cache line in the background,
        // so by the time we need it in the next loop iteration it's already hot.
        // 0 = read prefetch, 3 = highest temporal locality (keep in all cache levels).
        __builtin_prefetch(&book.bids[book.best_bid_tick - 1], 0, 3);
        __builtin_prefetch(&book.asks[book.best_ask_tick + 1], 0, 3);

        const int tradeQty = min(ask->quantity, bid->quantity);

        Execution exec = buildExecution(tradeQty, *ask, *bid, symbol, aggressor_side);
        if (onExecution_) onExecution_(exec);

        bid->quantity -= tradeQty;
        ask->quantity -= tradeQty;

        if (bid->quantity == 0) {
            book.bids[book.best_bid_tick].pop_front();
            pool_.release(bid);  // return slot to free-stack — O(1), no heap

            if (book.bids[book.best_bid_tick].empty()) {
                --book.best_bid_tick;
                while (book.best_bid_tick >= 0 &&
                       book.bids[book.best_bid_tick].empty())
                    --book.best_bid_tick;
            }
        }

        if (ask->quantity == 0) {
            book.asks[book.best_ask_tick].pop_front();
            pool_.release(ask);

            if (book.asks[book.best_ask_tick].empty()) {
                ++book.best_ask_tick;
                while (book.best_ask_tick < MAX_TICKS &&
                       book.asks[book.best_ask_tick].empty())
                    ++book.best_ask_tick;
                // If we walked all the way to MAX_TICKS the sentinel is restored automatically.
            }
        }
    }
}

Execution OrderBook::buildExecution(int tradeQty, const Order& ask,
                                    const Order& bid, const string& symbol,
                                    OrderType aggressor_side) {
    Execution exec{};

    // BUG FIX #1: Previously side was never set, so exec.side always defaulted
    // to the zero-value of the enum (BUY), silently misaccounting all SELL trades.
    // We now derive side from the aggressor — the order whose arrival caused the match.
    exec.side = (aggressor_side == OrderType::BUY) ? Side::BUY : Side::SELL;

    // strncpy copies up to n bytes and null-terminates within the destination buffer.
    // sizeof(exec.buy_id) - 1 leaves room for the null terminator.
    strncpy(exec.buy_id,  ask.id,    sizeof(exec.buy_id)  - 1);
    strncpy(exec.sell_id, bid.id,    sizeof(exec.sell_id) - 1);
    strncpy(exec.symbol,  symbol.c_str(), sizeof(exec.symbol) - 1);

    exec.quantity    = tradeQty;
    exec.trade_price = ask.price;  // resting order's price (standard price-time priority)
    exec.arrival_ns  = chrono::steady_clock::now().time_since_epoch().count();

    return exec;
}

double OrderBook::bestBid(const string& symbol) {
    auto it = books_.find(symbol);
    if (it != books_.end() && it->second.best_bid_tick != -1)
        return tickToPrice(it->second.best_bid_tick);
    return 0.0;
}

double OrderBook::bestAsk(const string& symbol) {
    auto it = books_.find(symbol);
    if (it != books_.end() && it->second.best_ask_tick != MAX_TICKS)
        return tickToPrice(it->second.best_ask_tick);
    return 0.0;
}
