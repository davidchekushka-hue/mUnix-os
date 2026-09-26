/*
 * mUnix-graph v0.5.0 — Deep Code v2.5 Architect Edition
 * Bare-metal x86 freestanding. VBE + RAMFS + Terminal + 4 games.
 */

#include "io.h"

#define SCREEN_W        320
#define SCREEN_H        200

#define C_BLACK         0
#define C_BLUE          1
#define C_GREEN         2
#define C_CYAN          3
#define C_RED           4
#define C_MAGENTA       5
#define C_BROWN         6
#define C_LGRAY         7
#define C_DGRAY         8
#define C_BBLUE         9
#define C_BGREEN        10
#define C_BCYAN         11
#define C_BRED          12
#define C_BMAGENTA      13
#define C_YELLOW        14
#define C_WHITE         15

struct multiboot_info {
    unsigned int  flags;
    unsigned int  mem_lower;
    unsigned int  mem_upper;
    unsigned int  boot_device;
    unsigned int  cmdline;
    unsigned int  mods_count;
    unsigned int  mods_addr;
    unsigned int  syms[4];
    unsigned int  mmap_length;
    unsigned int  mmap_addr;
    unsigned int  drives_length;
    unsigned int  drives_addr;
    unsigned int  config_table;
    unsigned int  boot_loader_name;
    unsigned int  apm_table;
    unsigned int  vbe_control_info;
    unsigned int  vbe_mode_info;
    unsigned short vbe_mode;
    unsigned short vbe_interface_seg;
    unsigned short vbe_interface_off;
    unsigned short vbe_interface_len;
    unsigned long long framebuffer_addr;
    unsigned int  framebuffer_pitch;
    unsigned int  framebuffer_width;
    unsigned int  framebuffer_height;
    unsigned char framebuffer_bpp;
    unsigned char framebuffer_type;
    unsigned char color_info[6];
} __attribute__((packed));

#define MB_FLAG_FB 0x1000u

static struct multiboot_info *g_mbi = 0;

/* forward */
static void draw_window_frame(const char *title);

/* ============================================================
 * Framebuffer
 * ============================================================ */

static unsigned char  g_vbuf[SCREEN_W * SCREEN_H];
static unsigned int   g_pal32[16];
static unsigned char *g_lfb = 0;
static unsigned int   g_pitch = 0;
static unsigned int   g_fb_w = 0, g_fb_h = 0;
static int            g_scale = 1;
static int            g_off_x = 0, g_off_y = 0;
static int            g_vbe = 0;

static unsigned int vga6_to_8(unsigned int v) { return (v << 2) | (v >> 4); }
static unsigned int rgb_of(unsigned int r, unsigned int g, unsigned int b) {
    return (vga6_to_8(r) << 16) | (vga6_to_8(g) << 8) | vga6_to_8(b);
}
static void palette_init(void) {
    g_pal32[ 0] = rgb_of( 0,  0,  0);
    g_pal32[ 1] = rgb_of( 0,  0, 42);
    g_pal32[ 2] = rgb_of( 0, 42,  0);
    g_pal32[ 3] = rgb_of( 0, 32, 32);
    g_pal32[ 4] = rgb_of(42,  0,  0);
    g_pal32[ 5] = rgb_of(42,  0, 42);
    g_pal32[ 6] = rgb_of(42, 21,  0);
    g_pal32[ 7] = rgb_of(42, 42, 42);
    g_pal32[ 8] = rgb_of(21, 21, 21);
    g_pal32[ 9] = rgb_of(21, 21, 63);
    g_pal32[10] = rgb_of(21, 63, 21);
    g_pal32[11] = rgb_of(21, 63, 63);
    g_pal32[12] = rgb_of(63, 21, 21);
    g_pal32[13] = rgb_of(63, 21, 63);
    g_pal32[14] = rgb_of(63, 63, 21);
    g_pal32[15] = rgb_of(63, 63, 63);
}
static void fb_setup_scale(void) {
    int sx = (int)(g_fb_w / SCREEN_W);
    int sy = (int)(g_fb_h / SCREEN_H);
    g_scale = (sx < sy) ? sx : sy;
    if (g_scale < 1) g_scale = 1;
    g_off_x = (int)((g_fb_w - (unsigned int)(SCREEN_W * g_scale)) / 2u);
    g_off_y = (int)((g_fb_h - (unsigned int)(SCREEN_H * g_scale)) / 2u);
}
static void blit_rect(int x, int y, int w, int h) {
    int sx, sy, dx, dy;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    if (!g_vbe) {
        int row;
        for (row = 0; row < h; row++) {
            unsigned int off = (unsigned int)(y + row) * SCREEN_W + (unsigned int)x;
            unsigned char *dst = (unsigned char *)0xA0000 + off;
            const unsigned char *src = g_vbuf + off;
            int i;
            for (i = 0; i < w; i++) dst[i] = src[i];
        }
        return;
    }
    for (sy = 0; sy < h; sy++) for (sx = 0; sx < w; sx++) {
        unsigned char c = g_vbuf[(y + sy) * SCREEN_W + (x + sx)] & 0x0F;
        unsigned int rgb = g_pal32[c];
        unsigned int px = (unsigned int)(g_off_x + (x + sx) * g_scale);
        unsigned int py = (unsigned int)(g_off_y + (y + sy) * g_scale);
        unsigned char *base = g_lfb + py * g_pitch + px * 4u;
        for (dy = 0; dy < g_scale; dy++) {
            unsigned int *row32 = (unsigned int *)(base + (unsigned int)dy * g_pitch);
            for (dx = 0; dx < g_scale; dx++) row32[dx] = rgb;
        }
    }
}
static void blit_full(void) { blit_rect(0, 0, SCREEN_W, SCREEN_H); }

/* ============================================================
 * Bochs VBE
 * ============================================================ */

#define VBE_DISPI_INDEX_ID       0x0
#define VBE_DISPI_INDEX_XRES     0x1
#define VBE_DISPI_INDEX_YRES     0x2
#define VBE_DISPI_INDEX_BPP      0x3
#define VBE_DISPI_INDEX_ENABLE   0x4
#define VBE_DISPI_DISABLED       0x00
#define VBE_DISPI_ENABLED        0x01
#define VBE_DISPI_LFB_ENABLED    0x40
#define VBE_DISPI_NOCLEARMEM     0x80

