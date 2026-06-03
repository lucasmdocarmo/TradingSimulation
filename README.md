# C++ Algorithmic Trading System

A low-latency trading system built in C++ that simulates the core infrastructure found on a real trading desk. It ingests market data over UDP, processes orders via a FIX session, maintains live positions, and runs a risk engine that produces mark-to-market P&L and Value at Risk every few seconds.


## What it does

The system runs two things simultaneously. On one side it reads a file of ten thousand FIX orders representing a day of trading activity, processes each one as an execution, and builds up a set of positions across six instruments. On the other side it receives a continuous stream of live tick prices over UDP, uses those prices to mark each position to market, and prints a risk report every five seconds showing how much money the portfolio is making or losing in real time.

When you stop the system it writes a final P&L report to disk.


## Project structure

```
Trading-System/
  src/
    main.cpp
  libs/
    reference-data/       instrument store
    order-book/           price-time priority matching engine
    position-manager/     position and P&L tracking
    fix-session/          FIX protocol parser and session reader
    market-data/          tick definitions, UDP subscriber, dispatcher
    volatility-surface/   vol surface loader and interpolator
    risk/                 risk engine, VaR, mark-to-market reporting
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
              ┌────────────────────┼────────────────────┐
              │                                         │
   ┌──────────▼──────────┐                   ┌─────────▼─────────┐
   │   FIX Session Reader │                   │  UDP Feed          │
   │   (reads orders.fix) │                   │  Subscriber        │
   └──────────┬──────────┘                   └─────────┬─────────┘
              │                                        │
              │ each order                             │ each tick
              │ = one execution                        ▼
              │                              ┌─────────────────────┐
              ▼                              │  Market Dispatcher   │
   ┌─────────────────────┐                   │  (routes ticks by   │
   │  Position Manager   │                   │   symbol)           │
   │  (qty, avg entry,   │                   └─────────┬───────────┘
   │   realised P&L)     │                             │
   └──────────┬──────────┘                             │ live prices
              │                                        │
              └──────────────┐           ┌─────────────┘
                             ▼           ▼
                          ┌─────────────────────┐
                          │     Risk Engine      │
                          │                      │
                          │  - unrealised P&L    │◄── Volatility Surface
                          │  - realised P&L      │
                          │  - VaR 95%           │
                          └──────────┬───────────┘
                                     │
                                     ▼
                             Risk report printed
                             every 5 seconds +
                             final P&L on exit
```


## Components

### Instrument Store

Loads static reference data at startup from a CSV file before anything else runs. Every other component looks up instrument details here. It is the first thing that initialises and the last thing that shuts down.

### FIX Session Reader

Reads a file of FIX protocol messages line by line. FIX is the industry standard messaging format for institutional trading. Each message encodes a single order with a symbol, side (buy or sell), price, and quantity. The reader parses the tag-value pairs and fires a callback for each valid order. In this system every order is treated as an immediate execution at its limit price, which is the standard assumption for a simulation.

### Order Book

Maintains two sorted lists of resting orders for each instrument bids sorted from highest to lowest price, asks sorted lowest to highest. When a new order arrives it checks whether any orders on the opposite side cross in price and matches them if so. The order book runs for market structure completeness but position tracking in this system happens directly from the FIX orders rather than from the matching engine.

### Position Manager

The central ledger. For every instrument it tracks the current net quantity held, the weighted average entry price across all executions so far, and the total realised P&L locked in from closed trades. When a sell happens it compares the sell price against the average entry price and books the difference as realised P&L. Selling more than you own opens a short position. Buying against an existing short covers it and books that P&L too.

### UDP Feed Subscriber

Listens on a UDP socket for incoming tick data. UDP is the protocol real exchanges use for market data distribution because it is fast and connectionless. Each datagram contains a tick with a symbol, last traded price, bid, and ask. The subscriber runs on a background thread so it never blocks the main loop.

### Market Dispatcher

Sits between the UDP subscriber and the rest of the system. It holds a registry of callbacks keyed by symbol and routes each incoming tick to whoever has registered for that instrument. This means the risk engine does not know or care that prices are arriving over UDP — it just registers a function and gets called when a price arrives.

### Volatility Surface

Loaded from a CSV at startup. Holds implied volatility values across a grid of strikes and expiries. The risk engine uses this to scale the VaR calculation to each instrument's observed volatility.

### Risk Engine

Runs every five seconds. Each cycle it asks the position manager for all current positions, looks up the latest tick price for each symbol, and computes unrealised P&L as the difference between the current market price and the average entry price multiplied by the quantity held. It also computes a 95% one-day Value at Risk estimate. Everything prints to the terminal. When the system receives a shutdown signal the final state is written to a file.


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

Generate a fresh set of simulated orders first, then start the trading system, then start the tick feed in a second terminal.

```bash
cmake -S . -B build
cmake --build build

./build/tools/fix_order_simulator      # writes config/orders.fix for the simulation of orders
./build/src/trading-system             # terminal 1
./build/tools/udp_tick_simulator       # terminal 2
```

Press Ctrl+C to stop. The final P&L report is written to `final_pnl_report.txt`.


## Running with sanitisers

To check for memory errors or undefined behaviour, build with AddressSanitizer enabled.

```bash
cmake -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" -S . -B build
cmake --build build
```

Run as normal. Any issues will be reported before the process exits.
