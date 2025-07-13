#!/bin/bash
set -xe
CFLAGS="-Wall -Wextra -Wpedantic -std=c99 -O3"

cc $CFLAGS -o chip8   chip8.c   -L/usr/local/lib/libraylib.a -lraylib -lm
cc $CFLAGS -o mos6502 mos6502.c