static void nw_out(unsigned short p, unsigned short v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static unsigned short nw_in(unsigned short p){ unsigned short r; __asm__ volatile("inw %1,%0":"=a"(r):"Nd"(p)); return r; }
static void nl_out(unsigned short p, unsigned int v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static unsigned int nl_in(unsigned short p){ unsigned int r; __asm__ volatile("inl %1,%0":"=a"(r):"Nd"(p)); return r; }

static unsigned int pci_r32(unsigned char bus, unsigned char dev,
                            unsigned char fn, unsigned char off) {
    unsigned int a = 0x80000000u | ((unsigned int)bus << 16)
                   | ((unsigned int)dev << 11) | ((unsigned int)fn << 8)
                   | (off & 0xFC);
    nl_out(0xCF8, a); return nl_in(0xCFC);
}
static void vbe_w(unsigned short i, unsigned short v) {
    nw_out(0x1CE, i); nw_out(0x1CF, v);
}
static unsigned short vbe_r(unsigned short i) {
    nw_out(0x1CE, i); return nw_in(0x1CF);
}
static int bochs_vbe_setup(void) {
    int dev, fn, found = 0;
    unsigned int bar0 = 0;
    unsigned short id;
    for (dev = 0; dev < 32 && !found; dev++) {
        for (fn = 0; fn < 8 && !found; fn++) {
            unsigned int vd  = pci_r32(0, (unsigned char)dev, (unsigned char)fn, 0x00);
            unsigned int cls;
            if ((vd & 0xFFFF) == 0xFFFF) continue;
            cls = pci_r32(0, (unsigned char)dev, (unsigned char)fn, 0x08);
            if (((cls >> 24) & 0xFF) == 0x03 && ((cls >> 16) & 0xFF) == 0x00) {
                found = 1;
                bar0 = pci_r32(0, (unsigned char)dev, (unsigned char)fn, 0x10);
            }
        }
    }
    if (!found) return -1;
    id = vbe_r(VBE_DISPI_INDEX_ID);
    if (id < 0xB0C0) return -2;
    {
        static const unsigned short modes[4][2] = {
            {1920, 1080}, {1280, 720}, {1024, 768}, {800, 600}
        };
        int m;
        for (m = 0; m < 4; m++) {
            unsigned short rw, rh, rb;
            vbe_w(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
            vbe_w(VBE_DISPI_INDEX_XRES, modes[m][0]);
            vbe_w(VBE_DISPI_INDEX_YRES, modes[m][1]);
            vbe_w(VBE_DISPI_INDEX_BPP, 32);
            vbe_w(VBE_DISPI_INDEX_ENABLE,
                  VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM);
            rw = vbe_r(VBE_DISPI_INDEX_XRES);
            rh = vbe_r(VBE_DISPI_INDEX_YRES);
            rb = vbe_r(VBE_DISPI_INDEX_BPP);
            if (rw == modes[m][0] && rh == modes[m][1] && rb == 32) {
                g_fb_w  = rw; g_fb_h = rh;
                g_pitch = (unsigned int)rw * 4u;
                g_lfb   = (unsigned char *)(bar0 & 0xFFFFFFF0u);
                fb_setup_scale();
                return 0;
            }
        }
    }
    vbe_w(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    return -3;
}

/* ============================================================
 * VGA Mode 13h fallback
 * ============================================================ */

static void vga_seq(unsigned char i, unsigned char v) { outb(0x3C4, i); outb(0x3C5, v); }
static void vga_crtc(unsigned char i, unsigned char v) { outb(0x3D4, i); outb(0x3D5, v); }
static void vga_gra(unsigned char i, unsigned char v) { outb(0x3CE, i); outb(0x3CF, v); }
static void dac_set(unsigned char idx, unsigned char r, unsigned char g, unsigned char b) {
    outb(0x3C8, idx); outb(0x3C9, r); outb(0x3C9, g); outb(0x3C9, b);
}
static void init_vga_mode13h(void) {
    int i;
    outb(0x3C2, 0x63);
    vga_seq(0x00, 0x01); vga_seq(0x01, 0x01); vga_seq(0x02, 0x0F);
    vga_seq(0x03, 0x00); vga_seq(0x04, 0x0E); vga_seq(0x00, 0x03);
    vga_crtc(0x11, 0x00);
    vga_crtc(0x00,0x5F); vga_crtc(0x01,0x4F); vga_crtc(0x02,0x50);
    vga_crtc(0x03,0x82); vga_crtc(0x04,0x54); vga_crtc(0x05,0x80);
    vga_crtc(0x06,0xBF); vga_crtc(0x07,0x1F); vga_crtc(0x08,0x00);
    vga_crtc(0x09,0x41); vga_crtc(0x0A,0x00); vga_crtc(0x0B,0x00);
    vga_crtc(0x0C,0x00); vga_crtc(0x0D,0x00); vga_crtc(0x0E,0x00);
    vga_crtc(0x0F,0x00); vga_crtc(0x10,0x9C); vga_crtc(0x11,0x8E);
    vga_crtc(0x12,0x8F); vga_crtc(0x13,0x28); vga_crtc(0x14,0x40);
    vga_crtc(0x15,0x96); vga_crtc(0x16,0xB9); vga_crtc(0x17,0xA3);
    vga_crtc(0x18,0xFF); vga_crtc(0x19,0x00);
    vga_gra(0x00,0x00); vga_gra(0x01,0x00); vga_gra(0x02,0x00);
    vga_gra(0x03,0x00); vga_gra(0x04,0x00); vga_gra(0x05,0x40);
    vga_gra(0x06,0x05); vga_gra(0x07,0x0F); vga_gra(0x08,0xFF);
    for (i = 0; i < 16; i++) {
        (void)inb(0x3DA); outb(0x3C0, (unsigned char)i); outb(0x3C0, (unsigned char)i);
    }
    (void)inb(0x3DA); outb(0x3C0, 0x20);
    outb(0x3C8, 0x00);
    for (i = 0; i < 256 * 3; i++) outb(0x3C9, 0x00);
    dac_set( 0,0,0,0); dac_set( 1,0,0,42); dac_set( 2,0,42,0);
    dac_set( 3,0,32,32); dac_set( 4,42,0,0); dac_set( 5,42,0,42);
    dac_set( 6,42,21,0); dac_set( 7,42,42,42); dac_set( 8,21,21,21);
    dac_set( 9,21,21,63); dac_set(10,21,63,21); dac_set(11,21,63,63);
    dac_set(12,63,21,21); dac_set(13,63,21,63); dac_set(14,63,63,21);
    dac_set(15,63,63,63);
}

/* ============================================================
 * PIT
 * ============================================================ */

static unsigned int   g_pit_ticks = 0;
static unsigned short g_pit_last = 0;
static unsigned short pit_read(void) {
    unsigned char lo, hi;
    outb(0x43, 0x00);
    lo = inb(0x40); hi = inb(0x40);
    return (unsigned short)((hi << 8) | lo);
}
static void pit_init(void) {
    outb(0x43, 0x36); outb(0x40, 0x00); outb(0x40, 0x00);
    g_pit_last = pit_read();
    g_pit_ticks = 0;
}
static void pit_poll(void) {
    unsigned short now = pit_read();
    if (now > g_pit_last) g_pit_ticks++;
    g_pit_last = now;
}
unsigned int system_ticks(void) { return g_pit_ticks; }

/* ============================================================
 * i8042
 * ============================================================ */

static void i8042_ww(void) { int t=200000; while (t-- > 0 && (inb(0x64) & 0x02)) {} }
static void i8042_wr(void) { int t=200000; while (t-- > 0 && !(inb(0x64) & 0x01)) {} }
static void i8042_dr(void) { int i; for (i=0;i<256;i++){ if(!(inb(0x64)&0x01))break; (void)inb(0x60);} }
static void i8042_cmd(unsigned char c) { i8042_ww(); outb(0x64, c); }
static void i8042_dout(unsigned char d) { i8042_ww(); outb(0x60, d); }
static unsigned char i8042_din(void) { i8042_wr(); return inb(0x60); }
static void mouse_wcmd(unsigned char b) { i8042_cmd(0xD4); i8042_dout(b); }
static unsigned char mouse_rd(void) { return i8042_din(); }
static void i8042_full_init(void) {
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

/* ============================================================
 * Примитивы
 * ============================================================ */

static void draw_pixel(int x, int y, unsigned char color) {
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    g_vbuf[y * SCREEN_W + x] = color;
}
static void draw_rect(int x, int y, int w, int h, unsigned char c) {
    int i, j;
    if (w <= 0 || h <= 0) return;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) draw_pixel(x + i, y + j, c);
}
static void draw_hline(int x, int y, int w, unsigned char c) {
    int i; for (i = 0; i < w; i++) draw_pixel(x + i, y, c);
}
static void clear_screen(unsigned char c) {
    unsigned int i; for (i = 0; i < SCREEN_W * SCREEN_H; i++) g_vbuf[i] = c;
}

/* ============================================================
 * Шрифт 8x8
 * ============================================================ */

static const unsigned char font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00},
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},
    {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00},
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00},
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00},
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00},
    {0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00},
    {0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x3C,0x00},
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00},
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00},
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00},
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x66,0x66,0x66,0x3C,0x0E,0x00},
    {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00},
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},
    {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00},
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00},
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00},
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00},
    {0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00},
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00},
    {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00},
    {0x1C,0x30,0x30,0x7C,0x30,0x30,0x30,0x00},
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C},
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00},
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    {0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x6C,0x38},
    {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00},
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0x00,0x00,0xEC,0xFE,0xD6,0xC6,0xC6,0x00},
    {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00},
    {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00},
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60},
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06},
    {0x00,0x00,0x6E,0x76,0x60,0x60,0x60,0x00},
    {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00},
    {0x30,0x30,0x7C,0x30,0x30,0x36,0x1C,0x00},
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00},
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00},
    {0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00},
    {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00},
    {0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C},
    {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00},
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};
static void draw_char(int x, int y, char c, unsigned char color) {
    unsigned char u = (unsigned char)c;
    const unsigned char *g; int row, col;
    if (u < 0x20 || u > 0x7F) u = '?';
    g = font8x8[(int)(u - 0x20)];
    for (row = 0; row < 8; row++) {
        unsigned char bits = g[row];
        for (col = 0; col < 8; col++)
            if (bits & (0x80u >> col)) draw_pixel(x + col, y + row, color);
    }
}
static void draw_string(int x, int y, const char *s, unsigned char color) {
    int cx = x, cy = y;
    while (*s) {
        if (*s == '\n') { cx = x; cy += 9; s++; continue; }
        if (cx + 8 > SCREEN_W) { cx = x; cy += 9; }
        draw_char(cx, cy, *s, color);
        cx += 8; s++;
    }
}

/* ============================================================
 * Утилиты
 * ============================================================ */

static int m_strlen(const char *s) { int n=0; while (s[n]) n++; return n; }
static int m_strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
void *memset(void *dst, int c, unsigned int n) {
    unsigned char *p = (unsigned char *)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}
void *memcpy(void *dst, const void *src, unsigned int n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}
static void u2s(unsigned int v, char *out) {
    char num[12]; int n = 0, i;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v > 0 && n < 10) { num[n++] = (char)('0' + v % 10); v /= 10; }
    for (i = 0; i < n; i++) out[i] = num[n - 1 - i];
    out[n] = 0;
}

/* ============================================================
 * RAMFS
 * ============================================================ */

#define FS_MAX_FILES  64
#define FS_NAME_MAX   32
#define FS_POOL_SIZE  (64 * 1024)

typedef struct {
    char name[FS_NAME_MAX];
    char *data;
    int  size, cap, is_used, is_dir, parent;
} VFile;

static VFile fs[FS_MAX_FILES];
static char fs_pool[FS_POOL_SIZE];
static unsigned int fs_pool_top = 0;
static int g_cwd = -1;

static char *fs_alloc(int bytes) {
    char *p;
    if (bytes <= 0) return 0;
    if (fs_pool_top + (unsigned int)bytes > FS_POOL_SIZE) return 0;
    p = &fs_pool[fs_pool_top];
    fs_pool_top += (unsigned int)bytes;
    fs_pool_top = (fs_pool_top + 3u) & ~3u;
    return p;
}
static int fs_lookup(int parent, const char *name) {
    int i;
    if (!name || !*name) return -1;
    for (i = 0; i < FS_MAX_FILES; i++)
        if (fs[i].is_used && fs[i].parent == parent &&
            m_strcmp(fs[i].name, name) == 0) return i;
    return -1;
}
static int fs_alloc_node(int parent, const char *name, int is_dir) {
    int i, slot = -1;
    if (!name || !*name) return -2;
    if (m_strlen(name) >= FS_NAME_MAX) return -3;
    if (fs_lookup(parent, name) >= 0) return -1;
    for (i = 0; i < FS_MAX_FILES; i++) if (!fs[i].is_used) { slot = i; break; }
    if (slot < 0) return -4;
    { int k; for (k = 0; k < FS_NAME_MAX; k++) fs[slot].name[k] = 0;
      for (k = 0; name[k] && k < FS_NAME_MAX - 1; k++) fs[slot].name[k] = name[k]; }
    fs[slot].data = 0; fs[slot].size = 0; fs[slot].cap = 0;
    fs[slot].is_dir = is_dir ? 1 : 0;
    fs[slot].is_used = 1; fs[slot].parent = parent;
    return slot;
}
static int fs_resolve(int cwd, const char *path) {
    int cur = cwd; const char *p = path; char comp[FS_NAME_MAX];
    if (!p || !*p) return cwd;
    if (*p == '/') { cur = -1; while (*p == '/') p++; }
    while (*p) {
        int n = 0;
        while (*p && *p != '/' && n < FS_NAME_MAX - 1) comp[n++] = *p++;
        comp[n] = 0;
        while (*p == '/') p++;
        if (n == 0) break;
        if (n == 1 && comp[0] == '.') continue;
        if (n == 2 && comp[0] == '.' && comp[1] == '.') { if (cur >= 0) cur = fs[cur].parent; continue; }
        cur = fs_lookup(cur, comp);
        if (cur < 0) return -2;
    }
    return cur;
}
static void fs_path_of(int idx, char *out, int cap) {
    int stack[32], depth = 0, cur = idx, k = 0, i, j;
    while (cur >= 0 && depth < 32) { stack[depth++] = cur; cur = fs[cur].parent; }
    if (depth == 0) { out[0] = '/'; out[1] = 0; return; }
    for (i = depth - 1; i >= 0; i--) {
        int n = m_strlen(fs[stack[i]].name);
        if (k + n + 2 >= cap) break;
        out[k++] = '/';
        for (j = 0; j < n; j++) out[k++] = fs[stack[i]].name[j];
    }
    out[k] = 0;
}
static int fs_remove(int idx) {
    int i;
    if (idx < 0) return -1;
    if (fs[idx].is_dir)
        for (i = 0; i < FS_MAX_FILES; i++)
            if (fs[i].is_used && fs[i].parent == idx) return -2;
    fs[idx].is_used = 0; fs[idx].size = 0; fs[idx].cap = 0;
    fs[idx].data = 0; fs[idx].is_dir = 0; fs[idx].parent = -1;
    return 0;
}
static int fs_write_idx(int idx, const char *text) {
    int want, i; char *buf;
    if (idx < 0) return -1;
    if (fs[idx].is_dir) return -2;
    if (!text) text = "";
    want = m_strlen(text) + 1;
    if (want > fs[idx].cap) {
        buf = fs_alloc(want);
        if (!buf) return -3;
        fs[idx].data = buf; fs[idx].cap = want;
    }
    for (i = 0; i < want; i++) fs[idx].data[i] = text[i];
    fs[idx].size = want - 1;
    return 0;
}
static int fs_used_count(void) {
    int i, n = 0;
    for (i = 0; i < FS_MAX_FILES; i++) if (fs[i].is_used) n++;
    return n;
}
static void fs_init(void) {
    int i, d_home, d_etc, d_docs;
    for (i = 0; i < FS_MAX_FILES; i++) {
        fs[i].is_used = 0; fs[i].size = 0; fs[i].cap = 0;
        fs[i].data = 0; fs[i].is_dir = 0; fs[i].parent = -1;
        { int k; for (k = 0; k < FS_NAME_MAX; k++) fs[i].name[k] = 0; }
    }
    fs_pool_top = 0; g_cwd = -1;
    fs_alloc_node(-1, "readme.txt", 0);
    fs_write_idx(fs_lookup(-1, "readme.txt"),
                 "Welcome to mUnix RAMFS! Deep Code v2.5.");
    fs_alloc_node(-1, "about.txt", 0);
    fs_write_idx(fs_lookup(-1, "about.txt"),
                 "mUnix-graph v0.5.0 - https://mUnixOs.com");
    d_home = fs_alloc_node(-1, "home", 1);
    d_etc  = fs_alloc_node(-1, "etc", 1);
    d_docs = fs_alloc_node(-1, "docs", 1);
    (void)d_etc;
    fs_alloc_node(d_home, "user.txt", 0);
    fs_write_idx(fs_lookup(d_home, "user.txt"), "default user: root");
    fs_alloc_node(d_docs, "intro.txt", 0);
    fs_write_idx(fs_lookup(d_docs, "intro.txt"), "mUnix is a tiny bare-metal OS.");
}

