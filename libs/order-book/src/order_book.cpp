#include "order_book.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>

using namespace std;

void OrderBook::acceptNewOrder(const Order& order) {
    const int tick = priceToTick(order.price);
    if (tick < 0 || tick >= MAX_TICKS) return;

    Order* o = pool_.alloc();
    if (!o) [[unlikely]] {
        cerr << "[OrderBook] CRITICAL: pool exhausted — order dropped\n";
        return;
    }
    *o = order;
    o->next = nullptr;

    // uint64_t key lookup: no std::string construction, no byte-by-byte hash.
    SymbolBook& book = books_[sym_key(o->symbol)];

    if (o->order_type == OrderType::BUY) {
        book.bids[tick].push(o);
        book.bid_set(tick);                                // mark level occupied
        if (tick > book.best_bid_tick) book.best_bid_tick = tick;
    } else {
        book.asks[tick].push(o);
        book.ask_set(tick);
        if (tick < book.best_ask_tick) book.best_ask_tick = tick;
    }

    matchOrders(book, o->symbol, o->order_type);
}

void OrderBook::matchOrders(SymbolBook& book, const char* symbol, OrderType aggressor) {
    while (true) {
        if (book.best_bid_tick == -1 || book.best_ask_tick == MAX_TICKS) return;
        if (book.best_bid_tick < book.best_ask_tick) return;

        Order* bid = book.bids[book.best_bid_tick].front();
        Order* ask = book.asks[book.best_ask_tick].front();

        // Prefetch next likely price levels into L1 while we process this match.
        __builtin_prefetch(&book.bids[book.best_bid_tick - 1], 0, 3);
        __builtin_prefetch(&book.asks[book.best_ask_tick + 1], 0, 3);

        const int tradeQty = min(ask->quantity, bid->quantity);

        Execution exec = buildExecution(tradeQty, *ask, *bid, symbol, aggressor);
        if (onExecution_) onExecution_(exec);

        bid->quantity -= tradeQty;
        ask->quantity -= tradeQty;

        if (bid->quantity == 0) {
            book.bids[book.best_bid_tick].pop_front();
            pool_.release(bid);

            if (book.bids[book.best_bid_tick].empty()) {
                // O(1) bitset scan: clear the bit, then find the highest set bit
                // below the current level.  Previously this was a while loop
                // that iterated up to 15,000 times through empty PriceLevels.
                book.bid_clear(book.best_bid_tick);
                book.best_bid_tick = book.highest_bid(book.best_bid_tick);
            }
        }

        if (ask->quantity == 0) {
            book.asks[book.best_ask_tick].pop_front();
            pool_.release(ask);

            if (book.asks[book.best_ask_tick].empty()) {
                book.ask_clear(book.best_ask_tick);
                book.best_ask_tick = book.lowest_ask(book.best_ask_tick);
            }
        }
    }
}

Execution OrderBook::buildExecution(int tradeQty, const Order& ask, const Order& bid,
                                    const char* symbol, OrderType aggressor) {
    Execution exec{};

    exec.side = (aggressor == OrderType::BUY) ? Side::BUY : Side::SELL;

    strncpy(exec.buy_id,  bid.id,  sizeof(exec.buy_id)  - 1);
    strncpy(exec.sell_id, ask.id,  sizeof(exec.sell_id) - 1);
    strncpy(exec.symbol,  symbol,  sizeof(exec.symbol)  - 1);

    exec.quantity         = tradeQty;
    // Trade executes at the passive (resting) order's price.
    // BUY aggressor: the ask was already resting  → ask.price.
    // SELL aggressor: the bid was already resting → bid.price (price improvement
    //   for the seller — they asked for X but got the higher resting bid price).
    exec.trade_price = (aggressor == OrderType::BUY) ? ask.price : bid.price;
    exec.arrival_ns       = chrono::steady_clock::now().time_since_epoch().count();
    // Aggressor's arrival timestamp: BUY aggressor is the bid, SELL is the ask.
    exec.order_arrival_ns = (aggressor == OrderType::BUY) ? bid.arrival_ns
                                                          : ask.arrival_ns;
    return exec;
}

double OrderBook::bestBid(const string& symbol) {
    char sym8[8] = {};
    strncpy(sym8, symbol.c_str(), sizeof(sym8) - 1);
    auto it = books_.find(sym_key(sym8));
    if (it != books_.end() && it->second.best_bid_tick != -1)
        return tickToPrice(it->second.best_bid_tick);
    return 0.0;
}

double OrderBook::bestAsk(const string& symbol) {
    char sym8[8] = {};
    strncpy(sym8, symbol.c_str(), sizeof(sym8) - 1);
    auto it = books_.find(sym_key(sym8));
    if (it != books_.end() && it->second.best_ask_tick != MAX_TICKS)
        return tickToPrice(it->second.best_ask_tick);
    return 0.0;
}
