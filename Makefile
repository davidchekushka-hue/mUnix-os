KERNEL    := mUnix-kernel.bin
ISO       := mUnix.iso
ISOROOT   := iso
BUILD     := build
CARGO_TGT := x86_64-unknown-none
RUST_LIB  := gui/target/$(CARGO_TGT)/release/libmunix_gui.a

CFLAGS    := -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
             -nostdlib -O2 -Wall -Wextra -Wno-unused-parameter \
             -Wno-unused-but-set-variable -Wno-unused-function \
             -std=gnu11 -mno-red-zone -mcmodel=kernel -mno-sse -mno-sse2

LDFLAGS   := -m elf_x86_64 -T linker.ld -nostdlib

QEMU_BIN  := qemu-system-x86_64
QFLAGS_B  := -m 256 -vga std -serial mon:stdio
OVMF_CODE := /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS := /usr/share/OVMF/OVMF_VARS_4M.fd

.PHONY: all iso rust run run-uefi run-kernel clean

all: $(ISO)

rust:
	cd gui && cargo build --release --target $(CARGO_TGT)
	@test -f $(RUST_LIB) || { echo "missing $(RUST_LIB)"; exit 1; }

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: boot.asm | $(BUILD)
	nasm -f elf64 -o $@ $<

$(BUILD)/kernel.o: kernel.c io.h | $(BUILD)
	gcc $(CFLAGS) -c -o $@ $<

KERNEL_NOBLOB := mUnix-kernel-noblob.bin

$(BUILD)/grub_blob.o: efi/EFI/BOOT/BOOTX64.EFI | $(BUILD)
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

$(BUILD)/kernel_nb.o: kernel.c io.h | $(BUILD)
	$(CC) $(CFLAGS) -DKERNEL_NO_BLOB -c -o $@ $<

$(BUILD)/lexbor_wrap.o: lexbor_wrap.c | $(BUILD)
	gcc -m64 -ffreestanding -fno-stack-protector -fno-builtin -nostdlib \
	    -O2 -msse -msse2 -mno-avx -Wall -Wextra -std=gnu11 -c -o $@ $<

$(KERNEL_NOBLOB): rust $(BUILD)/boot.o $(BUILD)/kernel_nb.o $(BUILD)/lexbor_wrap.o $(BUILD)/grub_blob.o linker.ld
	ld $(LDFLAGS) -z noexecstack -o $@ $(BUILD)/boot.o $(BUILD)/kernel_nb.o $(BUILD)/lexbor_wrap.o $(BUILD)/grub_blob.o liblexbor.a $(RUST_LIB)
	@echo ">>> $@: $$(stat -c%s $@) bytes"

$(BUILD)/kernel_blob.o: $(KERNEL_NOBLOB) | $(BUILD)
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

$(KERNEL): rust $(BUILD)/boot.o $(BUILD)/kernel.o $(BUILD)/lexbor_wrap.o $(BUILD)/grub_blob.o $(BUILD)/kernel_blob.o linker.ld
	ld $(LDFLAGS) -z noexecstack -o $@ $(BUILD)/boot.o $(BUILD)/kernel.o $(BUILD)/lexbor_wrap.o $(BUILD)/grub_blob.o $(BUILD)/kernel_blob.o liblexbor.a $(RUST_LIB)
	@echo ">>> $@: $$(stat -c%s $@) bytes"

iso: $(ISO)

$(ISO): $(KERNEL) boot/grub.cfg
	rm -rf $(ISOROOT)
	mkdir -p $(ISOROOT)/boot/grub
	cp $(KERNEL) $(ISOROOT)/boot/KERNEL.BIN
	cp boot/grub.cfg $(ISOROOT)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISOROOT) -- -volid MUNIX
	@echo ">>> $(ISO): $$(stat -c%s $@) bytes"

run: $(ISO)
	$(QEMU_BIN) -cdrom $(ISO) -boot d $(QFLAGS_B)

run-uefi: $(ISO)
	@test -f $(OVMF_CODE) || { echo "OVMF not found"; exit 1; }
	cp -f $(OVMF_VARS) /tmp/munix_OVMF_VARS.fd
	$(QEMU_BIN) \
	    -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
	    -drive if=pflash,format=raw,file=/tmp/munix_OVMF_VARS.fd \
	    -cdrom $(ISO) -boot d $(QFLAGS_B)

run-kernel: $(KERNEL)
	$(QEMU_BIN) -kernel $(KERNEL) $(QFLAGS_B)

clean:
	rm -rf $(BUILD) $(KERNEL) $(ISO) $(ISOROOT)
	cd gui && cargo clean || true