/* ============================================================
 * CPUID / Speaker / Reboot
 * ============================================================ */

static void cpuid_call(unsigned int code, unsigned int *a, unsigned int *b,
                       unsigned int *c, unsigned int *d) {
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(code));
}
static void cpuid_vendor(char out[13]) {
    unsigned int a, b, c, d;
    cpuid_call(0, &a, &b, &c, &d);
    out[0]=(char)(b&0xFF);        out[1]=(char)((b>>8)&0xFF);
    out[2]=(char)((b>>16)&0xFF);  out[3]=(char)((b>>24)&0xFF);
    out[4]=(char)(d&0xFF);        out[5]=(char)((d>>8)&0xFF);
    out[6]=(char)((d>>16)&0xFF);  out[7]=(char)((d>>24)&0xFF);
    out[8]=(char)(c&0xFF);        out[9]=(char)((c>>8)&0xFF);
    out[10]=(char)((c>>16)&0xFF); out[11]=(char)((c>>24)&0xFF);
    out[12]=0;
}
static void speaker_raw(unsigned int freq) {
    unsigned int div;
    if (freq == 0) return;
    div = 1193180u / freq;
    outb(0x43, 0xB6);
    outb(0x42, (unsigned char)(div & 0xFF));
    outb(0x42, (unsigned char)((div >> 8) & 0xFF));
    { unsigned char t = inb(0x61); if ((t & 0x03) != 0x03) outb(0x61, (unsigned char)(t | 0x03)); }
}
static void speaker_off(void) { unsigned char t = inb(0x61); outb(0x61, (unsigned char)(t & 0xFC)); }
static void speaker_delay(unsigned int loops) {
    unsigned int i;
    for (i = 0; i < loops; i++) { volatile unsigned int k; for (k = 0; k < 50000u; k++) {} }
}
static void speaker_beep(unsigned int freq, unsigned int loops) {
    speaker_raw(freq ? freq : 440); speaker_delay(loops); speaker_off();
}
static void sys_reboot(void) {
    while (inb(0x64) & 0x02) {}
    outb(0x64, 0xFE);
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

/* ============================================================
 * Клавиатура
 * ============================================================ */

#define KEY_NONE 0
#define KEY_UP 0x100
#define KEY_DOWN 0x101
#define KEY_LEFT 0x102
#define KEY_RIGHT 0x103
#define KEY_ESC 0x104
#define KEY_WIN 0x105
#define KEY_F1 0x106

static int shift_pressed = 0;
static int e0_pending = 0;

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
    if (sc == 0xE0) { e0_pending = 1; return KEY_NONE; }
    e0_pending = 0;
    if (!was_e0) {
        if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; return KEY_NONE; }
        if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; return KEY_NONE; }
    }
    if (sc & 0x80) return KEY_NONE;
    if (was_e0) {
        switch (sc) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
            case 0x5B: return KEY_WIN;
            case 0x5C: return KEY_WIN;
        }
        return KEY_NONE;
    }
    switch (sc) {
        case 0x3B: return KEY_F1;
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x01: return KEY_ESC;
    }
    { char c = shift_pressed ? sc_shift[sc] : sc_ascii[sc]; return (int)(unsigned char)c; }
}

/* ============================================================
 * PS/2 mouse
 * ============================================================ */

static unsigned char mouse_pkt[3];
static int mouse_pkt_idx = 0;
static int g_mouse_dx = 0, g_mouse_dy = 0, g_mouse_buttons = 0;

static void mouse_feed(unsigned char b) {
    if (mouse_pkt_idx == 0) if (!(b & 0x08)) return;
    mouse_pkt[mouse_pkt_idx++] = b;
    if (mouse_pkt_idx < 3) return;
    mouse_pkt_idx = 0;
    {
        unsigned char flags = mouse_pkt[0];
        int dx, dy;
        if (flags & 0xC0) return;
        dx = (int)mouse_pkt[1]; dy = (int)mouse_pkt[2];
        if (flags & 0x10) dx -= 256;
        if (flags & 0x20) dy -= 256;
        g_mouse_dx += dx; g_mouse_dy += dy;
        g_mouse_buttons = flags & 0x07;
    }
}
static int poll_input(void) {
    unsigned char st = inb(0x64);
    if (!(st & 0x01)) return KEY_NONE;
    {
        unsigned char b = inb(0x60);
        if (st & 0x20) { mouse_feed(b); return KEY_NONE; }
        return keyboard_feed(b);
    }
}

/* ============================================================
 * Курсор
 * ============================================================ */

static const unsigned char cursor_sprite[16][16] = {
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,1,1,1,1,0,0,0,0,0},
    {1,2,2,1,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0}
};
static unsigned char mouse_back_buffer[16 * 16];
static int mouse_x = 160, mouse_y = 100, mouse_visible = 0;

static void mouse_save_bg(void) {
    int i, j;
    for (j = 0; j < 16; j++) for (i = 0; i < 16; i++) {
        int sx = mouse_x + i, sy = mouse_y + j;
        unsigned char c = C_BLACK;
        if (sx >= 0 && sy >= 0 && sx < SCREEN_W && sy < SCREEN_H)
            c = g_vbuf[sy * SCREEN_W + sx];
        mouse_back_buffer[j * 16 + i] = c;
    }
}
static void mouse_restore_bg(void) {
    int i, j;
    if (!mouse_visible) return;
    for (j = 0; j < 16; j++) for (i = 0; i < 16; i++) {
        int sx = mouse_x + i, sy = mouse_y + j;
        if (sx >= 0 && sy >= 0 && sx < SCREEN_W && sy < SCREEN_H)
            g_vbuf[sy * SCREEN_W + sx] = mouse_back_buffer[j * 16 + i];
    }
    mouse_visible = 0;
}
static void mouse_draw(void) {
    int i, j;
    mouse_save_bg();
    for (j = 0; j < 16; j++) for (i = 0; i < 16; i++) {
        unsigned char p = cursor_sprite[j][i];
        int sx = mouse_x + i, sy = mouse_y + j;
        if (p == 0) continue;
        if (sx < 0 || sy < 0 || sx >= SCREEN_W || sy >= SCREEN_H) continue;
        g_vbuf[sy * SCREEN_W + sx] = (p == 1) ? C_BLACK : C_WHITE;
    }
    mouse_visible = 1;
}
static void mouse_move(int dx, int dy) {
    int old_x = mouse_x, old_y = mouse_y;
    mouse_restore_bg();
    mouse_x += dx; mouse_y += dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x > SCREEN_W - 16) mouse_x = SCREEN_W - 16;
    if (mouse_y > SCREEN_H - 16) mouse_y = SCREEN_H - 16;
    mouse_draw();
    blit_rect(old_x, old_y, 16, 16);
    blit_rect(mouse_x, mouse_y, 16, 16);
}

/* ============================================================
 * Layout
 * ============================================================ */

#define TASKBAR_H   16
#define TASKBAR_Y   (SCREEN_H - TASKBAR_H)
#define WIN_W       260
#define WIN_H       150
#define WIN_X       ((SCREEN_W - WIN_W) / 2)
#define WIN_Y       ((SCREEN_H - WIN_H) / 2 - 6)
#define CONTENT_X   (WIN_X + 4)
#define CONTENT_Y   (WIN_Y + 13)
#define CONTENT_COLS  30
#define CONTENT_LINES 13
#define APPLIST_W   128
#define APPLIST_H   100
#define APPLIST_X   2
#define APPLIST_Y   (TASKBAR_Y - APPLIST_H - 2)

typedef enum {
    WINDOW_NONE = 0, WINDOW_TERMINAL, WINDOW_FILEMAN, WINDOW_ABOUT,
    WINDOW_MEDIA, WINDOW_MINE, WINDOW_SNAKE, WINDOW_PONG, WINDOW_SHAPES
} window_t;

static window_t active_window = WINDOW_NONE;
static int appmenu_open = 0;

/* ============================================================
 * Terminal
 * ============================================================ */

static char term_lines[12][CONTENT_COLS + 1];
static int  term_line_count = 0;
static char term_input[CONTENT_COLS + 1];
static int  term_input_len = 0;

static void term_clear(void) {
    int i, j;
    for (i = 0; i < 12; i++) for (j = 0; j <= CONTENT_COLS; j++) term_lines[i][j] = 0;
    term_line_count = 0; term_input_len = 0; term_input[0] = 0;
}
static void term_add_line(const char *s) {
    int i, j;
    if (term_line_count >= 12) {
        for (i = 1; i < 12; i++)
            for (j = 0; j <= CONTENT_COLS; j++) term_lines[i-1][j] = term_lines[i][j];
        term_line_count = 11;
    }
    for (j = 0; j < CONTENT_COLS; j++)
        term_lines[term_line_count][j] = (s && s[j]) ? s[j] : 0;
    term_lines[term_line_count][CONTENT_COLS] = 0;
    term_line_count++;
}
static void term_input_char(char c) {
    if (term_input_len < CONTENT_COLS - 3) { term_input[term_input_len++] = c; term_input[term_input_len] = 0; }
}
static void term_input_backspace(void) { if (term_input_len > 0) { term_input_len--; term_input[term_input_len] = 0; } }
static char *tk_next(char **p) {
    char *s;
    while (**p == ' ') (*p)++;
    if (**p == 0) return 0;
    s = *p;
    while (**p && **p != ' ') (*p)++;
    if (**p == ' ') { **p = 0; (*p)++; }
    return s;
}
static void term_ls_children(int dir_idx) {
    int i, any = 0;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (fs[i].is_used && fs[i].parent == dir_idx) {
            char line[CONTENT_COLS + 1];
            int m, j;
            for (j = 0; j <= CONTENT_COLS; j++) line[j] = 0;
            for (m = 0; fs[i].name[m] && m < CONTENT_COLS - 3; m++) line[m] = fs[i].name[m];
            if (fs[i].is_dir) line[m++] = '/';
            line[m] = 0;
            term_add_line(line);
            any = 1;
        }
    }
    if (!any) term_add_line("(empty)");
}

/* ============================================================
 * Minesweeper
 * ============================================================ */

#define MINE_W 9
#define MINE_H 9
#define MINE_COUNT 10
#define MINE_CELL 10

static unsigned char mine_grid[MINE_H][MINE_W];
static unsigned char mine_state[MINE_H][MINE_W];   /* 0=hidden 1=open 2=flag */
static int mine_cx, mine_cy;
static int mine_over, mine_win;
static int mine_open_count, mine_flag_count;
static unsigned int mine_seed = 0xDEADBEEF;

