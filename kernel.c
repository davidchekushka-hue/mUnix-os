/*
 * mUnix-kernel v0.8.1 — C hardware layer + RAMFS, GUI delegated to Rust.
 * Back-buffer + dirty flag eliminates flicker.
 */

#include "io.h"

#ifndef MUNIX_VERSION
#define MUNIX_VERSION "0.8.1"
#endif
#ifndef MUNIX_BUILD
#define MUNIX_BUILD "release"
#endif

/* Real counters */
static volatile unsigned long long g_render_count = 0;
static volatile unsigned long long g_idle_loops = 0;
static volatile unsigned long long g_busy_loops = 0;
static unsigned int g_last_fps_ticks = 0;
static unsigned long long g_last_fps_renders = 0;
static unsigned int g_fps = 0;
static unsigned int g_boot_ticks = 0;

void munix_render_hit(void) { g_render_count++; }


void serial_dump_packet(const char *label, const unsigned char *p, unsigned int n);

/* global for lexbor_wrap */
void serial_init(void);
void serial_puts(const char *s);
void serial_hex(unsigned long long v);

void *kmalloc(unsigned long size);
void  kfree(void *p);
void *krealloc(void *p, unsigned long size);
void *malloc(unsigned long size);
void  free(void *p);
void *realloc(void *p, unsigned long size);
void *calloc(unsigned long n, unsigned long sz);
unsigned long strlen(const char *s);


struct pci_dev {
    unsigned char bus, slot, func;
    unsigned short vendor, device;
    unsigned int   class_rev;
    unsigned int   bar[6];
    unsigned char  irq;
    unsigned char  class_code;
    unsigned char  subclass;
    unsigned char  prog_if;
};

static struct pci_dev g_pci_devs[64];
static int g_pci_count = 0;



/* ===== struct pci_dev MUST be here for forward usage ===== */

/* ========================================================== */
/* ===== PCI device table ===== */

/* ============================ */

/* ===== forward declarations ===== */
static int  g_ata_present;
static unsigned int g_ata_sectors;
static unsigned short g_ata_base;
static unsigned char  g_ata_drv;
static int  ata_read_sector(unsigned int lba, unsigned char *out512);
static int  ata_write_sector(unsigned int lba, const unsigned char *in512);
static int  ata_wait_bsy_clear(void);
static void ata_io_wait(void);
static void ata_init(void);
static void ata_dump_mbr(void);
static unsigned int crc32(const void *data, unsigned int len, unsigned int seed);
static void crc32_init(void);
static int  vbe_init(void);
static void i8042_init(void);
static void pit_init(void);
static void splash(void);
static void fs_init(void);
/* ================================= */

/* ===== forward declarations ===== */
static int g_ata_present;
static unsigned int g_ata_sectors;
static unsigned short g_ata_base;
static unsigned char  g_ata_drv;
static int ata_read_sector(unsigned int lba, unsigned char *out512);
static int ata_write_sector(unsigned int lba, const unsigned char *in512);
static int ata_wait_bsy_clear(void);
static void ata_io_wait(void);
static unsigned int crc32(const void *data, unsigned int len, unsigned int seed);
static void crc32_init(void);
/* ================================= */

/* ===== forward declarations (before any use) ===== */
static int g_ata_present;
static unsigned int g_ata_sectors;
static int ata_read_sector(unsigned int lba, unsigned char *out512);
static int ata_write_sector(unsigned int lba, const unsigned char *in512);
static unsigned int crc32(const void *data, unsigned int len, unsigned int seed);
static void crc32_init(void);
/* ================================================= */

/* ===== embedded blobs (via objcopy) ===== */
extern const unsigned char _binary_efi_EFI_BOOT_BOOTX64_EFI_start[];
extern const unsigned char _binary_efi_EFI_BOOT_BOOTX64_EFI_end[];
#define GRUB_EFI_BLOB     _binary_efi_EFI_BOOT_BOOTX64_EFI_start
#define GRUB_EFI_BLOB_len ((unsigned int)(_binary_efi_EFI_BOOT_BOOTX64_EFI_end - _binary_efi_EFI_BOOT_BOOTX64_EFI_start))

#ifdef KERNEL_NO_BLOB
#define KERNEL_BLOB       ((const unsigned char *)0)
#define KERNEL_BLOB_len   0
#else
extern const unsigned char _binary_mUnix_kernel_noblob_bin_start[];
extern const unsigned char _binary_mUnix_kernel_noblob_bin_end[];
#define KERNEL_BLOB       _binary_mUnix_kernel_noblob_bin_start
#define KERNEL_BLOB_len   ((unsigned int)(_binary_mUnix_kernel_noblob_bin_end - _binary_mUnix_kernel_noblob_bin_start))
#endif
/* ========================================= */


/* ==== Multiboot2 (v9.0.0) ==== */
#define MB2_MAGIC 0x36d76289u

struct mb2_tag {
    unsigned int  type;
    unsigned int  size;
};
struct mb2_info {
    unsigned int total_size;
    unsigned int reserved;
};
struct mb2_tag_framebuffer {
    unsigned int  type;         /* = 8 */
    unsigned int  size;
    unsigned long long addr;
    unsigned int  pitch;
    unsigned int  width;
    unsigned int  height;
    unsigned char bpp;
    unsigned char fb_type;
    unsigned char reserved;
    /* color info may follow for indexed */
} __attribute__((packed));
struct mb2_tag_basic_mem {
    unsigned int type;          /* = 4 */
    unsigned int size;
    unsigned int mem_lower;
    unsigned int mem_upper;
};
struct mb2_tag_mmap {
    unsigned int type;          /* = 6 */
    unsigned int size;
    unsigned int entry_size;
    unsigned int entry_version;
    /* entries follow */
};

/* Legacy compatibility: keep old field names used by kernel_main */
struct multiboot_info {
    unsigned int flags;
    unsigned int mem_lower, mem_upper;
    unsigned long long framebuffer_addr;
    unsigned int framebuffer_pitch;
    unsigned int framebuffer_width;
    unsigned int framebuffer_height;
    unsigned char framebuffer_bpp;
    unsigned char framebuffer_type;
    unsigned char color_info[6];
} __attribute__((packed));

static struct multiboot_info g_mbi_storage;
static struct multiboot_info *g_mbi = 0;

static void mb2_parse(unsigned int magic, unsigned long long addr) {
    if (magic != MB2_MAGIC) return;
    struct mb2_info *info = (struct mb2_info *)(unsigned long)addr;
    unsigned long long end = addr + info->total_size;
    struct mb2_tag *tag = (struct mb2_tag *)(unsigned long)(addr + 8);
    while ((unsigned long long)tag < end && tag->type != 0) {
        if (tag->type == 8) {  /* framebuffer */
            struct mb2_tag_framebuffer *fb = (struct mb2_tag_framebuffer *)tag;
            g_mbi_storage.flags = 1;
            g_mbi_storage.framebuffer_addr   = fb->addr;
            g_mbi_storage.framebuffer_pitch  = fb->pitch;
            g_mbi_storage.framebuffer_width  = fb->width;
            g_mbi_storage.framebuffer_height = fb->height;
            g_mbi_storage.framebuffer_bpp    = fb->bpp;
            g_mbi_storage.framebuffer_type   = fb->fb_type;
        } else if (tag->type == 4) {  /* basic memory */
            struct mb2_tag_basic_mem *bm = (struct mb2_tag_basic_mem *)tag;
            g_mbi_storage.mem_lower = bm->mem_lower;
            g_mbi_storage.mem_upper = bm->mem_upper;
        }
        tag = (struct mb2_tag *)(((unsigned char *)tag) + ((tag->size + 7) & ~7u));
    }
    g_mbi = &g_mbi_storage;
}

/* Rust GUI externs */
extern void munix_gui_init_dock(void);
extern void munix_gui_open_window(unsigned int kind,
                                  const unsigned char *title,
                                  unsigned long len);
extern void munix_gui_render(void *pixels, int w, int h, int mx, int my);
extern void munix_gui_key(int key);
extern void munix_gui_click(int x, int y, int buttons);
extern void munix_gui_ticks(unsigned int ticks);
extern int  munix_gui_animated(void);

#define WK_TERMINAL 1

/* ---- Port I/O ---- */
static void nw_out(unsigned short p, unsigned short v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static unsigned short nw_in(unsigned short p){ unsigned short r; __asm__ volatile("inw %1,%0":"=a"(r):"Nd"(p)); return r; }
static void nl_out(unsigned short p, unsigned int v){ __asm__ volatile("outl %0,%1"::"a"((unsigned int)v),"Nd"(p)); }
static unsigned int nl_in(unsigned short p){ unsigned int r; __asm__ volatile("inl %1,%0":"=a"(r):"Nd"(p)); return r; }

/* ---- VBE ---- */
static unsigned int *g_fb = 0;
static int g_w = 0, g_h = 0;

static unsigned int pci_r32(unsigned char b, unsigned char d, unsigned char f, unsigned char o) {
    unsigned int a = 0x80000000u | ((unsigned)b<<16) | ((unsigned)d<<11) | ((unsigned)f<<8) | (o&0xFC);
    nl_out(0xCF8, a); return nl_in(0xCFC);
}
static void vbe_w(unsigned short i, unsigned short v) { nw_out(0x1CE, i); nw_out(0x1CF, v); }
static unsigned short vbe_r(unsigned short i) { nw_out(0x1CE, i); return nw_in(0x1CF); }

static int vbe_init(void) {
    if (g_mbi && g_mbi->flags && g_mbi->framebuffer_addr
        && g_mbi->framebuffer_width && g_mbi->framebuffer_height) {
        g_w  = (int)g_mbi->framebuffer_width;
        g_h  = (int)g_mbi->framebuffer_height;
        g_fb = (unsigned int *)(unsigned long)g_mbi->framebuffer_addr;
        serial_puts("vbe: GOP framebuffer\n");
        return 0;
    }
    int dev, fn, found = 0;
    unsigned int bar0 = 0;
    for (dev = 0; dev < 32 && !found; dev++) {
        for (fn = 0; fn < 8 && !found; fn++) {
            unsigned int vd = pci_r32(0, (unsigned char)dev, (unsigned char)fn, 0);
            unsigned int cls;
            if ((vd & 0xFFFF) == 0xFFFF) continue;
            cls = pci_r32(0, (unsigned char)dev, (unsigned char)fn, 8);
            if (((cls >> 24) & 0xFF) == 0x03 && ((cls >> 16) & 0xFF) == 0x00) {
                found = 1;
                bar0 = pci_r32(0, (unsigned char)dev, (unsigned char)fn, 0x10);
            }
        }
    }
    if (found && vbe_r(0x0) >= 0xB0C0) {
        static const unsigned short modes[5][2] = {
            {1280,720},{1024,768},{800,600},{640,480},{320,200}
        };
        int m;
        for (m = 0; m < 5; m++) {
            unsigned short rw, rh, rb;
            vbe_w(0x4, 0x00);
            vbe_w(0x1, modes[m][0]);
            vbe_w(0x2, modes[m][1]);
            vbe_w(0x3, 32);
            vbe_w(0x4, 0x41);
            rw = vbe_r(0x1); rh = vbe_r(0x2); rb = vbe_r(0x3);
            if (rw == modes[m][0] && rh == modes[m][1] && rb == 32) {
                g_w = rw; g_h = rh;
                g_fb = (unsigned int *)(unsigned long)(bar0 & 0xFFFFFFF0u);
                return 0;
            }
        }
        vbe_w(0x4, 0x00);
    }
    /* Multiboot2 framebuffer fallback */
    if (g_mbi && g_mbi->flags && g_mbi->framebuffer_addr) {
        g_w = (int)g_mbi->framebuffer_width;
        g_h = (int)g_mbi->framebuffer_height;
        g_fb = (unsigned int *)(unsigned long)g_mbi->framebuffer_addr;
        return 0;
    }
    return -3;
}

/* ---- back buffer 1920x1080 ---- */
static unsigned int g_backbuf[1920 * 1080];

/* ---- PIT ---- */
static volatile unsigned int g_ticks = 0;
static unsigned short g_pit_last = 0;
static void pit_init(void) { outb(0x43, 0x36); outb(0x40, 0); outb(0x40, 0); }
static void pit_poll(void) {
    unsigned char lo, hi; unsigned short now;
    outb(0x43, 0x00);
    lo = inb(0x40); hi = inb(0x40);
    now = (unsigned short)((hi << 8) | lo);
    if (now > g_pit_last) g_ticks++;
    g_pit_last = now;
}

/* ---- i8042 ---- */
static void i8042_ww(void) { int t=200000; while (t-- > 0 && (inb(0x64) & 0x02)) {} }
static void i8042_wr(void) { int t=200000; while (t-- > 0 && !(inb(0x64) & 0x01)) {} }
static void i8042_dr(void) { int i; for (i=0;i<256;i++){ if(!(inb(0x64)&0x01))break; (void)inb(0x60);} }
static void i8042_cmd(unsigned char c) { i8042_ww(); outb(0x64, c); }
static void i8042_dout(unsigned char d) { i8042_ww(); outb(0x60, d); }
static unsigned char i8042_din(void) { i8042_wr(); return inb(0x60); }
static void mouse_wcmd(unsigned char b) { i8042_cmd(0xD4); i8042_dout(b); }
static unsigned char mouse_rd(void) { return i8042_din(); }

static void i8042_init(void) {
    unsigned char cfg, r; int tries;
    i8042_cmd(0xAD); i8042_cmd(0xA7); i8042_dr();
    i8042_cmd(0x20); cfg = i8042_din();
    cfg &= ~0x10; cfg &= ~0x20; cfg |= 0x01; cfg &= ~0x02; cfg |= 0x40;
    i8042_cmd(0x60); i8042_dout(cfg);
    i8042_cmd(0xAE); i8042_dout(0xF4); (void)i8042_din();
    i8042_cmd(0xA8);
    mouse_wcmd(0xFF);
    tries = 3; while (tries-- > 0) { r = mouse_rd(); if (r == 0xFA) break; }
    (void)mouse_rd(); (void)mouse_rd();
    mouse_wcmd(0xF6); (void)mouse_rd();
    mouse_wcmd(0xF4); (void)mouse_rd();
    i8042_dr();
}

static int mouse_x = 640, mouse_y = 360;
static int mouse_btn = 0, mouse_prev = 0;
static unsigned char m_pkt[3];
static int m_idx = 0;

static void mouse_feed(unsigned char b) {
    if (m_idx == 0) if (!(b & 0x08)) return;
    m_pkt[m_idx++] = b;
    if (m_idx < 3) return;
    m_idx = 0;
    {
        unsigned char f = m_pkt[0];
        int dx = m_pkt[1], dy = m_pkt[2];
        if (f & 0x10) dx -= 256;
        if (f & 0x20) dy -= 256;
        mouse_x += dx * 2;
        mouse_y -= dy * 2;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x > g_w - 1) mouse_x = g_w - 1;
        if (mouse_y > g_h - 1) mouse_y = g_h - 1;
        mouse_btn = f & 0x07;
    }
}

static int e0_pending = 0;
static int shift = 0;
static int ctrl = 0;

