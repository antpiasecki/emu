#!/bin/bash
set -xe
cc -O3 -o chip8 chip8.c -L/usr/local/lib/libraylib.a -lraylib -lm
cc -O3 -o mos6502 mos6502.c