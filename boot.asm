; mUnix bootloader — Multiboot v1
BITS 32
MBOOT_MAGIC    equ 0x1BADB002
MBOOT_FLAGS    equ 0x00000002
MBOOT_CHECKSUM equ -(MBOOT_MAGIC + MBOOT_FLAGS)

section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM

section .bss
align 16
stack_bottom:
    resb 131072            ; 128 KB
stack_top:

section .text
global _start
extern kernel_main
_start:
    mov esp, stack_top
    push ebx
    push eax
    cli
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