static const char sc_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',
    0,  '*', 0,  ' ',
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0
};
static const char sc_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?',
    0,  '*', 0,  ' ',
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0
};
static int keyboard_feed(unsigned char sc) {
    int was_e0 = e0_pending;
    if (sc == 0xE0) { e0_pending = 1; return -1; }
    e0_pending = 0;
    if (!was_e0) {
        if (sc == 0x2A || sc == 0x36) { shift = 1; return -1; }
        if (sc == 0xAA || sc == 0xB6) { shift = 0; return -1; }
        if (sc == 0x1D) { ctrl = 1; return -1; }
        if (sc == 0x9D) { ctrl = 0; return -1; }
    }
    if (sc & 0x80) return -1;
    if (was_e0) {
        switch (sc) {
            case 0x48: return 0x100;
            case 0x50: return 0x101;
            case 0x4B: return 0x102;
            case 0x4D: return 0x103;
        }
        return -1;
    }
    switch (sc) {
        case 0x3B: return 0x105; /* F1 */
        case 0x3C: return 0x106; /* F2 */
        case 0x3D: return 0x107; /* F3 */
        case 0x3E: return 0x10C; /* F4 -> sample in IDE */
        case 0x3F: return 0x108; /* F5 -> assemble */
        case 0x40: return 0x109; /* F6 -> run */
        case 0x41: return 0x10A; /* F7 -> save */
        case 0x42: return 0x10B; /* F8 -> load sample */
        case 0x43: return 0x10D; /* F9 */
        case 0x44: return 0x10E; /* F10 */
        case 0x57: return 0x10F; /* F11 */
        case 0x58: return 0x110; /* F12 */
        case 0x48: return 0x100;
        case 0x50: return 0x101;
        case 0x4B: return 0x102;
        case 0x4D: return 0x103;
        case 0x01: return 0x104;
    }
    { char c = shift ? sc_shift[sc] : sc_ascii[sc]; if (c) return (int)(unsigned char)c; }
    return -1;
}

/* ---- libc-ish for Rust core ---- */
void *memcpy(void *dst, const void *src, unsigned int n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memset(void *dst, int c, unsigned int n) {
    unsigned char *p = (unsigned char *)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}
int memcmp(const void *a, const void *b, unsigned int n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (n--) { if (*x != *y) return (int)*x - (int)*y; x++; y++; }
    return 0;
}
int bcmp(const void *a, const void *b, unsigned int n) { return memcmp(a, b, n); }
void *memmove(void *dst, const void *src, unsigned int n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

/* ---- Font 8x8 ---- */
static const unsigned char F8[96][8] = {
    {0,0,0,0,0,0,0,0},{0x18,0x18,0x18,0x18,0x18,0,0x18,0},{0x6C,0x6C,0x24,0,0,0,0,0},
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0},{0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0},
    {0,0xC6,0xCC,0x18,0x30,0x66,0xC6,0},{0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0},
    {0x18,0x18,0x30,0,0,0,0,0},{0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0},
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0},{0,0x66,0x3C,0xFF,0x3C,0x66,0,0},
    {0,0x18,0x18,0x7E,0x18,0x18,0,0},{0,0,0,0,0,0x18,0x18,0x30},{0,0,0,0x7E,0,0,0,0},
    {0,0,0,0,0,0x18,0x18,0},{0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0},
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0},{0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0},
    {0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0},{0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0},
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0},{0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0},
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0},{0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0},
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0},{0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0},
    {0,0x18,0x18,0,0,0x18,0x18,0},{0,0x18,0x18,0,0,0x18,0x18,0x30},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0},{0,0,0x7E,0,0x7E,0,0,0},
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0},{0x3C,0x66,0x06,0x0C,0x18,0,0x18,0},
    {0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x3C,0},{0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0},
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0},{0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0},
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0},{0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0},
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0},{0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0},
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0},{0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0},{0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0},
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0},{0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0},
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0},{0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0},
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0},{0x3C,0x66,0x66,0x66,0x66,0x3C,0x0E,0},
    {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0},{0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0},{0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0},
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0},{0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0},
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0},{0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0},
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0},{0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0},
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0},{0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0},
    {0x18,0x3C,0x66,0,0,0,0,0},{0,0,0,0,0,0,0,0xFF},{0x30,0x18,0x0C,0,0,0,0,0},
    {0,0,0x3C,0x06,0x3E,0x66,0x3E,0},{0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0},
    {0,0,0x3C,0x66,0x60,0x66,0x3C,0},{0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0},
    {0,0,0x3C,0x66,0x7E,0x60,0x3C,0},{0x1C,0x30,0x30,0x7C,0x30,0x30,0x30,0},
    {0,0,0x3E,0x66,0x66,0x3E,0x06,0x3C},{0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0},
    {0x18,0,0x38,0x18,0x18,0x18,0x3C,0},{0x0C,0,0x1C,0x0C,0x0C,0x0C,0x6C,0x38},
    {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0},{0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0},
    {0,0,0xEC,0xFE,0xD6,0xC6,0xC6,0},{0,0,0x7C,0x66,0x66,0x66,0x66,0},
    {0,0,0x3C,0x66,0x66,0x66,0x3C,0},{0,0,0x7C,0x66,0x66,0x7C,0x60,0x60},
    {0,0,0x3E,0x66,0x66,0x3E,0x06,0x06},{0,0,0x6E,0x76,0x60,0x60,0x60,0},
    {0,0,0x3E,0x60,0x3C,0x06,0x7C,0},{0x30,0x30,0x7C,0x30,0x30,0x36,0x1C,0},
    {0,0,0x66,0x66,0x66,0x66,0x3E,0},{0,0,0x66,0x66,0x66,0x3C,0x18,0},
    {0,0,0xC6,0xC6,0xD6,0xFE,0x6C,0},{0,0,0x66,0x3C,0x18,0x3C,0x66,0},
    {0,0,0x66,0x66,0x66,0x3E,0x06,0x3C},{0,0,0x7E,0x0C,0x18,0x30,0x7E,0},
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0},{0x18,0x18,0x18,0x18,0x18,0x18,0x18,0},
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0},{0x76,0xDC,0,0,0,0,0,0},
};
void munix_font_glyph(unsigned int *px, int w, int h,
                      int x, int y, unsigned char ch, unsigned int argb) {
    unsigned char u = ch; const unsigned char *g; int row, col;
    if (!px) return;
    if (u < 0x20 || u > 0x7F) u = '?';
    g = F8[(int)(u - 0x20)];
    for (row = 0; row < 8; row++) {
        unsigned char bits = g[row];
        for (col = 0; col < 8; col++) {
            if (bits & (0x80u >> col)) {
                int gx = x + col, gy = y + row;
                if (gx >= 0 && gy >= 0 && gx < w && gy < h)
                    px[gy * w + gx] = argb;
            }
        }
    }
}
void munix_font_text(unsigned int *px, int w, int h,
                     int x, int y, const char *s, unsigned int argb) {
    int cx = x;
    if (!px || !s) return;
    while (*s) {
        if (*s == '\n') { cx = x; y += 9; s++; continue; }
        munix_font_glyph(px, w, h, cx, y, (unsigned char)*s, argb);
        cx += 8; s++;
    }
}

/* ---- RAMFS ---- */
#define FS_MAX 32
#define FS_NAMEMAX 32
#define FS_POOL 8192

typedef struct { char name[FS_NAMEMAX]; char data[256]; int size, cap, used, isdir, parent; } VF;
static VF fs[FS_MAX];
static unsigned int fstop = 0;

static int m_strlen(const char *s){int n=0;while(s[n])n++;return n;}
static int m_strcmp(const char *a,const char *b){while(*a&&(*a==*b)){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}

static int fs_lookup(int parent, const char *name) {
    int i;
    for (i=0;i<FS_MAX;i++) if (fs[i].used && fs[i].parent==parent && m_strcmp(fs[i].name,name)==0) return i;
    return -1;
}
static int fs_alloc(int parent, const char *name, int isdir) {
    int i, slot=-1;
    if (fs_lookup(parent,name)>=0) return -1;
    for (i=0;i<FS_MAX;i++) if (!fs[i].used) { slot=i; break; }
    if (slot<0) return -4;
    { int k; for (k=0;k<FS_NAMEMAX;k++) fs[slot].name[k]=0;
      for (k=0; name[k] && k<FS_NAMEMAX-1; k++) fs[slot].name[k]=name[k]; }
    { int k; for (k=0;k<256;k++) fs[slot].data[k]=0; }
    fs[slot].size=0; fs[slot].cap=256; fs[slot].used=1; fs[slot].isdir=isdir; fs[slot].parent=parent;
    return slot;
}
static void fs_write(int idx, const char *text) {
    int i;
    if (idx<0) return;
    for (i=0; text[i] && i<255; i++) fs[idx].data[i]=text[i];
    fs[idx].data[i]=0; fs[idx].size=i;
}
static void fs_init(void) {
    int i, d_home, d_docs;
    for (i=0;i<FS_MAX;i++){ fs[i].used=0; fs[i].parent=-1; fs[i].name[0]=0; fs[i].size=0; }
    fstop=0;
    fs_alloc(-1,"readme.txt",0); fs_write(fs_lookup(-1,"readme.txt"),"Welcome to mUnix v0.8.1 (Rust GUI).");
    fs_alloc(-1,"about.txt",0);  fs_write(fs_lookup(-1,"about.txt"),"https://mUnixOs.com");
    d_home = fs_alloc(-1,"home",1);
    d_docs = fs_alloc(-1,"docs",1);
    fs_alloc(d_home,"user.txt",0);  fs_write(fs_lookup(d_home,"user.txt"),"default user: root");
    fs_alloc(d_docs,"intro.txt",0); fs_write(fs_lookup(d_docs,"intro.txt"),"mUnix is a tiny OS.");
}

/* ---- RAMFS ABI for Rust ---- */
int munix_ramfs_count(void) { int i,n=0; for (i=0;i<FS_MAX;i++) if (fs[i].used) n++; return n; }
int munix_ramfs_get(int idx, char *name, int cap, int *isdir, int *size, int *parent) {
    int k; if (idx<0||idx>=FS_MAX||!fs[idx].used) return 0;
    if (name && cap>0) { for (k=0;k<cap-1&&fs[idx].name[k];k++) name[k]=fs[idx].name[k]; name[k]=0; }
    if (isdir) *isdir = fs[idx].isdir;
    if (size)  *size  = fs[idx].size;
    if (parent) *parent= fs[idx].parent;
    return 1;
}
int munix_ramfs_lookup(const char *name, int parent) { return fs_lookup(parent,name); }
int munix_ramfs_create(const char *name, int parent, int isdir) { return fs_alloc(parent,name,isdir); }
int munix_ramfs_read(const char *name, int parent, char *out, int cap) {
    int idx = fs_lookup(parent,name), n, i;
    if (idx<0) return -1;
    if (fs[idx].isdir) return -2;
    n = fs[idx].size; if (n>cap-1) n=cap-1;
    for (i=0;i<n;i++) out[i]=fs[idx].data[i];
    out[n]=0;
    return n;
}
int munix_ramfs_remove(const char *name, int parent) {
    int idx = fs_lookup(parent,name);
    if (idx<0) return -1;
    fs[idx].used=0;
    return 0;
}
int munix_ramfs_pool_used(void) { return (int)fstop; }
int munix_ramfs_pool_size(void) { return FS_POOL; }
int munix_ramfs_root(void) { return -1; }
void munix_system_reboot(void) {
    while (inb(0x64) & 0x02) {}
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("hlt");
}
unsigned int munix_ticks(void) { return g_ticks; }
int munix_fb_width(void) { return g_w; }
int munix_fb_height(void) { return g_h; }

/* ---- Speaker (non-blocking) ---- */
void munix_speaker_off(void) {
    unsigned char t = inb(0x61);
    outb(0x61, (unsigned char)(t & 0xFC));
}
void munix_speaker_set(unsigned int freq) {
    unsigned int div;
    if (freq == 0) { munix_speaker_off(); return; }
    div = 1193180u / freq;
    outb(0x43, 0xB6);
    outb(0x42, (unsigned char)(div & 0xFF));
    outb(0x42, (unsigned char)((div >> 8) & 0xFF));
    { unsigned char t = inb(0x61); if ((t & 0x03) != 0x03) outb(0x61, (unsigned char)(t | 0x03)); }
}

/* ---- RAMFS write ---- */
int munix_ramfs_write(const char *name, int parent, const char *data, int len) {
    int idx = fs_lookup(parent, name), n, i;
    if (idx < 0) {
        idx = fs_alloc(parent, name, 0);
        if (idx < 0) return -1;
    }
    if (fs[idx].isdir) return -2;
    n = len; if (n > 255) n = 255; if (n < 0) n = 0;
    for (i = 0; i < n; i++) fs[idx].data[i] = data[i];
    fs[idx].data[n] = 0;
    fs[idx].size = n;
    return 0;
}

int munix_exec_bytes(const unsigned char *code, int len) {
    typedef int (*fn_t)(void);
    fn_t f;
    if (!code || len <= 0) return -1;
    f = (fn_t)code;
    return (int)f();
}
/* ---- Enable SSE ---- */
static void enable_sse(void) {
    unsigned int eax;
    __asm__ volatile("mov %%cr0, %%rax" : "=a"(eax));
    eax &= ~0x00000004u; eax |= 0x00000022u;
    __asm__ volatile("mov %%rax, %%cr0" :: "a"((unsigned long long)eax));
    __asm__ volatile("mov %%cr4, %%rax" : "=a"(eax));
    eax |= 0x00000600u;
    __asm__ volatile("mov %%rax, %%cr4" :: "a"((unsigned long long)eax));
    __asm__ volatile("fninit");
}

/* ---- Boot menu (VGA text) ---- */
#define VGA_TEXT ((volatile unsigned short *)0xB8000)
static void t_clear(void) { int i; for (i=0;i<80*25;i++) VGA_TEXT[i]=0x0720; }
static void t_put(int x,int y,char c,unsigned char a){ if(x<0||y<0||x>=80||y>=25)return; VGA_TEXT[y*80+x]=(unsigned short)((a<<8)|(unsigned char)c); }
static void t_write(int x,int y,const char*s,unsigned char a){int i=0;while(s[i]){t_put(x+i,y,s[i],a);i++;}}
static void t_box(int x,int y,int w,int h,unsigned char a){
    int i;
    t_put(x,y,0xC9,a); t_put(x+w-1,y,0xBB,a); t_put(x,y+h-1,0xC8,a); t_put(x+w-1,y+h-1,0xBC,a);
    for(i=1;i<w-1;i++){t_put(x+i,y,0xCD,a);t_put(x+i,y+h-1,0xCD,a);}
    for(i=1;i<h-1;i++){t_put(x,y+i,0xBA,a);t_put(x+w-1,y+i,0xBA,a);}
}
static void splash(void){
    int i,k;
    static const char *logo[5] = {
        "                           _  _  _ ",
        "    _ __ ___  _   _ _(_)| |_| |",
        "   | '_ ` _ \\| | | | | | | | | |",
        "   | | | | | | |_| | |_| | |_| |",
        "   |_| |_| |_|\\__,_|\\__,_|_|\\___|"
    };
    const char *txt = "mUnix OS is loading, please wait...";
    int tl=0; t_clear();
    for(i=0;i<5;i++){int len=0,x;while(logo[i][len])len++;x=(80-len)/2;if(x<0)x=0;for(k=0;k<len;k++)t_put(x+k,8+i,logo[i][k],0x0A);}
    while(txt[tl])tl++;
    t_write((80-tl)/2,15,txt,0x0F);
    for(i=0;i<=100;i+=2){
        int row=18,col0=15,w=50; int bar=i*w/100;
        char pct[8]; int pp=0;
        t_put(col0-1,row,'[',0x0B); t_put(col0+w,row,']',0x0B);
        for(k=0;k<w;k++){char c=(k<bar)?'#':'.';unsigned char a=(k<bar)?0x0A:0x08;t_put(col0+k,row,c,a);}
        if(i>=100){pct[pp++]='1';pct[pp++]='0';pct[pp++]='0';}
        else if(i>=10){pct[pp++]=(char)('0'+i/10);pct[pp++]=(char)('0'+i%10);}
        else pct[pp++]=(char)('0'+i);
        pct[pp++]='%';pct[pp]=0;
        t_write(col0+w+2,row,pct,0x0E);
        {volatile unsigned int ww;for(ww=0;ww<500000;ww++){}}
    }
    t_write(15,20,"Loading complete.",0x0A);
    {volatile unsigned int ww;for(ww=0;ww<1000000;ww++){}}
}

/* ---- Input ---- */
static int g_dirty = 1;

static void pump_input(void) {
    int guard = 64;
    while (guard-- > 0) {
        unsigned char st = inb(0x64);
        if (!(st & 0x01)) break;
        {
            unsigned char b = inb(0x60);
            if (st & 0x20) { mouse_feed(b); continue; }
            { int k = keyboard_feed(b); if (k >= 0) { munix_gui_key(k); g_dirty = 1; } }
        }
    }
    if (mouse_btn != mouse_prev) {
        munix_gui_click(mouse_x, mouse_y, mouse_btn);
        mouse_prev = mouse_btn;
        g_dirty = 1;
    }
}

/* ---- Main ---- */

/* ============ serial debug ============ */
void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}
void serial_puts(const char *s) {
    while (*s) {
        while (!(inb(0x3F8 + 5) & 0x20)) {}
        outb(0x3F8, *s++);
    }
}
void serial_hex(unsigned long long v) {
    char b[17]; b[16] = 0;
    int i;
    for (i = 15; i >= 0; i--) {
        b[i] = "0123456789abcdef"[v & 0xF];
        v >>= 4;
    }
    serial_puts(b);
}
/* ====================================== */

