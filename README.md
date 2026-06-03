# C++ Algorithmic Trading System

A C++20 low-latency trading system simulation that replicates the core infrastructure found on a real trading desk. It ingests market data over UDP, processes orders via a FIX session, matches them in a cache-optimised order book, maintains live positions, and runs a risk engine that computes mark-to-market P&L and Value at Risk.

The primary goal is **memory handling at maximum capacity in an HFT context** — every design decision prioritises latency, cache locality, and zero per-operation heap allocation on the hot path.

---

## Project Structure

```
TradingSimulations/
├── src/
│   └── main.cpp                  entry point — wires all components, owns threads
│
├── libs/
│   ├── reference-data/           static instrument definitions (loaded once at startup)
│   ├── order-book/               cache-aligned flat-array matching engine + object pool
│   ├── position_manager/         position accounting and realised/unrealised P&L ledger
│   ├── fix_session/              FIX protocol parser (mmap-based, zero-copy)
│   ├── market_data/              UDP tick receiver + pub-sub dispatcher
│   ├── volatility_surface/       implied vol surface loader and interpolator
│   ├── risk_engine/              VaR computation and mark-to-market reporting
│   └── utils/
│       ├── spsc_queue.h          lock-free single-producer single-consumer ring buffer
│       ├── object_pool.h         slab allocator — pre-allocates order memory up front
│       └── cpu_affinity.h        cross-platform thread pinning utility
│
├── tools/
│   ├── fix_order_simulator.cpp   generates config/orders.fix (10,000 synthetic FIX orders)
│   └── udp_tick_simulator.cpp    broadcasts 1,000,000 synthetic ticks over UDP
│
├── config/
│   ├── instruments.csv           reference data: 15 instruments across 4 asset classes
│   ├── orders.fix                generated FIX order file (created by fix_order_simulator)
│   └── vol_surface.csv           implied volatility surface (strike × expiry → σ)
│
└── run.sh                        single-command simulation launcher
```

---

## Architecture

### Thread Model

The system uses three dedicated threads, each with a single responsibility:

```
  ┌─────────────────────────────────────────────────────────────────────┐
  │                         STARTUP (main thread)                        │
  │  InstrumentStore::loadFromCSV()  VolatilitySurface::loadFromCSV()    │
  │  Read-only after this point — safe to access from any thread.        │
  └──────────────────────────┬──────────────────────────────────────────┘
                              │
       ┌──────────────────────┴──────────────────────────┐
       │                                                  │
       ▼  HOT PATH 1 — executionThread (core 2)          ▼  HOT PATH 2 — udpThread (core 1)
  ┌────────────────────────┐                       ┌──────────────────────┐
  │  FixSessionReader       │                       │  UDPFeedSubscriber   │
  │  mmap orders.fix        │                       │  recvfrom port 9000  │
  │  zero-copy FIX parser   │                       └──────────┬───────────┘
  └──────────┬─────────────┘                                   │ Tick (binary, POD)
             │ Order (POD, 64 bytes)                           ▼
             ▼                                       ┌──────────────────────┐
  ┌────────────────────────┐                         │  MarketDispatcher    │
  │  OrderBook              │                         │  pub-sub callbacks   │
  │  flat array, O(1) tick  │                         └──────────┬───────────┘
  │  ObjectPool<65536>      │                                   │ push to queue
  │  __builtin_prefetch     │                                   │
  └──────────┬─────────────┘                                   │
             │ Execution (POD)  ◄────────────────────────────── ┘
             │                                 tick push
             ▼
  ╔══════════════════════════════════════════════════════════════════════╗
  ║             SpscQueue<Execution>       SpscQueue<Tick>               ║
  ║       lock-free ring buffers — zero mutex, zero blocking             ║
  ╚══════════════════════════════════════╦═════════════════════════════╝
                                          │
                                          ▼  COLD PATH — main thread (core 0)
                               ┌───────────────────────┐
                               │  RiskEngine            │
                               │  PositionManager       │◄── VolatilitySurface
                               │                        │
                               │  every 5 seconds:      │
                               │  · unrealised P&L      │
                               │  · realised P&L        │
                               │  · VaR (95%, 1-day)    │
                               └───────────┬────────────┘
                                           │
                                           ▼
                                  final_pnl_report.txt
```

### Why Three Threads?

The FIX matching and UDP receiving are **latency-critical** — every microsecond of delay could mean missing a price or executing at a worse level. The risk calculations (VaR, mark-to-market) are computationally expensive and involve `std::map` lookups and floating-point math that can take hundreds of microseconds. If we did risk on the same thread as matching, every trade would be stalled while VaR was computing.

The solution: three threads, two lock-free queues. The hot threads do one thing as fast as possible and push results to queues. The cold thread drains those queues at its own pace without ever blocking the hot threads.

---

## Memory Model

This is the core learning goal of the project. Every HFT system is ultimately a battle against memory latency.

### The Problem with `new` and `delete` on the Hot Path