static unsigned int mine_rand(void) {
    mine_seed = mine_seed * 1103515245u + 12345u;
    return (mine_seed >> 16) & 0x7FFF;
}
static void mine_init(void) {
    int i, j, placed = 0;
    mine_seed = system_ticks() * 1103515245u + 12345u;
    if (mine_seed == 0) mine_seed = 0xDEADBEEFu;
    for (j = 0; j < MINE_H; j++) for (i = 0; i < MINE_W; i++) {
        mine_grid[j][i] = 0; mine_state[j][i] = 0;
    }
    while (placed < MINE_COUNT) {
        int x = (int)(mine_rand() % MINE_W);
        int y = (int)(mine_rand() % MINE_H);
        if (mine_grid[y][x] != 9) { mine_grid[y][x] = 9; placed++; }
    }
    for (j = 0; j < MINE_H; j++) for (i = 0; i < MINE_W; i++) {
        int dx, dy, cnt = 0;
        if (mine_grid[j][i] == 9) continue;
        for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) {
            int nx = i + dx, ny = j + dy;
            if (nx < 0 || ny < 0 || nx >= MINE_W || ny >= MINE_H) continue;
            if (mine_grid[ny][nx] == 9) cnt++;
        }
        mine_grid[j][i] = (unsigned char)cnt;
    }
    mine_cx = 4; mine_cy = 4;
    mine_over = 0; mine_win = 0;
    mine_open_count = 0; mine_flag_count = 0;
}
static void mine_reveal(int x, int y) {
    if (x < 0 || y < 0 || x >= MINE_W || y >= MINE_H) return;
    if (mine_state[y][x] != 0) return;
    if (mine_grid[y][x] == 9) {
        mine_state[y][x] = 1;
        mine_over = 1;
        return;
    }
    mine_state[y][x] = 1;
    mine_open_count++;
    if (mine_grid[y][x] == 0) {
        int dx, dy;
        for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            mine_reveal(x + dx, y + dy);
        }
    }
    if (mine_open_count >= MINE_W * MINE_H - MINE_COUNT) mine_win = 1;
}
static void mine_toggle_flag(int x, int y) {
    if (mine_state[y][x] == 0) { mine_state[y][x] = 2; mine_flag_count++; }
    else if (mine_state[y][x] == 2) { mine_state[y][x] = 0; mine_flag_count--; }
}

static void mine_draw_cell(int x, int y, int px, int py) {
    unsigned char c = mine_state[y][x];
    if (c == 0) {
        draw_rect(px, py, MINE_CELL, MINE_CELL, C_LGRAY);
        draw_rect(px, py, MINE_CELL, 1, C_WHITE);
        draw_rect(px, py, 1, MINE_CELL, C_WHITE);
        draw_rect(px + MINE_CELL - 1, py, 1, MINE_CELL, C_DGRAY);
        draw_rect(px, py + MINE_CELL - 1, MINE_CELL, 1, C_DGRAY);
    } else if (c == 2) {
        draw_rect(px, py, MINE_CELL, MINE_CELL, C_LGRAY);
        draw_rect(px + 2, py + 2, 6, 6, C_RED);
        draw_rect(px + 4, py + 7, 2, 2, C_BLACK);
    } else {
        draw_rect(px, py, MINE_CELL, MINE_CELL, C_CYAN);
        draw_rect(px, py, MINE_CELL, 1, C_LGRAY);
        if (mine_grid[y][x] == 9) {
            draw_rect(px + 2, py + 2, 6, 6, C_BLACK);
            draw_rect(px + 4, py + 4, 2, 2, C_WHITE);
        } else if (mine_grid[y][x] > 0) {
            char ch = (char)('0' + mine_grid[y][x]);
            unsigned char col = C_BLACK;
            switch (mine_grid[y][x]) {
                case 1: col = C_BBLUE; break;
                case 2: col = C_BGREEN; break;
                case 3: col = C_BRED; break;
                case 4: col = C_BLUE; break;
                case 5: col = C_BROWN; break;
                case 6: col = C_MAGENTA; break;
                case 7: col = C_BMAGENTA; break;
                default: col = C_DGRAY;
            }
            draw_char(px + 1, py + 1, ch, col);
        }
    }
}
static void mine_draw(void) {
    draw_window_frame("Minesweeper");
    draw_rect(CONTENT_X, CONTENT_Y, CONTENT_COLS * 8, CONTENT_LINES * 9, C_LGRAY);

    int gx = CONTENT_X + (CONTENT_COLS * 8 - MINE_W * MINE_CELL) / 2;
    int gy = CONTENT_Y + 14;

    {
        char buf[32]; int k = 0; int rem = MINE_COUNT - mine_flag_count;
        const char *pfx = "Mines: ";
        while (pfx[k]) { buf[k] = pfx[k]; k++; }
        if (rem < 0) rem = 0;
        buf[k++] = (char)('0' + rem / 10);
        buf[k++] = (char)('0' + rem % 10);
        buf[k] = 0;
        draw_string(CONTENT_X + 4, CONTENT_Y, buf, C_BLACK);
        if (mine_over) draw_string(CONTENT_X + 120, CONTENT_Y, "BOOM!", C_BRED);
        else if (mine_win) draw_string(CONTENT_X + 120, CONTENT_Y, "YOU WIN!", C_BGREEN);
    }

    int x, y;
    for (y = 0; y < MINE_H; y++)
        for (x = 0; x < MINE_W; x++)
            mine_draw_cell(x, y, gx + x * MINE_CELL, gy + y * MINE_CELL);

    {
        int cx = gx + mine_cx * MINE_CELL;
        int cy = gy + mine_cy * MINE_CELL;
        draw_rect(cx, cy, MINE_CELL, 1, C_YELLOW);
        draw_rect(cx, cy + MINE_CELL - 1, MINE_CELL, 1, C_YELLOW);
        draw_rect(cx, cy, 1, MINE_CELL, C_YELLOW);
        draw_rect(cx + MINE_CELL - 1, cy, 1, MINE_CELL, C_YELLOW);
    }

    draw_string(CONTENT_X, CONTENT_Y + (CONTENT_LINES - 1) * 9,
                "arrows space=f f=flag r=reset", C_DGRAY);
}

/* ============================================================
 * Snake
 * ============================================================ */

#define SN_W 20
#define SN_H 11
#define SN_CW 11
#define SN_CH 8

static int sn_x[SN_W * SN_H];
static int sn_y[SN_W * SN_H];
static int sn_len;
static int sn_dx, sn_dy;
static int sn_dead;
static int sn_fx, sn_fy;
static unsigned int sn_seed = 0xCAFEBABE;
static unsigned int sn_last_move;

static unsigned int sn_rand(void) {
    sn_seed = sn_seed * 1103515245u + 12345u;
    return (sn_seed >> 16) & 0x7FFF;
}
static void sn_init(void) {
    int i;
    sn_seed = system_ticks() * 2654435761u + 0xC0FFEEu;
    if (sn_seed == 0) sn_seed = 0xC0FFEEu;
    sn_len = 3;
    sn_x[0] = 7; sn_y[0] = 5;
    sn_x[1] = 6; sn_y[1] = 5;
    sn_x[2] = 5; sn_y[2] = 5;
    sn_dx = 1; sn_dy = 0;
    sn_dead = 0;
    sn_fx = 15; sn_fy = 5;
    sn_last_move = system_ticks();
    (void)i;
}
static void sn_move(void) {
    int i, nx, ny, grew, tries;
    if (sn_dead) return;
    nx = sn_x[0] + sn_dx;
    ny = sn_y[0] + sn_dy;
    if (nx < 0 || ny < 0 || nx >= SN_W || ny >= SN_H) { sn_dead = 1; return; }
    for (i = 0; i < sn_len; i++) {
        if (sn_x[i] == nx && sn_y[i] == ny) { sn_dead = 1; return; }
    }
    grew = (nx == sn_fx && ny == sn_fy);
    if (grew && sn_len < SN_W * SN_H) sn_len++;
    for (i = sn_len - 1; i > 0; i--) { sn_x[i] = sn_x[i-1]; sn_y[i] = sn_y[i-1]; }
    sn_x[0] = nx; sn_y[0] = ny;
    if (grew) {
        tries = 200;
        while (tries-- > 0) {
            int fx = (int)(sn_rand() % SN_W);
            int fy = (int)(sn_rand() % SN_H);
            int hit = 0;
            for (i = 0; i < sn_len; i++) {
                if (sn_x[i] == fx && sn_y[i] == fy) { hit = 1; break; }
            }
            if (!hit) { sn_fx = fx; sn_fy = fy; break; }
        }
    }
}
static void sn_draw(void) {
    int i;
    int gx, gy;
    draw_window_frame("Snake");
    draw_rect(CONTENT_X, CONTENT_Y, CONTENT_COLS * 8, CONTENT_LINES * 9, C_BLACK);

    gx = CONTENT_X + (CONTENT_COLS * 8 - SN_W * SN_CW) / 2;
    gy = CONTENT_Y + 8;

    draw_rect(gx + sn_fx * SN_CW + 2, gy + sn_fy * SN_CH + 2,
              SN_CW - 4, SN_CH - 4, C_RED);

    for (i = 0; i < sn_len; i++) {
        unsigned char col = (i == 0) ? C_YELLOW : C_BGREEN;
        draw_rect(gx + sn_x[i] * SN_CW + 1, gy + sn_y[i] * SN_CH + 1,
                  SN_CW - 2, SN_CH - 2, col);
    }

    if (sn_dead) {
        draw_string(CONTENT_X + 84, CONTENT_Y + 55, "GAME OVER", C_BRED);
        draw_string(CONTENT_X + 60, CONTENT_Y + 70, "press R to restart", C_WHITE);
    } else {
        char buf[24]; int k = 0; int sc = sn_len - 3;
        const char *pfx = "Score: ";
        while (pfx[k]) { buf[k] = pfx[k]; k++; }
        buf[k++] = (char)('0' + sc / 10);
        buf[k++] = (char)('0' + sc % 10);
        buf[k] = 0;
        draw_string(CONTENT_X + 2, CONTENT_Y, buf, C_WHITE);
    }
    draw_string(CONTENT_X, CONTENT_Y + (CONTENT_LINES - 1) * 9,
                "arrows r=reset esc=close", C_LGRAY);
}

/* ============================================================
 * Pong
 * ============================================================ */

#define PG_W 220
#define PG_H 88

static int pg_py, pg_ay;
static int pg_bx, pg_by, pg_bdx, pg_bdy;
static int pg_sp, pg_sa;
static int pg_over;
static unsigned int pg_last_move;

