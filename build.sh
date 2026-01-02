#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "=== Building Polymarket Arbitrage Bot (C++) ==="
echo ""

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "[1/3] Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo ""
echo "[2/3] Building..."
make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

echo ""
echo "[3/3] Build complete!"
echo ""
echo "Binary location: $(pwd)/polymarket_arb"
echo ""
echo "Run with: ./build/polymarket_arb --help"