When you call `new Order()`, the allocator must:
1. Acquire a thread-safe heap lock (or CAS loop)
2. Walk a free-list to find a fitting block
3. Update bookkeeping metadata

This takes 50–500 ns and is non-deterministic. A garbage-collection pause or heap fragmentation can spike this to microseconds. In a system trying to process orders in < 1 µs, `new` is catastrophic.

### ObjectPool — The Solution

`libs/utils/include/object_pool.h`

```
 ┌──────────────────────────────────────────────────────────┐
 │ ObjectPool<Order, 65536>                                  │
 │                                                           │
 │  slab_: [Order][Order][Order]...[Order]  ← 4 MB heap     │
 │          ^      ^      ^         ^                        │
 │          │      │      │         │                        │
 │  free_stack_: [ptr][ptr][ptr]...[ptr]   ← pointer stack  │
 │                                         top_ = 65536      │
 └──────────────────────────────────────────────────────────┘

  alloc():  return free_stack_[--top_]   O(1), ~2 ns
  release(): free_stack_[top_++] = ptr   O(1), ~2 ns
```

65,536 Order objects (each 64 bytes = one cache line) are allocated once in a contiguous heap block at startup. During the trading session, `acceptNewOrder` calls `pool_.alloc()` — a single pointer pop. `matchOrders` calls `pool_.release()` when an order is fully filled — a single pointer push. The heap is never touched again.

**Pool utilisation from the last run:** 1,058 live orders remain at shutdown (8,942 orders were matched and returned to the pool during the session).

### Cache Lines and False Sharing

A CPU cache line is 64 bytes. When the CPU loads a memory address, it loads the entire 64-byte line. If two threads write to different variables that happen to share a cache line, every write by one thread invalidates the other thread's cached copy — **false sharing** — even though they're writing to logically independent data.

`alignas(64)` on `Order` guarantees each order occupies exactly one cache line. `alignas(64)` on `SpscQueue::head_` and `tail_` separates the producer's and consumer's atomic integers onto separate cache lines, eliminating false sharing between the hot and cold threads.

### Intrusive Linked Lists — Zero Node Allocation

The old `PriceLevel` used `std::queue<Order>`, which is backed by `std::deque`. A `std::deque` has internal bookkeeping of ~40–80 bytes even when empty. With 500,000 price levels per side × 2 sides = 1,000,000 PriceLevels per symbol, the constructor was triggering ~60 MB of heap allocation before any orders arrived.

The new `PriceLevel` holds only two `Order*` pointers (`head`, `tail`) and a count. The `Order` struct has an `Order* next` field for intrusive linking. "Intrusive" means the list node is embedded inside the element — the order is the node. No separate allocation: when an order joins a price level, we just update `tail->next = newOrder`. When it's matched and removed, we call `pool_.release(order)` — one pointer push.

---

## Components

### Instrument Store (`libs/reference-data`)
Loaded once at startup before any threads start. Maps symbol strings to full instrument definitions: asset class, tick size, contract size, and for derivatives, strike/expiry/underlying. Uses `std::optional<T>` for fields that only apply to certain asset classes — a compile-time safety guarantee that callers must check `.has_value()` before reading optional fields.

### FIX Session Reader (`libs/fix_session`)
Reads `config/orders.fix` using `mmap()` instead of `fstream`. The file's pages are mapped directly into virtual address space — no `read()` syscall per line. `MADV_SEQUENTIAL` tells the kernel to prefetch pages aggressively. The in-place parser uses `memchr` to locate tag boundaries and pointer arithmetic to extract values without any `std::string` construction.

### Order Book (`libs/order-book`)
Cache-aligned flat-array matching engine with price-time priority.

- **Flat array vs `std::map`**: `std::map<double, queue<Order>>` (textbook) uses a Red-Black tree with O(log n) lookup and pointer-chasing. The flat array `std::vector<PriceLevel>` indexed by integer tick (price × 100) gives O(1) by direct offset. `best_bid_tick` and `best_ask_tick` cache the top of book — no scan needed.
- **`__builtin_prefetch`**: while matching the current price level, prefetch the next level so it's in L1 cache before the next loop iteration.
- **Pool allocator**: all order memory comes from `ObjectPool<Order, 65536>` — zero heap allocation per order.

### Lock-Free SPSC Queues (`libs/utils/spsc_queue.h`)
Two queues bridge the hot and cold paths:
- `SpscQueue<Tick>` — UDP thread → cold path
- `SpscQueue<Execution>` — execution thread → cold path

Each uses `std::atomic` with `memory_order_release` (publish) and `memory_order_acquire` (consume) — the minimal synchronisation required. Head and tail are `alignas(64)` padded to separate cache lines.

### Position Manager (`libs/position_manager`)
Owns all position state exclusively on the cold path. Never touched by hot threads — state arrives only via Execution objects popped from `execQueue`. Tracks quantity, VWAP entry price, and realised P&L for every symbol. Handles long, short, flip-from-long-to-short, and partial close accounting.