static void pg_reset_ball(void) {
    pg_bx = PG_W / 2; pg_by = PG_H / 2;
    pg_bdx = (pg_bdx >= 0) ? -2 : 2;
    pg_bdy = (pg_bdy >= 0) ? 1 : -1;
}
static void pg_init(void) {
    pg_py = (PG_H - 20) / 2;
    pg_ay = (PG_H - 20) / 2;
    pg_bx = PG_W / 2; pg_by = PG_H / 2;
    pg_bdx = 2; pg_bdy = 1;
    pg_sp = 0; pg_sa = 0;
    pg_over = 0;
    pg_last_move = system_ticks();
}
static void pg_move(void) {
    int py, ay;
    if (pg_over) return;
    pg_bx += pg_bdx;
    pg_by += pg_bdy;
    if (pg_by <= 0) { pg_by = 0; pg_bdy = -pg_bdy; }
    if (pg_by >= PG_H - 4) { pg_by = PG_H - 4; pg_bdy = -pg_bdy; }
    if (pg_bdx < 0 && pg_bx <= 4 && pg_bx >= 0) {
        if (pg_by + 4 >= pg_py && pg_by <= pg_py + 20) {
            pg_bdx = -pg_bdx; pg_bx = 5;
        }
    }
    if (pg_bdx > 0 && pg_bx + 4 >= PG_W - 4 && pg_bx + 4 <= PG_W) {
        if (pg_by + 4 >= pg_ay && pg_by <= pg_ay + 20) {
            pg_bdx = -pg_bdx; pg_bx = PG_W - 9;
        }
    }
    if (pg_bx >= PG_W) {
        pg_sp++;
        if (pg_sp >= 5) pg_over = 1;
        else pg_reset_ball();
        return;
    }
    if (pg_bx <= -4) {
        pg_sa++;
        if (pg_sa >= 5) pg_over = 1;
        else pg_reset_ball();
        return;
    }
    py = pg_ay + 10;
    ay = pg_by + 2;
    if (py < ay) pg_ay += 1;
    else if (py > ay) pg_ay -= 1;
    if (pg_ay < 0) pg_ay = 0;
    if (pg_ay > PG_H - 20) pg_ay = PG_H - 20;
}
static void pg_draw(void) {
    int gx, gy, i;
    draw_window_frame("Pong");
    draw_rect(CONTENT_X, CONTENT_Y, CONTENT_COLS * 8, CONTENT_LINES * 9, C_BLACK);

    gx = CONTENT_X + (CONTENT_COLS * 8 - PG_W) / 2;
    gy = CONTENT_Y + 10;

    for (i = 0; i < PG_H; i += 6) {
        draw_rect(gx + PG_W / 2, gy + i, 1, 3, C_LGRAY);
    }

    draw_rect(gx + 0, gy + pg_py, 4, 20, C_YELLOW);
    draw_rect(gx + PG_W - 4, gy + pg_ay, 4, 20, C_BRED);
    draw_rect(gx + pg_bx, gy + pg_by, 4, 4, C_WHITE);

    {
        char buf[32]; int k = 0;
        const char *pfx = "You ";
        while (pfx[k]) { buf[k] = pfx[k]; k++; }
        buf[k++] = (char)('0' + pg_sp);
        buf[k++] = ' '; buf[k++] = ':'; buf[k++] = ' ';
        buf[k++] = (char)('0' + pg_sa);
        buf[k++] = ' '; buf[k++] = 'A'; buf[k++] = 'I';
        buf[k] = 0;
        draw_string(CONTENT_X + 4, CONTENT_Y, buf, C_WHITE);
    }
    if (pg_over) {
        draw_string(CONTENT_X + 90, CONTENT_Y + 40, "GAME OVER", C_BRED);
    }
    draw_string(CONTENT_X, CONTENT_Y + (CONTENT_LINES - 1) * 9,
                "up/down move r=reset esc=close", C_LGRAY);
}

/* ============================================================
 * Shapes
 * ============================================================ */

#define SHAPE_COUNT 6
#define MAX_SHAPES 64

typedef struct { int x, y, type; unsigned char color; } PlacedShape;

static PlacedShape sh_placed[MAX_SHAPES];
static int sh_count = 0;
static int sh_current = 0;
static unsigned char sh_color = C_RED;
static int sh_color_step = 0;

static void sh_draw_one(int x, int y, int type, unsigned char color) {
    int i, j;
    if (type == 0) {
        draw_rect(x - 5, y - 5, 11, 11, color);
    } else if (type == 1) {
        for (j = -5; j <= 5; j++)
            for (i = -5; i <= 5; i++)
                if (i*i + j*j <= 25) draw_pixel(x + i, y + j, color);
    } else if (type == 2) {
        for (i = 0; i <= 5; i++)
            draw_rect(x - i, y - 5 + i, 2 * i + 1, 1, color);
    } else if (type == 3) {
        for (j = -5; j <= 5; j++) {
            int w = 5 - ((j < 0) ? -j : j);
            for (i = -w; i <= w; i++) draw_pixel(x + i, y + j, color);
        }
    } else if (type == 4) {
        for (i = -5; i <= 5; i++) draw_pixel(x + i, y + i, color);
    } else {
        for (i = -5; i <= 5; i++) {
            draw_pixel(x + i, y, color);
            draw_pixel(x, y + i, color);
        }
        for (i = -3; i <= 3; i++) {
            draw_pixel(x + i, y + i, color);
            draw_pixel(x + i, y - i, color);
        }
    }
}
static void sh_init(void) {
    sh_count = 0;
    sh_current = 0;
    sh_color = C_RED;
    sh_color_step = 0;
}
static void sh_draw(void) {
    int i;
    int canvas_y, canvas_h;
    draw_window_frame("Shapes");
    draw_rect(CONTENT_X, CONTENT_Y, CONTENT_COLS * 8, CONTENT_LINES * 9, C_WHITE);

    /* Buttons */
    for (i = 0; i < SHAPE_COUNT; i++) {
        int bx = CONTENT_X + 4 + i * 39;
        int by = CONTENT_Y + 2;
        unsigned char bg = (i == sh_current) ? C_BBLUE : C_LGRAY;
        draw_rect(bx, by, 35, 22, bg);
        draw_rect(bx, by, 35, 1, C_WHITE);
        draw_rect(bx, by, 1, 22, C_WHITE);
        draw_rect(bx + 34, by, 1, 22, C_DGRAY);
        draw_rect(bx, by + 21, 35, 1, C_DGRAY);
        sh_draw_one(bx + 17, by + 11, i, C_BLACK);
    }

    canvas_y = CONTENT_Y + 26;
    canvas_h = CONTENT_LINES * 9 - 26 - 9;

    /* Border */
    draw_rect(CONTENT_X, canvas_y, CONTENT_COLS * 8, 1, C_BLACK);
    draw_rect(CONTENT_X, canvas_y + canvas_h, CONTENT_COLS * 8, 1, C_BLACK);

    /* Placed shapes — clip to canvas */
    for (i = 0; i < sh_count; i++) {
        int x = sh_placed[i].x;
        int y = sh_placed[i].y;
        if (x < CONTENT_X + 6) x = CONTENT_X + 6;
        if (x > CONTENT_X + CONTENT_COLS * 8 - 6) x = CONTENT_X + CONTENT_COLS * 8 - 6;
        if (y < canvas_y + 6) y = canvas_y + 6;
        if (y > canvas_y + canvas_h - 6) y = canvas_y + canvas_h - 6;
        sh_draw_one(x, y, sh_placed[i].type, sh_placed[i].color);
    }

    draw_string(CONTENT_X + 2, CONTENT_Y + (CONTENT_LINES - 1) * 9,
                "1-6:tool click:place c:clear", C_DGRAY);
}
static void sh_place(int x, int y) {
    int canvas_y = CONTENT_Y + 26;
    int canvas_h = CONTENT_LINES * 9 - 26 - 9;
    if (y < canvas_y + 6 || y > canvas_y + canvas_h - 6) return;
    if (x < CONTENT_X + 6 || x > CONTENT_X + CONTENT_COLS * 8 - 6) return;
    if (sh_count >= MAX_SHAPES) {
        /* shift out oldest */
        int i;
        for (i = 1; i < MAX_SHAPES; i++) sh_placed[i-1] = sh_placed[i];
        sh_count = MAX_SHAPES - 1;
    }
    sh_placed[sh_count].x = x;
    sh_placed[sh_count].y = y;
    sh_placed[sh_count].type = sh_current;
    sh_placed[sh_count].color = sh_color;
    sh_count++;
    sh_color_step = (sh_color_step + 1) & 7;
    {
        static const unsigned char cols[8] = {
            C_RED, C_BLUE, C_GREEN, C_BROWN, C_MAGENTA, C_BGREEN, C_BBLUE, C_BRED
        };
        sh_color = cols[sh_color_step];
    }
}

/* ============================================================
 * Terminal execute
 * ============================================================ */

