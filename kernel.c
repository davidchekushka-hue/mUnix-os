/*
 * mUnix-kernel v0.8.0 — C hardware layer + RAMFS, GUI delegated to Rust.
 * Back-buffer + dirty flag eliminates flicker.
 */

#include "io.h"

struct multiboot_info {
    unsigned int  flags, mem_lower, mem_upper;
    unsigned int  boot_device, cmdline, mods_count, mods_addr;
    unsigned int  syms[4];
    unsigned int  mmap_length, mmap_addr;
    unsigned int  drives_length, drives_addr;
    unsigned int  config_table, boot_loader_name, apm_table;
    unsigned int  vbe_control_info, vbe_mode_info;
    unsigned short vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    unsigned long long framebuffer_addr;
    unsigned int  framebuffer_pitch, framebuffer_width, framebuffer_height;
    unsigned char framebuffer_bpp, framebuffer_type;
    unsigned char color_info[6];
} __attribute__((packed));

static struct multiboot_info *g_mbi = 0;

/* Rust GUI externs */
extern void munix_gui_init_dock(void);
extern void munix_gui_open_window(unsigned int kind,
                                  const unsigned char *title,
                                  unsigned long len);
extern void munix_gui_render(void *pixels, int w, int h, int mx, int my);
extern void munix_gui_key(int key);
extern void munix_gui_click(int x, int y, int buttons);
extern void munix_gui_ticks(unsigned int ticks);

#define WK_TERMINAL 1

/* ---- Port I/O ---- */
static void nw_out(unsigned short p, unsigned short v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static unsigned short nw_in(unsigned short p){ unsigned short r; __asm__ volatile("inw %1,%0":"=a"(r):"Nd"(p)); return r; }
static void nl_out(unsigned short p, unsigned int v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
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
    if (!found) return -1;
    if (vbe_r(0x0) < 0xB0C0) return -2;
    {
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
    }
    vbe_w(0x4, 0x00);
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
    fs_alloc(-1,"readme.txt",0); fs_write(fs_lookup(-1,"readme.txt"),"Welcome to mUnix v0.8.0 (Rust GUI).");
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

/* ---- Enable SSE ---- */
static void enable_sse(void) {
    unsigned int eax;
    __asm__ volatile("mov %%cr0, %%eax" : "=a"(eax));
    eax &= ~0x00000004u; eax |= 0x00000022u;
    __asm__ volatile("mov %%eax, %%cr0" :: "a"(eax));
    __asm__ volatile("mov %%cr4, %%eax" : "=a"(eax));
    eax |= 0x00000600u;
    __asm__ volatile("mov %%eax, %%cr4" :: "a"(eax));
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
#define BM_ITEMS 3
static const char *bm_labels[BM_ITEMS] = {"Start mUnix","Halt PC","Reboot PC"};
static void bm_draw(int sel,int first){
    int i; int bx=22,by=6,bw=36,bh=2*BM_ITEMS+8;
    if(first){ t_clear();
        t_write(1,0,"mUnix-kernel v0.8.0 (Rust GUI)",0x0A);
        t_write(1,2,"Multiboot ...... OK",0x0A);
        t_write(1,3,"Rust GUI ....... OK",0x0A);
        t_write(1,5,"Up/Down, Enter to activate",0x08); }
    {int yy,xx;for(yy=by;yy<by+bh;yy++)for(xx=bx;xx<bx+bw;xx++)t_put(xx,yy,' ',0x07);}
    t_box(bx,by,bw,bh,0x0B);
    t_write(bx+8,by+1,"mUnix Bootloader",0x0F);
    t_write(bx+4,by+2,"Up/Down + Enter",0x08);
    for(i=0;i<BM_ITEMS;i++){
        int y=by+4+i*2;
        if(i==sel){int k;for(k=0;k<bw-6;k++)t_put(bx+3+k,y,' ',0x1F);t_put(bx+3,y,0x10,0x1F);t_write(bx+5,y,bm_labels[i],0x1F);}
        else t_write(bx+5,y,bm_labels[i],0x07);
    }
}
static int bm_run(void){
    int sel=0,first=1;
    for(;;){
        int k=-1;
        if(first){bm_draw(sel,1);first=0;}
        {unsigned char st=inb(0x64); if((st&0x01)&&!(st&0x20)) k=keyboard_feed(inb(0x60));}
        if(k<0) continue;
        if(k==0x100){sel--;if(sel<0)sel=BM_ITEMS-1;bm_draw(sel,0);}
        else if(k==0x101){sel++;if(sel>=BM_ITEMS)sel=0;bm_draw(sel,0);}
        else if(k=='\n'||k=='\r'){
            if(sel==1){t_write(1,24,"Halted.",0x0C);for(;;)__asm__ volatile("hlt");}
            else if(sel==2){while(inb(0x64)&0x02){}outb(0x64,0xFE);for(;;)__asm__ volatile("hlt");}
            else return 0;
        }
    }
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
void kernel_main(unsigned int magic, unsigned int mbi_addr) {
    if (magic == 0x2BADB002u) g_mbi = (struct multiboot_info *)mbi_addr;

    enable_sse();
    i8042_init();
    pit_init();

    bm_run();
    splash();
    fs_init();

    if (vbe_init() != 0) { t_clear(); t_write(1,0,"VBE init failed.",0x0C); for(;;) __asm__ volatile("hlt"); }

    /* Invalidate VGA text → switch to graphics */
    munix_gui_init_dock();
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
                munix_gui_render((void *)g_backbuf, g_w, g_h, mouse_x, mouse_y);
                for (y = 0; y < g_h; y++) {
                    int x;
                    unsigned int *dst = g_fb + y * g_w;
                    unsigned int *src = g_backbuf + y * g_w;
                    for (x = 0; x < g_w; x++) dst[x] = src[x];
                }
                g_dirty = 0;
            }
        }
    }
}
