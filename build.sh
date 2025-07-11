#!/bin/bash
set -xe
cc -O3 -o chip8 chip8.c -lSDL2