static void term_execute(void) {
    char saved[40]; char *p, *cmd, *a1, *a2; int i, n;
    n = term_input_len; if (n > 38) n = 38;
    for (i = 0; i < n; i++) saved[i] = term_input[i];
    saved[n] = 0;
    {
        char echo_line[CONTENT_COLS + 6];
        echo_line[0] = '>'; echo_line[1] = ' ';
        for (i = 0; i < n && i < CONTENT_COLS - 3; i++) echo_line[2 + i] = saved[i];
        echo_line[2 + (n < CONTENT_COLS - 3 ? n : CONTENT_COLS - 3)] = 0;
        term_add_line(echo_line);
    }
    term_input_len = 0; term_input[0] = 0;
    p = saved; cmd = tk_next(&p); a1 = tk_next(&p); a2 = tk_next(&p);
    if (!cmd || !*cmd) return;

    if (m_strcmp(cmd, "help") == 0) {
        term_add_line("help ls cd pwd cat write");
        term_add_line("mkdir touch rm echo clear");
        term_add_line("version about whoami");
        term_add_line("cpuid meminfo mount");
        term_add_line("beep play exit");
        term_add_line("mine snake pong shapes");
    } else if (m_strcmp(cmd, "ls") == 0) {
        int target = g_cwd;
        if (a1) { target = fs_resolve(g_cwd, a1);
                  if (target == -2) { term_add_line("ls: not found"); return; } }
        if (target >= 0 && !fs[target].is_dir) { term_add_line(fs[target].name); return; }
        term_ls_children(target);
    } else if (m_strcmp(cmd, "cd") == 0) {
        int idx;
        if (!a1 || !*a1) { g_cwd = -1; return; }
        idx = fs_resolve(g_cwd, a1);
        if (idx == -2) term_add_line("cd: no such directory");
        else if (idx >= 0 && !fs[idx].is_dir) term_add_line("cd: not a directory");
        else g_cwd = idx;
    } else if (m_strcmp(cmd, "pwd") == 0) {
        char path[200]; fs_path_of(g_cwd, path, 200); term_add_line(path);
    } else if (m_strcmp(cmd, "cat") == 0) {
        int idx;
        if (!a1) { term_add_line("cat: file?"); return; }
        idx = fs_resolve(g_cwd, a1);
        if (idx == -2) term_add_line("cat: not found");
        else if (idx < 0) term_add_line("cat: is root");
        else if (fs[idx].is_dir) term_add_line("cat: is a directory");
        else if (fs[idx].size == 0) term_add_line("(empty)");
        else {
            int pos = 0, total = fs[idx].size; char buf[CONTENT_COLS + 1];
            while (pos < total) {
                int c = 0;
                while (c < CONTENT_COLS && pos < total && fs[idx].data[pos] != '\n')
                    buf[c++] = fs[idx].data[pos++];
                buf[c] = 0; term_add_line(buf);
                if (pos < total && fs[idx].data[pos] == '\n') pos++;
            }
        }
    } else if (m_strcmp(cmd, "write") == 0) {
        int idx;
        if (!a1) term_add_line("write: filename?");
        else if (!a2) term_add_line("write: text?");
        else {
            idx = fs_resolve(g_cwd, a1);
            if (idx == -2) {
                int rc = fs_alloc_node(g_cwd, a1, 0);
                if (rc < 0) { term_add_line("write: create failed"); return; }
                idx = rc;
            }
            if (idx < 0) { term_add_line("write: is root"); return; }
            { int rc = fs_write_idx(idx, a2);
              if (rc == 0) term_add_line("written");
              else if (rc == -2) term_add_line("write: is a dir");
              else term_add_line("write: out of memory"); }
        }
    } else if (m_strcmp(cmd, "mkdir") == 0) {
        if (!a1) term_add_line("mkdir: name?");
        else { int rc = fs_alloc_node(g_cwd, a1, 1);
               if (rc >= 0) term_add_line("dir created");
               else if (rc == -1) term_add_line("mkdir: exists");
               else term_add_line("mkdir: failed"); }
    } else if (m_strcmp(cmd, "touch") == 0) {
        if (!a1) term_add_line("touch: name?");
        else { int rc = fs_alloc_node(g_cwd, a1, 0);
               if (rc >= 0) term_add_line("created");
               else if (rc == -1) term_add_line("exists");
               else term_add_line("touch: failed"); }
    } else if (m_strcmp(cmd, "rm") == 0) {
        int idx;
        if (!a1) { term_add_line("rm: name?"); return; }
        idx = fs_resolve(g_cwd, a1);
        if (idx == -2) { term_add_line("rm: not found"); return; }
        if (idx < 0) { term_add_line("rm: is root"); return; }
        { int rc = fs_remove(idx);
          if (rc == 0) term_add_line("removed");
          else if (rc == -2) term_add_line("rm: dir not empty");
          else term_add_line("rm: failed"); }
    } else if (m_strcmp(cmd, "echo") == 0) term_add_line(a1 ? a1 : "");
    else if (m_strcmp(cmd, "clear") == 0) term_clear();
    else if (m_strcmp(cmd, "version") == 0) term_add_line("mUnix-graph v0.5.0");
    else if (m_strcmp(cmd, "about") == 0) {
        term_add_line("Bare-metal x86 OS");
        term_add_line("https://mUnixOs.com");
    } else if (m_strcmp(cmd, "whoami") == 0) term_add_line("root");
    else if (m_strcmp(cmd, "beep") == 0) { speaker_beep(1000, 30); term_add_line("beep"); }
    else if (m_strcmp(cmd, "play") == 0) {
        unsigned int e4=329, g4=392, b4=493, d5=587, f5=698;
        term_add_line("playing Imperial March...");
        speaker_beep(g4,30); speaker_beep(g4,30); speaker_beep(g4,30);
        speaker_beep(e4,20); speaker_beep(b4,15); speaker_beep(g4,30);
        speaker_beep(e4,20); speaker_beep(b4,15); speaker_beep(g4,40);
        speaker_beep(d5,30); speaker_beep(d5,30); speaker_beep(d5,30);
        speaker_beep(e4,20); speaker_beep(b4,15); speaker_beep(f5,30);
        speaker_beep(e4,20); speaker_beep(b4,15); speaker_beep(g4,40);
        term_add_line("done");
    }
    else if (m_strcmp(cmd, "cpuid") == 0) {
        char vendor[13]; unsigned int a,b,c,d;
        cpuid_vendor(vendor);
        term_add_line("CPU:"); term_add_line(vendor);
        cpuid_call(0, &a, &b, &c, &d);
        { char buf[40]; int k = 0; char num[12];
          const char *pfx = "max leaf: ";
          while (pfx[k]) { buf[k] = pfx[k]; k++; }
          u2s(a, num);
          for (i = 0; num[i] && k < 39; i++) buf[k++] = num[i];
          buf[k] = 0; term_add_line(buf); }
        cpuid_call(1, &a, &b, &c, &d);
        { char buf[40]; int k = 0;
          const char *pfx = "stepping: ";
          while (pfx[k]) { buf[k] = pfx[k]; k++; }
          buf[k++] = (char)('0' + (a & 0xF));
          buf[k] = 0; term_add_line(buf); }
    }
    else if (m_strcmp(cmd, "meminfo") == 0) {
        char buf[40]; int k, j; char num[12];
        if (g_mbi && (g_mbi->flags & 0x01u)) {
            unsigned int total_kb = g_mbi->mem_lower + g_mbi->mem_upper + 1024;
            k = 0;
            { const char *pfx = "RAM: "; while (pfx[k]) { buf[k] = pfx[k]; k++; } }
            u2s(total_kb / 1024, num);
            for (j = 0; num[j]; j++) buf[k++] = num[j];
            buf[k++] = 'M'; buf[k++] = 'B'; buf[k] = 0;
            term_add_line(buf);
        } else term_add_line("RAM: unknown");
        k = 0;
        { const char *pfx = "RAMFS: "; while (pfx[k]) { buf[k] = pfx[k]; k++; } }
        u2s((unsigned int)fs_used_count(), num);
        for (j = 0; num[j]; j++) buf[k++] = num[j];
        buf[k++] = '/';
        u2s(FS_MAX_FILES, num);
        for (j = 0; num[j]; j++) buf[k++] = num[j];
        buf[k] = 0; term_add_line(buf);
        if (g_vbe) {
            char v[40]; int kk = 0;
            const char *pfx = "VBE ";
            while (pfx[kk]) { v[kk] = pfx[kk]; kk++; }
            u2s(g_fb_w, num);
            for (j = 0; num[j] && kk < 30; j++) v[kk++] = num[j];
            v[kk++] = 'x';
            u2s(g_fb_h, num);
            for (j = 0; num[j] && kk < 30; j++) v[kk++] = num[j];
            v[kk++] = ' '; v[kk++] = 'x';
            u2s((unsigned int)g_scale, num);
            for (j = 0; num[j] && kk < 30; j++) v[kk++] = num[j];
            v[kk] = 0; term_add_line(v);
        } else term_add_line("VGA 13h 320x200");
    }
    else if (m_strcmp(cmd, "mount") == 0) {
        term_add_line("ramfs on / (rw)");
    }
    else if (m_strcmp(cmd, "mine") == 0)   { mine_init();  active_window = WINDOW_MINE; }
    else if (m_strcmp(cmd, "snake") == 0)  { sn_init();    active_window = WINDOW_SNAKE; }
    else if (m_strcmp(cmd, "pong") == 0)   { pg_init();    active_window = WINDOW_PONG; }
    else if (m_strcmp(cmd, "shapes") == 0) { sh_init();    active_window = WINDOW_SHAPES; }
    else if (m_strcmp(cmd, "exit") == 0) active_window = WINDOW_NONE;
    else term_add_line("unknown command");
}

/* ============================================================
 * Media player
 * ============================================================ */

static const char *mp_tracks[4] = {
    "1. Imperial March", "2. mUnix Chime",
    "3. Silent Night", "4. Tetris Theme"
};
static const int mp_freqs[4][8] = {
    { 392, 392, 392, 329, 493, 392, 329, 493 },
    { 262, 330, 392, 523, 392, 330, 262, 0 },
    { 440, 494, 523, 494, 440, 392, 330, 294 },
    { 659, 494, 523, 587, 523, 494, 440, 440 }
};
static int mp_current = 0, mp_playing = 0, mp_pos = 0;
static unsigned int mp_last_tick = 0;
static void mp_start_track(void) { mp_pos = 0; mp_last_tick = system_ticks(); }

/* ============================================================
 * Иконки / taskbar
 * ============================================================ */

static void draw_icon_trash(int x, int y) {
    draw_rect(x + 1, y, 12, 2, C_LGRAY);
    draw_rect(x + 3, y - 1, 8, 1, C_DGRAY);
    draw_rect(x, y + 2, 14, 14, C_LGRAY);
    draw_rect(x + 2, y + 5, 2, 9, C_DGRAY);
    draw_rect(x + 6, y + 5, 2, 9, C_DGRAY);
    draw_rect(x + 10, y + 5, 2, 9, C_DGRAY);
    draw_string(x - 4, y + 17, "Trash", C_WHITE);
}
static void draw_icon_floppy(int x, int y) {
    draw_rect(x, y, 14, 14, C_BLACK);
    draw_rect(x + 1, y + 1, 12, 12, C_LGRAY);
    draw_rect(x + 3, y + 1, 8, 5, C_DGRAY);
    draw_rect(x + 4, y + 2, 6, 3, C_WHITE);
    draw_rect(x + 3, y + 8, 8, 5, C_WHITE);
    draw_string(x - 4, y + 17, "Disk", C_WHITE);
}
static void draw_icon_media(int x, int y) {
    draw_rect(x, y, 14, 14, C_BLACK);
    draw_rect(x + 1, y + 1, 12, 12, C_LGRAY);
    draw_rect(x + 4, y + 3, 2, 8, C_BLACK);
    draw_rect(x + 9, y + 4, 2, 7, C_BLACK);
    draw_rect(x + 4, y + 3, 6, 2, C_BLACK);
    draw_rect(x + 3, y + 10, 3, 2, C_BLACK);
    draw_rect(x + 8, y + 10, 3, 2, C_BLACK);
    draw_string(x - 4, y + 17, "Media", C_WHITE);
}
static void draw_icon_mine(int x, int y) {
    draw_rect(x, y, 14, 14, C_BLACK);
    draw_rect(x + 1, y + 1, 12, 12, C_LGRAY);
    draw_rect(x + 4, y + 4, 6, 6, C_BLACK);
    draw_rect(x + 6, y + 6, 2, 2, C_WHITE);
    draw_rect(x + 6, y, 2, 3, C_RED);
    draw_string(x - 4, y + 17, "Mine", C_WHITE);
}
static void draw_icon_snake(int x, int y) {
    draw_rect(x, y, 14, 14, C_BLACK);
    draw_rect(x + 1, y + 1, 12, 12, C_BGREEN);
    draw_rect(x + 3, y + 4, 8, 2, C_BLACK);
    draw_rect(x + 3, y + 8, 3, 2, C_BLACK);
    draw_rect(x + 5, y + 5, 2, 2, C_RED);
    draw_string(x - 4, y + 17, "Snake", C_WHITE);
}
static void draw_icon_pong(int x, int y) {
    draw_rect(x, y, 14, 14, C_BLACK);
    draw_rect(x + 1, y + 1, 12, 12, C_YELLOW);
    draw_rect(x + 3, y + 3, 1, 8, C_BLACK);
    draw_rect(x + 10, y + 3, 1, 8, C_BLACK);
    draw_rect(x + 6, y + 6, 2, 2, C_WHITE);
    draw_string(x - 4, y + 17, "Pong", C_WHITE);
}
static void draw_icon_shapes(int x, int y) {
    draw_rect(x, y, 14, 14, C_BLACK);
    draw_rect(x + 1, y + 1, 12, 12, C_WHITE);
    draw_rect(x + 2, y + 2, 4, 4, C_RED);
    draw_rect(x + 8, y + 2, 4, 4, C_BLUE);
    draw_rect(x + 2, y + 8, 4, 4, C_GREEN);
    draw_rect(x + 8, y + 8, 4, 4, C_YELLOW);
    draw_string(x - 4, y + 17, "Draw", C_WHITE);
}
static void draw_desktop(void) {
    clear_screen(C_CYAN);
    draw_icon_trash(4, 12);
    draw_icon_floppy(48, 12);
    draw_icon_media(92, 12);
    draw_icon_mine(136, 12);
    draw_icon_snake(180, 12);
    draw_icon_pong(224, 12);
    draw_icon_shapes(268, 12);
}
static void draw_taskbar(void) {
    draw_rect(0, TASKBAR_Y, SCREEN_W, TASKBAR_H, C_LGRAY);
    draw_hline(0, TASKBAR_Y, SCREEN_W, C_WHITE);
    draw_rect(2, TASKBAR_Y + 1, 44, 10, C_LGRAY);
    draw_rect(2, TASKBAR_Y + 1, 44, 1, C_WHITE);
    draw_rect(2, TASKBAR_Y + 1, 1, 10, C_WHITE);
    draw_rect(45, TASKBAR_Y + 1, 1, 10, C_BLACK);
    draw_rect(2, TASKBAR_Y + 10, 44, 1, C_BLACK);
    draw_string(4, TASKBAR_Y + 2, "AppList", C_BLACK);
    draw_string(SCREEN_W - 100, TASKBAR_Y + 2, "mUnix v0.7.0", C_BLACK);
}

/* ============================================================
 * Окна
 * ============================================================ */

