#!/bin/bash
set -euo pipefail

CFLAGS="-Wall -Wextra -Wpedantic -std=c99 -O3"

echo "building mos6502..."
cc $CFLAGS -o mos6502 mos6502.c

if command -v pkg-config >/dev/null; then
    RAYLIB_FLAGS=$(pkg-config --cflags --libs raylib 2>/dev/null)
    if [ $? -eq 0 ]; then
        echo "building chip8..."
        cc $CFLAGS -o chip8 chip8.c $RAYLIB_FLAGS
    else
        echo "raylib not found - skipping chip8..."
    fi
else
    echo "pkg-config not found - skipping chip8..."
fi