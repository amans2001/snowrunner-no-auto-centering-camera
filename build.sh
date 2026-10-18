#!/bin/sh
# Builds SnowRunner_NoFollowCamera.exe from source.
# Needs: nasm, python3, x86_64-w64-mingw32-gcc  (Ubuntu/Debian: apt install nasm gcc-mingw-w64-x86-64 python3)
set -e
cd "$(dirname "$0")"
mkdir -p build
nasm -f bin src/hook.asm -o build/hook.bin
python3 tools/blob2h.py build/hook.bin build/hook_blob.h
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -static -s -Wl,--no-insert-timestamp -Ibuild -o build/SnowRunner_NoFollowCamera.exe src/nofollow.c
echo "Built build/SnowRunner_NoFollowCamera.exe"
