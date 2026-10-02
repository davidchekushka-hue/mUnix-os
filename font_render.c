/* mUnix pre-rendered AA font renderer — reads .mfnt blob */
/* No stb, no SSE, no float. Pure integer blit. */

extern void serial_puts(const char *s);
extern const unsigned char _binary_fonts_DejaVu14_mfnt_start[];
extern const unsigned char _binary_fonts_DejaVu14_mfnt_end[];

struct mfnt_hdr {
    unsigned char magic[4];
    unsigned int  version;
    unsigned int  size;
    unsigned int  first;
    unsigned int  last;
    int           line_height;
    int           ascent;
    unsigned int  count;
} __attribute__((packed));

struct mfnt_entry {
    unsigned int   data_offset;
    unsigned short w, h;
    short          bx, by;
    unsigned short adv;
} __attribute__((packed));

static const unsigned char *g_font = 0;
static const struct mfnt_hdr *g_hdr = 0;
static int g_ready = 0;

int munix_font_aa_init(void) {
    if (g_ready) return 0;
    g_font = _binary_fonts_DejaVu14_mfnt_start;
    if (!g_font) return -1;
    g_hdr = (const struct mfnt_hdr *)g_font;
    if (g_hdr->magic[0] != (unsigned char)'M' ||
        g_hdr->magic[1] != (unsigned char)'F' ||
        g_hdr->magic[2] != (unsigned char)'N' ||
        g_hdr->magic[3] != (unsigned char)'T') return -2;
    g_ready = 1;
    return 0;
}

int munix_font_aa_ready(void) { return g_ready; }
int munix_font_aa_line_height(int size) { (void)size; return g_ready ? g_hdr->line_height : 16; }
int munix_font_aa_ascent(int size) { (void)size; return g_ready ? g_hdr->ascent : 12; }

static const struct mfnt_entry *entry_for(int ch) {
    if (!g_ready) return 0;
    if ((unsigned)ch < g_hdr->first || (unsigned)ch > g_hdr->last) return 0;
    unsigned idx = (unsigned)ch - g_hdr->first;
    if (idx >= g_hdr->count) return 0;
    const struct mfnt_entry *tbl = (const struct mfnt_entry *)(g_font + sizeof(struct mfnt_hdr));
    return &tbl[idx];
}

int munix_font_aa_advance(int ch, int size) {
    (void)size;
    const struct mfnt_entry *e = entry_for(ch);
    return e ? (int)e->adv : 0;
}

static unsigned int blend(unsigned int bg, unsigned int fg, unsigned int a) {
    if (a == 0) return bg;
    if (a == 255) return fg;
    unsigned int br = (bg >> 16) & 0xFF, bg2 = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    unsigned int fr = (fg >> 16) & 0xFF, fg2 = (fg >> 8) & 0xFF, fb = fg & 0xFF;
    unsigned int r = (br * (255 - a) + fr * a) / 255;
    unsigned int g = (bg2 * (255 - a) + fg2 * a) / 255;
    unsigned int b = (bb * (255 - a) + fb * a) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

int munix_font_aa_glyph(unsigned int *px, int w, int h, int x, int y, int ch, unsigned int fg, int size) {
    (void)size;
    if (!g_ready || !px) return 0;
    const struct mfnt_entry *e = entry_for(ch);
    if (!e) return 0;
    int advance = (int)e->adv;
    int gw = e->w, gh = e->h;
    if (gw <= 0 || gh <= 0) return advance;
    const unsigned char *data = g_font + e->data_offset;
    int gx = x + e->bx;
    int gy = y + e->by;
    int row, col;
    for (row = 0; row < gh; row++) {
        int py = gy + row;
        if (py < 0 || py >= h) continue;
        for (col = 0; col < gw; col++) {
            int pxx = gx + col;
            if (pxx < 0 || pxx >= w) continue;
            unsigned char a = data[row * gw + col];
            if (a == 0) continue;
            px[py * w + pxx] = blend(px[py * w + pxx], fg, a);
        }
    }
    return advance;
}

int munix_font_aa_text(unsigned int *px, int w, int h, int x, int y, const char *s, unsigned int fg, int size) {
    if (!g_ready || !px || !s) return 0;
    int cx = x, cy = y;
    int lh = munix_font_aa_line_height(size);
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c == 10) { cx = x; cy += lh; continue; }
        if (c < 32) continue;
        int adv = munix_font_aa_glyph(px, w, h, cx, cy, c, fg, size);
        cx += adv;
    }
    return cx - x;
}

int munix_font_aa_measure(const char *s, int size) {
    if (!g_ready || !s) return 0;
    int t = 0;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c < 32) continue;
        t += munix_font_aa_advance(c, size);
    }
    return t;
}
/* debug: dump advances at runtime */
extern void serial_puts(const char *s);
extern void serial_hex(unsigned long long v);
extern void munix_debug_adv(void);

void munix_debug_adv(void) {
    if (!g_ready) { serial_puts("adv: not ready\n"); return; }
    serial_puts("adv dump: first="); serial_hex(g_hdr->first);
    serial_puts(" last="); serial_hex(g_hdr->last);
    serial_puts(" count="); serial_hex(g_hdr->count);
    serial_puts(" lh="); serial_hex(g_hdr->line_height);
    serial_puts("\n");
    const char *test = "Welcome";
    int i = 0;
    while (test[i]) {
        int ch = (unsigned char)test[i];
        const struct mfnt_entry *e = entry_for(ch);
        serial_puts("  '"); 
        char tmp[2] = { test[i], 0 };
        serial_puts(tmp);
        serial_puts("': w=");
        if (e) serial_hex(e->w); else serial_puts("?");
        serial_puts(" adv=");
        if (e) serial_hex(e->adv); else serial_puts("?");
        serial_puts(" bx=");
        if (e) serial_hex(e->bx); else serial_puts("?");
        serial_puts("\n");
        i++;
    }
}
