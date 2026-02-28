#!/bin/bash
set -euo pipefail

CFLAGS="-Wall -Wextra -Wpedantic -std=c99"

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

if command -v pkg-config >/dev/null; then
    LIBELF_FLAGS=$(pkg-config --cflags --libs libelf 2>/dev/null)
    if [ $? -eq 0 ]; then
        echo "building riscv64..."
        cc $CFLAGS -o riscv64 riscv64.c $LIBELF_FLAGS
    else
        echo "raylib not found - skipping riscv64..."
    fi
else
    echo "pkg-config not found - skipping riscv64..."
fi
