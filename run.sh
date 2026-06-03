#!/bin/bash
set -e

echo "======================================"
echo "   Trading Simulator - Auto Run       "
echo "======================================"

echo "[1/4] Building the project..."
cmake -S . -B build
cmake --build build

echo "[2/4] Generating simulated FIX orders..."
./build/tools/fix_order_simulator

echo "[3/4] Starting trading system (Hot & Cold paths)..."
./build/src/trading-system &
SYSTEM_PID=$!

# Give the system a tiny fraction of a second to spin up threads
sleep 0.5 

echo "[4/4] Blasting 1,000,000 UDP market data ticks..."
./build/tools/udp_tick_simulator

echo "Market data stream finished. Waiting 1 second for lock-free queues to drain..."
sleep 1

echo "Sending shutdown signal to trading system..."
kill -2 $SYSTEM_PID
wait $SYSTEM_PID 2>/dev/null || true

echo "======================================"
echo " Simulation complete!                 "
echo " Check final_pnl_report.txt           "
echo "======================================"
