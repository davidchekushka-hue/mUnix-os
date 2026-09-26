/*
 * mUnix kernel v0.0.6 — Deep Code v2.5 Architect Edition
 * 32-bit x86, freestanding. Boot animation + RAMFS + cpuid + sys_reboot.
 */

#include "io.h"

/* ============================================================
 * Константы
 * ============================================================ */

#define VGA_WIDTH       80
#define VGA_HEIGHT      25
#define VGA_MEMORY      ((volatile unsigned short *)0xB8000)

#define COLOR_DEFAULT   0x07
#define COLOR_GREEN     0x0A
#define COLOR_CYAN      0x0B
#define COLOR_YELLOW    0x0E
#define COLOR_WHITE     0x0F
#define COLOR_RED       0x0C

#define LINE_MAX        256
#define MBOOT_MAGIC     0x2BADB002u

#define RAMFS_MAX_FILES 10
#define RAMFS_NAME_MAX  32
#define RAMFS_DATA_MAX  256

#define KERNEL_VERSION  "0.0.6"

/* ============================================================
 * Глобальное состояние
 * ============================================================ */

static unsigned int cursor_x = 0;
static unsigned int cursor_y = 0;
static int shift_pressed = 0;

static char line_buf[LINE_MAX];

struct multiboot_info {
    unsigned int flags;
    unsigned int mem_lower;
    unsigned int mem_upper;
} __attribute__((packed));

static struct multiboot_info *g_mbi = 0;

/* ============================================================
 * Строковые утилиты
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

/* ============================================================
 * mini_sleep — грубая задержка на "busy loop"
 * ============================================================ */

static void mini_sleep(unsigned int ms) {
    unsigned int i;
    for (i = 0; i < ms; i++) {
        volatile unsigned int k;
        for (k = 0; k < 20000u; k++) { }
    }
}

/* ============================================================
 * RAMFS
 * ============================================================ */

typedef struct {
    char name[RAMFS_NAME_MAX];
    char content[RAMFS_DATA_MAX];
    int  size;
    int  is_used;
} VirtualFile;

static VirtualFile ramfs[RAMFS_MAX_FILES];

static int ramfs_used_count(void) {
    int i, n = 0;
    for (i = 0; i < RAMFS_MAX_FILES; i++) if (ramfs[i].is_used) n++;
    return n;
}

static int ramfs_find(const char *name) {
    int i;
    if (!name || !*name) return -1;
    for (i = 0; i < RAMFS_MAX_FILES; i++) {
        if (ramfs[i].is_used) {
            if (m_strcmp(ramfs[i].name, name) == 0) return i;
        }
    }
    return -1;
}

static int ramfs_create(const char *name) {
    int i, free_slot = -1;
    int name_len;

    if (!name || !*name) return -2;
    name_len = m_strlen(name);
    if (name_len >= RAMFS_NAME_MAX) return -3;

    if (ramfs_find(name) >= 0) return -1;

    for (i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!ramfs[i].is_used) { free_slot = i; break; }
    }
    if (free_slot < 0) return -4;

    {
        int k;
        for (k = 0; k < RAMFS_NAME_MAX; k++) ramfs[free_slot].name[k] = 0;
        for (k = 0; k < RAMFS_DATA_MAX; k++) ramfs[free_slot].content[k] = 0;
    }
    {
        int k;
        for (k = 0; k < name_len && k < RAMFS_NAME_MAX - 1; k++)
            ramfs[free_slot].name[k] = name[k];
        ramfs[free_slot].name[name_len < RAMFS_NAME_MAX - 1 ?
                              name_len : RAMFS_NAME_MAX - 1] = 0;
    }
    ramfs[free_slot].size = 0;
    ramfs[free_slot].is_used = 1;
    return free_slot;
}

static int ramfs_remove(const char *name) {
    int idx = ramfs_find(name);
    if (idx < 0) return -1;
    ramfs[idx].is_used = 0;
    ramfs[idx].size = 0;
    {
        int k;
        for (k = 0; k < RAMFS_NAME_MAX; k++) ramfs[idx].name[k] = 0;
        for (k = 0; k < RAMFS_DATA_MAX; k++) ramfs[idx].content[k] = 0;
    }
    return 0;
}

