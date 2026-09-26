# mUnix-graph v0.5.0

CC      = gcc
AS      = nasm
LD      = ld
QEMU    = qemu-system-i386

CFLAGS  = -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector \
          -fno-builtin -nostdlib -nostdinc -Wall -O2
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

OBJS    = boot.o kernel.o
TARGET  = munix.bin

all: $(TARGET)

boot.o: boot.asm
	$(AS) $(ASFLAGS) $< -o $@

kernel.o: kernel.c io.h
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

run: $(TARGET)
	$(QEMU) -kernel $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all run clean