static void draw_window_frame(const char *title) {
    int x = WIN_X, y = WIN_Y, w = WIN_W, h = WIN_H;
    draw_rect(x, y, w, h, C_LGRAY);
    draw_rect(x, y, w, 1, C_WHITE);
    draw_rect(x, y, 1, h, C_WHITE);
    draw_rect(x + w - 1, y, 1, h, C_BLACK);
    draw_rect(x, y + h - 1, w, 1, C_BLACK);
    draw_rect(x + 1, y + 1, w - 2, 10, C_CYAN);
    draw_string(x + 3, y + 2, title, C_WHITE);
    draw_rect(x + w - 12, y + 2, 9, 8, C_LGRAY);
    draw_rect(x + w - 12, y + 2, 9, 1, C_WHITE);
    draw_rect(x + w - 12, y + 2, 1, 8, C_WHITE);
    draw_rect(x + w - 4, y + 2, 1, 8, C_BLACK);
    draw_rect(x + w - 12, y + 9, 9, 1, C_BLACK);
    draw_char(x + w - 10, y + 2, 'x', C_BLACK);
}

static void draw_window_terminal(void) {
    int i;
    char title[40]; char path[60]; int k = 0, j;
    fs_path_of(g_cwd, path, 60);
    { const char *pfx = "Terminal - ";
      while (pfx[k] && k < 39) { title[k] = pfx[k]; k++; }
      for (j = 0; path[j] && k < 39; j++) title[k++] = path[j];
      title[k] = 0; }
    draw_window_frame(title);
    draw_rect(CONTENT_X, CONTENT_Y, CONTENT_COLS * 8, CONTENT_LINES * 9, C_BLACK);
    for (i = 0; i < term_line_count && i < 12; i++)
        if (term_lines[i][0])
            draw_string(CONTENT_X, CONTENT_Y + i * 9, term_lines[i], C_BGREEN);
    { char prompt[CONTENT_COLS + 1]; int p = 0;
      prompt[p++] = '>'; prompt[p++] = ' ';
      for (i = 0; i < term_input_len && p < CONTENT_COLS; i++) prompt[p++] = term_input[i];
      prompt[p] = 0;
      draw_string(CONTENT_X, CONTENT_Y + 12 * 9, prompt, C_WHITE);
      draw_rect(CONTENT_X + p * 8, CONTENT_Y + 12 * 9, 7, 8, C_LGRAY); }
}

static void draw_window_fileman(void) {
    int i, row;
    draw_window_frame("File Manager");
    draw_rect(CONTENT_X, CONTENT_Y, CONTENT_COLS * 8, CONTENT_LINES * 9, C_WHITE);
    draw_string(CONTENT_X, CONTENT_Y, "Name           Type Size", C_BLACK);
    draw_hline(CONTENT_X, CONTENT_Y + 9, CONTENT_COLS * 8, C_DGRAY);
    row = CONTENT_Y + 12;
    for (i = 0; i < FS_MAX_FILES && row < CONTENT_Y + CONTENT_LINES * 9 - 8; i++) {
        if (fs[i].is_used && fs[i].parent == g_cwd) {
            char line[CONTENT_COLS + 1];
            int k, n, sz, j;
            for (j = 0; j <= CONTENT_COLS; j++) line[j] = ' ';
            for (k = 0; fs[i].name[k] && k < 14; k++) line[k] = fs[i].name[k];
            if (fs[i].is_dir) { line[15] = 'D'; line[16] = 'I'; line[17] = 'R'; }
            else {
                line[15] = 'F'; line[16] = 'I'; line[17] = 'L'; line[18] = 'E';
                sz = fs[i].size; n = 0;
                { char tmp[8];
                  if (sz == 0) tmp[n++] = '0';
                  else while (sz > 0 && n < 7) { tmp[n++] = (char)('0' + sz % 10); sz /= 10; }
                  for (k = 0; k < n; k++) line[20 + k] = tmp[n - 1 - k]; }
            }
            line[CONTENT_COLS] = 0;
            draw_string(CONTENT_X, row, line, C_BLACK);
            row += 9;
        }
    }
    draw_string(CONTENT_X, CONTENT_Y + (CONTENT_LINES - 1) * 9,
                "click [x] or ESC to close", C_DGRAY);
}

static void draw_window_about(void) {
    char vendor[13];
    cpuid_vendor(vendor);
    draw_window_frame("About mUnix");
    draw_rect(CONTENT_X, CONTENT_Y, CONTENT_COLS * 8, CONTENT_LINES * 9, C_WHITE);
    draw_string(CONTENT_X, CONTENT_Y + 0 * 9, "mUnix-graph v0.5.0", C_BLACK);
    draw_string(CONTENT_X, CONTENT_Y + 1 * 9, "x86 32-bit PM Bare-Metal", C_BLACK);
    draw_string(CONTENT_X, CONTENT_Y + 2 * 9, "Kernel: Deep Code v2.5", C_BLACK);
    draw_string(CONTENT_X, CONTENT_Y + 3 * 9, "RAMFS + VBE + 4 games", C_BLACK);
    draw_string(CONTENT_X, CONTENT_Y + 4 * 9, "Video: adaptive LFB", C_BLACK);
    draw_string(CONTENT_X, CONTENT_Y + 5 * 9, "Input: i8042 Kbd + Mouse", C_BLACK);
    draw_string(CONTENT_X, CONTENT_Y + 6 * 9, "CPU: ", C_BLACK);
    draw_string(CONTENT_X + 40, CONTENT_Y + 6 * 9, vendor, C_BLACK);
    draw_string(CONTENT_X, CONTENT_Y + 8 * 9, "https://mUnixOs.com", C_BLUE);
    draw_string(CONTENT_X, CONTENT_Y + 12 * 9, "click [x] or ESC to close", C_DGRAY);
}

static void draw_window_media(void) {
    int i;
    draw_window_frame("Media Player");
    draw_rect(CONTENT_X, CONTENT_Y, CONTENT_COLS * 8, CONTENT_LINES * 9, C_BLACK);
    { char line[CONTENT_COLS + 1]; int k = 0, m;
      const char *pfx = "Now: ";
      while (pfx[k]) { line[k] = pfx[k]; k++; }
      for (m = 0; mp_tracks[mp_current][m] && k < CONTENT_COLS; m++)
          line[k++] = mp_tracks[mp_current][m];
      line[k] = 0;
      draw_string(CONTENT_X, CONTENT_Y, line, C_YELLOW); }
    { int filled = (mp_pos * 26) / 100;
      draw_char(CONTENT_X, CONTENT_Y + 9, '[', C_WHITE);
      for (i = 0; i < 26; i++)
          draw_char(CONTENT_X + 8 + i * 8, CONTENT_Y + 9,
                    (i < filled) ? '=' : ' ', C_BGREEN);
      draw_char(CONTENT_X + 8 + 26 * 8, CONTENT_Y + 9, ']', C_WHITE); }
    { const char *st;
      if (mp_playing) st = "  PLAYING   [space=n/p=trk]";
      else            st = "  PAUSED    [space=n/p=trk]";
      draw_string(CONTENT_X, CONTENT_Y + 18, st, C_LGRAY); }
    for (i = 0; i < 4; i++) {
        int y = CONTENT_Y + (3 + i) * 9;
        if (i == mp_current) draw_string(CONTENT_X, y, "> ", C_BGREEN);
        else                 draw_string(CONTENT_X, y, "  ", C_LGRAY);
        draw_string(CONTENT_X + 16, y, mp_tracks[i],
                    (i == mp_current) ? C_YELLOW : C_LGRAY);
    }
    draw_string(CONTENT_X, CONTENT_Y + 12 * 9, "click [x] or ESC to close", C_DGRAY);
}

static void draw_appmenu(void) {
    int x = APPLIST_X, y = APPLIST_Y, w = APPLIST_W, h = APPLIST_H;
    draw_rect(x, y, w, h, C_LGRAY);
    draw_rect(x, y, w, 1, C_WHITE);
    draw_rect(x, y, 1, h, C_WHITE);
    draw_rect(x + w - 1, y, 1, h, C_BLACK);
    draw_rect(x, y + h - 1, w, 1, C_BLACK);
    draw_string(x + 4, y + 4,  "1. Terminal", C_BLACK);
    draw_string(x + 4, y + 14, "2. File Manager", C_BLACK);
    draw_string(x + 4, y + 24, "3. About OS", C_BLACK);
    draw_string(x + 4, y + 34, "4. Media Player", C_BLACK);
    draw_string(x + 4, y + 44, "5. Minesweeper", C_BLACK);
    draw_string(x + 4, y + 54, "6. Snake", C_BLACK);
    draw_string(x + 4, y + 64, "7. Pong", C_BLACK);
    draw_string(x + 4, y + 74, "8. Shapes", C_BLACK);
    draw_string(x + 4, y + 84, "9. Reboot", C_BRED);
}

static void redraw_scene(void) {
    mouse_visible = 0;
    draw_desktop();
    draw_taskbar();
    if (active_window == WINDOW_TERMINAL) draw_window_terminal();
    else if (active_window == WINDOW_FILEMAN) draw_window_fileman();
    else if (active_window == WINDOW_ABOUT)   draw_window_about();
    else if (active_window == WINDOW_MEDIA)   draw_window_media();
    else if (active_window == WINDOW_MINE)    mine_draw();
    else if (active_window == WINDOW_SNAKE)   sn_draw();
    else if (active_window == WINDOW_PONG)    pg_draw();
    else if (active_window == WINDOW_SHAPES)  sh_draw();
    if (appmenu_open) draw_appmenu();
    mouse_draw();
    blit_full();
}

/* ============================================================
 * Click
 * ============================================================ */

static void on_left_click(int cx, int cy) {
    if (cy >= TASKBAR_Y && cx >= 2 && cx < 46) {
        appmenu_open = !appmenu_open; redraw_scene(); return;
    }
    if (appmenu_open && cx >= APPLIST_X && cx < APPLIST_X + APPLIST_W &&
        cy >= APPLIST_Y && cy < APPLIST_Y + APPLIST_H) {
        int rel = cy - APPLIST_Y;
        if (rel >= 4  && rel < 14) { active_window = WINDOW_TERMINAL; appmenu_open = 0; redraw_scene(); return; }
        if (rel >= 14 && rel < 24) { active_window = WINDOW_FILEMAN;  appmenu_open = 0; redraw_scene(); return; }
        if (rel >= 24 && rel < 34) { active_window = WINDOW_ABOUT;    appmenu_open = 0; redraw_scene(); return; }
        if (rel >= 34 && rel < 44) { active_window = WINDOW_MEDIA;    appmenu_open = 0; redraw_scene(); return; }
        if (rel >= 44 && rel < 54) { mine_init(); active_window = WINDOW_MINE; appmenu_open = 0; redraw_scene(); return; }
        if (rel >= 54 && rel < 64) { sn_init();   active_window = WINDOW_SNAKE; appmenu_open = 0; redraw_scene(); return; }
        if (rel >= 64 && rel < 74) { pg_init();   active_window = WINDOW_PONG; appmenu_open = 0; redraw_scene(); return; }
        if (rel >= 74 && rel < 84) { sh_init();   active_window = WINDOW_SHAPES; appmenu_open = 0; redraw_scene(); return; }
        if (rel >= 84 && rel < 94) sys_reboot();
        return;
    }
    if (active_window != WINDOW_NONE) {
        int bx = WIN_X + WIN_W - 12, by = WIN_Y + 2;
        if (cx >= bx && cx < bx + 9 && cy >= by && cy < by + 8) {
            if (active_window == WINDOW_MEDIA && mp_playing) { mp_playing = 0; speaker_off(); }
            active_window = WINDOW_NONE; redraw_scene(); return;
        }
    }
    /* Shapes: pick tool from buttons, or place shape */
    if (active_window == WINDOW_SHAPES) {
        int i;
        int btns_y = CONTENT_Y + 2;
        if (cy >= btns_y && cy < btns_y + 22) {
            for (i = 0; i < SHAPE_COUNT; i++) {
                int bx = CONTENT_X + 4 + i * 39;
                if (cx >= bx && cx < bx + 35) {
                    sh_current = i;
                    redraw_scene();
                    return;
                }
            }
        }
        sh_place(cx, cy);
        redraw_scene();
        return;
    }

    /* Minesweeper: click reveals */
    if (active_window == WINDOW_MINE) {
        int gx = CONTENT_X + (CONTENT_COLS * 8 - MINE_W * MINE_CELL) / 2;
        int gy = CONTENT_Y + 14;
        int cxx = (cx - gx) / MINE_CELL;
        int cyy = (cy - gy) / MINE_CELL;
        if (cxx >= 0 && cxx < MINE_W && cyy >= 0 && cyy < MINE_H &&
            cx >= gx && cy >= gy) {
            mine_cx = cxx; mine_cy = cyy;
            if (!mine_over && !mine_win && mine_state[cyy][cxx] == 0)
                mine_reveal(cxx, cyy);
            redraw_scene();
            return;
        }
        /* click inside minesweeper window: consume */
        if (cx >= WIN_X && cx < WIN_X + WIN_W &&
            cy >= WIN_Y && cy < WIN_Y + WIN_H) return;
    }

    /* Desktop icons */
    if (cy >= 6 && cy < 36) {
        if (cx >= 0   && cx < 44) { active_window = WINDOW_ABOUT;   redraw_scene(); return; }
        if (cx >= 44  && cx < 88) { active_window = WINDOW_FILEMAN; redraw_scene(); return; }
        if (cx >= 88  && cx < 132){ active_window = WINDOW_MEDIA;   redraw_scene(); return; }
        if (cx >= 132 && cx < 176){ mine_init(); active_window = WINDOW_MINE;  redraw_scene(); return; }
        if (cx >= 176 && cx < 220){ sn_init();   active_window = WINDOW_SNAKE; redraw_scene(); return; }
        if (cx >= 220 && cx < 264){ pg_init();   active_window = WINDOW_PONG;  redraw_scene(); return; }
        if (cx >= 264 && cx < 308){ sh_init();   active_window = WINDOW_SHAPES;redraw_scene(); return; }
    }
}

