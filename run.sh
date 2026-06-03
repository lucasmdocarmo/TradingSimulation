#!/bin/bash
# run.sh — single-command simulation launcher
#
# Orchestrates the full simulation in the correct order:
#   1. Build all binaries
#   2. Generate a fresh set of synthetic FIX orders (10,000 orders)
#   3. Start the trading system (spawns Hot Path 1 + Hot Path 2 + Cold Path threads)
#   4. Blast 1,000,000 UDP market data ticks at the system
#   5. Drain the lock-free queues, then send SIGINT for graceful shutdown
#
# The trading system writes final_pnl_report.txt on exit.

set -e  # exit immediately if any command fails

echo "======================================"
echo "   Trading Simulator - Auto Run       "
echo "======================================"

echo "[1/4] Building the project..."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

echo "[2/4] Generating simulated FIX orders..."
# Produces config/orders.fix — 10,000 pipe-delimited FIX New Order Singles.
# Re-running this regenerates the file with the same seed (deterministic).
./build/tools/fix_order_simulator

echo "[3/4] Starting trading system (Hot & Cold paths)..."
# Background process. Binds UDP port 9000 and begins the FIX replay immediately.
./build/src/trading-system &
SYSTEM_PID=$!

# Allow the trading system time to bind the UDP socket before the simulator
# starts sending. Without this pause the first packets may be dropped.
sleep 0.5

echo "[4/4] Blasting 1,000,000 UDP market data ticks..."
# Runs to completion (sends all ticks, then exits). Approximately 10 seconds
# at 10 µs per tick. This call BLOCKS until all ticks are sent.
./build/tools/udp_tick_simulator

# The tick simulator is done, but the SPSC queues may still contain unprocessed
# ticks. Give the cold-path thread 1 second to drain both queues.
echo "Market data stream finished. Waiting 1 second for lock-free queues to drain..."
sleep 1

echo "Sending shutdown signal to trading system..."
# SIGINT (signal 2) triggers the handleSignal() handler in main.cpp,
# setting running=0 so the cold-path loop exits cleanly and writes the report.
kill -2 $SYSTEM_PID
wait $SYSTEM_PID 2>/dev/null || true

echo "======================================"
echo " Simulation complete!                 "
echo " Check final_pnl_report.txt           "
echo "======================================"