### Risk Engine (`libs/risk_engine`)
Runs on the cold path every 5 seconds:
- **Mark-to-market**: unrealised P&L = (current tick price − entry price) × quantity
- **Parametric VaR**: uses daily volatility (annual σ ÷ √252) from the vol surface, scaled by 1.645 for 95% confidence. Sums across all positions (conservative: assumes full correlation).

### Volatility Surface (`libs/volatility_surface`)
A 2D nearest-neighbour lookup table: strike × expiry → annual implied volatility. Loaded from `config/vol_surface.csv`. Used by the Risk Engine to get per-symbol σ for VaR. `std::map::lower_bound` provides O(log n) snap-to-nearest interpolation.

---

## Configuration Files

### `config/instruments.csv`
15 instruments across 4 asset classes:

| Symbol | Description | Class |
|---|---|---|
| AAPL, MSFT, GOOGL, TSLA | US equities | Equity |
| ESZ4, NQZ4, CLZ4, GCZ4 | Index and commodity futures | Future |
| AAPL240119C150, AAPL240119P140, etc. | Equity options | Option |
| GBPUSD, EURUSD, USDJPY | FX spot pairs | FX |

### `config/orders.fix`
Generated by `fix_order_simulator`. 10,000 pipe-delimited FIX New Order Singles for 6 actively traded symbols (AAPL, MSFT, GOOGL, TSLA, ESZ4, GBPUSD). Prices follow a Gaussian random walk. Must be re-generated before each run if you want fresh prices.

### `config/vol_surface.csv`
A 5-strike × 3-expiry volatility grid (30/60/90-day). Values are annual implied volatility (e.g. 0.25 = 25%). Used only by the cold-path VaR calculation.

---

## How to Run

### Method 1 — Single Command (Recommended)

```bash
chmod +x run.sh
./run.sh
```

This builds, generates orders, starts the system, blasts 1M ticks, and writes `final_pnl_report.txt`. Takes ~15 seconds total.

### Method 2 — Manual (Multiple Terminals)

**Step 1 — Build**
```bash
cmake -S . -B build
cmake --build build
```

**Step 2 — Generate orders**
```bash
./build/tools/fix_order_simulator
# Writes config/orders.fix
```

**Step 3 — Start the trading system (Terminal 1)**
```bash
./build/src/trading-system
# Binds UDP port 9000, starts all three threads, prints risk reports every 5s
```

**Step 4 — Send market data (Terminal 2)**
```bash
./build/tools/udp_tick_simulator
# Sends 1,000,000 ticks at 10µs intervals (~10 seconds total)
```

**Step 5 — Shut down**
Press `Ctrl+C` in Terminal 1. The system drains queues, writes `final_pnl_report.txt`, and exits.

### Method 3 — CLion

CLion auto-detects `cmake-build-debug` and `cmake-build-release`. Open the project, select a CMake profile, and run the `trading-system` target. Use the separate `fix_order_simulator` and `udp_tick_simulator` run configurations for the tools.

---

## Sanitizer Builds

Sanitizers instrument the binary to detect bugs at runtime. Always run these before pushing to catch memory errors, data races, and undefined behaviour.

```bash
# AddressSanitizer + UBSanitizer: detects heap/stack overflows, use-after-free, UB
cmake -DSANITIZE=asan -S . -B build-asan
cmake --build build-asan
./build-asan/src/trading-system

# ThreadSanitizer: detects data races between threads
cmake -DSANITIZE=tsan -S . -B build-tsan
cmake --build build-tsan
./build-tsan/src/trading-system
```

---

## Key C++ Patterns

| Pattern | Where | Why |
|---|---|---|
| `alignas(64)` | `Order`, `SpscQueue::head_/tail_` | One struct per cache line; prevents false sharing |
| Intrusive linked list | `PriceLevel`, `Order::next` | Zero allocation per enqueue — borrow a field in the existing object |
| Object pool (slab allocator) | `ObjectPool<Order, 65536>` | Pre-allocated slab; O(1) alloc/release with no heap traffic |
| Lock-free atomics | `SpscQueue`, `memory_order_release/acquire` | Synchronisation without mutexes; minimal fence instructions |
| `mmap` + `MADV_SEQUENTIAL` | `FixSessionReader` | OS maps file pages directly; no `read()` syscalls or copy to userspace buffer |
| `__builtin_prefetch` | `OrderBook::matchOrders` | Prefetch next price level while processing current match |
| `enum class` | `Side`, `OrderType`, `AssetClass` | Scoped, no implicit int conversion — prevents the zero-init bug that broke P&L |
| `std::optional<T>` | `Instrument` fields | Compile-time forced null-check for fields that don't exist on all instrument types |
| POD wire types | `Tick`, `Order`, `Execution` | `char[]` fields make structs trivially copyable — safe for `reinterpret_cast`, `memcpy`, atomic queues |
| CPU thread pinning | `cpu_affinity.h`, `main.cpp` | Prevents OS from migrating hot threads; keeps hot-path data in the same core's L1/L2 cache |