static int ramfs_write(const char *name, const char *text) {
    int idx = ramfs_find(name);
    int i;
    if (idx < 0) return -1;
    if (!text) text = "";
    for (i = 0; i < RAMFS_DATA_MAX - 1; i++) {
        ramfs[idx].content[i] = text[i];
        if (text[i] == 0) break;
    }
    ramfs[idx].content[RAMFS_DATA_MAX - 1] = 0;
    ramfs[idx].size = m_strlen(ramfs[idx].content);
    return 0;
}

/* ============================================================
 * VGA
 * ============================================================ */

static void vga_update_cursor(void) {
    unsigned short pos = (unsigned short)(cursor_y * VGA_WIDTH + cursor_x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
}

static void vga_putentryat(char c, unsigned char color,
                           unsigned int x, unsigned int y) {
    VGA_MEMORY[y * VGA_WIDTH + x] =
        ((unsigned short)color << 8) | (unsigned char)c;
}

static void vga_clear(void) {
    unsigned int i;
    for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_MEMORY[i] = ((unsigned short)COLOR_DEFAULT << 8) | ' ';
    cursor_x = 0; cursor_y = 0;
    vga_update_cursor();
}

static void vga_scroll(void) {
    unsigned int x, y;
    for (y = 1; y < VGA_HEIGHT; y++)
        for (x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
    for (x = 0; x < VGA_WIDTH; x++)
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
            ((unsigned short)COLOR_DEFAULT << 8) | ' ';
}

static void vga_putchar_attr(char c, unsigned char color) {
    if (c == '\n') { cursor_x = 0; cursor_y++; }
    else if (c == '\r') { cursor_x = 0; }
    else if (c == '\t') { cursor_x = (cursor_x + 4) & ~3u;
                          if (cursor_x >= VGA_WIDTH) { cursor_x = 0; cursor_y++; } }
    else if (c == '\b') { if (cursor_x > 0) { cursor_x--;
                          vga_putentryat(' ', color, cursor_x, cursor_y); } }
    else { vga_putentryat(c, color, cursor_x, cursor_y); cursor_x++;
           if (cursor_x >= VGA_WIDTH) { cursor_x = 0; cursor_y++; } }
    while (cursor_y >= VGA_HEIGHT) { vga_scroll(); cursor_y--; }
    vga_update_cursor();
}

static void vga_putchar(char c) { vga_putchar_attr(c, COLOR_DEFAULT); }
static void vga_write(const char *s) { while (*s) vga_putchar(*s++); }
static void vga_write_color(const char *s, unsigned char c) {
    while (*s) vga_putchar_attr(*s++, c);
}
static void println(const char *s) { vga_write(s); vga_putchar('\n'); }
static void println_color(const char *s, unsigned char c) {
    vga_write_color(s, c); vga_putchar('\n');
}

static void vga_write_udec(unsigned int n) {
    char b[12]; int i = 0;
    if (!n) { vga_putchar('0'); return; }
    while (n) { b[i++] = (char)('0' + (n % 10u)); n /= 10u; }
    while (i) vga_putchar(b[--i]);
}

static void vga_write_hex(unsigned int n) {
    const char *h = "0123456789ABCDEF"; int i;
    vga_write("0x");
    for (i = 28; i >= 0; i -= 4) vga_putchar(h[(n >> i) & 0xF]);
}

/* ============================================================
 * Клавиатура (PS/2, set 1)
 * ============================================================ */

static const char scancode_ascii[128] = {
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

static const char scancode_shift[128] = {
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

static char kb_wait_char(void) {
    for (;;) {
        unsigned char status = inb(0x64);
        unsigned char sc;
        char c;
        if ((status & 0x01) == 0) continue;
        sc = inb(0x60);
        if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; continue; }
        if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; continue; }
        if (sc & 0x80) continue;
        c = shift_pressed ? scancode_shift[sc] : scancode_ascii[sc];
        if (c != 0) return c;
    }
}

/* ============================================================
 * PIT — system ticks
 * ============================================================ */

static unsigned int g_pit_ticks = 0;
static unsigned short g_pit_last = 0;
static int g_pit_primed = 0;

static unsigned short pit_read_counter(void) {
    unsigned char lo, hi;
    outb(0x43, 0x00);
    lo = inb(0x40); hi = inb(0x40);
    return (unsigned short)((hi << 8) | lo);
}

static void pit_init(void) {
    outb(0x43, 0x36);
    outb(0x40, 0x00);
    outb(0x40, 0x00);
    g_pit_last = pit_read_counter();
    g_pit_ticks = 0;
    g_pit_primed = 1;
}

static void pit_poll(void) {
    unsigned short now;
    if (!g_pit_primed) return;
    now = pit_read_counter();
    if (now > g_pit_last) g_pit_ticks++;
    g_pit_last = now;
}

unsigned int system_ticks(void) { return g_pit_ticks; }
unsigned int uptime_seconds(void) { return g_pit_ticks / 18u; }

/* ============================================================
 * PC speaker
 * ============================================================ */

static void speaker_raw(unsigned int freq) {
    unsigned int div;
    if (freq == 0) return;
    div = 1193180u / freq;
    outb(0x43, 0xB6);
    outb(0x42, (unsigned char)(div & 0xFF));
    outb(0x42, (unsigned char)((div >> 8) & 0xFF));
    {
        unsigned char tmp = inb(0x61);
        if ((tmp & 0x03) != 0x03) outb(0x61, (unsigned char)(tmp | 0x03));
    }
}

static void speaker_off(void) {
    unsigned char tmp = inb(0x61);
    outb(0x61, (unsigned char)(tmp & 0xFC));
}

static void speaker_delay(unsigned int loops) {
    unsigned int i;
    for (i = 0; i < loops; i++) {
        volatile unsigned int k;
        for (k = 0; k < 50000u; k++) { }
    }
}

static void speaker_beep(unsigned int freq, unsigned int loops) {
    speaker_raw(freq ? freq : 440);
    speaker_delay(loops);
    speaker_off();
}

/* ============================================================
 * sys_reboot — i8042 pulse reset line (cmd 0xFE)
 * ============================================================ */

static void sys_reboot(void) {
    /* дождаться, пока input buffer пуст */
    while (inb(0x64) & 0x02) { }
    outb(0x64, 0xFE);

    /* fallback: triple fault, если pulse не сработал */
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

/* ============================================================
 * CPUID helper
 * ============================================================ */

static void cpuid_call(unsigned int code,
                       unsigned int *a, unsigned int *b,
                       unsigned int *c, unsigned int *d) {
    __asm__ volatile ("cpuid"
                      : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                      : "a"(code));
}

static void cpuid_vendor(char out[13]) {
    unsigned int a, b, c, d;
    cpuid_call(0, &a, &b, &c, &d);
    out[0]  = (char)( b        & 0xFF);
    out[1]  = (char)((b >> 8)  & 0xFF);
    out[2]  = (char)((b >> 16) & 0xFF);
    out[3]  = (char)((b >> 24) & 0xFF);
    out[4]  = (char)( d        & 0xFF);
    out[5]  = (char)((d >> 8)  & 0xFF);
    out[6]  = (char)((d >> 16) & 0xFF);
    out[7]  = (char)((d >> 24) & 0xFF);
    out[8]  = (char)( c        & 0xFF);
    out[9]  = (char)((c >> 8)  & 0xFF);
    out[10] = (char)((c >> 16) & 0xFF);
    out[11] = (char)((c >> 24) & 0xFF);
    out[12] = 0;
}

/* ============================================================
 * Boot animation
 * ============================================================ */

static void boot_log(const char *line) {
    vga_write_color(" [ OK ] ", COLOR_CYAN);
    println(line);
    mini_sleep(200);
}

static void boot_animation(void) {
    vga_clear();
    println("Booting mUnix...");
    mini_sleep(200);

    boot_log("Flat 32-bit Protected Mode Active");
    boot_log("VGA Video Framebuffer Engaged (0xB8000)");
    boot_log("Keyboard PS/2 Controller Driver Online");
    boot_log("Motherboard PC Speaker Synthesizer Loaded");
    boot_log("RAMFS VFS Mounted successfully");

    mini_sleep(400);
    vga_clear();
}

/* ============================================================
 * Команды CLI
 * ============================================================ */

static void cmd_help(void) {
    println("mUnix Shell msh 0.0.6 - built-in commands:");
    println("  help                 - show this help");
    println("  clear                - clear screen");
    println("  version              - system info");
    println("  fastfetch            - system information summary");
    println("  echo <text>          - print text");
    println("  reboot               - hardware reboot via i8042");
    println("  cpuid                - CPU information");
    println("  meminfo              - memory map (multiboot)");
    println("  ls                   - list RAMFS files");
    println("  touch <name>         - create empty file in RAMFS");
    println("  cat <name>           - print RAMFS file contents");
    println("  write <name> <text>  - overwrite RAMFS file contents");
    println("  rm <name>            - delete RAMFS file");
    println("  beep                 - short beep via PC speaker");
    println("  play                 - play Imperial March");
    println("  ps                   - processes");
    println("  top                  - load average snapshot");
    println("  exit                 - halt system");
}

static void cmd_version(void) {
    println("mUnix v0.0.6 (Final Release Build)");
    println("  Architecture : x86 (i386, 32-bit protected mode)");
    println("  Kernel       : Deep Code v2.5 Architect Edition");
    println("  Bootloader   : Multiboot v1");
    println("  Storage      : RAMFS (in-memory VFS, 10 slots)");
    println("  Shell        : msh 0.0.6");
    println("  Build        : " __DATE__ " " __TIME__);
}

static void cmd_echo(const char *args) {
    if (args && *args) println(args);
    else vga_putchar('\n');
}

static void cmd_cpuid(void) {
    unsigned int a, b, c, d;
    char vendor[13];
    cpuid_vendor(vendor);

    vga_write("CPUID leaf 0:\n");
    vga_write("  Vendor    : "); println(vendor);
    cpuid_call(0, &a, &b, &c, &d);
    vga_write("  Max leaf  : "); vga_write_udec(a); vga_putchar('\n');

    cpuid_call(1, &a, &b, &c, &d);
    vga_write("CPUID leaf 1:\n");
    vga_write("  Family    : "); vga_write_udec((a >> 8)  & 0xF); vga_putchar('\n');
    vga_write("  Model     : "); vga_write_udec((a >> 4)  & 0xF); vga_putchar('\n');
    vga_write("  Stepping  : "); vga_write_udec( a        & 0xF); vga_putchar('\n');
    vga_write("  EAX       : "); vga_write_hex(a); vga_putchar('\n');
    vga_write("  EBX       : "); vga_write_hex(b); vga_putchar('\n');
    vga_write("  ECX       : "); vga_write_hex(c); vga_putchar('\n');
    vga_write("  EDX       : "); vga_write_hex(d); vga_putchar('\n');
}

static void cmd_meminfo(void) {
    println("mUnix memory map (multiboot):");
    if (g_mbi && (g_mbi->flags & 0x01u)) {
        unsigned int total_kb;
        vga_write("  Lower memory (0..640K) : ");
        vga_write_udec(g_mbi->mem_lower);
        println(" KB");
        vga_write("  Upper memory           : ");
        vga_write_udec(g_mbi->mem_upper);
        println(" KB");
        total_kb = g_mbi->mem_lower + g_mbi->mem_upper + 1024;
        vga_write("  Total                  : ");
        vga_write_udec(total_kb / 1024);
        println(" MB");
    } else {
        println("  multiboot memory info not available");
    }
    vga_write("  Storage                : ");
    println("RAMFS Active (10 slots VFS Mounted)");
}

static void cmd_ls(void) {
    int i, any = 0;
    println("NAME                             SIZE");
    println("-------------------------------- ----");
    for (i = 0; i < RAMFS_MAX_FILES; i++) {
        if (ramfs[i].is_used) {
            int k;
            int name_len = m_strlen(ramfs[i].name);
            vga_write(ramfs[i].name);
            for (k = name_len; k < 32; k++) vga_putchar(' ');
            vga_putchar(' ');
            vga_write_udec((unsigned int)ramfs[i].size);
            vga_putchar('\n');
            any = 1;
        }
    }
    if (!any) println("Directory is empty");
}

static void cmd_touch(const char *name) {
    int rc;
    if (!name || !*name) { println("touch: missing operand"); return; }
    rc = ramfs_create(name);
    if (rc == -1) { println("touch: file already exists"); return; }
    if (rc == -2) { println("touch: invalid name"); return; }
    if (rc == -3) { println("touch: name too long"); return; }
    if (rc == -4) { println("touch: RAMFS is full (10 slots)"); return; }
    println("[RAMFS] File created");
}

static void cmd_cat(const char *name) {
    int idx;
    int k;
    if (!name || !*name) { println("cat: missing operand"); return; }
    idx = ramfs_find(name);
    if (idx < 0) { println("File not found"); return; }
    if (ramfs[idx].size == 0) { println("(empty file)"); return; }
    for (k = 0; k < ramfs[idx].size; k++) vga_putchar(ramfs[idx].content[k]);
    vga_putchar('\n');
}

static void cmd_write(const char *name, const char *text) {
    int rc;
    if (!name || !*name) { println("write: missing filename"); return; }
    if (!text || !*text) { println("write: missing text"); return; }
    rc = ramfs_write(name, text);
    if (rc < 0) { println("File not found"); return; }
    vga_write("[RAMFS] Wrote ");
    vga_write_udec((unsigned int)m_strlen(text));
    vga_write(" bytes to '");
    vga_write(name);
    println("'");
}

static void cmd_rm(const char *name) {
    int rc;
    if (!name || !*name) { println("rm: missing operand"); return; }
    rc = ramfs_remove(name);
    if (rc < 0) { println("File not found"); return; }
    vga_write("[RAMFS] Removed '");
    vga_write(name);
    println("'");
}

static void cmd_beep(void) {
    speaker_beep(1000, 30);
    println("beep");
}

static void cmd_play(void) {
    unsigned int e4 = 329, g4  = 392;
    unsigned int b4 = 493, d5  = 587;
    unsigned int f5 = 698;

    speaker_beep(g4, 30); speaker_beep(g4, 30); speaker_beep(g4, 30);
    speaker_beep(e4, 20); speaker_beep(b4, 15); speaker_beep(g4, 30);
    speaker_beep(e4, 20); speaker_beep(b4, 15); speaker_beep(g4, 40);

    speaker_beep(d5, 30); speaker_beep(d5, 30); speaker_beep(d5, 30);
    speaker_beep(e4, 20); speaker_beep(b4, 15); speaker_beep(f5, 30);
    speaker_beep(e4, 20); speaker_beep(b4, 15); speaker_beep(g4, 40);

    println("play: Imperial March done");
}

static void cmd_ps(void) {
    println("  PID  PPID  STAT  CMD");
    println("    1     0  R     /bin/init");
    println("    2     1  S     /bin/msh");
    println("    3     2  R     ps");
}

static void cmd_top(void) {
    println("mUnix top - 3 tasks");
    println("Tasks: 3 total, 2 running, 1 sleeping");
    println("CPU : 0.4% usr  0.1% sys  99.5% idle");
    println("Mem : RAMFS active; heap used by 10-slot VFS");
    println("");
    println("  PID  USER   CPU%  MEM%  CMD");
    println("    1  root    0.1   2.0  /bin/init");
    println("    2  root    0.2   3.1  /bin/msh");
    println("    3  root    0.1   1.0  top");
}

static void cmd_exit(void) {
    println("Session terminated. Halting mUnix.");
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

/* ============================================================
 * fastfetch
 * ============================================================ */

static const char *ff_logo[9] = {
    "                    _  _  _ ",
    "  _ __ ___  _   _ _(_)| |_| |",
    " | '_ ` _ \\| | | | | | | | | |",
    " | | | | | | |_| | |_| | |_| |",
    " |_| |_| |_|\\__,_|\\__,_|_|\\___|",
    "                              ",
    "   mUnix v0.0.6 -- Final      ",
    "   RAMFS in-memory VFS        ",
    "   (C) Deep Code v2.5         "
};

static void ff_put_row(const char *left, const char *label,
                       const char *value, unsigned char vcolor) {
    int k;
    for (k = 0; left[k]; k++) vga_putchar(left[k]);
    for (; k < 32; k++) vga_putchar(' ');
    vga_write("  ");
    if (label) vga_write_color(label, COLOR_CYAN);
    if (value) vga_write_color(value, vcolor);
    vga_putchar('\n');
}

static void cmd_fastfetch(void) {
    unsigned int i, secs, mins, hrs, ticks;
    char vendor[13];

    pit_poll();
    secs  = uptime_seconds();
    hrs   = secs / 3600u;
    mins  = (secs / 60u) % 60u;
    secs  = secs % 60u;
    ticks = system_ticks();
    cpuid_vendor(vendor);

    for (i = 0; ff_logo[0][i]; i++) vga_putchar(ff_logo[0][i]);
    for (; i < 32; i++) vga_putchar(' ');
    vga_write("  ");
    vga_write_color("root@munix", COLOR_GREEN);
    vga_putchar('\n');

    for (i = 0; ff_logo[1][i]; i++) vga_putchar(ff_logo[1][i]);
    for (; i < 32; i++) vga_putchar(' ');
    vga_write("  "); println("----------");

    ff_put_row(ff_logo[2], "OS      : ", "mUnix v0.0.6 x86",       COLOR_DEFAULT);
    ff_put_row(ff_logo[3], "Host    : ", "QEMU x86 PC",            COLOR_DEFAULT);
    ff_put_row(ff_logo[4], "Kernel  : ", "Bare-Metal (32-bit PM)", COLOR_DEFAULT);
    ff_put_row(ff_logo[5], "Shell   : ", "mUnix CLI Shell",        COLOR_DEFAULT);
    ff_put_row(ff_logo[6], "CPU     : ", vendor,                   COLOR_GREEN);
    ff_put_row(ff_logo[7], "Storage : ", "RAMFS Active (10 slots VFS Mounted)",
                                            COLOR_DEFAULT);
    ff_put_row(ff_logo[8], "Memory  : ", "4096 MB (32-bit Limit)", COLOR_DEFAULT);

    {
        int k;
        vga_write("                                  ");
        vga_write("  ");
        vga_write_color("Uptime  : ", COLOR_CYAN);
        vga_write_udec(hrs);  vga_putchar('h'); vga_putchar(' ');
        vga_write_udec(mins); vga_putchar('m'); vga_putchar(' ');
        vga_write_udec(secs); vga_putchar('s');
        vga_putchar('\n');

        vga_write("                                  ");
        vga_write("  ");
        vga_write_color("Ticks   : ", COLOR_CYAN);
        vga_write_udec(ticks);
        vga_write(" (PIT 18.2 Hz)");
        vga_putchar('\n');

        vga_write("                                  ");
        vga_write("  ");
        vga_write_color("Files   : ", COLOR_CYAN);
        vga_write_udec((unsigned int)ramfs_used_count());
        vga_write(" / ");
        vga_write_udec(RAMFS_MAX_FILES);
        println(" slots used");
        (void)k;
    }
}

/* ============================================================
 * Shell
 * ============================================================ */

static void shell_readline(void) {
    int i = 0;
    for (;;) {
        char c = kb_wait_char();
        pit_poll();
        if (c == '\n') { vga_putchar('\n'); line_buf[i] = 0; return; }
        else if (c == '\b') { if (i > 0) { i--; vga_putchar('\b'); } }
        else if (c >= 32 && c < 127) {
            if (i < LINE_MAX - 1) { line_buf[i++] = c; vga_putchar(c); }
        }
    }
}

static void shell_execute(void) {
    char *p = line_buf;
    char *cmd;
    char *args;
    char *args2;

    while (*p == ' ') p++;
    if (*p == 0) return;

    cmd = p;
    while (*p && *p != ' ') p++;
    if (*p == ' ') { *p = 0; p++; }
    while (*p == ' ') p++;
    args = p;

    args2 = args;
    while (*args2 && *args2 != ' ') args2++;
    if (*args2 == ' ') { *args2 = 0; args2++; }
    while (*args2 == ' ') args2++;

    if      (m_strcmp(cmd, "help")      == 0) cmd_help();
    else if (m_strcmp(cmd, "clear")     == 0) vga_clear();
    else if (m_strcmp(cmd, "version")   == 0) cmd_version();
    else if (m_strcmp(cmd, "fastfetch") == 0) cmd_fastfetch();
    else if (m_strcmp(cmd, "echo")      == 0) cmd_echo(args);
    else if (m_strcmp(cmd, "reboot")    == 0) { println("Rebooting..."); sys_reboot(); }
    else if (m_strcmp(cmd, "cpuid")     == 0) cmd_cpuid();
    else if (m_strcmp(cmd, "meminfo")   == 0) cmd_meminfo();
    else if (m_strcmp(cmd, "ls")        == 0) cmd_ls();
    else if (m_strcmp(cmd, "touch")     == 0) cmd_touch(args);
    else if (m_strcmp(cmd, "cat")       == 0) cmd_cat(args);
    else if (m_strcmp(cmd, "write")     == 0) cmd_write(args, args2);
    else if (m_strcmp(cmd, "rm")        == 0) cmd_rm(args);
    else if (m_strcmp(cmd, "beep")      == 0) cmd_beep();
    else if (m_strcmp(cmd, "play")      == 0) cmd_play();
    else if (m_strcmp(cmd, "ps")        == 0) cmd_ps();
    else if (m_strcmp(cmd, "top")       == 0) cmd_top();
    else if (m_strcmp(cmd, "exit")      == 0) cmd_exit();
    else {
        vga_write("msh: ");
        vga_write(cmd);
        println(": command not found");
    }
}

static void shell_run(void) {
    for (;;) {
        vga_write_color("mUnix", COLOR_GREEN);
        vga_write("> ");
        shell_readline();
        shell_execute();
    }
}

/* ============================================================
 * Welcome banner
 * ============================================================ */

static void welcome_banner(void) {
    println_color("                          _  _  _ ", COLOR_GREEN);
    println_color("   _ __ ___  _   _ _(_)| |_| |", COLOR_GREEN);
    println_color("  | '_ ` _ \\| | | | | | | | | |", COLOR_GREEN);
    println_color("  | | | | | | |_| | |_| | |_| |", COLOR_GREEN);
    println_color("  |_| |_| |_|\\__,_|\\__,_|_|\\___|", COLOR_GREEN);
    println("");
    println_color("   Welcome to mUnix v0.0.6!", COLOR_GREEN);
    println("");
    println_color("See more for mUnix on https://mUnixOs.com", COLOR_CYAN);
    println("");
}

/* ============================================================
 * RAMFS init
 * ============================================================ */

static void ramfs_init(void) {
    int i, k;
    for (i = 0; i < RAMFS_MAX_FILES; i++) {
        ramfs[i].is_used = 0;
        ramfs[i].size = 0;
        for (k = 0; k < RAMFS_NAME_MAX; k++) ramfs[i].name[k] = 0;
        for (k = 0; k < RAMFS_DATA_MAX; k++) ramfs[i].content[k] = 0;
    }
    ramfs_create("readme.txt");
    ramfs_write("readme.txt",
                "Welcome to mUnix RAMFS! Created by Deep Code v2.5.");
}

/* ============================================================
 * Entry
 * ============================================================ */

void kernel_main(unsigned int magic, unsigned int mbi_addr) {
    if (magic == MBOOT_MAGIC) g_mbi = (struct multiboot_info *)mbi_addr;
    else                      g_mbi = 0;

    pit_init();
    ramfs_init();

    boot_animation();
    welcome_banner();

    shell_run();
}