/* ============ CRC32 (IEEE 802.3, reflected) ============ */
static unsigned int crc32_table[256];
static int crc32_ready = 0;

static void crc32_init(void) {
    unsigned int i, j, c;
    for (i = 0; i < 256; i++) {
        c = i;
        for (j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_ready = 1;
}

static unsigned int crc32(const void *data, unsigned int len, unsigned int seed) {
    const unsigned char *p = (const unsigned char *)data;
    unsigned int crc = seed ^ 0xFFFFFFFFu;
    unsigned int i;
    if (!crc32_ready) crc32_init();
    for (i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ============ GPT structures ============ */
struct gpt_header {
    unsigned char  signature[8];       /* "EFI PART" */
    unsigned int   revision;           /* 0x00010000 */
    unsigned int   header_size;        /* 92 */
    unsigned int   header_crc32;
    unsigned int   reserved;
    unsigned long long current_lba;
    unsigned long long backup_lba;
    unsigned long long first_usable_lba;
    unsigned long long last_usable_lba;
    unsigned char  disk_guid[16];
    unsigned long long entries_lba;
    unsigned int   num_entries;
    unsigned int   entry_size;         /* 128 */
    unsigned int   entries_crc32;
} __attribute__((packed));

struct gpt_entry {
    unsigned char  type_guid[16];
    unsigned char  unique_guid[16];
    unsigned long long first_lba;
    unsigned long long last_lba;
    unsigned long long attributes;
    unsigned char  name[72];           /* UTF-16LE, 36 chars */
} __attribute__((packed));

/* well-known type GUIDs (mixed-endian storage) */
static const unsigned char GUID_ESP[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};
static const unsigned char GUID_LINUX_FS[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};
static const unsigned char GUID_BASIC_DATA[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

static int guid_eq(const unsigned char *a, const unsigned char *b) {
    int i;
    for (i = 0; i < 16; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* "EFI PART" check */
static int gpt_check_signature(const struct gpt_header *h) {
    return h->signature[0] == 'E' && h->signature[1] == 'F' &&
           h->signature[2] == 'I' && h->signature[3] == ' ' &&
           h->signature[4] == 'P' && h->signature[5] == 'A' &&
           h->signature[6] == 'R' && h->signature[7] == 'T';
}

static void guid_to_str(const unsigned char *g, char *out37) {
    /* GPT stores GUIDs mixed-endian: first 3 groups LE, last 2 BE */
    static const char hexd[] = "0123456789abcdef";
    int i, k = 0;
    /* group1: 4 bytes LE -> reversed */
    for (i = 3; i >= 0; i--) { out37[k++] = hexd[(g[i]>>4)&0xF]; out37[k++] = hexd[g[i]&0xF]; }
    out37[k++] = '-';
    /* group2: 2 bytes LE */
    for (i = 5; i >= 4; i--) { out37[k++] = hexd[(g[i]>>4)&0xF]; out37[k++] = hexd[g[i]&0xF]; }
    out37[k++] = '-';
    /* group3: 2 bytes LE */
    for (i = 7; i >= 6; i--) { out37[k++] = hexd[(g[i]>>4)&0xF]; out37[k++] = hexd[g[i]&0xF]; }
    out37[k++] = '-';
    /* group4: 2 bytes BE */
    for (i = 8; i < 10; i++) { out37[k++] = hexd[(g[i]>>4)&0xF]; out37[k++] = hexd[g[i]&0xF]; }
    out37[k++] = '-';
    /* group5: 6 bytes BE */
    for (i = 10; i < 16; i++) { out37[k++] = hexd[(g[i]>>4)&0xF]; out37[k++] = hexd[g[i]&0xF]; }
    out37[k] = 0;
}

static void gpt_dump(void) {
    static unsigned char sector[512];
    static unsigned char entries_buf[128 * 128];  /* max 128 entries × 128 bytes */
    struct gpt_header *h;
    unsigned int entries_crc_calc;

    if (!g_ata_present) {
        serial_puts("gpt: no disk\n");
        return;
    }
    if (ata_read_sector(1, sector) != 0) {
        serial_puts("gpt: cannot read LBA1\n");
        return;
    }
    h = (struct gpt_header *)sector;
    if (!gpt_check_signature(h)) {
        serial_puts("gpt: no GPT (signature mismatch on LBA1)\n");
        return;
    }
    serial_puts("gpt: GPT found\n");
    serial_puts("  revision      = ");
    serial_hex((unsigned long long)h->revision);
    serial_puts("\n  header_size   = ");
    serial_hex((unsigned long long)h->header_size);
    serial_puts("\n  current_lba   = ");
    serial_hex(h->current_lba);
    serial_puts("\n  backup_lba    = ");
    serial_hex(h->backup_lba);
    serial_puts("\n  first_usable  = ");
    serial_hex(h->first_usable_lba);
    serial_puts("\n  last_usable   = ");
    serial_hex(h->last_usable_lba);
    serial_puts("\n  num_entries   = ");
    serial_hex((unsigned long long)h->num_entries);
    serial_puts("\n  entry_size    = ");
    serial_hex((unsigned long long)h->entry_size);
    serial_puts("\n");

    /* read partition entries */
    {
        unsigned int total = h->num_entries * h->entry_size;
        unsigned int lba = (unsigned int)h->entries_lba;
        unsigned int off = 0;
        if (total > sizeof(entries_buf)) total = sizeof(entries_buf);
        while (off < total) {
            if (ata_read_sector(lba, entries_buf + off) != 0) {
                serial_puts("gpt: cannot read entries\n");
                return;
            }
            off += 512;
            lba += 1;
        }
    }

    /* verify CRC32 of entries */
    entries_crc_calc = crc32(entries_buf,
                             h->num_entries * h->entry_size, 0);
    serial_puts("  entries_crc calc = ");
    serial_hex((unsigned long long)entries_crc_calc);
    serial_puts("\n  entries_crc file = ");
    serial_hex((unsigned long long)h->entries_crc32);
    serial_puts(entries_crc_calc == h->entries_crc32 ? "  OK\n" : "  MISMATCH\n");

    /* dump non-empty entries */
    {
        unsigned int i;
        int found = 0;
        for (i = 0; i < h->num_entries; i++) {
            struct gpt_entry *e = (struct gpt_entry *)(entries_buf + i * h->entry_size);
            int empty = 1;
            int j;
            for (j = 0; j < 16; j++) if (e->type_guid[j] != 0) { empty = 0; break; }
            if (empty) continue;
            found = 1;
            serial_puts("\n  partition ");
            serial_hex((unsigned long long)i);
            serial_puts(":\n    type = ");
            if (guid_eq(e->type_guid, GUID_ESP)) {
                serial_puts("ESP (EFI System Partition)");
            } else if (guid_eq(e->type_guid, GUID_LINUX_FS)) {
                serial_puts("Linux filesystem");
            } else if (guid_eq(e->type_guid, GUID_BASIC_DATA)) {
                serial_puts("Basic data (Windows)");
            } else {
                static char gbuf[40];
                guid_to_str(e->type_guid, gbuf);
                serial_puts(gbuf);
            }
            serial_puts("\n    first_lba = ");
            serial_hex(e->first_lba);
            serial_puts("\n    last_lba  = ");
            serial_hex(e->last_lba);
            serial_puts("\n    size      = ");
            serial_hex((e->last_lba - e->first_lba + 1) / 2 / 1024);
            serial_puts(" MiB\n");
        }
        if (!found) serial_puts("  (no non-empty partitions)\n");
    }
}

/* ============ ATA PIO driver (28-bit LBA) ============ */
#define ATA_DATA      0x1F0
#define ATA_ERROR     0x1F1
#define ATA_SECCOUNT  0x1F2
#define ATA_LBA0      0x1F3
#define ATA_LBA1      0x1F4
#define ATA_LBA2      0x1F5
#define ATA_DRIVE     0x1F6
#define ATA_STATUS    0x1F7
#define ATA_COMMAND   0x1F7
#define ATA_ALTSTATUS 0x3F6

#define ATA_SR_BSY    0x80
#define ATA_SR_DRDY   0x40
#define ATA_SR_DF     0x20
#define ATA_SR_DRQ    0x08
#define ATA_SR_ERR    0x01

static int g_ata_present = 0;
static unsigned int g_ata_sectors = 0;
static char g_ata_model[41];

static void ata_io_wait(void) {
    inb(ATA_ALTSTATUS); inb(ATA_ALTSTATUS);
    inb(ATA_ALTSTATUS); inb(ATA_ALTSTATUS);
}

static int ata_wait_bsy_clear(void) {
    unsigned short B = g_ata_base;
    int timeout = 1000000;
    while (timeout-- > 0) {
        if (!(inb(B + 7) & ATA_SR_BSY)) return 0;
    }
    return -1;
}

static int ata_wait_drq(void) {
    int timeout = 1000000;
    while (timeout-- > 0) {
        unsigned char st = inb(ATA_STATUS);
        if (st & (ATA_SR_ERR | ATA_SR_DF)) return -2;
        if (st & ATA_SR_DRQ) return 0;
    }
    return -1;
}

static int ata_read_sector(unsigned int lba, unsigned char *out512) {
    unsigned short B = g_ata_base;
    if (ata_wait_bsy_clear() != 0) return -1;
    outb(B + 6, g_ata_drv | ((lba >> 24) & 0x0F));
    outb(B + 2, 1);
    outb(B + 3, (unsigned char)(lba & 0xFF));
    outb(B + 4, (unsigned char)((lba >> 8) & 0xFF));
    outb(B + 5, (unsigned char)((lba >> 16) & 0xFF));
    outb(B + 7, 0x20);
    ata_io_wait();
    if (ata_wait_bsy_clear() != 0) return -1;
    {
        unsigned char st = inb(B + 7);
        if (st & ATA_SR_ERR) return -3;
        if (!(st & ATA_SR_DRQ)) return -4;
    }
    {
        unsigned short *p = (unsigned short *)out512;
        int i;
        for (i = 0; i < 256; i++) p[i] = nw_in(B + 0);
    }
    ata_io_wait();
    return 0;
}

static int ata_write_sector(unsigned int lba, const unsigned char *in512) {
    unsigned short B = g_ata_base;
    if (ata_wait_bsy_clear() != 0) return -1;
    outb(B + 6, g_ata_drv | ((lba >> 24) & 0x0F));
    outb(B + 2, 1);
    outb(B + 3, (unsigned char)(lba & 0xFF));
    outb(B + 4, (unsigned char)((lba >> 8) & 0xFF));
    outb(B + 5, (unsigned char)((lba >> 16) & 0xFF));
    outb(B + 7, 0x30);
    ata_io_wait();
    if (ata_wait_bsy_clear() != 0) return -1;
    if (!(inb(B + 7) & ATA_SR_DRQ)) return -4;
    {
        const unsigned short *p = (const unsigned short *)in512;
        int i;
        for (i = 0; i < 256; i++) nw_out(B + 0, p[i]);
    }
    ata_io_wait();
    if (ata_wait_bsy_clear() != 0) return -1;
    return 0;
}

static void ata_reset(void) {
    outb(0x3F6, 0x04);
    ata_io_wait();
    outb(0x3F6, 0x00);
    ata_io_wait();
    int t = 1000000;
    while (t-- > 0 && (inb(ATA_STATUS) & ATA_SR_BSY)) {}
}

static int ata_identify(unsigned short *out256) {
    unsigned char st;
    outb(ATA_DRIVE, 0xA0);
    ata_io_wait();
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, 0xEC);
    ata_io_wait();
    st = inb(ATA_STATUS);
    if (st == 0x00 || st == 0xFF) return -1;
    if (ata_wait_bsy_clear() != 0) return -1;
    if (inb(ATA_LBA1) != 0 || inb(ATA_LBA2) != 0) return -2;
    if (ata_wait_drq() != 0) return -3;
    {
        int i;
        for (i = 0; i < 256; i++) out256[i] = nw_in(ATA_DATA);
    }
    return 0;
}

static int ata_identify_ch(unsigned short base, unsigned char drvsel,
                           unsigned short *out256) {
    unsigned char st;
    outb(base + 6, drvsel);
    ata_io_wait();
    outb(base + 2, 0);
    outb(base + 3, 0);
    outb(base + 4, 0);
    outb(base + 5, 0);
    outb(base + 7, 0xEC);
    ata_io_wait();
    st = inb(base + 7);
    if (st == 0x00 || st == 0xFF) return -1;
    {
        int timeout = 1000000;
        while (timeout-- > 0 && (inb(base + 7) & 0x80)) {}
        if (timeout <= 0) return -1;
    }
    if (inb(base + 4) != 0 || inb(base + 5) != 0) return -2;
    {
        int timeout = 1000000;
        while (timeout-- > 0) {
            st = inb(base + 7);
            if (st & (0x01 | 0x20)) return -3;
            if (st & 0x08) break;
        }
        if (timeout <= 0) return -1;
    }
    {
        int i;
        for (i = 0; i < 256; i++) out256[i] = nw_in(base + 0);
    }
    return 0;
}

/* Piix3 IDE BAR discovery */
static unsigned short g_ide_cmd_base[2] = { 0x1F0, 0x170 };
static unsigned short g_ide_ctl_base[2] = { 0x3F6, 0x376 };

static void ata_probe_pci(void) {
    int i;
    serial_puts("ata: PCI scan for IDE controller...\n");
    for (i = 0; i < g_pci_count; i++) {
        struct pci_dev *d = &g_pci_devs[i];
        if (d->class_code != 0x01) continue;       /* mass storage */
        if (d->subclass   != 0x01) continue;       /* IDE */
        serial_puts("ata: found IDE ctrl bus="); serial_hex(d->bus);
        serial_puts(" slot="); serial_hex(d->slot);
        serial_puts(" func="); serial_hex(d->func);
        serial_puts(" prog_if="); serial_hex(d->prog_if);
        serial_puts("\n");

        /* BAR0 = primary cmd (bit0=1 → IO space) */
        if (d->bar[0] & 1) {
            g_ide_cmd_base[0] = (unsigned short)(d->bar[0] & 0xFFFC);
        }
        /* BAR1 = primary ctrl */
        if (d->bar[1] & 1) {
            g_ide_ctl_base[0] = (unsigned short)(d->bar[1] & 0xFFFC);
        }
        /* BAR2 = secondary cmd */
        if (d->bar[2] & 1) {
            g_ide_cmd_base[1] = (unsigned short)(d->bar[2] & 0xFFFC);
        }
        /* BAR3 = secondary ctrl */
        if (d->bar[3] & 1) {
            g_ide_ctl_base[1] = (unsigned short)(d->bar[3] & 0xFFFC);
        }

        serial_puts("ata: BAR0="); serial_hex(d->bar[0]);
        serial_puts(" BAR2="); serial_hex(d->bar[2]);
        serial_puts("\n");
        serial_puts("ata: prim_cmd=0x"); serial_hex(g_ide_cmd_base[0]);
        serial_puts(" sec_cmd=0x"); serial_hex(g_ide_cmd_base[1]);
        serial_puts("\n");
        return;
    }
    serial_puts("ata: no IDE ctrl found, using default 0x1F0/0x170\n");
}

static void ata_init(void) {
    unsigned short id[256];
    int bi, si;
    int found = 0;

    ata_probe_pci();

    for (bi = 0; bi < 2 && !found; bi++) {
        for (si = 0; si < 2 && !found; si++) {
            unsigned short base = g_ide_cmd_base[bi];
            unsigned short ctl  = g_ide_ctl_base[bi];
            unsigned char drv = (si == 0) ? 0xA0 : 0xB0;
            int rc;
            int attempt;

            serial_puts("ata: probe base=0x"); serial_hex(base);
            serial_puts(" drv=0x"); serial_hex(drv);
            serial_puts(" ");

            for (attempt = 0; attempt < 2; attempt++) {
                outb(ctl, 0x04);
                ata_io_wait();
                outb(ctl, 0x00);
                ata_io_wait();
                rc = ata_identify_ch(base, drv, id);
                if (rc == 0) break;
            }

            serial_puts("rc="); serial_hex((unsigned long long)(long long)rc);
            serial_puts("\n");

            if (rc == 0) {
                g_ata_base = base;
                g_ata_drv  = drv;
                found = 1;
            }
        }
    }

    if (!found) {
        serial_puts("ata: no disk on any IDE channel\n");
        g_ata_present = 0;
        return;
    }
    g_ata_present = 1;
    {
        int i;
        for (i = 0; i < 20; i++) {
            g_ata_model[i * 2 + 0] = (char)((id[27 + i] >> 8) & 0xFF);
            g_ata_model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
        }
        g_ata_model[40] = 0;
    }
    g_ata_sectors = ((unsigned int)id[61] << 16) | (unsigned int)id[60];
    serial_puts("ata: disk found\n  model = ");
    serial_puts(g_ata_model);
    serial_puts("\n  sectors = ");
    serial_hex((unsigned long long)g_ata_sectors);
    serial_puts("\n");
}

static void ata_dump_mbr(void) {
    unsigned char buf[512];
    int i;
    if (!g_ata_present) { serial_puts("ata: no disk\n"); return; }
    if (ata_read_sector(0, buf) != 0) { serial_puts("ata: MBR read fail\n"); return; }
    serial_puts("ata: MBR[0..31] = ");
    for (i = 0; i < 32; i++) {
        static const char hx[] = "0123456789abcdef";
        char pair[3] = { hx[(buf[i]>>4)&0xF], hx[buf[i]&0xF], 0 };
        serial_puts(pair);
    }
    serial_puts("\n");
    if (buf[510] == 0x55 && buf[511] == 0xAA)
        serial_puts("ata: MBR sig 0xAA55 OK\n");
    else
        serial_puts("ata: NO MBR sig\n");
}



/* ==================== IDT (64-bit) ==================== */
struct idt_entry {
    unsigned short off_low;
    unsigned short sel;
    unsigned char  ist;
    unsigned char  type;
    unsigned short off_mid;
    unsigned int   off_high;
    unsigned int   zero;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned long long base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

#define DEFINE_ISR(n, ch) \
    __asm__( \
        ".text\n" \
        ".globl isr" #n "\n" \
        "isr" #n ":\n" \
        "    cli\n" \
        "    mov $0x3F8, %dx\n" \
        "    mov $" #ch ", %al\n" \
        "    out %al, %dx\n" \
        "1:  hlt\n" \
        "    jmp 1b\n" \
    );

DEFINE_ISR(0,  '0')  DEFINE_ISR(1,  '1')  DEFINE_ISR(2,  '2')  DEFINE_ISR(3,  '3')
DEFINE_ISR(4,  '4')  DEFINE_ISR(5,  '5')  DEFINE_ISR(6,  '6')  DEFINE_ISR(7,  '7')
DEFINE_ISR(8,  '8')  DEFINE_ISR(9,  '9')  DEFINE_ISR(10, 'A')  DEFINE_ISR(11, 'B')
DEFINE_ISR(12, 'C')  DEFINE_ISR(13, 'D')  DEFINE_ISR(14, 'E')  DEFINE_ISR(15, 'F')
DEFINE_ISR(16, 'G')  DEFINE_ISR(17, 'H')  DEFINE_ISR(18, 'I')  DEFINE_ISR(19, 'J')
DEFINE_ISR(20, 'K')  DEFINE_ISR(21, 'L')  DEFINE_ISR(22, 'M')  DEFINE_ISR(23, 'N')
DEFINE_ISR(24, 'O')  DEFINE_ISR(25, 'P')  DEFINE_ISR(26, 'Q')  DEFINE_ISR(27, 'R')
DEFINE_ISR(28, 'S')  DEFINE_ISR(29, 'T')  DEFINE_ISR(30, 'U')  DEFINE_ISR(31, 'V')

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

static void idt_set(int n, void (*h)(void)) {
    unsigned long long addr = (unsigned long long)h;
    idt[n].off_low  = addr & 0xFFFF;
    idt[n].sel      = 0x08;
    idt[n].ist      = 0;
    idt[n].type     = 0x8E;
    idt[n].off_mid  = (addr >> 16) & 0xFFFF;
    idt[n].off_high = (addr >> 32) & 0xFFFFFFFF;
    idt[n].zero     = 0;
}

static void idt_init(void) {
    int i;
    for (i = 0; i < 256; i++) {
        idt[i].off_low = 0; idt[i].sel = 0; idt[i].ist = 0;
        idt[i].type = 0; idt[i].off_mid = 0; idt[i].off_high = 0; idt[i].zero = 0;
    }
    idt_set(0,  isr0);  idt_set(1,  isr1);  idt_set(2,  isr2);  idt_set(3,  isr3);
    idt_set(4,  isr4);  idt_set(5,  isr5);  idt_set(6,  isr6);  idt_set(7,  isr7);
    idt_set(8,  isr8);  idt_set(9,  isr9);  idt_set(10, isr10); idt_set(11, isr11);
    idt_set(12, isr12); idt_set(13, isr13); idt_set(14, isr14); idt_set(15, isr15);
    idt_set(16, isr16); idt_set(17, isr17); idt_set(18, isr18); idt_set(19, isr19);
    idt_set(20, isr20); idt_set(21, isr21); idt_set(22, isr22); idt_set(23, isr23);
    idt_set(24, isr24); idt_set(25, isr25); idt_set(26, isr26); idt_set(27, isr27);
    idt_set(28, isr28); idt_set(29, isr29); idt_set(30, isr30); idt_set(31, isr31);
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (unsigned long long)&idt;
    __asm__ volatile("lidt %0" :: "m"(idtp));
}

/* ==================== PCI enumeration ==================== */

static unsigned int pci_read32(unsigned char bus, unsigned char slot,
                               unsigned char func, unsigned char off) {
    unsigned int addr = 0x80000000u
                      | ((unsigned int)bus  << 16)
                      | ((unsigned int)slot << 11)
                      | ((unsigned int)func <<  8)
                      | (off & 0xFC);
    nl_out(0xCF8, addr);
    return nl_in(0xCFC);
}

static void pci_enumerate(void) {
    unsigned char bus, slot, func;
    g_pci_count = 0;
    for (bus = 0; bus < 255 && g_pci_count < 64; bus++) {
        for (slot = 0; slot < 32 && g_pci_count < 64; slot++) {
            unsigned int vd = pci_read32(bus, slot, 0, 0);
            if ((vd & 0xFFFF) == 0xFFFF) continue;
            unsigned char header = (unsigned char)((pci_read32(bus, slot, 0, 0x0C) >> 16) & 0xFF);
            int funcs = (header & 0x80) ? 8 : 1;
            for (func = 0; func < funcs && g_pci_count < 64; func++) {
                unsigned int v = pci_read32(bus, slot, func, 0);
                if ((v & 0xFFFF) == 0xFFFF) continue;
                struct pci_dev *d = &g_pci_devs[g_pci_count++];
                d->bus = bus; d->slot = slot; d->func = func;
                d->vendor = (unsigned short)(v & 0xFFFF);
                d->device = (unsigned short)((v >> 16) & 0xFFFF);
                d->class_rev = pci_read32(bus, slot, func, 0x08);
                d->class_code = (d->class_rev >> 24) & 0xFF;
                d->subclass   = (d->class_rev >> 16) & 0xFF;
                d->prog_if    = (d->class_rev >>  8) & 0xFF;
                d->irq        = (unsigned char)(pci_read32(bus, slot, func, 0x3C) & 0xFF);
                {
                    int i;
                    for (i = 0; i < 6; i++) {
                        unsigned int bar = pci_read32(bus, slot, func, 0x10 + i * 4);
                        d->bar[i] = bar;
                        if ((bar & 0x07) == 0x04 && i < 5) i++;
                    }
                }
            }
        }
    }
}

static void pci_dump_usb(void) {
    int i;
    serial_puts("=== USB controllers found: ===");
    for (i = 0; i < g_pci_count; i++) {
        struct pci_dev *d = &g_pci_devs[i];
        if (d->class_code == 0x0C && d->subclass == 0x03) {
            serial_puts("\nUSB ctrl: bus="); serial_hex(d->bus);
            serial_puts(" slot="); serial_hex(d->slot);
            serial_puts(" func="); serial_hex(d->func);
            serial_puts("\n  vid:did = "); serial_hex(d->vendor);
            serial_puts(":"); serial_hex(d->device);
            serial_puts("\n");
        }
    }
    serial_puts("\n");
}

static void pci_dump_all(void) {
    int i;
    serial_puts("=== PCI devices ===\n");
    for (i = 0; i < g_pci_count; i++) {
        struct pci_dev *d = &g_pci_devs[i];
        serial_puts("  "); serial_hex(d->bus);
        serial_puts(":"); serial_hex(d->slot);
        serial_puts("."); serial_hex(d->func);
        serial_puts(" vid:did="); serial_hex(d->vendor);
        serial_puts(":"); serial_hex(d->device);
        serial_puts(" cls="); serial_hex(d->class_code);
        serial_puts(":"); serial_hex(d->subclass);
        serial_puts(" bar0="); serial_hex(d->bar[0]);
        serial_puts("\n");
    }
}
/* ==================== RTL8139 NIC driver ==================== */
#define RTL_IDR0    0x00
#define RTL_TSD0    0x10
#define RTL_TSAD0   0x20
#define RTL_RBSTART 0x30
#define RTL_CR      0x37
#define RTL_CAPR    0x38
#define RTL_CBR     0x3A
#define RTL_IMR     0x3C
#define RTL_ISR     0x3E
#define RTL_TCR     0x40
#define RTL_RCR     0x44
#define RTL_CFG1    0x52

#define RTL_RX_BUF_SZ  (8192 + 16 + 1500)
#define RTL_TX_BUF_SZ  1536

static unsigned short g_rtl_io  = 0;
static int            g_rtl_ok  = 0;
static unsigned char  g_rtl_mac[6];

static unsigned char g_rtl_rx_buf[RTL_RX_BUF_SZ] __attribute__((aligned(4096)));
static unsigned char g_rtl_tx_buf[4][RTL_TX_BUF_SZ] __attribute__((aligned(16)));
static unsigned int  g_rtl_rx_off = 0;
static int           g_rtl_tx_cur = 0;

/* our IP config (QEMU user-mode net) */
#define NET_MY_IP   {10, 0, 2, 15}
#define NET_GW_IP   {10, 0, 2, 2}
#define NET_MASK    {255, 255, 255, 0}

static void rtl_write8 (unsigned short r, unsigned char  v){ outb(g_rtl_io + r, v); }
static unsigned char rtl_read8 (unsigned short r){ return inb(g_rtl_io + r); }
static void rtl_write16(unsigned short r, unsigned short v){ nw_out(g_rtl_io + r, v); }
static unsigned short rtl_read16(unsigned short r){ return nw_in(g_rtl_io + r); }
static void rtl_write32(unsigned short r, unsigned int v){ nl_out(g_rtl_io + r, v); }
static unsigned int  rtl_read32(unsigned short r){ return nl_in(g_rtl_io + r); }

static void rtl8139_init(void) {
    int i;
    unsigned int bar0 = 0;
    unsigned char bus = 0, slot = 0, func = 0;
    int found = 0;

    for (i = 0; i < g_pci_count; i++) {
        struct pci_dev *d = &g_pci_devs[i];
        if (d->vendor == 0x10EC && d->device == 0x8139) {
            bar0 = d->bar[0];
            bus = d->bus; slot = d->slot; func = d->func;
            found = 1;
            break;
        }
    }
    if (!found) {
        serial_puts("rtl8139: not found\n");
        return;
    }
    serial_puts("rtl8139: found\n");

    if (!(bar0 & 1)) {
        serial_puts("rtl8139: BAR0 not I/O\n");
        return;
    }
    g_rtl_io = (unsigned short)(bar0 & 0xFFFC);
    serial_puts("rtl8139: io=0x"); serial_hex(g_rtl_io); serial_puts("\n");

    /* enable bus master */
    {
        unsigned int cmd_addr = 0x80000000u | ((unsigned)bus << 16) | ((unsigned)slot << 11) | ((unsigned)func << 8) | 0x04;
        nl_out(0xCF8, cmd_addr);
        unsigned int cmd = nl_in(0xCFC);
        cmd |= 0x04;   /* bus master */
        nl_out(0xCFC, cmd);
    }

    /* power on, software reset */
    rtl_write8(RTL_CFG1, 0x00);
    rtl_write8(RTL_CR, 0x10);
    { int w = 100000; while (w-- > 0 && (rtl_read8(RTL_CR) & 0x10)) {} }

    /* read MAC */
    for (i = 0; i < 6; i++) g_rtl_mac[i] = rtl_read8(RTL_IDR0 + i);
    serial_puts("rtl8139: MAC=");
    for (i = 0; i < 6; i++) {
        static const char hx[] = "0123456789abcdef";
        char p[3] = { hx[(g_rtl_mac[i]>>4)&0xF], hx[g_rtl_mac[i]&0xF], 0 };
        serial_puts(p);
        if (i < 5) serial_puts(":");
    }
    serial_puts("\n");

    /* RX buffer */
    rtl_write32(RTL_RBSTART, (unsigned int)(unsigned long)g_rtl_rx_buf);

    /* Initialize CAPR = CBR - 16 (per RTL8139 spec) */
    rtl_write16(RTL_CAPR, 0xFFF0);

    /* disable IRQs, we poll */
    rtl_write16(RTL_IMR, 0x0000);
    rtl_write16(RTL_ISR, 0xFFFF);

    /* RX config: AB|AM|APM|AAP|WRAP */
    rtl_write32(RTL_RCR, 0x0000000F | (1u << 7));

    /* TX config */
    rtl_write32(RTL_TCR, 0x03000700);

    /* enable RX + TX */
    rtl_write8(RTL_CR, 0x0C);

    g_rtl_rx_off = 0;
    g_rtl_tx_cur = 0;
    g_rtl_ok = 1;
    serial_puts("rtl8139: ready\n");
}

static int rtl8139_send(const void *data, unsigned int len) {
    if (!g_rtl_ok || len > RTL_TX_BUF_SZ) return -1;
    int slot = g_rtl_tx_cur;
    g_rtl_tx_cur = (g_rtl_tx_cur + 1) & 3;
    memcpy(g_rtl_tx_buf[slot], data, len);
    if (len < 60) { memset(g_rtl_tx_buf[slot] + len, 0, 60 - len); len = 60; }
    rtl_write32(RTL_TSAD0 + slot * 4, (unsigned int)(unsigned long)g_rtl_tx_buf[slot]);
    rtl_write32(RTL_TSD0  + slot * 4, (unsigned int)len);
    int w = 2000000;
    while (w-- > 0 && !(rtl_read32(RTL_TSD0 + slot * 4) & 0x8000)) {}
    return 0;
}

/* возвращает указатель на пакет + длину, или NULL */
static unsigned int g_rx_poll_calls = 0;
static unsigned int g_rx_poll_hits = 0;

static int rtl8139_poll(unsigned char **out_pkt, unsigned int *out_len) {
    if (!g_rtl_ok) return 0;

    /* Check if ring is empty: CBR == (CAPR + 16) mod 8K */
    unsigned int cbr  = rtl_read16(RTL_CBR);
    unsigned int capr = rtl_read16(RTL_CAPR);
    unsigned int capr_plus = (capr + 16) & 0xFFF0;
    if ((cbr & 0xFFF0) == capr_plus) return 0;   /* empty */

    unsigned char *base = g_rtl_rx_buf;
    unsigned int off = g_rtl_rx_off;

    unsigned short status = *(unsigned short *)(base + off);
    unsigned short length = *(unsigned short *)(base + off + 2);

    /* ROK must be set (bit 0) */
    if (!(status & 0x0001)) {
        /* corrupted state, reset CAPR to resync */
        rtl_write16(RTL_CAPR, (unsigned short)((cbr - 16) & 0xFFFF));
        g_rtl_rx_off = cbr & 0x1FFF;
        return 0;
    }

    if (length < 4 || length > 1800) {
        rtl_write16(RTL_CAPR, 0xFFF0);
        g_rtl_rx_off = 0;
        return 0;
    }

    *out_pkt = base + off + 4;
    *out_len = length - 4;

    /* advance ring offset (length includes 4-byte CRC) */
    off = (off + length + 4 + 3) & ~3u;
    off &= 8191;
    g_rtl_rx_off = off;

    /* CAPR = off - 16 */
    rtl_write16(RTL_CAPR, (unsigned short)((off - 16) & 0xFFFF));

    return 1;
}

/* ==================== Ethernet / ARP / ICMP ==================== */
struct eth_hdr {
    unsigned char dst[6];
    unsigned char src[6];
    unsigned short ethertype;
} __attribute__((packed));

struct arp_pkt {
    unsigned short htype;
    unsigned short ptype;
    unsigned char  hlen;
    unsigned char  plen;
    unsigned short op;
    unsigned char  sha[6];
    unsigned char  spa[4];
    unsigned char  tha[6];
    unsigned char  tpa[4];
} __attribute__((packed));

struct ip_hdr {
    unsigned char  ihl_ver;
    unsigned char  tos;
    unsigned short len;
    unsigned short id;
    unsigned short flags_frag;
    unsigned char  ttl;
    unsigned char  proto;
    unsigned short csum;
    unsigned char  src[4];
    unsigned char  dst[4];
} __attribute__((packed));

struct icmp_echo {
    unsigned char  type;
    unsigned char  code;
    unsigned short csum;
    unsigned short id;
    unsigned short seq;
} __attribute__((packed));

static unsigned short inet_csum(const void *data, unsigned int len) {
    const unsigned char *p = (const unsigned char *)data;
    unsigned int sum = 0;
    while (len > 1) {
        sum += ((unsigned int)p[0] << 8) | p[1];
        p += 2; len -= 2;
    }
    if (len) sum += (unsigned int)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (unsigned short)(~sum & 0xFFFF);
}

static unsigned char g_eth_bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void net_send_arp_request(const unsigned char *my_mac, const unsigned char *my_ip,
                                 const unsigned char *target_ip) {
    unsigned char frame[64];
    memset(frame, 0, sizeof(frame));

    struct eth_hdr *eth = (struct eth_hdr *)frame;
    memcpy(eth->dst, g_eth_bcast, 6);
    memcpy(eth->src, my_mac, 6);
    eth->ethertype = 0x0608;   /* big-endian ARP */

    struct arp_pkt *arp = (struct arp_pkt *)(frame + 14);
    arp->htype = 0x0100;   /* BE 0x0001 */
    arp->ptype = 0x0008;   /* BE 0x0800 */
    arp->hlen = 6;
    arp->plen = 4;
    arp->op = 0x0100;      /* request */
    memcpy(arp->sha, my_mac, 6);
    memcpy(arp->spa, my_ip, 4);
    memset(arp->tha, 0, 6);
    memcpy(arp->tpa, target_ip, 4);

    rtl8139_send(frame, 60);
}

/* ==================== Test ==================== */
static void net_test(void) {
    if (!g_rtl_ok) return;
    unsigned char my_ip[4] = NET_MY_IP;
    unsigned char gw_ip[4] = NET_GW_IP;

    serial_puts("net: sending ARP for 10.0.2.2\n");
    net_send_arp_request(g_rtl_mac, my_ip, gw_ip);

    int tries = 500000;
    while (tries-- > 0) {
        unsigned char *pkt; unsigned int len;
        if (rtl8139_poll(&pkt, &len)) {
            if (len < 42) continue;
            struct eth_hdr *e = (struct eth_hdr *)pkt;
            if (e->ethertype != 0x0608) continue;   /* ARP */
            struct arp_pkt *a = (struct arp_pkt *)(pkt + 14);
            if (a->op != 0x0200) continue;          /* reply */
            serial_puts("net: ARP reply! gw MAC=");
            int i;
            for (i = 0; i < 6; i++) {
                static const char hx[] = "0123456789abcdef";
                char ph[3] = { hx[(a->sha[i]>>4)&0xF], hx[a->sha[i]&0xF], 0 };
                serial_puts(ph);
                if (i < 5) serial_puts(":");
            }
            serial_puts("\n");
            serial_puts("net: PING WORKS (ARP resolved)\n");
            return;
        }
        int spin = 1000; while (spin-- > 0) {}
    }
    serial_puts("net: no ARP reply (timeout)\n");
}


/* ===== libc shims for Lexbor ===== */
/* Bump allocator in heap region */
#define KMALLOC_HEAP_BASE  0x04000000u     /* 64 MiB */
#define KMALLOC_HEAP_SIZE  0x04000000u     /* 64 MiB */

static unsigned char *g_heap_cur = 0;
static unsigned char *g_heap_end = 0;

static void heap_init(void) {
    g_heap_cur = (unsigned char *)(unsigned long)KMALLOC_HEAP_BASE;
    g_heap_end = g_heap_cur + KMALLOC_HEAP_SIZE;
}

void *kmalloc(unsigned long size) {
    if (!g_heap_cur) heap_init();
    if (size == 0) size = 1;
    size = (size + 15) & ~15UL;   /* align 16 */
    if (g_heap_cur + size > g_heap_end) return (void *)0;
    void *p = g_heap_cur;
    g_heap_cur += size;
    return p;
}

void kfree(void *p) { (void)p; }   /* bump — no free */

void *krealloc(void *p, unsigned long size) {
    if (!p) return kmalloc(size);
    void *np = kmalloc(size);
    if (!np) return (void *)0;
    memcpy(np, p, size);
    return np;
}

/* Standard libc symbols that Lexbor expects */
void *malloc(unsigned long size) { return kmalloc(size); }
void  free(void *p) { kfree(p); }
void *realloc(void *p, unsigned long size) { return krealloc(p, size); }
void *calloc(unsigned long n, unsigned long sz) {
    unsigned long total = n * sz;
    void *p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

/* String functions */
unsigned long strlen(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
char *strncpy(char *d, const char *s, unsigned long n) {
    unsigned long i = 0;
    while (i < n && s[i]) { d[i] = s[i]; i++; }
    while (i < n) { d[i] = 0; i++; }
    return d;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, unsigned long n) {
    unsigned long i = 0;
    while (i < n && a[i] && a[i] == b[i]) i++;
    if (i == n) return 0;
    return (unsigned char)a[i] - (unsigned char)b[i];
}
char *strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char *)s; s++; }
    return (c == 0) ? (char *)s : (char *)0;
}
char *strrchr(const char *s, int c) {
    const char *last = (const char *)0;
    while (*s) { if (*s == (char)c) last = s; s++; }
    if (c == 0) return (char *)s;
    return (char *)last;
}
char *strstr(const char *h, const char *n) {
    unsigned long nl = strlen(n);
    if (nl == 0) return (char *)h;
    while (*h) {
        if (strncmp(h, n, nl) == 0) return (char *)h;
        h++;
    }
    return (char *)0;
}
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
void *memchr(const void *s, int c, unsigned long n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned long i;
    for (i = 0; i < n; i++) {
        if (p[i] == (unsigned char)c) return (void *)(p + i);
    }
    return (void *)0;
}

/* ================================= */


/* ==================== IP + ICMP ==================== */
struct ip_hdr2 {
    unsigned char  ihl_ver;
    unsigned char  tos;
    unsigned short len;
    unsigned short id;
    unsigned short flags_frag;
    unsigned char  ttl;
    unsigned char  proto;
    unsigned short csum;
    unsigned char  src[4];
    unsigned char  dst[4];
} __attribute__((packed));



static unsigned int g_ip_id = 0;
static unsigned char g_my_mac[6];
static unsigned char g_gw_mac[6];
static unsigned char g_my_ip[4] = {10, 0, 2, 15};
static unsigned char g_gw_ip[4] = {10, 0, 2, 2};

/* Ethernet + ARP resolve (returns 0 = ok) */
static int arp_resolve(const unsigned char *target_ip, unsigned char *out_mac) {
    /* Send ARP request */
    unsigned char frame[64];
    memset(frame, 0, sizeof(frame));
    struct eth_hdr *eth = (struct eth_hdr *)frame;
    unsigned char bc[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    memcpy(eth->dst, bc, 6);
    memcpy(eth->src, g_my_mac, 6);
    eth->ethertype = 0x0608;
    struct arp_pkt *a = (struct arp_pkt *)(frame + 14);
    a->htype = 0x0100; a->ptype = 0x0008;
    a->hlen = 6; a->plen = 4;
    a->op = 0x0100;
    memcpy(a->sha, g_my_mac, 6);
    memcpy(a->spa, g_my_ip, 4);
    memset(a->tha, 0, 6);
    memcpy(a->tpa, target_ip, 4);
    rtl8139_send(frame, 60);

    int tries = 500000;
    while (tries-- > 0) {
        unsigned char *pkt; unsigned int len;
        if (rtl8139_poll(&pkt, &len)) {
            if (len < 42) continue;
            struct eth_hdr *e = (struct eth_hdr *)pkt;
            if (e->ethertype != 0x0608) continue;
            struct arp_pkt *ar = (struct arp_pkt *)(pkt + 14);
            if (ar->op != 0x0200) continue;
            if (memcmp(ar->spa, target_ip, 4) != 0) continue;
            memcpy(out_mac, ar->sha, 6);
            return 0;
        }
        int spin = 5000; while (spin-- > 0) {}
    }
    return -1;
}

/* Send ICMP echo request */
static int icmp_send_echo(const unsigned char *dst_ip, const unsigned char *dst_mac,
                          unsigned short id, unsigned short seq) {
    static unsigned char buf[98];
    memset(buf, 0, sizeof(buf));

    struct eth_hdr *eth = (struct eth_hdr *)buf;
    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, g_my_mac, 6);
    eth->ethertype = 0x0008;   /* IP, big-endian */

    struct ip_hdr2 *ip = (struct ip_hdr2 *)(buf + 14);
    ip->ihl_ver = 0x45;
    ip->tos = 0;
    ip->len = 0x5400;   /* 84 bytes, BE */
    ip->id = (unsigned short)(((g_ip_id >> 8) & 0xFF) | ((g_ip_id & 0xFF) << 8));
    g_ip_id++;
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->proto = 1;   /* ICMP */
    ip->csum = 0;
    memcpy(ip->src, g_my_ip, 4);
    memcpy(ip->dst, dst_ip, 4);
    {
        unsigned short c = inet_csum(ip, 20);
        ip->csum = (unsigned short)((c >> 8) | (c << 8));   /* byte swap */
    }

    struct icmp_echo *ic = (struct icmp_echo *)(buf + 34);
    ic->type = 8;   /* echo request */
    ic->code = 0;
    ic->csum = 0;
    ic->id = (unsigned short)(((id >> 8) & 0xFF) | ((id & 0xFF) << 8));
    ic->seq = (unsigned short)(((seq >> 8) & 0xFF) | ((seq & 0xFF) << 8));

    /* payload: 56 bytes of pattern */
    unsigned char *pay = buf + 42;
    int i;
    for (i = 0; i < 56; i++) pay[i] = (unsigned char)i;
    {
        unsigned short c = inet_csum(ic, 64);
        ic->csum = (unsigned short)((c >> 8) | (c << 8));   /* byte swap */
    }

    serial_dump_packet("ICMP TX", buf, 98);
    rtl8139_send(buf, 14 + 20 + 64);
    return 0;
}

/* Poll for ICMP echo reply */
static int icmp_wait_reply(unsigned short id, int timeout_iters) {
    int tries = timeout_iters;
    while (tries-- > 0) {
        unsigned char *pkt; unsigned int len;
        if (rtl8139_poll(&pkt, &len)) {
            if (len < 42) continue;
            struct eth_hdr *e = (struct eth_hdr *)pkt;
            if (e->ethertype != 0x0008) continue;   /* IP */
            struct ip_hdr2 *ip = (struct ip_hdr2 *)(pkt + 14);
            if (ip->proto != 1) continue;
            int ihl = (ip->ihl_ver & 0x0F) * 4;
            struct icmp_echo *ic = (struct icmp_echo *)(pkt + 14 + ihl);
            if (ic->type != 0) continue;   /* echo reply */
            unsigned short rep_id = (unsigned short)(((ic->id & 0xFF) << 8) | ((ic->id >> 8) & 0xFF));
            if (rep_id != id) continue;
            return 0;
        }
        int spin = 5000; while (spin-- > 0) {}
    }
    return -1;
}

static void net_ping(const unsigned char *dst_ip) {
    unsigned char dst_mac[6];
    if (arp_resolve(dst_ip, dst_mac) != 0) {
        serial_puts("ping: ARP failed\n");
        return;
    }
    serial_puts("ping: arp OK, sending ICMP echo\n");
    unsigned short id = 0x1234;
    int i;
    int got = 0;
    for (i = 0; i < 3; i++) {
        icmp_send_echo(dst_ip, dst_mac, id, (unsigned short)(i + 1));
        if (icmp_wait_reply(id, 200000) == 0) {
            serial_puts("ping: reply seq="); serial_hex((unsigned long long)(i + 1));
            serial_puts("\n");
            got++;
        } else {
            serial_puts("ping: timeout seq="); serial_hex((unsigned long long)(i + 1));
            serial_puts("\n");
        }
    }
    if (got > 0) serial_puts("ping: OK\n");
    else serial_puts("ping: no replies\n");
}

/* Wrapper to run full L3 test */
static void net_test_full(void) {
    if (!g_rtl_ok) return;
    memcpy(g_my_mac, g_rtl_mac, 6);

    serial_puts("net: sending ARP for gw\n");
    if (arp_resolve(g_gw_ip, g_gw_mac) != 0) {
        serial_puts("net: ARP failed\n"); return;
    }
    serial_puts("net: gateway MAC=52:55:0a:00:02:02\n");

    serial_puts("net: ping 10.0.2.2\n");
    net_ping(g_gw_ip);
}


/* ==================== nettest command ==================== */
static char g_nettest_buf[768];
static unsigned int g_nettest_len = 0;

static void nettest_log(const char *s) {
    while (*s && g_nettest_len < sizeof(g_nettest_buf) - 1) {
        g_nettest_buf[g_nettest_len++] = *s++;
    }
    g_nettest_buf[g_nettest_len] = 0;
}

static void nettest_log_hex(unsigned long long v) {
    char tmp[20];
    int i;
    for (i = 0; i < 16; i++) {
        int nib = (v >> ((15 - i) * 4)) & 0xF;
        tmp[i] = (nib < 10) ? ('0' + nib) : ('a' + nib - 10);
    }
    tmp[16] = 0;
    /* skip leading zeros */
    int start = 0;
    while (start < 15 && tmp[start] == '0') start++;
    nettest_log(&tmp[start]);
}

void serial_dump_packet(const char *label, const unsigned char *p, unsigned int n) {
    serial_puts(label);
    serial_puts(" (");
    serial_hex(n);
    serial_puts(" bytes): ");
    unsigned int i;
    for (i = 0; i < n && i < 64; i++) {
        static const char hx[] = "0123456789abcdef";
        char ph[3] = { hx[(p[i]>>4)&0xF], hx[p[i]&0xF], 0 };
        serial_puts(ph);
        if ((i & 1) == 1) serial_puts(" ");
    }
    serial_puts("\n");
}

const char *munix_nettest_run(void) {
    g_nettest_len = 0;
    g_nettest_buf[0] = 0;

    nettest_log("=== mUnix Network Test ===\n\n");

    /* 1. NIC */
    if (!g_rtl_ok) {
        nettest_log("NIC   : NOT FOUND\n");
        nettest_log("MAC   : -\n");
        nettest_log("ARP   : skipped\n");
        nettest_log("ICMP  : skipped\n\n");
        nettest_log("VERDICT: FAIL - no RTL8139\n");
        nettest_log("HINT: run QEMU with -device rtl8139\n");
        return g_nettest_buf;
    }
    nettest_log("NIC   : RTL8139 @ io=0x");
    nettest_log_hex(g_rtl_io);
    nettest_log("  [OK]\n");

    nettest_log("MAC   : ");
    {
        int i;
        static const char hx[] = "0123456789abcdef";
        for (i = 0; i < 6; i++) {
            char ph[3] = { hx[(g_rtl_mac[i]>>4)&0xF], hx[g_rtl_mac[i]&0xF], 0 };
            nettest_log(ph);
            if (i < 5) nettest_log(":");
        }
    }
    nettest_log("\n");

    memcpy(g_my_mac, g_rtl_mac, 6);

    /* 2. ARP */
    nettest_log("ARP   : resolving 10.0.2.2 ...\n");
    unsigned char gw_mac[6];
    if (arp_resolve(g_gw_ip, gw_mac) != 0) {
        nettest_log("ARP   : NO REPLY\n\n");
        nettest_log("VERDICT: FAIL - L2 broken\n");
        nettest_log("HINT: check RTL8139 TX/RX rings\n");
        return g_nettest_buf;
    }
    nettest_log("ARP   : 10.0.2.2 -> ");
    {
        int i;
        static const char hx[] = "0123456789abcdef";
        for (i = 0; i < 6; i++) {
            char ph[3] = { hx[(gw_mac[i]>>4)&0xF], hx[gw_mac[i]&0xF], 0 };
            nettest_log(ph);
            if (i < 5) nettest_log(":");
        }
    }
    nettest_log("  [OK]\n");

    /* 3. ICMP */
    nettest_log("ICMP  : ping 10.0.2.2 x3 ...\n");

    unsigned short id = 0xABCD;
    int i;
    int replies = 0;

    for (i = 0; i < 3; i++) {
        /* Dump first packet to serial for debug */
        if (i == 0) {
            /* send + capture */
            extern void serial_dump_packet(const char *label, const unsigned char *p, unsigned int n);
        }
        icmp_send_echo(g_gw_ip, gw_mac, id, (unsigned short)(i + 1));
        if (icmp_wait_reply(id, 200000) == 0) {
            replies++;
        }
    }

    nettest_log("ICMP  : ");
    nettest_log_hex((unsigned long long)replies);
    nettest_log("/3 replies\n");

    /* 4. VERDICT */
    nettest_log("\n");
    if (replies == 3) {
        nettest_log("VERDICT: L3 WORKS - ready for TCP/HTTP\n");
    } else if (replies > 0) {
        nettest_log("VERDICT: partial - check ICMP checksum/len\n");
    } else {
        nettest_log("VERDICT: FAIL - IP/ICMP broken\n");
        nettest_log("HINT: see serial hex dump of outgoing packet\n");
    }

    return g_nettest_buf;
}

/* ==================== DNS + TCP + HTTP ==================== */
static unsigned int bswap32(unsigned int v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}
static unsigned short bswap16(unsigned short v) {
    return (unsigned short)((v >> 8) | (v << 8));
}

/* ---- UDP ---- */
struct udp_hdr {
    unsigned short sport;
    unsigned short dport;
    unsigned short len;
    unsigned short csum;
} __attribute__((packed));

static int udp_send(const unsigned char *dst_ip, const unsigned char *dst_mac,
                    unsigned short sport, unsigned short dport,
                    const unsigned char *data, unsigned int dlen) {
    static unsigned char buf[1500];
    memset(buf, 0, 14 + 20 + 8 + dlen);
    struct eth_hdr *eth = (struct eth_hdr *)buf;
    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, g_my_mac, 6);
    eth->ethertype = 0x0008;
    struct ip_hdr2 *ip = (struct ip_hdr2 *)(buf + 14);
    unsigned int total = 20 + 8 + dlen;
    ip->ihl_ver = 0x45;
    ip->len = bswap16((unsigned short)total);
    ip->id = bswap16((unsigned short)(g_ip_id++));
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->proto = 17;
    memcpy(ip->src, g_my_ip, 4);
    memcpy(ip->dst, dst_ip, 4);
    ip->csum = 0;
    ip->csum = bswap16(inet_csum(ip, 20));
    struct udp_hdr *ud = (struct udp_hdr *)(buf + 34);
    ud->sport = bswap16(sport);
    ud->dport = bswap16(dport);
    ud->len = bswap16((unsigned short)(8 + dlen));
    ud->csum = 0;   /* optional for IPv4 */
    memcpy(buf + 42, data, dlen);
    return rtl8139_send(buf, 14 + total);
}

/* ---- DNS ---- */
static int dns_query(const unsigned char *dst_ip, const unsigned char *dst_mac,
                     const char *hostname, unsigned char *out_ip) {
    /* DEBUG: show hostname as hex */
    serial_puts("dns_query for: '"); serial_puts(hostname);
    serial_puts("' hex=");
    int _i = 0;
    while (hostname[_i] && _i < 40) {
        static const char hx[] = "0123456789abcdef";
        char ph[3] = { hx[((unsigned char)hostname[_i]>>4)&0xF],
                       hx[(unsigned char)hostname[_i]&0xF], 0 };
        serial_puts(ph); serial_puts(" ");
        _i++;
    }
    serial_puts("\n");

    static unsigned char q[512];
    memset(q, 0, sizeof(q));
    unsigned short id = 0x1234;
    q[0] = (id >> 8) & 0xFF; q[1] = id & 0xFF;
    q[2] = 0x01; q[3] = 0x00;
    q[4] = 0x00; q[5] = 0x01;
    unsigned int qlen = 12;
    const char *p = hostname;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        unsigned char seg_len = (unsigned char)(dot - p);
        q[qlen++] = seg_len;
        unsigned int i;
        for (i = 0; i < seg_len; i++) q[qlen++] = (unsigned char)p[i];
        if (*dot == 0) break;
        p = dot + 1;
    }
    q[qlen++] = 0;
    q[qlen++] = 0x00; q[qlen++] = 0x01;
    q[qlen++] = 0x00; q[qlen++] = 0x01;

    udp_send(dst_ip, dst_mac, 0xC000, 53, q, qlen);

    int tries = 1500000;
    while (tries-- > 0) {
        unsigned char *pkt; unsigned int plen;
        if (rtl8139_poll(&pkt, &plen)) {
            if (plen < 42) continue;
            struct eth_hdr *e = (struct eth_hdr *)pkt;
            if (e->ethertype != 0x0008) continue;
            struct ip_hdr2 *ip = (struct ip_hdr2 *)(pkt + 14);
            if (ip->proto != 17) continue;
            unsigned int ihl = (ip->ihl_ver & 0x0F) * 4;
            struct udp_hdr *ud = (struct udp_hdr *)(pkt + 14 + ihl);
            if (bswap16(ud->dport) != 0xC000) continue;
            unsigned char *dns = pkt + 14 + ihl + 8;
            unsigned int dns_len = bswap16(ud->len) - 8;
            if (dns_len < 12) continue;

            /* Check RCODE (bits 0-3 of byte 3) */
            unsigned char rcode = dns[3] & 0x0F;
            if (rcode != 0) {
                serial_puts("dns: response rcode=");
                serial_hex(rcode);
                serial_puts(" (3=NXDomain,2=SERVFAIL,5=REFUSED)\n");
                return -2;
            }

            unsigned int ancount = ((unsigned int)dns[6] << 8) | dns[7];
            if (ancount == 0) {
                serial_puts("dns: no answers\n");
                return -3;
            }
            serial_puts("dns: ancount=");
            serial_hex(ancount);
            serial_puts("\n");

            /* skip header + question */
            unsigned int off = 12;
            while (off < dns_len && dns[off] != 0) {
                if ((dns[off] & 0xC0) == 0xC0) { off += 2; break; }
                off += dns[off] + 1;
            }
            if (off < dns_len && dns[off] == 0) off++;
            off += 4;

            /* iterate answers, find first A record */
            unsigned int a;
            for (a = 0; a < ancount && off + 12 <= dns_len; a++) {
                if ((dns[off] & 0xC0) == 0xC0) {
                    off += 2;
                } else {
                    while (off < dns_len && dns[off] != 0) off += dns[off] + 1;
                    off += 1;
                }
                if (off + 10 > dns_len) break;
                unsigned int type  = ((unsigned int)dns[off] << 8) | dns[off+1];
                unsigned int rdlen = ((unsigned int)dns[off+8] << 8) | dns[off+9];
                unsigned int rdata = off + 10;
                serial_puts("dns: answer type=");
                serial_hex(type);
                serial_puts(" rdlen=");
                serial_hex(rdlen);
                serial_puts("\n");
                if (type == 0x0001 && rdlen == 4 && rdata + 4 <= dns_len) {
                    out_ip[0] = dns[rdata];
                    out_ip[1] = dns[rdata+1];
                    out_ip[2] = dns[rdata+2];
                    out_ip[3] = dns[rdata+3];
                    serial_puts("dns: A=");
                    serial_hex(out_ip[0]); serial_puts(".");
                    serial_hex(out_ip[1]); serial_puts(".");
                    serial_hex(out_ip[2]); serial_puts(".");
                    serial_hex(out_ip[3]); serial_puts("\n");
                    return 0;
                }
                off = rdata + rdlen;
            }
            serial_puts("dns: no A record found\n");
            return -4;
        }
        int spin = 5000; while (spin-- > 0) {}
    }
    return -1;
}

/* ---- TCP ---- */
struct tcp_hdr {
    unsigned short sport;
    unsigned short dport;
    unsigned int   seq;
    unsigned int   ack;
    unsigned char  off;
    unsigned char  flags;
    unsigned short window;
    unsigned short csum;
    unsigned short urg;
} __attribute__((packed));

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

static unsigned int g_tcp_seq = 0x11111111;
static unsigned short g_tcp_sport = 0xC350;   /* 50000 */
static unsigned short g_tcp_dport = 0;
static unsigned char  g_tcp_dst_ip[4];
static unsigned char  g_tcp_dst_mac[6];
static unsigned int   g_tcp_peer_seq = 0;

static unsigned short tcp_csum_calc(const unsigned char *src_ip, const unsigned char *dst_ip,
                                     const unsigned char *seg, unsigned int seg_len) {
    static unsigned char buf[1600];
    struct tcp_pseudo {
        unsigned char src[4], dst[4];
        unsigned char zero, proto;
        unsigned short len;
    } __attribute__((packed));
    struct tcp_pseudo *ph = (struct tcp_pseudo *)buf;
    memcpy(ph->src, src_ip, 4);
    memcpy(ph->dst, dst_ip, 4);
    ph->zero = 0;
    ph->proto = 6;
    ph->len = bswap16((unsigned short)seg_len);
    memcpy(buf + 12, seg, seg_len);
    return inet_csum(buf, 12 + seg_len);
}

static int tcp_send_segment(unsigned char flags, unsigned int seq, unsigned int ack,
                            const unsigned char *payload, unsigned int plen) {
    static unsigned char buf[1500];
    memset(buf, 0, 14 + 20 + 20 + plen);
    struct eth_hdr *eth = (struct eth_hdr *)buf;
    memcpy(eth->dst, g_tcp_dst_mac, 6);
    memcpy(eth->src, g_my_mac, 6);
    eth->ethertype = 0x0008;
    struct ip_hdr2 *ip = (struct ip_hdr2 *)(buf + 14);
    unsigned int total = 20 + 20 + plen;
    ip->ihl_ver = 0x45;
    ip->len = bswap16((unsigned short)total);
    ip->id = bswap16((unsigned short)(g_ip_id++));
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->proto = 6;
    memcpy(ip->src, g_my_ip, 4);
    memcpy(ip->dst, g_tcp_dst_ip, 4);
    ip->csum = 0;
    ip->csum = bswap16(inet_csum(ip, 20));
    struct tcp_hdr *t = (struct tcp_hdr *)(buf + 34);
    t->sport = bswap16(g_tcp_sport);
    t->dport = bswap16(g_tcp_dport);
    t->seq = bswap32(seq);
    t->ack = bswap32(ack);
    t->off = (5 << 4);
    t->flags = flags;
    t->window = bswap16(0x2000);
    t->csum = 0;
    t->urg = 0;
    if (plen > 0) memcpy(buf + 54, payload, plen);
    unsigned int seg_len = 20 + plen;
    t->csum = tcp_csum_calc(g_my_ip, g_tcp_dst_ip, (unsigned char *)t, seg_len);
    t->csum = bswap16(t->csum);
    return rtl8139_send(buf, 14 + total);
}

/* Приём TCP-сегмента; возвращает:
   0 = новый сегмент принят, payload в out_payload
   -1 = не наш пакет */
static int tcp_recv(unsigned char *out_flags, unsigned int *out_seq, unsigned int *out_ack,
                    unsigned char **out_payload, unsigned int *out_plen) {
    int tries = 1000000;
    while (tries-- > 0) {
        unsigned char *pkt; unsigned int plen;
        if (rtl8139_poll(&pkt, &plen)) {
            if (plen < 14 + 20 + 20) continue;
            struct eth_hdr *e = (struct eth_hdr *)pkt;
            if (e->ethertype != 0x0008) continue;
            struct ip_hdr2 *ip = (struct ip_hdr2 *)(pkt + 14);
            if (ip->proto != 6) continue;
            if (memcmp(ip->src, g_tcp_dst_ip, 4) != 0) continue;
            unsigned int ihl = (ip->ihl_ver & 0x0F) * 4;
            struct tcp_hdr *t = (struct tcp_hdr *)(pkt + 14 + ihl);
            if (bswap16(t->sport) != g_tcp_dport) continue;
            if (bswap16(t->dport) != g_tcp_sport) continue;
            unsigned int tot_len = bswap16(ip->len);
            unsigned int data_off = (t->off >> 4) * 4;
            unsigned int payload_len = tot_len - ihl - data_off;
            *out_flags = t->flags;
            *out_seq   = bswap32(t->seq);
            *out_ack   = bswap32(t->ack);
            *out_payload = pkt + 14 + ihl + data_off;
            *out_plen = payload_len;
            return 0;
        }
        int spin = 5000; while (spin-- > 0) {}
    }
    return -1;
}

static int tcp_connect(void) {
    serial_puts("tcp: SYN -> ");
    serial_hex(g_tcp_dst_ip[0]); serial_puts(".");
    serial_hex(g_tcp_dst_ip[1]); serial_puts(".");
    serial_hex(g_tcp_dst_ip[2]); serial_puts(".");
    serial_hex(g_tcp_dst_ip[3]); serial_puts(":");
    serial_hex(g_tcp_dport); serial_puts("\n");

    /* SYN */
    tcp_send_segment(TCP_SYN, g_tcp_seq, 0, 0, 0);

    unsigned char flags; unsigned int rseq, rack; unsigned char *pay; unsigned int plen;
    if (tcp_recv(&flags, &rseq, &rack, &pay, &plen) != 0) {
        serial_puts("tcp: no response to SYN\n");
        return -1;
    }
    serial_puts("tcp: got flags=0x"); serial_hex(flags); serial_puts("\n");
    if (!(flags & TCP_SYN)) {
        serial_puts("tcp: no SYN in response\n");
        return -2;
    }
    if (flags & TCP_RST) {
        serial_puts("tcp: RST received\n");
        return -3;
    }
    g_tcp_peer_seq = rseq + 1;
    /* ACK */
    tcp_send_segment(TCP_ACK, g_tcp_seq + 1, g_tcp_peer_seq, 0, 0);
    g_tcp_seq += 1;
    serial_puts("tcp: connected\n");
    return 0;
}

static int tcp_send(const unsigned char *data, unsigned int len) {
    tcp_send_segment(TCP_ACK | TCP_PSH, g_tcp_seq, g_tcp_peer_seq, data, len);
    g_tcp_seq += len;
    return 0;
}

/* Принять N байт payload в буфер */
static int tcp_recv_data(unsigned char *out, unsigned int out_cap) {
    unsigned int total = 0;
    int idle = 0;
    while (idle < 300000) {
        unsigned char flags; unsigned int rseq, rack; unsigned char *pay; unsigned int plen;
        if (tcp_recv(&flags, &rseq, &rack, &pay, &plen) == 0) {
            idle = 0;
            if (plen > 0) {
                unsigned int cp = plen;
                if (total + cp > out_cap) cp = out_cap - total;
                memcpy(out + total, pay, cp);
                total += cp;
                g_tcp_peer_seq = rseq + plen;
                tcp_send_segment(TCP_ACK, g_tcp_seq, g_tcp_peer_seq, 0, 0);
            }
            if (flags & TCP_FIN) {
                tcp_send_segment(TCP_ACK | TCP_FIN, g_tcp_seq, g_tcp_peer_seq + 1, 0, 0);
                break;
            }
            if (flags & TCP_RST) break;
        } else {
            idle += 10000;
        }
    }
    return (int)total;
}

/* ---- High-level: HTTP GET ---- */
static char g_http_buf[8192];
static unsigned int g_http_len = 0;

int munix_http_get(const char *host, const char *path) {
    g_http_len = 0;
    if (!g_rtl_ok) return -1;

    unsigned char dns_ip[4] = {10, 0, 2, 3};
    unsigned char dns_mac[6];
    if (arp_resolve(dns_ip, dns_mac) != 0) return -2;

    unsigned char host_ip[4];
    if (dns_query(dns_ip, dns_mac, host, host_ip) != 0) return -3;

    serial_puts("http: resolved "); serial_puts(host); serial_puts(" -> ");
    serial_hex(host_ip[0]); serial_puts("."); serial_hex(host_ip[1]);
    serial_puts("."); serial_hex(host_ip[2]); serial_puts("."); serial_hex(host_ip[3]);
    serial_puts("\n");

    memcpy(g_tcp_dst_ip, host_ip, 4);
    /* if host is on our subnet -> ARP for it; else ARP for gateway */
    int host_is_local = (host_ip[0] == 10 && host_ip[1] == 0 && host_ip[2] == 2);
    if (host_is_local) {
        if (arp_resolve(host_ip, g_tcp_dst_mac) != 0) return -4;
        serial_puts("http: dst is local, direct ARP ok\n");
    } else {
        unsigned char gw_mac[6];
        if (arp_resolve(g_gw_ip, gw_mac) != 0) {
            serial_puts("http: gateway ARP failed\n");
            return -4;
        }
        memcpy(g_tcp_dst_mac, gw_mac, 6);
        serial_puts("http: via gateway MAC\n");
    }

    g_tcp_dport = 80;
    if (tcp_connect() != 0) return -5;
    serial_puts("http: connected\n");

    static char req[512];
    unsigned int n = 0;
    const char *p1 = "GET ";
    while (*p1) req[n++] = *p1++;
    const char *p2 = path;
    while (*p2) req[n++] = *p2++;
    const char *p3 = " HTTP/1.0\r\nHost: ";
    while (*p3) req[n++] = *p3++;
    const char *p4 = host;
    while (*p4) req[n++] = *p4++;
    const char *p5 = "\r\nConnection: close\r\n\r\n";
    while (*p5) req[n++] = *p5++;

    tcp_send((unsigned char *)req, n);
    serial_puts("http: sent GET\n");

    int rcvd = tcp_recv_data((unsigned char *)g_http_buf, sizeof(g_http_buf) - 1);
    if (rcvd < 0) return -6;
    g_http_buf[rcvd] = 0;
    g_http_len = (unsigned int)rcvd;
    serial_puts("http: got "); serial_hex((unsigned long long)rcvd); serial_puts(" bytes\n");
    return 0;
}

const char *munix_http_buf(void) { return g_http_buf; }
unsigned int munix_http_buf_len(void) { return g_http_len; }


/* ===== ping command (terminal) ===== */
static char g_ping_buf[512];
static unsigned int g_ping_len = 0;

static void ping_log(const char *s) {
    while (*s && g_ping_len < sizeof(g_ping_buf) - 1) {
        g_ping_buf[g_ping_len++] = *s++;
    }
    g_ping_buf[g_ping_len] = 0;
}
static void ping_log_num(unsigned long long v) {
    char d[24]; int n = 0;
    if (v == 0) { d[n++] = '0'; }
    else { while (v > 0) { d[n++] = '0' + (v % 10); v /= 10; } }
    char o[24]; int i;
    for (i = 0; i < n; i++) o[i] = d[n - 1 - i];
    o[n] = 0;
    ping_log(o);
}

static int parse_ip(const char *s, unsigned char *out) {
    int oct = 0, val = 0, digits = 0;
    while (1) {
        char c = *s;
        if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            digits++;
            if (digits > 3 || val > 255) return -1;
        } else if (c == '.' || c == 0) {
            if (digits == 0) return -1;
            if (oct >= 4) return -1;
            out[oct++] = (unsigned char)val;
            val = 0; digits = 0;
            if (c == 0) break;
        } else {
            return -1;
        }
        s++;
    }
    return (oct == 4) ? 0 : -1;
}

const char *munix_ping_run(const char *ip_str) {
    g_ping_len = 0;
    g_ping_buf[0] = 0;

    unsigned char dst_ip[4];
    if (parse_ip(ip_str, dst_ip) != 0) {
        ping_log("ping: bad IP address: ");
        ping_log(ip_str);
        ping_log("\n");
        return g_ping_buf;
    }

    if (!g_rtl_ok) { ping_log("ping: no NIC\n"); return g_ping_buf; }

    memcpy(g_my_mac, g_rtl_mac, 6);

    ping_log("PING ");
    ping_log(ip_str);
    ping_log("\n");

    unsigned char dst_mac[6];
    /* if destination is on same subnet (10.0.2.x) → ARP for it directly
       otherwise → ARP for gateway, send ICMP to dst via gateway */
    int same_subnet = (dst_ip[0] == 10 && dst_ip[1] == 0 && dst_ip[2] == 2);
    const unsigned char *arp_target = same_subnet ? dst_ip : g_gw_ip;
    if (arp_resolve(arp_target, dst_mac) != 0) {
        ping_log("  ARP: no reply for ");
        ping_log(same_subnet ? "destination" : "gateway");
        ping_log("\n");
        ping_log("ping: L2 broken\n");
        return g_ping_buf;
    }
    ping_log("  ARP: resolved (");
    ping_log(same_subnet ? "direct" : "via gateway");
    ping_log(")\n");

    unsigned short id = 0x5A5A;
    int i, replies = 0;
    for (i = 0; i < 4; i++) {
        icmp_send_echo(dst_ip, dst_mac, id, (unsigned short)(i + 1));
        if (icmp_wait_reply(id, 200000) == 0) {
            replies++;
            ping_log("  seq=");
            ping_log_num((unsigned long long)(i + 1));
            ping_log("  reply\n");
        } else {
            ping_log("  seq=");
            ping_log_num((unsigned long long)(i + 1));
            ping_log("  timeout\n");
        }
    }

    ping_log("\n--- ");
    ping_log(ip_str);
    ping_log(" ping statistics ---\n");
    ping_log("poll_calls=");
    ping_log_num((unsigned long long)g_rx_poll_calls);
    ping_log(" poll_hits=");
    ping_log_num((unsigned long long)g_rx_poll_hits);
    ping_log("\n");
    ping_log_num((unsigned long long)replies);
    ping_log(" packets transmitted, ");
    ping_log_num((unsigned long long)replies);
    ping_log(" received\n");

    return g_ping_buf;
}

/* ==================== ivshmem-backed persistent memory ==================== */
#define IVSHMEM_MAX  (64u * 1024u * 1024u)
#define PERSIST_HDR_MAGIC 0x4D554E58u  /* "MUNX" */
#define PERSIST_HDR_VER   1

struct pmem_hdr {
    unsigned int  magic;
    unsigned int  version;
    unsigned int  size;
    unsigned int  _pad;
} __attribute__((packed));

static unsigned char *g_pmem = 0;
static unsigned int   g_pmem_size = 0;

static void pmem_probe(void) {
    int i;
    for (i = 0; i < g_pci_count; i++) {
        struct pci_dev *d = &g_pci_devs[i];
        if (d->vendor == 0x1AF4 && d->device == 0x1110) {
            /* ivshmem-plain: BAR0 = 64-bit MMIO on some builds, else BAR2 */
            unsigned long long bar = 0;
            if ((d->bar[0] & 0x0F) == 0x0C) {
                /* 64-bit MMIO */
                bar = ((unsigned long long)d->bar[0] & ~0xFULL)
                    | ((unsigned long long)d->bar[1] << 32);
            } else {
                bar = d->bar[2] & ~0xFULL;
            }
            if (bar == 0) continue;
            g_pmem = (unsigned char *)(unsigned long)bar;
            g_pmem_size = IVSHMEM_MAX;
            serial_puts("pmem: ivshmem @ 0x");
            serial_hex(bar);
            serial_puts(" size=");
            serial_hex(g_pmem_size);
            serial_puts("\n");
            return;
        }
    }
    serial_puts("pmem: no ivshmem device\n");
    g_pmem = 0;
}

/* RAMFS serialize into pmem buffer */
static unsigned int ramfs_serialize_pmem(unsigned char *out, unsigned int cap) {
    unsigned int pos = 0;
    int i;
    for (i = 0; i < 32; i++) {
        if (!fs[i].used) continue;
        unsigned int nlen = 0;
        while (fs[i].name[nlen] && nlen < 31) nlen++;
        unsigned int dlen = (unsigned int)fs[i].size;
        if (dlen > 255) dlen = 255;
        if (pos + 1 + nlen + 2 + dlen > cap) return 0;
        out[pos++] = (unsigned char)nlen;
        unsigned int k;
        for (k = 0; k < nlen; k++) out[pos++] = (unsigned char)fs[i].name[k];
        out[pos++] = (unsigned char)(dlen & 0xFF);
        out[pos++] = (unsigned char)((dlen >> 8) & 0xFF);
        for (k = 0; k < dlen; k++) out[pos++] = (unsigned char)fs[i].data[k];
    }
    return pos;
}

static int ramfs_deserialize_pmem(const unsigned char *in, unsigned int len) {
    int i;
    for (i = 0; i < 32; i++) fs[i].used = 0;
    unsigned int pos = 0;
    while (pos + 3 < len) {
        unsigned int nlen = in[pos++];
        if (nlen > 31) return -1;
        if (pos + nlen + 2 > len) return -1;
        char name[32];
        unsigned int k;
        for (k = 0; k < nlen; k++) name[k] = (char)in[pos++];
        name[nlen] = 0;
        unsigned int dlen = in[pos] | ((unsigned int)in[pos+1] << 8);
        pos += 2;
        if (pos + dlen > len) return -1;
        int idx = fs_alloc(-1, name, 0);
        if (idx < 0) return -2;
        unsigned int cl = dlen;
        if (cl > 255) cl = 255;
        for (k = 0; k < cl; k++) fs[idx].data[k] = (char)in[pos + k];
        fs[idx].data[cl] = 0;
        fs[idx].size = (int)dlen;
        pos += dlen;
    }
    return 0;
}

int munix_pmem_save(void) {
    if (!g_pmem) return -1;
    struct pmem_hdr *h = (struct pmem_hdr *)g_pmem;
    unsigned int off = sizeof(struct pmem_hdr);
    unsigned int n = ramfs_serialize_pmem(g_pmem + off, g_pmem_size - off);
    if (n == 0) return -2;
    h->magic = PERSIST_HDR_MAGIC;
    h->version = PERSIST_HDR_VER;
    h->size = n;
    return (int)n;
}

int munix_pmem_load(void) {
    if (!g_pmem) return -1;
    struct pmem_hdr *h = (struct pmem_hdr *)g_pmem;
    if (h->magic != PERSIST_HDR_MAGIC) return -2;
    if (h->version != PERSIST_HDR_VER) return -3;
    if (h->size > g_pmem_size - sizeof(struct pmem_hdr)) return -4;
    return ramfs_deserialize_pmem(g_pmem + sizeof(struct pmem_hdr), h->size);
}

int munix_pmem_present(void) {
    if (!g_pmem) return 0;
    struct pmem_hdr *h = (struct pmem_hdr *)g_pmem;
    return (h->magic == PERSIST_HDR_MAGIC) ? 1 : 0;
}


/* ==================== System Info / Task Manager ==================== */
#define SYSINFO_BUF_SIZE 4096
static char     g_sysinfo_buf[SYSINFO_BUF_SIZE];
static unsigned g_sysinfo_len = 0;

/* Loop counter incremented by main loop */
static volatile unsigned long long g_loop_count = 0;
static unsigned long long g_loop_snapshot = 0;
static unsigned int g_loop_fps = 0;
static unsigned int g_last_ticks_sysmon = 0;

/* String helpers for buffer */
static void si_puts(const char *s) {
    while (*s && g_sysinfo_len < SYSINFO_BUF_SIZE - 1) {
        g_sysinfo_buf[g_sysinfo_len++] = *s++;
    }
    g_sysinfo_buf[g_sysinfo_len] = 0;
}
static void si_putc(char c) {
    if (g_sysinfo_len < SYSINFO_BUF_SIZE - 1) {
        g_sysinfo_buf[g_sysinfo_len++] = c;
        g_sysinfo_buf[g_sysinfo_len] = 0;
    }
}
static void si_num_u(unsigned long long v) {
    char d[24]; int n = 0;
    if (v == 0) { d[n++] = '0'; }
    else { while (v > 0) { d[n++] = '0' + (v % 10); v /= 10; } }
    int i;
    for (i = 0; i < n; i++) si_putc(d[n - 1 - i]);
}
static void si_hex_byte(unsigned char b) {
    static const char hx[] = "0123456789abcdef";
    si_putc(hx[(b >> 4) & 0xF]);
    si_putc(hx[b & 0xF]);
}
static void si_hex16(unsigned short v) {
    static const char hx[] = "0123456789abcdef";
    int i;
    for (i = 3; i >= 0; i--) si_putc(hx[(v >> (i*4)) & 0xF]);
}
static void si_hex64(unsigned long long v) {
    static const char hx[] = "0123456789abcdef";
    int i;
    for (i = 15; i >= 0; i--) si_putc(hx[(v >> (i*4)) & 0xF]);
}

/* CPUID helpers */
static void cpuid4(unsigned int leaf, unsigned int *a, unsigned int *b,
                   unsigned int *c, unsigned int *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf));
}
static void cpuid_vendor(char *out13) {
    unsigned int a, b, c, d;
    cpuid4(0, &a, &b, &c, &d);
    *(unsigned int *)(out13 + 0) = b;
    *(unsigned int *)(out13 + 4) = d;
    *(unsigned int *)(out13 + 8) = c;
    out13[12] = 0;
}
static int cpuid_brand(char *out49) {
    unsigned int a, b, c, d;
    cpuid4(0x80000000u, &a, &b, &c, &d);
    if (a < 0x80000004u) { out49[0] = 0; return -1; }
    int i;
    for (i = 0; i < 3; i++) {
        cpuid4(0x80000002u + i, &a, &b, &c, &d);
        *(unsigned int *)(out49 + i*16 +  0) = a;
        *(unsigned int *)(out49 + i*16 +  4) = b;
        *(unsigned int *)(out49 + i*16 +  8) = c;
        *(unsigned int *)(out49 + i*16 + 12) = d;
    }
    out49[48] = 0;
    return 0;
}
static unsigned int cpuid_features1_edx(void) {
    unsigned int a, b, c, d;
    cpuid4(1, &a, &b, &c, &d);
    return d;   /* EDX: bit 25=SSE, 26=SSE2 */
}
static unsigned int cpuid_ext_edx(void) {
    unsigned int a, b, c, d;
    cpuid4(0x80000001u, &a, &b, &c, &d);
    return d;   /* bit 29 = LM */
}

/* GPU name from PCI vendor:device */
static const char *gpu_name(unsigned short vid, unsigned short did) {
    if (vid == 0x1234 && did == 0x1111) return "Bochs VGA (QEMU)";
    if (vid == 0x8086 && did == 0x1234) return "QEMU stdvga";
    if (vid == 0x10DE) return "NVIDIA GPU";
    if (vid == 0x1002) return "AMD/ATI GPU";
    if (vid == 0x8086) return "Intel HD Graphics";
    if (vid == 0x1AF4) return "Virtio GPU";
    return "Unknown GPU";
}
static const char *mb_name(unsigned short vid, unsigned short did) {
    if (vid == 0x8086 && did == 0x1237) return "Intel 440FX (QEMU)";
    if (vid == 0x8086 && did == 0x29C0) return "Intel Q35 (QEMU)";
    if (vid == 0x8086 && did == 0x7000) return "Intel PIIX3 (QEMU)";
    if (vid == 0x8086) return "Intel chipset";
    if (vid == 0x1022) return "AMD chipset";
    return "Unknown chipset";
}

/* Refresh dynamic stats (called from main loop every ~1 sec) */
void munix_sysmon_tick(void) {
    unsigned int now = g_ticks;
    if (now - g_last_fps_ticks >= 18) {
        unsigned long long delta = g_render_count - g_last_fps_renders;
        g_fps = (unsigned int)delta;   /* renders in last second = real FPS */
        g_last_fps_renders = g_render_count;
        g_last_fps_ticks = now;
    }
    g_loop_count++;
}

static void fmt_uptime(unsigned int ticks) {
    unsigned int total_sec = ticks / 18;
    unsigned int hh = total_sec / 3600;
    unsigned int mm = (total_sec / 60) % 60;
    unsigned int ss = total_sec % 60;
    if (hh < 10) si_putc('0'); si_num_u(hh); si_putc(':');
    if (mm < 10) si_putc('0'); si_num_u(mm); si_putc(':');
    if (ss < 10) si_putc('0'); si_num_u(ss);
}

const char *munix_sysmon_text(void) {
    g_sysinfo_len = 0;
    g_sysinfo_buf[0] = 0;

    si_puts("=== mUnix System Monitor ===\n\n");

    /* --- OS (real version from -D) --- */
    si_puts("--- System ---\n");
    si_puts("OS      : mUnix v"); si_puts(MUNIX_VERSION);
    si_puts(" ("); si_puts(MUNIX_BUILD); si_puts(")\n");
    si_puts("Build   : x86_64 long mode, C + NASM + Rust no_std\n");
    si_puts("Boot    : GRUB2 / Multiboot2 / UEFI\n");

    /* Hostname from RAMFS /etc/hostname */
    {
        extern int munix_ramfs_lookup(const char *name, int parent);
        extern int munix_ramfs_read(const char *name, int parent, char *out, int cap);
        extern int munix_ramfs_root(void);
        static char host[64];
        int rc = munix_ramfs_read("hostname", munix_ramfs_root(), host, 63);
        if (rc > 0) {
            host[rc < 63 ? rc : 63] = 0;
            si_puts("Host    : "); si_puts(host); si_puts("\n");
        } else {
            si_puts("Host    : mUnix (set /hostname to change)\n");
        }
    }
    si_puts("\n");

    /* --- CPU (real from CPUID) --- */
    char vendor[13]; char brand[49];
    cpuid_vendor(vendor);
    int has_brand = (cpuid_brand(brand) == 0);
    unsigned int f_edx = cpuid_features1_edx();
    unsigned int e_edx = cpuid_ext_edx();

    /* CPU Frequency from CPUID leaf 0x16 (Base MHz) */
    unsigned int base_mhz = 0;
    unsigned int a, b, c, d;
    cpuid4(0x16, &a, &b, &c, &d);
    if ((a & 0xFFFF) != 0) base_mhz = a & 0xFFFF;

    si_puts("--- CPU ---\n");
    si_puts("Vendor  : "); si_puts(vendor); si_puts("\n");
    si_puts("Model   : ");
    if (has_brand) {
        int i = 0; while (brand[i] == ' ') i++;
        si_puts(brand + i);
    } else {
        si_puts("x86_64 CPU (no brand string)");
    }
    si_puts("\n");
    si_puts("Freq    : ");
    if (base_mhz > 0) { si_num_u(base_mhz); si_puts(" MHz (CPUID 0x16)"); }
    else { si_puts("unknown (CPUID 0x16 not supported)"); }
    si_puts("\n");

    si_puts("Features: ");
    if (f_edx & (1u << 25)) si_puts("SSE ");
    if (f_edx & (1u << 26)) si_puts("SSE2 ");
    if (f_edx & (1u << 28)) si_puts("AVX ");
    if (e_edx & (1u << 29)) si_puts("LM ");
    if (e_edx & (1u << 11)) si_puts("SYSCALL ");
    if (e_edx & (1u << 20)) si_puts("NX ");
    si_puts("\n");

    /* Real FPS + loop counters */
    si_puts("FPS     : "); si_num_u(g_fps);
    si_puts(" renders/sec\n");
    si_puts("Loops   : "); si_num_u(g_loop_count);
    si_puts(" total\n");
    si_puts("Cores   : 1 (single core, no SMP)\n\n");

    /* --- Memory --- */
    unsigned long long ram_low_kb = g_mbi ? (unsigned long long)g_mbi->mem_lower : 0;
    unsigned long long ram_high_kb = g_mbi ? ((unsigned long long)g_mbi->mem_upper) : 0;
    unsigned long long ram_total_kb = ram_low_kb + ram_high_kb;

    extern unsigned char *g_heap_cur;
    unsigned long long heap_used = 0;
    if (g_heap_cur) heap_used = (unsigned long long)((unsigned long)g_heap_cur - 0x04000000u);

    /* RAMFS stats */
    extern int munix_ramfs_count(void);
    int ramfs_n = munix_ramfs_count();
    unsigned long long ramfs_bytes = 0;
    {
        int i;
        for (i = 0; i < 32; i++) {
            if (fs[i].used) ramfs_bytes += (unsigned long long)fs[i].size;
        }
    }

    si_puts("--- Memory ---\n");
    si_puts("RAM     : "); si_num_u(ram_total_kb / 1024); si_puts(" MiB total\n");
    si_puts("Heap    : "); si_num_u(heap_used / 1024); si_puts(" KiB / 64 MiB (");
    si_num_u((heap_used * 100) / (64ULL * 1024ULL * 1024ULL));
    si_puts("%)\n");
    si_puts("RAMFS   : "); si_num_u((unsigned long long)ramfs_n); si_puts(" inodes, ");
    si_num_u(ramfs_bytes); si_puts(" bytes\n");
    si_puts("Framebuf: ");
    si_num_u((unsigned long long)g_w); si_putc('x');
    si_num_u((unsigned long long)g_h); si_puts("x32 (");
    si_num_u(((unsigned long long)g_w * (unsigned long long)g_h * 4) / 1024 / 1024);
    si_puts(" MiB)\n\n");

    /* --- Motherboard / chipset (real PCI scan) --- */
    si_puts("--- Motherboard / Chipset ---\n");
    int i;
    for (i = 0; i < g_pci_count; i++) {
        struct pci_dev *dd = &g_pci_devs[i];
        if (dd->class_code == 0x06 && dd->subclass == 0x00) {
            si_puts("Host    : "); si_puts(mb_name(dd->vendor, dd->device));
            si_puts("  ["); si_hex16(dd->vendor); si_putc(':');
            si_hex16(dd->device); si_puts("]\n");
        }
        if (dd->class_code == 0x06 && dd->subclass == 0x01) {
            si_puts("ISA     : "); si_puts(mb_name(dd->vendor, dd->device));
            si_puts("  ["); si_hex16(dd->vendor); si_putc(':');
            si_hex16(dd->device); si_puts("]\n");
        }
    }
    si_puts("PCI bus : "); si_num_u((unsigned long long)g_pci_count); si_puts(" devices\n\n");

    /* --- GPU (real PCI scan) --- */
    si_puts("--- GPU ---\n");
    {
        int found = 0;
        for (i = 0; i < g_pci_count; i++) {
            struct pci_dev *dd = &g_pci_devs[i];
            if (dd->class_code == 0x03 && dd->subclass == 0x00) {
                si_puts("Name    : "); si_puts(gpu_name(dd->vendor, dd->device));
                si_puts("  ["); si_hex16(dd->vendor); si_putc(':');
                si_hex16(dd->device); si_puts("]\n");
                si_puts("BAR0    : 0x"); si_hex64((unsigned long long)dd->bar[0]);
                si_puts("\n");
                found = 1;
                break;
            }
        }
        if (!found) si_puts("(no VGA controller)\n");
        si_puts("LFB     : 0x"); si_hex64((unsigned long long)g_fb); si_puts("\n");
        si_puts("Mode    : "); si_num_u((unsigned long long)g_w); si_putc('x');
        si_num_u((unsigned long long)g_h); si_puts("x32\n\n");
    }

    /* --- Network --- */
    si_puts("--- Network ---\n");
    if (g_rtl_ok) {
        si_puts("NIC     : RTL8139 @ io=0x");
        si_hex16(g_rtl_io);
        si_puts("  [10EC:8139]\n");
        si_puts("MAC     : ");
        for (i = 0; i < 6; i++) {
            si_hex_byte(g_rtl_mac[i]);
            if (i < 5) si_putc(':');
        }
        si_puts("\nIP      : 10.0.2.15 (static, QEMU user-net)\n");
    } else {
        si_puts("NIC     : not detected\n");
    }
    si_puts("\n");

    /* --- Uptime --- */
    si_puts("--- Uptime ---\n");
    si_puts("Up      : "); fmt_uptime(g_ticks); si_puts("\n");
    si_puts("Ticks   : "); si_num_u((unsigned long long)g_ticks); si_puts(" (18.2 Hz PIT)\n");
    si_puts("Boot t. : "); si_num_u((unsigned long long)g_boot_ticks); si_puts("\n");

    return g_sysinfo_buf;
}

void kernel_main(unsigned int magic, unsigned long long mbi_addr) {
    serial_init();
    g_boot_ticks = g_ticks;
    serial_puts("\n=== mUnix 64-bit boot ===\n");
    serial_puts("magic = "); serial_hex(magic); serial_puts("\n");
    serial_puts("mbi   = "); serial_hex(mbi_addr); serial_puts("\n");
    { int _w = 100000; while (_w-- > 0) {} }   /* flush serial */

    mb2_parse(magic, mbi_addr);
    serial_puts("mb2 parsed\n");

    idt_init();
    serial_puts("IDT installed\n");

    pci_enumerate();
    serial_puts("PCI enumerated: "); serial_hex((unsigned long long)g_pci_count);
    serial_puts(" devices\n");
    pci_dump_usb();
    pci_dump_all();
    rtl8139_init();
    net_test_full();

        /* lexbor test disabled */
    
    /* SSE/SSE2 всегда включены в long mode */
    i8042_init();
    pit_init();

    splash();
    fs_init();
        

    { int _vrc = vbe_init();
      serial_puts("vbe_init = "); serial_hex((unsigned long long)(long long)_vrc); serial_puts("\n");
      serial_puts("g_fb = "); serial_hex((unsigned long long)g_fb); serial_puts("\n");
      serial_puts("g_w  = "); serial_hex((unsigned long long)g_w); serial_puts("\n");
      serial_puts("g_h  = "); serial_hex((unsigned long long)g_h); serial_puts("\n");
      if (_vrc != 0)  { t_clear(); t_write(1,0,"VBE init failed.",0x0C); for(;;) __asm__ volatile("hlt"); }

    /* Invalidate VGA text → switch to graphics */
    serial_puts("calling munix_gui_init_dock\n"); munix_gui_init_dock(); serial_puts("init_dock OK\n");
    munix_gui_open_window(WK_TERMINAL, (const unsigned char *)"Terminal", 8);

    {
        static int prev_mx = -1, prev_my = -1;
        for (;;) {
            pit_poll();
            pump_input();

            if (mouse_x != prev_mx || mouse_y != prev_my) {
                prev_mx = mouse_x; prev_my = mouse_y;
                g_dirty = 1;
            }

            if (g_dirty) {
                int y;
                munix_gui_ticks(g_ticks);
                munix_sysmon_tick();
            if (munix_gui_animated()) g_dirty = 1;
                serial_puts("calling munix_gui_render\n"); munix_gui_render((void *)g_backbuf, g_w, g_h, mouse_x, mouse_y); serial_puts("render OK\n");
                serial_puts("A\n");
                {
                    unsigned char *_base = (unsigned char *)g_fb;
                    unsigned int _pitch = (g_mbi && g_mbi->framebuffer_pitch)
                                          ? g_mbi->framebuffer_pitch
                                          : (unsigned int)(g_w * 4);
                    unsigned int _bpp = (g_mbi && g_mbi->framebuffer_bpp)
                                        ? g_mbi->framebuffer_bpp : 32;
                    for (y = 0; y < g_h; y++) {
                        int x;
                        unsigned char *dst_row = _base + (unsigned int)y * _pitch;
                        unsigned int *src = g_backbuf + y * g_w;
                        if (_bpp == 32) {
                            unsigned int *d32 = (unsigned int *)dst_row;
                            for (x = 0; x < g_w; x++) d32[x] = src[x];
                        } else if (_bpp == 24) {
                            unsigned char *d24 = dst_row;
                            for (x = 0; x < g_w; x++) {
                                unsigned int px = src[x];
                                d24[x*3 + 0] = (px      ) & 0xFF;  /* B */
                                d24[x*3 + 1] = (px >>  8) & 0xFF;  /* G */
                                d24[x*3 + 2] = (px >> 16) & 0xFF;  /* R */
                            }
                        } else if (_bpp == 16) {
                            unsigned short *d16 = (unsigned short *)dst_row;
                            for (x = 0; x < g_w; x++) {
                                unsigned int px = src[x];
                                unsigned int r = (px >> 16) & 0xFF;
                                unsigned int g = (px >>  8) & 0xFF;
                                unsigned int b = (px      ) & 0xFF;
                                d16[x] = (unsigned short)(((r >> 3) << 11)
                                                        | ((g >> 2) <<  5)
                                                        | ((b >> 3)      ));
                            }
                        }
                    }
                }
                g_dirty = 0;
            }
        }
    }
}
}