/* ============================================================
 * GUI loop
 * ============================================================ */

static void gui_loop(void) {
    int prev_buttons = 0;
    redraw_scene();
    for (;;) {
        int k; unsigned int now;
        pit_poll(); now = system_ticks();
        k = poll_input();

        if (g_mouse_dx != 0 || g_mouse_dy != 0) {
            mouse_move(g_mouse_dx, -g_mouse_dy);
            g_mouse_dx = 0; g_mouse_dy = 0;
        }
        if ((g_mouse_buttons & 0x01) && !(prev_buttons & 0x01))
            on_left_click(mouse_x, mouse_y);
        prev_buttons = g_mouse_buttons;

        /* Media playback tick */
        if (active_window == WINDOW_MEDIA && mp_playing) {
            if (now - mp_last_tick >= 1) {
                mp_last_tick = now;
                mp_pos += 4;
                if (mp_pos >= 100) { mp_pos = 0; mp_current = (mp_current + 1) % 4; }
                if ((now & 0x03) == 0) {
                    int f = mp_freqs[mp_current][(now / 4) % 8];
                    if (f > 0) speaker_beep((unsigned int)f, 3);
                }
                redraw_scene();
            }
        }

        /* Snake tick */
        if (active_window == WINDOW_SNAKE) {
            if (now - sn_last_move >= 8 && !sn_dead) {
                sn_last_move = now;
                sn_move();
                redraw_scene();
            }
        }

        /* Pong tick */
        if (active_window == WINDOW_PONG) {
            if (now - pg_last_move >= 2 && !pg_over) {
                pg_last_move = now;
                pg_move();
                redraw_scene();
            }
        }

        if (k == KEY_NONE) continue;

        /* Terminal input mode */
        if (active_window == WINDOW_TERMINAL) {
            if (k >= 32 && k < 127) { term_input_char((char)k); redraw_scene(); continue; }
            if (k == '\b') { term_input_backspace(); redraw_scene(); continue; }
            if (k == '\n') { term_execute(); redraw_scene(); continue; }
        }

        /* Media input mode */
        if (active_window == WINDOW_MEDIA) {
            if (k == ' ') {
                mp_playing = !mp_playing;
                if (mp_playing) mp_start_track(); else speaker_off();
                redraw_scene(); continue;
            }
            if (k == 'n' || k == 'N') { mp_current = (mp_current + 1) % 4; mp_start_track(); redraw_scene(); continue; }
            if (k == 'p' || k == 'P') { mp_current = (mp_current + 3) % 4; mp_start_track(); redraw_scene(); continue; }
        }

        /* Minesweeper input mode */
        if (active_window == WINDOW_MINE) {
            if (k == KEY_UP)    { if (mine_cy > 0) mine_cy--; redraw_scene(); continue; }
            if (k == KEY_DOWN)  { if (mine_cy < MINE_H - 1) mine_cy++; redraw_scene(); continue; }
            if (k == KEY_LEFT)  { if (mine_cx > 0) mine_cx--; redraw_scene(); continue; }
            if (k == KEY_RIGHT) { if (mine_cx < MINE_W - 1) mine_cx++; redraw_scene(); continue; }
            if (k == ' ') {
                if (!mine_over && !mine_win && mine_state[mine_cy][mine_cx] == 0)
                    mine_reveal(mine_cx, mine_cy);
                redraw_scene(); continue;
            }
            if (k == 'f' || k == 'F') {
                if (!mine_over && !mine_win)
                    mine_toggle_flag(mine_cx, mine_cy);
                redraw_scene(); continue;
            }
            if (k == 'r' || k == 'R') { mine_init(); redraw_scene(); continue; }
        }

        /* Snake input mode */
        if (active_window == WINDOW_SNAKE) {
            if (k == KEY_UP    && sn_dy == 0) { sn_dx = 0; sn_dy = -1; continue; }
            if (k == KEY_DOWN  && sn_dy == 0) { sn_dx = 0; sn_dy =  1; continue; }
            if (k == KEY_LEFT  && sn_dx == 0) { sn_dx = -1; sn_dy = 0; continue; }
            if (k == KEY_RIGHT && sn_dx == 0) { sn_dx =  1; sn_dy = 0; continue; }
            if (k == 'r' || k == 'R') { sn_init(); redraw_scene(); continue; }
        }

        /* Pong input mode */
        if (active_window == WINDOW_PONG) {
            if (k == KEY_UP)   { if (pg_py > 0) pg_py -= 4; redraw_scene(); continue; }
            if (k == KEY_DOWN) { if (pg_py < PG_H - 20) pg_py += 4; redraw_scene(); continue; }
            if (k == 'r' || k == 'R') { pg_init(); redraw_scene(); continue; }
        }

        /* Shapes input */
        if (active_window == WINDOW_SHAPES) {
            if (k >= '1' && k <= '6') {
                sh_current = k - '1';
                redraw_scene(); continue;
            }
            if (k == 'c' || k == 'C') { sh_init(); redraw_scene(); continue; }
        }

        /* Global keys */
        if (k == KEY_ESC) {
            if (active_window != WINDOW_NONE || appmenu_open) {
                if (active_window == WINDOW_MEDIA && mp_playing) { mp_playing = 0; speaker_off(); }
                active_window = WINDOW_NONE; appmenu_open = 0; redraw_scene();
            }
        }
        else if (k == KEY_WIN || k == KEY_F1) { appmenu_open = !appmenu_open; redraw_scene(); }
        else if (k == ' ') {
            if (active_window != WINDOW_TERMINAL && active_window != WINDOW_MEDIA &&
                active_window != WINDOW_MINE && active_window != WINDOW_SNAKE) {
                appmenu_open = !appmenu_open; redraw_scene();
            }
        }
        else if (k == KEY_UP || k == KEY_DOWN || k == KEY_LEFT || k == KEY_RIGHT) {
            /* arrows in no-focus state: move mouse */
            if (active_window == WINDOW_NONE || active_window == WINDOW_TERMINAL ||
                active_window == WINDOW_FILEMAN || active_window == WINDOW_ABOUT ||
                active_window == WINDOW_MEDIA) {
                if (k == KEY_UP)    mouse_move(0, -3);
                if (k == KEY_DOWN)  mouse_move(0, 3);
                if (k == KEY_LEFT)  mouse_move(-3, 0);
                if (k == KEY_RIGHT) mouse_move(3, 0);
            }
        }
        else if (k == 'r' || k == 'R') {
            if (active_window == WINDOW_SHAPES) { sh_init(); redraw_scene(); }
            else if (active_window != WINDOW_TERMINAL &&
                     active_window != WINDOW_SNAKE &&
                     active_window != WINDOW_MINE &&
                     active_window != WINDOW_PONG) sys_reboot();
        }
        else if (k == '1' && active_window != WINDOW_TERMINAL) { active_window = WINDOW_TERMINAL; appmenu_open = 0; redraw_scene(); }
        else if (k == '2' && active_window != WINDOW_TERMINAL) { active_window = WINDOW_FILEMAN;  appmenu_open = 0; redraw_scene(); }
        else if (k == '3' && active_window != WINDOW_TERMINAL) { active_window = WINDOW_ABOUT;    appmenu_open = 0; redraw_scene(); }
        else if (k == '4' && active_window != WINDOW_TERMINAL) { active_window = WINDOW_MEDIA;    appmenu_open = 0; redraw_scene(); }
        else if (k == '5' && active_window != WINDOW_TERMINAL) { mine_init(); active_window = WINDOW_MINE;   appmenu_open = 0; redraw_scene(); }
        else if (k == '6' && active_window != WINDOW_TERMINAL) { sn_init();   active_window = WINDOW_SNAKE;  appmenu_open = 0; redraw_scene(); }
        else if (k == '7' && active_window != WINDOW_TERMINAL) { pg_init();   active_window = WINDOW_PONG;   appmenu_open = 0; redraw_scene(); }
        else if (k == '8' && active_window != WINDOW_TERMINAL) { sh_init();   active_window = WINDOW_SHAPES; appmenu_open = 0; redraw_scene(); }
    }
}

/* ============================================================
 * Entry
 * ============================================================ */

void kernel_main(unsigned int magic, unsigned int mbi_addr) {
    if (magic == 0x2BADB002u) g_mbi = (struct multiboot_info *)mbi_addr;

    pit_init();
    fs_init();
    i8042_full_init();
    palette_init();

    if (bochs_vbe_setup() == 0) {
        g_vbe = 1;
    } else if (g_mbi && (g_mbi->flags & MB_FLAG_FB) &&
               (unsigned int)(g_mbi->framebuffer_addr >> 32) == 0 &&
               g_mbi->framebuffer_bpp == 32 &&
               g_mbi->framebuffer_width > 0 &&
               g_mbi->framebuffer_height > 0) {
        g_lfb   = (unsigned char *)(unsigned int)g_mbi->framebuffer_addr;
        g_pitch = g_mbi->framebuffer_pitch;
        g_fb_w  = g_mbi->framebuffer_width;
        g_fb_h  = g_mbi->framebuffer_height;
        fb_setup_scale();
        g_vbe = 1;
    } else {
        init_vga_mode13h();
        g_vbe = 0;
    }

    mine_init();
    sn_init();
    pg_init();
    sh_init();

    term_clear();
    term_add_line("mUnix Terminal v0.5.0");
    term_add_line("Type 'help' for commands");
    term_add_line("games: mine snake pong shapes");
    if (g_vbe) term_add_line("VBE framebuffer active");
    else       term_add_line("VGA 13h 320x200");

    gui_loop();
}
