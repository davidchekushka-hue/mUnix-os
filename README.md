# mUnix

**mUnix** — minimal 64-bit UEFI operating system with Rust GUI.

![Arch](https://img.shields.io/badge/arch-x86__64-blue)
![Boot](https://img.shields.io/badge/boot-UEFI%20%2B%20Multiboot2-green)
![Mode](https://img.shields.io/badge/mode-long%20mode-orange)
![GUI](https://img.shields.io/badge/gui-Rust%20no__std-red)
![Version](https://img.shields.io/badge/version-v0.8.1-blue)

## Features

- x86_64 long mode, identity-mapped 4 GiB
- GRUB2 + Multiboot2, manual long mode switch
- UEFI x86_64 (OVMF tested), BIOS fallback
- GOP framebuffer 1280x720x32
- Rust no_std GUI: WM, dock, browser, mOffice, ASM IDE, 5 games
- RTL8139 + ARP + IP + ICMP + UDP + DNS + TCP + HTTP GET
- RAMFS (32 inodes, in-memory)
- PS/2 keyboard + mouse, PIT, PC speaker, UHCI USB probe
- IDT with 32 ISR stubs, serial debug on COM1
- SDK: syscall table @ 0x7000

## Build

```bash
make iso        # → mUnix.iso (BIOS + UEFI hybrid)
make run        # QEMU BIOS
make run-uefi   # QEMU OVMF (UEFI)
make clean
```

## Applications

- Terminal, Files, Editor, Media, Settings
- Browser (HTTP GET + HTML→text)
- ASM IDE (assemble/run/save)
- mOffice (Writer, Calc WIP, Impress WIP)
- SysMon (CPU/RAM/PCI/NIC/GPU info)
- Minesweeper, Snake, Pong, Shapes, Tetris

## SDK

```c
#include "syscall.h"
int main(void) {
    sys_write("Hello mUnix!\n");
    return 0;
}
```

```bash
cd sdk && ./build.sh hello.c   # → hello.bin
```

In mUnix Terminal: `run hello`

## License

MIT
