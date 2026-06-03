# C++ Algorithmic Trading System

A low-latency trading system built in C++ that simulates the core infrastructure found on a real trading desk. It ingests market data over UDP, processes orders via a FIX session, maintains live positions, and runs a risk engine that produces mark-to-market P&L and Value at Risk every few seconds.

## What it does

The system operates using an advanced high-frequency trading (HFT) architecture. The **Hot Path** processes a stream of simulated FIX orders (matching them via a highly optimized flat-array order book) and simultaneously ingests a massive stream of UDP market data ticks. 

To ensure the trading execution is never blocked by IO or complex math, the system utilizes **lock-free SPSC (Single-Producer Single-Consumer) ring buffers** to asynchronously dispatch state updates to a **Cold Path** thread. This Risk Engine thread calculates complex Value at Risk (VaR) models and prints real-time reports without introducing any jitter or latency to the core execution engine.

## Project structure

```
Trading-System/
  src/
    main.cpp
  libs/
    reference-data/       instrument store
    order-book/           cache-aligned flat array matching engine
    position-manager/     position and P&L tracking
    fix-session/          FIX protocol parser and session reader
    market-data/          tick definitions, UDP subscriber, dispatcher
    volatility-surface/   vol surface loader and interpolator
    risk/                 risk engine, VaR, mark-to-market reporting
    utils/                lock-free SPSC ring buffers
  tools/
    fix_order_simulator   generates synthetic FIX order file
    udp_tick_simulator    broadcasts synthetic tick prices over UDP
  config/
    instruments.csv
    orders.fix
    vol_surface.csv
```

## Architecture

```
                        ┌─────────────────────┐
                        │   Instrument Store   │
                        │  (reference data,    │
                        │   loads at startup)  │
                        └──────────┬──────────┘
                                   │
      ┌────────────────────────────┴───────────────────────────┐
      │                                                        │
      ▼ HOT PATH 1 (Execution)                                 ▼ HOT PATH 2 (Market Data)
┌───────────┴──────────┐                               ┌───────┴─────────┐
│  FIX Session Reader  │                               │ UDP Subscriber  │
└───────────┬──────────┘                               └───────┬─────────┘
            │ orders                                           │ ticks
            ▼                                                  ▼
┌──────────────────────┐                               ┌─────────────────┐
│ Flat Array OrderBook │                               │ Market Dispatcher│
│ (Cache-line aligned) │                               └───────┬─────────┘
└───────────┬──────────┘                                       │ 
            │ executions                                       │ 
            ▼                                                  ▼
      ╔══════════════════════════════════════════════════════════════╗
      ║      Lock-Free SPSC Ring Buffers (Zero Mutex Dispatch)       ║
      ╚═════════════════════════════╦════════════════════════════════╝
                                    │ state updates
                                    ▼ COLD PATH (Reporting)
                         ┌────────────────────┐
                         │   Risk Engine &    │
                         │  Position Manager  │◄── Volatility Surface
                         │                    │
                         │  - VaR 95%         │
                         │  - Unrealised P&L  │
                         └──────────┬─────────┘
                                    │
                                    ▼
                          Risk report printed
```

## Components

### Instrument Store
Loads static reference data at startup from a CSV file before anything else runs. Every other component looks up instrument details here. 

### FIX Session Reader
Reads a file of FIX protocol messages. The reader parses the tag-value pairs and fires a callback for each valid order, dispatching them to the Order Book for execution.

### Order Book (Cache Aligned & Flat)
Unlike standard implementations that use slow `std::map` Red-Black trees, this Order Book is optimized for HFT. It uses **flat contiguous arrays** (`std::vector<PriceLevel>`) indexed by integer ticks for $O(1)$ constant-time lookup. Crucially, the internal data structures are padded using `alignas(64)` to ensure they fit perfectly inside the CPU's L1 cache line, preventing "false sharing" and eliminating cache misses.

### Lock-Free SPSC Queues
Custom ring buffers (`spsc_queue.h`) leveraging `std::atomic` with explicit memory orderings (`memory_order_release` / `memory_order_acquire`). These queues pass `Tick`s and `Execution`s from the network threads to the risk thread without ever taking an OS-level mutex.

### Position Manager
The central ledger. For every instrument it tracks the current net quantity held, the weighted average entry price, and the total realised P&L locked in from closed trades. 

### Risk Engine (Cold Path)
Runs on its own dedicated thread to prevent blocking the trading loop. It continuously drains the lock-free queues to update internal positions, and runs a busy-loop timer to print the VaR and P&L reports to the terminal every 5 seconds.

## Instruments

The system trades across six instruments covering equities, futures, and FX.
- AAPL — US equity
- MSFT — US equity
- GOOGL — US equity
- TSLA — US equity
- ESZ4 — E-mini S&P 500 futures contract
- GBPUSD — sterling / dollar spot FX

---

## How to run

You can run the simulation manually across multiple terminals, or launch it using a single command. 

### Method 1: Single-Line Execution
If you want to run the entire simulation quickly in one go, you can simply execute the provided bash script. This will automatically build the project, run the trading system in the background, fire all the UDP ticks, and gracefully shut down.

```bash
./run.sh
```

### Method 2: Manual Execution (Multiple Terminals)
First, ensure the project is built.
```bash
cmake -S . -B build
cmake --build build
```

Generate a fresh set of simulated orders:
```bash
./build/tools/fix_order_simulator      # writes config/orders.fix for the simulation of orders
```

In **Terminal 1**, start the core trading system. This will spin up the Hot Path and Cold Path threads:
```bash
./build/src/trading-system             # Terminal 1
```

In **Terminal 2**, start the tick feed to blast the system with UDP market data:
```bash
./build/tools/udp_tick_simulator       # Terminal 2
```

Press `Ctrl+C` in Terminal 1 to stop the system gracefully. The final P&L report is written to `final_pnl_report.txt`.

## Running with sanitisers

To check for memory errors or undefined behaviour, build with AddressSanitizer enabled.
```bash
cmake -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" -S . -B build
cmake --build build
```
Run as normal. Any issues will be reported before the process exits.
