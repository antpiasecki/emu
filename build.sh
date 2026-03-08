#!/bin/bash

CFLAGS="-Wall -Wextra -Wpedantic"

if [[ -n "$RELEASE" ]]; then
    CFLAGS="$CFLAGS -O3 -flto -march=native"
else
    CFLAGS="$CFLAGS -g"
fi

echo "CFLAGS: $CFLAGS"

echo "building mos6502..."
cc $CFLAGS -std=c99 -o mos6502 mos6502.c

if command -v pkg-config >/dev/null; then
    RAYLIB_FLAGS=$(pkg-config --cflags --libs raylib 2>/dev/null)
    if [ $? -eq 0 ]; then
        echo "building chip8..."
        cc $CFLAGS -std=c99 -o chip8 chip8.c $RAYLIB_FLAGS
    else
        echo "raylib not found - skipping chip8..."
    fi

    LIBELF_FLAGS=$(pkg-config --cflags --libs libelf 2>/dev/null)
    if [ $? -eq 0 ]; then
        echo "building riscv64..."
        c++ -std=c++23 $CFLAGS -o riscv64 riscv64.cc $LIBELF_FLAGS
    else
        echo "libelf not found - skipping riscv64..."
    fi
else
    echo "pkg-config not found - skipping the rest..."
fi
