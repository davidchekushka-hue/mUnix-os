# mUnix build system — v0.0.4

CC      = gcc
AS      = nasm
LD      = ld
QEMU    = qemu-system-i386

CFLAGS  = -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-builtin -nostdlib -nostdinc -Wall -O2
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

OBJS    = boot.o kernel.o
TARGET  = munix.bin
DISK    = disk.img

all: $(TARGET)

boot.o: boot.asm
	$(AS) $(ASFLAGS) $< -o $@

kernel.o: kernel.c io.h
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

disk.img:
	dd if=/dev/zero of=$(DISK) bs=1M count=64
	mkfs.vfat -F 32 $(DISK)
	printf 'Welcome to mUnix v0.0.4!\nServed from FAT32 via ATA PIO.\n' > readme.txt
	printf '# mUnix config\nversion=0.0.4\nkernel=munix.bin\n' > munix.conf
	mcopy -i $(DISK) readme.txt ::/readme.txt
	mcopy -i $(DISK) munix.conf ::/munix.conf
	rm -f readme.txt munix.conf

run: $(TARGET) $(DISK)
	$(QEMU) -kernel $(TARGET) -drive file=$(DISK),format=raw,if=ide

run-hdd: $(TARGET) $(DISK)
	$(QEMU) -hda $(DISK)

run-nodisk: $(TARGET)
	$(QEMU) -kernel $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
	rm -f $(DISK)

.PHONY: all run run-hdd run-nodisk disk.img clean
