# mUnix

**mUnix** — минималистичная 64-битная UEFI операционная система с Rust GUI.
Загружается через GRUB2 + Multiboot2 в long mode (x86_64).

![Arch](https://img.shields.io/badge/arch-x86__64-blue)
![Boot](https://img.shields.io/badge/boot-UEFI%20%2B%20Multiboot2-green)
![GUI](https://img.shields.io/badge/gui-Rust%20no__std-red)
![Version](https://img.shields.io/badge/version-v8.2-blue)
![License](https://img.shields.io/badge/license-MIT-brightgreen)

## Features

### Kernel
- x86_64 long mode, ring 0, identity-mapped 4 GiB
- GRUB2 + Multiboot2, manual long mode switch
- UEFI x86_64 (OVMF tested), BIOS fallback
- GOP framebuffer 1280x720x32
- IDT + 32 ISR stubs, serial debug on COM1

### GUI (Rust no_std)
- Window manager — blue title bars, rounded corners, dock
- Terminal — 25+ команд
- Files — менеджер RAMFS
- Editor — текстовый редактор
- Browser — HTTP GET + HTML → text
- ASM IDE — mini-assembler + run + save .bin
- mOffice — Writer / Calc / Impress
- SysMon — Task Manager
- About — модальное окно с енотом
- Games — Minesweeper, Snake, Pong, Shapes, Tetris
