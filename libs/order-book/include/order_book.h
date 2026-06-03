#pragma once

#include "execution.h"
#include "order.h"
#include "../../utils/include/object_pool.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Max price: $5000.00, tick size: $0.01 → 500,000 ticks.
// Chosen so the flat vector covers every valid price without a hash lookup.
constexpr int MAX_TICKS = 500000;

// Intrusive FIFO queue for orders at a single price level.
//
// "Intrusive" means the list pointers (Order::next) live inside the Order object
// itself instead of in a separately heap-allocated node. Consequences:
//   - Zero extra allocation per enqueue: we use a slot from the pool, not the heap.
//   - Better cache locality: following the list traverses the pool slab, which is
//     already hot if the matching engine is actively working this price level.
//   - Trivially constructible (head=nullptr, tail=nullptr, count=0): the 500K-entry
//     std::vector<PriceLevel> initialises in one memset instead of calling 500K
//     std::queue (std::deque) constructors — eliminates ~30 MB of cold-start heap churn.
struct PriceLevel {
    Order*   head  = nullptr;
    Order*   tail  = nullptr;
    uint32_t count = 0;

    void push(Order* o) noexcept {
        o->next = nullptr;
        if (!head) head = o;
        else        tail->next = o;
        tail = o;
        ++count;
    }

    Order* front() noexcept { return head; }

    // Removes and returns the front order (does NOT release back to pool).
    // Caller is responsible for calling pool.release() when done with the slot.
    Order* pop_front() noexcept {
        Order* o = head;
        if (o) {
            head = o->next;
            if (!head) tail = nullptr;
            --count;
        }
        return o;
    }

    bool empty() const noexcept { return !head; }
};

// Per-symbol flat order book.
// Two vectors of PriceLevel, indexed directly by integer tick.
// best_bid_tick / best_ask_tick cache the current top-of-book so matching
// never scans the full vector — it walks inward from the cached best price.
//
// Sentinel value for "no orders on this side": MAX_TICKS.
// Valid tick indices are 0 … MAX_TICKS-1. MAX_TICKS is always out of bounds
// and is never used as a vector index — only for sentinel comparisons.
struct SymbolBook {
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;

    int best_bid_tick = -1;          // -1  = no bids
    int best_ask_tick = MAX_TICKS;   // MAX_TICKS = no asks

    SymbolBook() : bids(MAX_TICKS), asks(MAX_TICKS) {}
};

class OrderBook {
public:
    void acceptNewOrder(const Order& order);

    // aggressor_side: the side of the order that just triggered this match call.
    // Required to correctly populate Execution::side (BUG FIX #1).
    void matchOrders(const std::string& symbol, OrderType aggressor_side);

    double bestBid(const std::string& symbol);
    double bestAsk(const std::string& symbol);

    Execution buildExecution(int tradeQty, const Order& ask, const Order& bid,
                             const std::string& symbol, OrderType aggressor_side);

    void setExecutionCallback(std::function<void(Execution)> callback) noexcept {
        onExecution_ = std::move(callback);
    }

    // Diagnostics: how many order slots remain in the pool.
    // In production, alert if this drops below a safety threshold.
    std::size_t poolAvailable() const noexcept { return pool_.available(); }

private:
    int    priceToTick(double price) const noexcept {
        return static_cast<int>(price * 100.0 + 0.5);
    }
    double tickToPrice(int tick) const noexcept { return tick / 100.0; }

    // 65,536 pre-allocated Order slots: 65536 * 64 bytes = 4 MB, one arena.
    // All order memory for all symbols lives here — no per-order heap allocation.
    ObjectPool<Order, 65536>                     pool_;
    std::unordered_map<std::string, SymbolBook>  books_;
    std::function<void(Execution)>               onExecution_;
};
