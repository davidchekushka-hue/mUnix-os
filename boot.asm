BITS 32
MB2_MAGIC    equ 0xE85250D6
MB2_ARCH     equ 0

section .multiboot2
align 8
mb2_start:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd mb2_end - mb2_start
    dd -(MB2_MAGIC + MB2_ARCH + (mb2_end - mb2_start))

    align 8
    dw 1
    dw 0
    dd 20
    dd 8
    dd 6
    dd 4
    dd 0

    align 8
    dw 5
    dw 0
    dd 20
    dd 1280
    dd 720
    dd 32

    align 8
    dw 0
    dw 0
    dd 8
mb2_end:

section .bss
align 16
stack_bottom:
    resb 131072
stack_top:

align 4096
pml4:
    resq 512
align 4096
pdpt:
    resq 512

section .data
align 16
gdt64:
    dq 0
gdt64_code equ $ - gdt64
    dq 0x00209A0000000000
gdt64_data equ $ - gdt64
    dq 0x0000920000000000
gdt64_ptr:
    dw gdt64_ptr - gdt64 - 1
    dq gdt64

section .text
global _start
extern kernel_main

_start:
    cli
    mov esp, stack_top
    mov edi, eax
    mov esi, ebx

    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_lm
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_lm

    mov eax, pdpt
    or eax, 0x03
    mov [pml4], eax
    mov dword [pml4 + 4], 0

    mov ecx, 0
.fill_pdpt:
    mov eax, ecx
    shl eax, 30
    or eax, 0x83
    mov [pdpt + ecx*8], eax
    mov dword [pdpt + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 4
    jb .fill_pdpt

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov eax, pml4
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    lgdt [gdt64_ptr]
    jmp 0x08:long_mode_entry

.no_lm:
    mov dx, 0x3F8
    mov al, 'N'
    out dx, al
.hang32:
    cli
    hlt
    jmp .hang32

BITS 64
long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, stack_top
    and rsp, -16
    call kernel_main
.hang64:
    cli
    hlt
    jmp .hang64

section .note.GNU-stack noalloc noexec nowrite progbits
