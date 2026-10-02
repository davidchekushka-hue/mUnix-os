#!/bin/sh
set -eu
cd "$(dirname "$0")"
make -j2 iso
exec qemu-system-i386 -cdrom mUnix.iso -boot d -m 256 -vga std -serial mon:stdio
