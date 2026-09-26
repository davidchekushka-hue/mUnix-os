; mUnix bootloader — Multiboot v1 compliant entry.
BITS 32

MBOOT_MAGIC    equ 0x1BADB002
MBOOT_FLAGS    equ 0x00000000
MBOOT_CHECKSUM equ -(MBOOT_MAGIC + MBOOT_FLAGS)

section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top

    push ebx        ; mbi_ptr
    push eax        ; magic

    cli
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
