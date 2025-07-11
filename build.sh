#!/bin/bash
set -xe
cc -O3 -o chip8 chip8.c -L/usr/local/lib/libraylib.a -lraylib -lm