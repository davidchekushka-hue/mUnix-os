#!/bin/sh
if [ -z "$1" ]; then echo "usage: $0 <file.c>"; exit 1; fi
BIN="${1%.c}.bin"
gcc -m64 -ffreestanding -nostdlib -nostartfiles \
    -fno-pic -fno-pie -fno-stack-protector -fno-builtin \
    -mcmodel=small -mno-red-zone -mno-sse -mno-sse2 -O2 \
    -c "$1" -o /tmp/app.o
ld -Ttext=0x400000 --oformat=binary -e main /tmp/app.o -o "$BIN"
echo "built: $BIN ($(stat -c%s $BIN) bytes)"
