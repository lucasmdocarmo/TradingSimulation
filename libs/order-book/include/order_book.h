#pragma once

#include "execution.h"
#include "order.h"
#include "../../utils/include/object_pool.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Max price $5000.00, tick size $0.01 → 500,000 ticks.
constexpr int MAX_TICKS = 500'000;

// Number of 64-bit words needed for a bitset covering MAX_TICKS bits.
// e.g. 500,000 / 64 = 7,813 words (rounded up).
constexpr int BITSET_WORDS = (MAX_TICKS + 63) / 64;

// ─────────────────────────────────────────────────────────────────────────────
// PriceLevel — intrusive FIFO queue for orders at a single price level.
//
// "Intrusive" means list pointers live inside the Order (Order::next) rather
// than in separately heap-allocated nodes. Zero extra allocation per enqueue.
// ─────────────────────────────────────────────────────────────────────────────
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
    Order* front()    noexcept { return head; }
    Order* pop_front() noexcept {
        Order* o = head;
        if (o) { head = o->next; if (!head) tail = nullptr; --count; }
        return o;
    }
    bool empty() const noexcept { return !head; }
};

// ─────────────────────────────────────────────────────────────────────────────
// SymbolBook — flat-array order book for one instrument.
//
// O(1) BEST-PRICE TRACKING WITH OCCUPANCY BITSET
//
// Previously, after consuming the last order at a price level, the code scanned
// downward (or upward for asks) through empty PriceLevel slots until it found
// the next occupied one:
//
//   while (best_bid_tick >= 0 && bids[best_bid_tick].empty())
//       --best_bid_tick;                         ← O(N) in worst case
//
// At $150.00 (tick 15000), this iterates up to 15,000 times after clearing the
// level — each iteration reading a PriceLevel from memory, potentially missing
// L1 cache. The benchmark showed ~520 µs per match pair.
//
// THE FIX: 64-bit occupancy bitset
// Each bit represents one tick (1 = has resting orders, 0 = empty).
// To find the next occupied bid below tick T:
//   1. Clear bit T in the bitset.
//   2. Mask the current 64-bit word to bits 0..T.
//   3. If the masked word is non-zero, the answer is 63 - __builtin_clzll(word)
//      — one CPU instruction.
//   4. If zero, step to the next lower word and repeat.
//
// In practice step 3 almost always succeeds (consecutive price levels have
// resting orders). The worst case is O(MAX_TICKS/64) = O(7813) word checks —
// still 64x better than the naive scan, and the bitset is much smaller (62 KB
// vs 12 MB for the PriceLevel vectors) so it stays in L1/L2 cache.
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolBook {
    std::vector<PriceLevel> bids;           // indexed by tick
    std::vector<PriceLevel> asks;
    std::array<uint64_t, BITSET_WORDS> bid_bits{};  // 1 = level has orders
    std::array<uint64_t, BITSET_WORDS> ask_bits{};
    int best_bid_tick = -1;           // -1        = no bids
    int best_ask_tick = MAX_TICKS;    // MAX_TICKS = no asks

    SymbolBook() : bids(MAX_TICKS), asks(MAX_TICKS) {}

    // ── Bitset helpers ──────────────────────────────────────────────────────
    void bid_set  (int t) noexcept { bid_bits[t >> 6] |=  (1ULL << (t & 63)); }
    void bid_clear(int t) noexcept { bid_bits[t >> 6] &= ~(1ULL << (t & 63)); }
    void ask_set  (int t) noexcept { ask_bits[t >> 6] |=  (1ULL << (t & 63)); }
    void ask_clear(int t) noexcept { ask_bits[t >> 6] &= ~(1ULL << (t & 63)); }

    // Find the highest set bit <= from_tick. Returns -1 if none.
    // Used after clearing a bid level to find the new best bid.
    //
    // HOW THE MASK WORKS
    // We want bits 0..from_tick in the current word. The mask is:
    //   (2ULL << (from_tick & 63)) - 1
    // When from_tick & 63 == 63:  (2ULL << 63) overflows to 0 → 0-1 = 0xFFFF...
    // (all bits set) — correct, we want the full word.
    int highest_bid(int from_tick) const noexcept {
        if (from_tick < 0) return -1;
        int w = from_tick >> 6;
        uint64_t mask = bid_bits[w] & ((2ULL << (from_tick & 63)) - 1);
        while (mask == 0 && w > 0) mask = bid_bits[--w];
        if (mask == 0) return -1;
        // 63 - clzll(mask) = position of the highest set bit
        return (w << 6) + 63 - __builtin_clzll(mask);
    }

    // Find the lowest set bit >= from_tick. Returns MAX_TICKS if none.
    // Used after clearing an ask level to find the new best ask.
    int lowest_ask(int from_tick) const noexcept {
        if (from_tick >= MAX_TICKS) return MAX_TICKS;
        int w     = from_tick >> 6;
        int max_w = (MAX_TICKS - 1) >> 6;
        // Mask: bits from_tick & 63 .. 63 (clear the lower bits we've passed)
        uint64_t mask = ask_bits[w] & ~((1ULL << (from_tick & 63)) - 1);
        while (mask == 0 && w < max_w) mask = ask_bits[++w];
        if (mask == 0) return MAX_TICKS;
        // __builtin_ctzll = count trailing zeros = position of lowest set bit
        return (w << 6) + __builtin_ctzll(mask);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// OrderBook — multi-symbol matching engine.
// ─────────────────────────────────────────────────────────────────────────────
class OrderBook {
public:
    void acceptNewOrder(const Order& order);

    double bestBid(const std::string& symbol);
    double bestAsk(const std::string& symbol);

    void setExecutionCallback(std::function<void(Execution)> cb) noexcept {
        onExecution_ = std::move(cb);
    }

    std::size_t poolAvailable() const noexcept { return pool_.available(); }

private:
    // ── Symbol key: char[8] → uint64_t ────────────────────────────────────
    // All 8 bytes of the symbol field are always initialised (Order is zero-
    // constructed). memcpy from char[8] to uint64_t is well-defined and
    // compiles to a single MOV on x86 / LDR on ARM.
    static uint64_t sym_key(const char* s) noexcept {
        uint64_t k = 0;
        std::memcpy(&k, s, sizeof(k));
        return k;
    }

    static int    priceToTick(double p) noexcept { return static_cast<int>(p * 100.0 + 0.5); }
    static double tickToPrice(int t)    noexcept { return t / 100.0; }

    // matchOrders takes SymbolBook by reference (already looked up in acceptNewOrder)
    // and const char* symbol (points directly into the pool slot — no copy).
    void matchOrders(SymbolBook& book, const char* symbol, OrderType aggressor);
    Execution buildExecution(int qty, const Order& ask, const Order& bid,
                             const char* symbol, OrderType aggressor);

    ObjectPool<Order, 65536>              pool_;
    std::unordered_map<uint64_t, SymbolBook> books_;
    std::function<void(Execution)>        onExecution_;
};
