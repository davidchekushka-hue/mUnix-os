/* mUnix Public SDK v0.1 */
#ifndef MUNIX_SDK_H
#define MUNIX_SDK_H
#define SYSCALL_BASE ((unsigned long long *)0x7000)
typedef void (*sys_write_t)(const char *);
typedef void (*sys_putchar_t)(int);
typedef unsigned int (*sys_ticks_t)(void);
typedef void (*sys_speaker_t)(unsigned int);
typedef void (*sys_open_win_t)(unsigned int, const unsigned char *, unsigned long);
typedef void (*sys_reboot_t)(void);
typedef void (*sys_fill_rect_t)(int, int, int, int, unsigned int);
typedef int  (*sys_http_get_t)(const char *, const char *);
static inline void sys_write(const char *s) { ((sys_write_t)SYSCALL_BASE[0])(s); }
static inline void sys_putchar(int c) { ((sys_putchar_t)SYSCALL_BASE[1])(c); }
static inline unsigned int sys_ticks(void) { return ((sys_ticks_t)SYSCALL_BASE[2])(); }
static inline void sys_speaker(unsigned int f) { ((sys_speaker_t)SYSCALL_BASE[3])(f); }
static inline void sys_open_window(unsigned int k, const unsigned char *t, unsigned long l) { ((sys_open_win_t)SYSCALL_BASE[4])(k, t, l); }
static inline void sys_reboot(void) { ((sys_reboot_t)SYSCALL_BASE[5])(); }
static inline void sys_fill_rect(int x, int y, int w, int h, unsigned int c) { ((sys_fill_rect_t)SYSCALL_BASE[6])(x, y, w, h, c); }
static inline int sys_http_get(const char *h, const char *p) { return ((sys_http_get_t)SYSCALL_BASE[7])(h, p); }
#endif
