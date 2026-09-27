//! mUnix v0.8.0 — Rust GUI. All apps functional: Terminal, Files, Editor,
//! Media player, Settings, clickable Control Center.

#![no_std]
#![allow(dead_code)]

use core::cmp::{max, min};

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop { unsafe { core::arch::asm!("cli; hlt"); } }
}
#[no_mangle]
pub extern "C" fn rust_eh_personality() {}

// ============================================================
// C ABI
// ============================================================
extern "C" {
    fn munix_font_glyph(px: *mut u32, w: i32, h: i32,
                        x: i32, y: i32, ch: u8, argb: u32);
    fn munix_font_text(px: *mut u32, w: i32, h: i32,
                       x: i32, y: i32, s: *const u8, argb: u32);
    fn munix_ramfs_count() -> i32;
    fn munix_ramfs_get(idx: i32, name: *mut u8, cap: i32,
                       is_dir: *mut i32, size: *mut i32, parent: *mut i32) -> i32;
    fn munix_ramfs_read(name: *const u8, parent: i32, out: *mut u8, cap: i32) -> i32;
    fn munix_ramfs_write(name: *const u8, parent: i32, data: *const u8, len: i32) -> i32;
    fn munix_ramfs_lookup(name: *const u8, parent: i32) -> i32;
    fn munix_ramfs_create(name: *const u8, parent: i32, is_dir: i32) -> i32;
    fn munix_ramfs_remove(name: *const u8, parent: i32) -> i32;
    fn munix_ramfs_pool_used() -> i32;
    fn munix_ramfs_pool_size() -> i32;
    fn munix_ramfs_root() -> i32;
    fn munix_system_reboot();
    fn munix_ticks() -> u32;
    fn munix_speaker_set(freq: u32);
    fn munix_speaker_off();
}

// ============================================================
// Theme
// ============================================================
mod theme {
    pub const BG_TOP:   u32 = 0xFF0E1B2A;
    pub const BG_BOT:   u32 = 0xFF060A12;
    pub const BG_WIN:   u32 = 0xFF141A26;
    pub const BG_PANEL: u32 = 0xFF1B2333;
    pub const BG_TERM:  u32 = 0xFF0A0E15;
    pub const BORDER:   u32 = 0xFF2A3446;
    pub const BORDER_H: u32 = 0xFF3F4D66;
    pub const TEXT:     u32 = 0xFFE6EDF3;
    pub const TEXT_DIM: u32 = 0xFF8A96A8;
    pub const TEXT_OK:  u32 = 0xFF22C55E;
    pub const TEXT_ERR: u32 = 0xFFEF4444;
    pub const ACCENT:   u32 = 0xFF3B82F6;
    pub const ACCENT_H: u32 = 0xFF60A5FA;
    pub const RED_H:    u32 = 0xFFEF4444;
    pub const SEL_BG:   u32 = 0x553B82F6;
    pub const SHADOW:   u32 = 0x60000000;
    pub const CORNER_R: i32 = 4;
    pub const DOCK_H:   i32 = 56;
    pub const DOCK_ICON:i32 = 34;
    pub const DOCK_GAP: i32 = 10;
    pub const DOCK_PAD: i32 = 14;
    pub const CC_TILE:  i32 = 64;
    pub const CC_GAP:   i32 = 8;
    pub const CC_PAD:   i32 = 14;
    pub const TITLE_H:  i32 = 26;
    pub const CLOSE_SZ: i32 = 16;
}

#[inline]
fn isqrt(n: i32) -> i32 {
    if n <= 0 { return 0; }
    let mut x = n;
    let mut y = (x + 1) / 2;
    while y < x { x = y; y = (x + n / x) / 2; }
    x
}

#[inline]
fn lerp8(d: u32, s: u32, a: u32) -> u32 { (d * (255 - a) + s * a) / 255 }

const CORNER_TL: [[bool; 4]; 4] = [
    [false, false, false, true],
    [false, true,  true,  true],
    [false, true,  true,  true],
    [true,  true,  true,  true],
];

// ============================================================
// Surface
// ============================================================
pub struct Surface<'a> {
    pub px: &'a mut [u32],
    pub w:  i32,
    pub h:  i32,
}
impl<'a> Surface<'a> {
    pub fn new(px: &'a mut [u32], w: i32) -> Self {
        let h = if w > 0 { (px.len() as i32) / w } else { 0 };
        Self { px, w, h }
    }
    #[inline]
    fn idx(&self, x: i32, y: i32) -> Option<usize> {
        if x < 0 || y < 0 || x >= self.w || y >= self.h { None }
        else { Some((y as usize) * (self.w as usize) + (x as usize)) }
    }
    #[inline]
    pub fn put(&mut self, x: i32, y: i32, c: u32) {
        if let Some(i) = self.idx(x, y) { self.px[i] = c; }
    }
    pub fn hline(&mut self, x0: i32, x1: i32, y: i32, c: u32) {
        if y < 0 || y >= self.h { return; }
        let (a, b) = if x0 <= x1 { (x0, x1) } else { (x1, x0) };
        let a = max(a, 0);
        let b = min(b, self.w - 1);
        let row = (y as usize) * (self.w as usize);
        for x in a..=b { self.px[row + x as usize] = c; }
    }
    pub fn vline(&mut self, x: i32, y0: i32, y1: i32, c: u32) {
        if x < 0 || x >= self.w { return; }
        let (a, b) = if y0 <= y1 { (y0, y1) } else { (y1, y0) };
        let a = max(a, 0);
        let b = min(b, self.h - 1);
        for y in a..=b { self.px[(y as usize) * (self.w as usize) + x as usize] = c; }
    }
    pub fn rect(&mut self, x: i32, y: i32, w: i32, h: i32, c: u32) {
        if w <= 0 || h <= 0 { return; }
        self.hline(x, x + w - 1, y, c);
        self.hline(x, x + w - 1, y + h - 1, c);
        self.vline(x, y, y + h - 1, c);
        self.vline(x + w - 1, y, y + h - 1, c);
    }
    pub fn rect_fill(&mut self, x: i32, y: i32, w: i32, h: i32, c: u32) {
        if w <= 0 || h <= 0 { return; }
        for yy in y..(y + h) { self.hline(x, x + w - 1, yy, c); }
    }
    pub fn line(&mut self, x0: i32, y0: i32, x1: i32, y1: i32, c: u32) {
        let dx = (x1 - x0).abs();
        let dy = -(y1 - y0).abs();
        let sx = if x0 < x1 { 1 } else { -1 };
        let sy = if y0 < y1 { 1 } else { -1 };
        let mut err = dx + dy;
        let (mut x, mut y) = (x0, y0);
        loop {
            self.put(x, y, c);
            if x == x1 && y == y1 { break; }
            let e2 = 2 * err;
            if e2 >= dy { err += dy; x += sx; }
            if e2 <= dx { err += dx; y += sy; }
        }
    }
    pub fn circle(&mut self, cx: i32, cy: i32, r: i32, c: u32) {
        if r <= 0 { return; }
        let mut x = 0; let mut y = r; let mut d = 1 - r;
        while x <= y {
            self.put(cx+x, cy+y, c); self.put(cx-x, cy+y, c);
            self.put(cx+x, cy-y, c); self.put(cx-x, cy-y, c);
            self.put(cx+y, cy+x, c); self.put(cx-y, cy+x, c);
            self.put(cx+y, cy-x, c); self.put(cx-y, cy-x, c);
            if d < 0 { d += 2*x + 3; } else { d += 2*(x - y) + 5; y -= 1; }
            x += 1;
        }
    }
    pub fn circle_fill(&mut self, cx: i32, cy: i32, r: i32, c: u32) {
        if r <= 0 { return; }
        let r2 = r * r;
        for dy in -r..=r {
            let y2 = dy * dy;
            if y2 > r2 { continue; }
            let dx = isqrt(r2 - y2);
            self.hline(cx - dx, cx + dx, cy + dy, c);
        }
    }
    pub fn panel_fill(&mut self, x: i32, y: i32, w: i32, h: i32, c: u32) {
        let r = theme::CORNER_R;
        if h <= r { self.rect_fill(x, y, w, h, c); return; }
        self.rect_fill(x, y + r, w, h - r, c);
        for dy in 0..r {
            for dx in 0..w {
                let inside = if dx < r { CORNER_TL[dy as usize][dx as usize] }
                             else if dx >= w - r { CORNER_TL[dy as usize][(w-1-dx) as usize] }
                             else { true };
                if inside { self.put(x + dx, y + dy, c); }
            }
        }
    }
    pub fn panel_border(&mut self, x: i32, y: i32, w: i32, h: i32, c: u32) {
        let r = theme::CORNER_R;
        self.hline(x + r, x + w - 1 - r, y, c);
        self.hline(x, x + w - 1, y + h - 1, c);
        self.vline(x, y + r, y + h - 1, c);
        self.vline(x + w - 1, y + r, y + h - 1, c);
        let arc: [(i32, i32); 4] = [(3, 0), (1, 1), (1, 2), (0, 3)];
        for &(dx, dy) in arc.iter() {
            if dx < r && dy < r {
                self.put(x + dx, y + dy, c);
                self.put(x + w - 1 - dx, y + dy, c);
            }
        }
    }
    pub fn panel(&mut self, x: i32, y: i32, w: i32, h: i32, bg: u32, bd: u32) {
        self.panel_fill(x, y, w, h, bg);
        self.panel_border(x, y, w, h, bd);
    }
    pub fn gradient_v(&mut self, y0: i32, y1: i32, top: u32, bot: u32) {
        let n = y1 - y0;
        if n <= 0 { return; }
        for y in y0..y1 {
            let t = ((y - y0) as u32) * 255 / (n as u32);
            let r = lerp8((top >> 16) & 0xFF, (bot >> 16) & 0xFF, t);
            let g = lerp8((top >>  8) & 0xFF, (bot >>  8) & 0xFF, t);
            let b = lerp8( top        & 0xFF,  bot        & 0xFF, t);
            let c = 0xFF00_0000 | (r << 16) | (g << 8) | b;
            self.hline(0, self.w - 1, y, c);
        }
    }
    pub fn text_cstr(&mut self, x: i32, y: i32, s: &[u8], argb: u32) {
        let mut buf = [0u8; 257];
        let n = min(s.len(), 256);
        buf[..n].copy_from_slice(&s[..n]);
        unsafe {
            munix_font_text(self.px.as_mut_ptr(), self.w, self.h,
                            x, y, buf.as_ptr(), argb);
        }
    }
}

// ============================================================
// Icons
// ============================================================
mod icon {
    use super::Surface;
    pub fn terminal(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let p = sz / 4;
        s.rect(x, y, sz, sz, c);
        s.line(x+p, y+p, x+2*p, y+sz/2, c);
        s.line(x+2*p, y+sz/2, x+p, y+sz-p, c);
        s.rect_fill(x + 2*p + 2, y + sz - p - 2, 4, 2, c);
    }
    pub fn files(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let tw = sz / 3; let th = sz / 5;
        s.rect(x, y + th, sz, sz - th, c);
        s.rect(x, y, tw, th + 1, c);
    }
    pub fn editor(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let l = x + sz / 6; let r = x + sz - sz / 6;
        s.rect(l, y, r - l, sz, c);
        for i in 1..=3 { s.hline(l + 3, r - 3, y + (sz * i) / 4, c); }
    }
    pub fn media(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let cx = x + sz / 2; let cy = y + sz / 2; let r = sz / 2 - 2;
        s.line(cx-r+3, cy-r, cx+r, cy, c);
        s.line(cx+r, cy, cx-r+3, cy+r, c);
        s.line(cx-r+3, cy+r, cx-r+3, cy-r, c);
    }
    pub fn settings(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let cx = x + sz/2; let cy = y + sz/2; let r = sz/4;
        s.circle(cx, cy, r, c);
        s.circle(cx, cy, r/2, c);
        let d: [(i32, i32); 8] = [(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1),(0,-1),(1,-1)];
        for &(dx, dy) in d.iter() {
            s.line(cx + dx*r, cy + dy*r, cx + dx*(r+3), cy + dy*(r+3), c);
        }
    }
    pub fn wifi(s: &mut Surface, cx: i32, cy: i32, c: u32) {
        s.circle(cx, cy, 4, c);
        s.circle(cx, cy, 8, c);
        s.circle(cx, cy, 12, c);
        s.rect_fill(cx - 13, cy + 1, 27, 14, 0);
        s.circle_fill(cx, cy, 2, c);
    }
    pub fn battery(s: &mut Surface, x: i32, y: i32, w: i32, h: i32, pct: u8, c: u32) {
        s.rect(x, y, w, h, c);
        s.rect_fill(x + w, y + 3, 2, h - 6, c);
        let fill = ((pct as i32) * (w - 4)) / 100;
        s.rect_fill(x + 2, y + 2, fill, h - 4, c);
    }
}

// ============================================================
// Language
// ============================================================
#[derive(Copy, Clone, PartialEq, Eq)]
pub enum Lang { En, Ru }

fn t(_lang: Lang, en: &'static [u8], _ru: &'static [u8]) -> &'static [u8] {
    // Cyrillic font not implemented — always English.
    en
}

// ============================================================
// Terminal
// ============================================================
const TERM_COLS: usize = 72;
const TERM_ROWS: usize = 18;

pub struct Terminal {
    lines: [[u8; TERM_COLS]; TERM_ROWS],
    count: usize,
    input: [u8; 96],
    input_len: usize,
}

impl Terminal {
    pub const fn new() -> Self {
        Self {
            lines: [[0u8; TERM_COLS]; TERM_ROWS],
            count: 0,
            input: [0u8; 96], input_len: 0,
        }
    }
    fn clear(&mut self) { self.count = 0; self.input_len = 0; }
    fn push_line_raw(&mut self, s: &[u8]) {
        if self.count >= TERM_ROWS {
            for i in 1..TERM_ROWS { self.lines[i-1] = self.lines[i]; }
            self.count = TERM_ROWS - 1;
        }
        let n = min(s.len(), TERM_COLS);
        for i in 0..n { self.lines[self.count][i] = s[i]; }
        for i in n..TERM_COLS { self.lines[self.count][i] = 0; }
        self.count += 1;
    }
    fn push_line(&mut self, s: &[u8]) {
        let mut start = 0; let mut i = 0;
        while i <= s.len() {
            if i == s.len() || s[i] == b'\n' {
                self.push_line_raw(&s[start..i]);
                start = i + 1;
            }
            i += 1;
        }
    }
    fn print(&mut self, s: &[u8]) { self.push_line(s); }
    fn print_num(&mut self, mut v: u32) {
        let mut buf = [0u8; 12]; let mut n = 0;
        if v == 0 { buf[n] = b'0'; n += 1; }
        else { while v > 0 && n < 11 { buf[n] = b'0' + (v % 10) as u8; v /= 10; n += 1; } }
        let mut out = [0u8; 12];
        for i in 0..n { out[i] = buf[n - 1 - i]; }
        self.push_line_raw(&out[..n]);
    }
    fn exec(&mut self, lang: Lang, editor: &mut Editor) {
        let mut echo = [0u8; 128]; let mut k = 0;
        for c in b"mUnix> " { echo[k] = *c; k += 1; }
        for i in 0..self.input_len { echo[k] = self.input[i]; k += 1; }
        self.push_line_raw(&echo[..k]);

        let mut cmd_buf = [0u8; 96];
        let mut arg_buf = [0u8; 96];
        let mut cmd_len = 0; let mut arg_len = 0;
        {
            let mut i = 0;
            while i < self.input_len && self.input[i] != b' ' {
                if cmd_len < 95 { cmd_buf[cmd_len] = self.input[i]; cmd_len += 1; }
                i += 1;
            }
            while i < self.input_len && self.input[i] == b' ' { i += 1; }
            while i < self.input_len {
                if arg_len < 95 { arg_buf[arg_len] = self.input[i]; arg_len += 1; }
                i += 1;
            }
        }
        self.input_len = 0;
        let cmd: &[u8] = &cmd_buf[..cmd_len];
        let args: &[u8] = &arg_buf[..arg_len];
        if cmd.is_empty() { return; }

        if eq(cmd, b"help") {
            self.print(t(lang, b"Commands:", b"Commands:"));
            self.print(b"  help, ls, cat <f>, echo <t>");
            self.print(b"  clear, version, whoami, meminfo");
            self.print(b"  cpuid, lang en|ru, reboot");
        } else if eq(cmd, b"ls") {
            unsafe {
                let n = munix_ramfs_count();
                let mut i = 0;
                while i < 32 {
                    let mut name = [0u8; 32];
                    let mut isdir = 0; let mut sz = 0; let mut par = 0;
                    if munix_ramfs_get(i, name.as_mut_ptr(), 32, &mut isdir, &mut sz, &mut par) == 1 {
                        if par == -1 {
                            let nl = name.iter().position(|&c| c == 0).unwrap_or(32);
                            let mut line = [0u8; 64]; let mut kk = 0;
                            for j in 0..nl { line[kk] = name[j]; kk += 1; }
                            if isdir != 0 { line[kk] = b'/'; kk += 1; }
                            else {
                                while kk < 20 { line[kk] = b' '; kk += 1; }
                                let mut v = sz as u32;
                                let mut dig = [0u8; 12]; let mut dn = 0;
                                if v == 0 { dig[dn] = b'0'; dn += 1; }
                                else { while v > 0 && dn < 11 { dig[dn] = b'0' + (v % 10) as u8; v /= 10; dn += 1; } }
                                for j in 0..dn { line[kk] = dig[dn - 1 - j]; kk += 1; }
                                line[kk] = b'b'; kk += 1;
                            }
                            self.push_line_raw(&line[..kk]);
                        }
                    }
                    i += 1;
                    if i >= n { break; }
                }
            }
        } else if eq(cmd, b"cat") {
            if args.is_empty() { self.print(b"cat: missing operand"); return; }
            let mut name = [0u8; 32];
            let n = min(args.len(), 31);
            for j in 0..n { name[j] = args[j]; }
            let mut out = [0u8; 256];
            let rc = unsafe {
                munix_ramfs_read(name.as_ptr(), munix_ramfs_root(), out.as_mut_ptr(), 256)
            };
            if rc < 0 { self.print(b"cat: file not found"); }
            else {
                let len = out.iter().position(|&c| c == 0).unwrap_or(256);
                self.push_line(&out[..len]);
            }
        } else if eq(cmd, b"echo") {
            let mut tmp = [0u8; 96];
            let n = if args.len() > 95 { 95 } else { args.len() };
            for j in 0..n { tmp[j] = args[j]; }
            self.push_line(&tmp[..n]);
        } else if eq(cmd, b"clear") {
            self.clear();
        } else if eq(cmd, b"version") {
            self.print(b"mUnix-kernel v0.8.0");
            self.print(b"  Arch   : x86 (i686, PM)");
            self.print(b"  GUI    : Rust (no_std, ARGB)");
            self.print(b"  Video  : Bochs VBE, 32-bit LFB");
        } else if eq(cmd, b"whoami") {
            self.print(b"root");
        } else if eq(cmd, b"meminfo") {
            unsafe {
                let used = munix_ramfs_pool_used();
                let total = munix_ramfs_pool_size();
                self.print(b"RAMFS pool:");
                self.print(b"  used : ");
                self.print_num(used as u32);
                self.print(b"  total: ");
                self.print_num(total as u32);
            }
        } else if eq(cmd, b"cpuid") {
            self.print(b"CPUID:");
            self.print(b"  Vendor: GenuineIntel");
            self.print(b"  Family: i686");
        } else if eq(cmd, b"lang") {
            if eq(args, b"ru") { self.print(b"language: ru"); }
            else if eq(args, b"en") { self.print(b"language: en"); }
            else { self.print(b"usage: lang en | lang ru"); }
        } else if eq(cmd, b"edit") {
            if !args.is_empty() {
                let mut name = [0u8; 32];
                let n = min(args.len(), 31);
                for j in 0..n { name[j] = args[j]; }
                editor.load(&name[..n]);
            }
        } else if eq(cmd, b"write") {
            let mut sp = 0;
            while sp < args.len() && args[sp] != b' ' { sp += 1; }
            if sp == 0 { self.print(b"write: missing filename"); return; }
            if sp >= args.len() { self.print(b"write: missing text"); return; }
            let mut name = [0u8; 32];
            let nl = if sp > 31 { 31 } else { sp };
            for j in 0..nl { name[j] = args[j]; }
            name[nl] = 0;
            let mut text = [0u8; 256];
            let mut j = sp;
            while j < args.len() && args[j] == b' ' { j += 1; }
            let mut tl = 0;
            while j < args.len() && tl < 255 {
                text[tl] = args[j]; tl += 1; j += 1;
            }
            unsafe {
                if munix_ramfs_lookup(name.as_ptr(), munix_ramfs_root()) < 0 {
                    munix_ramfs_create(name.as_ptr(), munix_ramfs_root(), 0);
                }
                munix_ramfs_write(name.as_ptr(), munix_ramfs_root(),
                                  text.as_ptr(), tl as i32);
            }
            self.print(b"written");
        } else if eq(cmd, b"touch") {
            if args.is_empty() { self.print(b"touch: missing name"); return; }
            let mut name = [0u8; 32];
            let n = if args.len() > 31 { 31 } else { args.len() };
            for j in 0..n { name[j] = args[j]; }
            unsafe { munix_ramfs_create(name.as_ptr(), munix_ramfs_root(), 0); }
            self.print(b"created");
        } else if eq(cmd, b"mkdir") {
            if args.is_empty() { self.print(b"mkdir: missing name"); return; }
            let mut name = [0u8; 32];
            let n = if args.len() > 31 { 31 } else { args.len() };
            for j in 0..n { name[j] = args[j]; }
            unsafe { munix_ramfs_create(name.as_ptr(), munix_ramfs_root(), 1); }
            self.print(b"directory created");
        } else if eq(cmd, b"rm") {
            if args.is_empty() { self.print(b"rm: missing name"); return; }
            let mut name = [0u8; 32];
            let n = if args.len() > 31 { 31 } else { args.len() };
            for j in 0..n { name[j] = args[j]; }
            unsafe { munix_ramfs_remove(name.as_ptr(), munix_ramfs_root()); }
            self.print(b"removed");
        } else if eq(cmd, b"pwd") {
            self.print(b"/");
        } else if eq(cmd, b"about") {
            self.print(b"mUnix -- minimal UNIX-like OS");
            self.print(b"C + Rust, no_std, live in RAM");
        } else if eq(cmd, b"reboot") {
            unsafe { munix_system_reboot(); }
        } else {
            let mut line = [0u8; 64]; let mut kk = 0;
            for c in b"unknown: " { line[kk] = *c; kk += 1; }
            let n = min(cmd.len(), 40);
            for j in 0..n { line[kk] = cmd[j]; kk += 1; }
            self.push_line_raw(&line[..kk]);
        }
    }
    fn handle_key(&mut self, k: i32, lang: Lang, editor: &mut Editor) -> bool {
        if k >= 32 && k < 127 {
            if self.input_len < 95 { self.input[self.input_len] = k as u8; self.input_len += 1; }
            return true;
        }
        if k == 8 { if self.input_len > 0 { self.input_len -= 1; } return true; }
        if k == 10 || k == 13 { self.exec(lang, editor); return true; }
        false
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, w: i32, h: i32) {
        s.rect_fill(x, y, w, h, theme::BG_TERM);
        let line_h = 12; let pad = 6;
        let mut yy = y + pad;
        for i in 0..self.count {
            let line = &self.lines[i];
            let len = line.iter().position(|&c| c == 0).unwrap_or(TERM_COLS);
            let color = if len >= 6 && &line[..6] == b"mUnix>" { theme::TEXT_OK } else { theme::TEXT };
            s.text_cstr(x + pad, yy, &line[..len], color);
            yy += line_h;
            if yy > y + h - 24 { break; }
        }
        let py = y + h - pad - 10;
        s.text_cstr(x + pad, py, b"mUnix> ", theme::TEXT_OK);
        let px = x + pad + 7 * 8;
        s.text_cstr(px, py, &self.input[..self.input_len], theme::TEXT);
        let cx = px + (self.input_len as i32) * 8;
        s.rect_fill(cx, py, 7, 9, theme::ACCENT_H);
    }
}

fn eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() { return false; }
    for i in 0..a.len() { if a[i] != b[i] { return false; } }
    true
}

// ============================================================
// Editor — real text editor
// ============================================================
const ED_COLS: usize = 60;
const ED_ROWS: usize = 14;

pub struct Editor {
    name: [u8; 32],
    name_len: usize,
    buf: [[u8; ED_COLS]; ED_ROWS],
    cx: i32, cy: i32,
    dirty: bool,
    has_file: bool,
}

impl Editor {
    pub const fn new() -> Self {
        Self {
            name: [0u8; 32], name_len: 0,
            buf: [[b' '; ED_COLS]; ED_ROWS],
            cx: 0, cy: 0, dirty: false, has_file: false,
        }
    }
    pub fn load(&mut self, fname: &[u8]) {
        for y in 0..ED_ROWS { for x in 0..ED_COLS { self.buf[y][x] = b' '; } }
        self.name_len = min(fname.len(), 31);
        for i in 0..self.name_len { self.name[i] = fname[i]; }
        for i in self.name_len..32 { self.name[i] = 0; }
        self.has_file = true;
        self.cx = 0; self.cy = 0;
        let mut out = [0u8; 1024];
        let mut nm = [0u8; 32];
        for i in 0..self.name_len { nm[i] = self.name[i]; }
        unsafe {
            let rc = munix_ramfs_read(nm.as_ptr(), munix_ramfs_root(),
                                      out.as_mut_ptr(), 1024);
            if rc > 0 {
                let mut x = 0usize; let mut y = 0usize;
                for i in 0..(rc as usize) {
                    let c = out[i];
                    if c == 0 { break; }
                    if c == b'\n' { y += 1; x = 0; if y >= ED_ROWS { break; } continue; }
                    if x >= ED_COLS { y += 1; x = 0; if y >= ED_ROWS { break; } }
                    self.buf[y][x] = c;
                    x += 1;
                }
            }
        }
        self.dirty = false;
    }
    pub fn save(&mut self) {
        if !self.has_file { return; }
        let mut out = [0u8; 1024]; let mut n = 0;
        for y in 0..ED_ROWS {
            let mut last = 0;
            for x in 0..ED_COLS { if self.buf[y][x] != b' ' { last = x + 1; } }
            for x in 0..last {
                if n < 1023 { out[n] = self.buf[y][x]; n += 1; }
            }
            if n < 1023 { out[n] = b'\n'; n += 1; }
        }
        let mut nm = [0u8; 32];
        for i in 0..self.name_len { nm[i] = self.name[i]; }
        unsafe {
            munix_ramfs_write(nm.as_ptr(), munix_ramfs_root(), out.as_ptr(), n as i32);
        }
        self.dirty = false;
    }
    fn handle_key(&mut self, k: i32) {
        if k >= 32 && k < 127 {
            if (self.cx as usize) < ED_COLS {
                self.buf[self.cy as usize][self.cx as usize] = k as u8;
                self.cx += 1;
                self.dirty = true;
            }
        } else if k == 8 {
            if self.cx > 0 {
                self.cx -= 1;
                self.buf[self.cy as usize][self.cx as usize] = b' ';
                self.dirty = true;
            }
        } else if k == 10 || k == 13 {
            if (self.cy as usize) < ED_ROWS - 1 { self.cy += 1; self.cx = 0; }
        } else if k == 0x100 {
            if self.cy > 0 { self.cy -= 1; }
        } else if k == 0x101 {
            if (self.cy as usize) < ED_ROWS - 1 { self.cy += 1; }
        } else if k == 0x102 {
            if self.cx > 0 { self.cx -= 1; }
        } else if k == 0x103 {
            if (self.cx as usize) < ED_COLS { self.cx += 1; }
        } else if k == 0x107 {  // F2 = save
            self.save();
        }
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, w: i32, h: i32) {
        s.rect_fill(x, y, w, h, theme::BG_TERM);
        let line_h = 12; let pad = 6;
        let start_y = y + pad;
        for row in 0..ED_ROWS {
            let yy = start_y + (row as i32) * line_h;
            if yy > y + h - 14 { break; }
            let line = &self.buf[row];
            let mut len = ED_COLS;
            while len > 0 && line[len - 1] == b' ' { len -= 1; }
            if len > 0 {
                s.text_cstr(x + pad + 24, yy, &line[..len], theme::TEXT);
            }
        }
        // cursor
        let cy = start_y + self.cy * line_h;
        let cx = x + pad + 24 + self.cx * 8;
        s.rect_fill(cx, cy, 7, 9, theme::ACCENT_H);
        // header
        let mut hdr = [0u8; 40]; let mut k = 0;
        for c in b"File: " { hdr[k] = *c; k += 1; }
        for i in 0..self.name_len { hdr[k] = self.name[i]; k += 1; }
        if self.dirty { hdr[k] = b'*'; k += 1; }
        s.text_cstr(x + pad, y + 2, &hdr[..k], theme::TEXT_DIM);
    }
}

// ============================================================
// Files — real browser
// ============================================================
pub struct Files {
    sel: i32,
    scroll: i32,
    count: i32,
}

impl Files {
    pub const fn new() -> Self { Self { sel: 0, scroll: 0, count: 0 } }
    fn refresh_count(&mut self) {
        self.count = 0;
        unsafe {
            let n = munix_ramfs_count();
            let mut i = 0;
            while i < 32 {
                let mut name = [0u8; 32];
                let mut isdir = 0; let mut sz = 0; let mut par = 0;
                if munix_ramfs_get(i, name.as_mut_ptr(), 32, &mut isdir, &mut sz, &mut par) == 1 {
                    if par == -1 { self.count += 1; }
                }
                i += 1;
                if i >= n { break; }
            }
        }
    }
    fn get_at(&self, idx: i32) -> Option<([u8; 32], usize, i32, i32)> {
        let mut i = 0;
        let mut found = 0;
        unsafe {
            while i < 32 {
                let mut name = [0u8; 32];
                let mut isdir = 0; let mut sz = 0; let mut par = 0;
                if munix_ramfs_get(i, name.as_mut_ptr(), 32, &mut isdir, &mut sz, &mut par) == 1 {
                    if par == -1 {
                        if found == idx {
                            let nl = name.iter().position(|&c| c == 0).unwrap_or(32);
                            return Some((name, nl, isdir, sz));
                        }
                        found += 1;
                    }
                }
                i += 1;
            }
        }
        None
    }
    fn handle_key(&mut self, k: i32, editor: &mut Editor) {
        self.refresh_count();
        if k == 0x100 { // up
            if self.sel > 0 { self.sel -= 1; }
            if self.sel < self.scroll { self.scroll = self.sel; }
        } else if k == 0x101 { // down
            if self.sel < self.count - 1 { self.sel += 1; }
            if self.sel >= self.scroll + 12 { self.scroll = self.sel - 11; }
        } else if k == 10 || k == 13 { // enter — open in editor
            if let Some((name, nl, isdir, _sz)) = self.get_at(self.sel) {
                if isdir == 0 {
                    editor.load(&name[..nl]);
                }
            }
        } else if k == b'd' as i32 || k == b'D' as i32 { // delete
            if let Some((name, nl, _isdir, _sz)) = self.get_at(self.sel) {
                let mut nm = [0u8; 32];
                for j in 0..nl { nm[j] = name[j]; }
                unsafe {
                    munix_ramfs_remove(nm.as_ptr(), munix_ramfs_root());
                }
                self.refresh_count();
                if self.sel >= self.count { self.sel = self.count - 1; if self.sel < 0 { self.sel = 0; } }
            }
        } else if k == b'n' as i32 || k == b'N' as i32 { // new file
            static NAME_SEQ: [&[u8]; 5] = [b"new.txt\0", b"new2.txt\0", b"new3.txt\0", b"new4.txt\0", b"new5.txt\0"];
            for nm in NAME_SEQ.iter() {
                unsafe {
                    let r = munix_ramfs_lookup(nm.as_ptr(), munix_ramfs_root());
                    if r < 0 {
                        munix_ramfs_create(nm.as_ptr(), munix_ramfs_root(), 0);
                        break;
                    }
                }
            }
            self.refresh_count();
        }
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, w: i32, h: i32) {
        s.rect_fill(x, y, w, h, theme::BG_TERM);
        let hdr = b"Name                  Size   [Enter=open D=del N=new]";
        s.text_cstr(x + 6, y + 2, hdr, theme::TEXT_DIM);
        let mut row = 0;
        let mut i = 0;
        unsafe {
            let n = munix_ramfs_count();
            while i < 32 {
                let mut name = [0u8; 32];
                let mut isdir = 0; let mut sz = 0; let mut par = 0;
                if munix_ramfs_get(i, name.as_mut_ptr(), 32, &mut isdir, &mut sz, &mut par) == 1 {
                    if par == -1 {
                        if row >= self.scroll && row < self.scroll + 12 {
                            let yy = y + 20 + (row - self.scroll) * 14;
                            let selected = row == self.sel;
                            if selected {
                                s.rect_fill(x + 4, yy - 2, w - 8, 13, theme::SEL_BG);
                            }
                            let color = if isdir != 0 { theme::ACCENT_H } else { theme::TEXT };
                            let nl = name.iter().position(|&c| c == 0).unwrap_or(32);
                            let mut line = [0u8; 64]; let mut kk = 0;
                            for j in 0..nl { line[kk] = name[j]; kk += 1; }
                            if isdir != 0 { line[kk] = b'/'; kk += 1; }
                            else {
                                while kk < 22 { line[kk] = b' '; kk += 1; }
                                let mut v = sz as u32;
                                let mut dig = [0u8; 12]; let mut dn = 0;
                                if v == 0 { dig[dn] = b'0'; dn += 1; }
                                else { while v > 0 && dn < 11 { dig[dn] = b'0' + (v % 10) as u8; v /= 10; dn += 1; } }
                                for j in 0..dn { line[kk] = dig[dn - 1 - j]; kk += 1; }
                                line[kk] = b'b'; kk += 1;
                            }
                            s.text_cstr(x + 8, yy, &line[..kk], color);
                        }
                        row += 1;
                    }
                }
                i += 1;
                if i >= n { break; }
            }
        }
    }
}

// ============================================================
// Media player — real PC speaker playback
// ============================================================
const MP_NOTES: usize = 8;
const MP_TRACKS: usize = 4;
const MP_MELODIES: [[u16; MP_NOTES]; MP_TRACKS] = [
    [392, 392, 392, 329, 493, 392, 329, 493],   // Imperial March (short)
    [262, 330, 392, 523, 392, 330, 262, 0],     // Chime
    [440, 494, 523, 494, 440, 392, 330, 294],   // Silent Night
    [659, 494, 523, 587, 523, 494, 440, 440],   // Tetris
];
const MP_NAMES: [&[u8]; MP_TRACKS] = [b"Imperial March", b"Chime", b"Silent Night", b"Tetris"];

pub struct Media {
    track: u8,
    playing: bool,
    note: u8,
    last_tick: u32,
}
impl Media {
    pub const fn new() -> Self {
        Self { track: 0, playing: false, note: 0, last_tick: 0 }
    }
    fn tick(&mut self) {
        if !self.playing { return; }
        let now = unsafe { munix_ticks() };
        if now - self.last_tick < 4 { return; }
        self.last_tick = now;
        let freq = MP_MELODIES[self.track as usize][self.note as usize];
        if freq == 0 {
            unsafe { munix_speaker_off(); }
        } else {
            unsafe { munix_speaker_set(freq as u32); }
        }
        self.note = (self.note + 1) % MP_NOTES as u8;
    }
    fn stop(&mut self) {
        self.playing = false;
        unsafe { munix_speaker_off(); }
    }
    fn handle_key(&mut self, k: i32) {
        if k == b' ' as i32 {
            self.playing = !self.playing;
            if !self.playing { unsafe { munix_speaker_off(); } }
            else { self.note = 0; self.last_tick = unsafe { munix_ticks() }; }
        } else if k == b'n' as i32 || k == b'N' as i32 {
            self.track = (self.track + 1) % MP_TRACKS as u8;
            self.note = 0;
        } else if k == b'p' as i32 || k == b'P' as i32 {
            self.track = (self.track + MP_TRACKS as u8 - 1) % MP_TRACKS as u8;
            self.note = 0;
        }
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, w: i32, h: i32) {
        s.rect_fill(x, y, w, h, theme::BG_TERM);
        s.text_cstr(x + 8, y + 8, b"Now playing:", theme::TEXT_DIM);
        s.text_cstr(x + 8, y + 24, MP_NAMES[self.track as usize], theme::ACCENT_H);

        let state = if self.playing { b"PLAYING" as &[u8] } else { b"PAUSED" as &[u8] };
        s.text_cstr(x + 8, y + 44, state, if self.playing { theme::TEXT_OK } else { theme::TEXT_DIM });

        // progress
        let bx = x + 8; let by = y + 64; let bw = w - 16;
        s.rect(bx, by, bw, 10, theme::BORDER);
        let fill = ((self.note as i32) * (bw - 4)) / MP_NOTES as i32;
        s.rect_fill(bx + 2, by + 2, fill, 6, theme::ACCENT_H);

        // controls
        s.text_cstr(x + 8, y + 90, b"SPACE: play/pause  N/P: next/prev", theme::TEXT_DIM);

        // track list
        let mut yy = y + 120;
        for i in 0..MP_TRACKS {
            if i as u8 == self.track {
                s.text_cstr(x + 8, yy, b"> ", theme::TEXT_OK);
            }
            s.text_cstr(x + 24, yy, MP_NAMES[i], theme::TEXT);
            yy += 14;
        }
    }
}

// ============================================================
// Settings — working language toggle
// ============================================================
pub struct Settings {
    sel: u8,
}
impl Settings {
    pub const fn new() -> Self { Self { sel: 0 } }
    fn handle_key(&mut self, k: i32, lang: &mut Lang) {
        if k == 0x100 { if self.sel > 0 { self.sel -= 1; } }
        else if k == 0x101 { if self.sel < 2 { self.sel += 1; } }
        else if k == 10 || k == 13 || k == b' ' as i32 {
            if self.sel == 0 {
                *lang = if *lang == Lang::En { Lang::Ru } else { Lang::En };
            }
        }
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, w: i32, h: i32, lang: Lang) {
        s.rect_fill(x, y, w, h, theme::BG_TERM);
        let hdr = t(lang, b"Settings", b"Settings");
        s.text_cstr(x + 8, y + 8, hdr, theme::TEXT_DIM);
        let mut yy = y + 32;
        // row 1: language
        if self.sel == 0 { s.rect_fill(x + 6, yy - 2, w - 12, 14, theme::SEL_BG); }
        let l_en = t(lang, b"Language ......... EN", b"Language ......... EN");
        s.text_cstr(x + 10, yy, l_en, theme::TEXT);
        yy += 20;
        let cur = match lang { Lang::En => b"English" as &[u8], Lang::Ru => b"Russian" as &[u8] };
        s.text_cstr(x + 24, yy, cur, theme::ACCENT_H);
        yy += 24;
        // row 2: theme
        if self.sel == 1 { s.rect_fill(x + 6, yy - 2, w - 12, 14, theme::SEL_BG); }
        let th = t(lang, b"Theme ............ Dark", b"Theme ............ Dark");
        s.text_cstr(x + 10, yy, th, theme::TEXT);
        yy += 24;
        // row 3: about
        if self.sel == 2 { s.rect_fill(x + 6, yy - 2, w - 12, 14, theme::SEL_BG); }
        let ab = t(lang, b"About ............ mUnix v0.8.0", b"About ............ mUnix v0.8.0");
        s.text_cstr(x + 10, yy, ab, theme::TEXT);
        yy += 28;
        let hint = t(lang, b"Enter/Space: activate", b"Enter/Space: activate");
        s.text_cstr(x + 8, yy, hint, theme::TEXT_DIM);
    }
}

// ============================================================
// Window
// ============================================================
#[derive(Copy, Clone, PartialEq, Eq)]
pub enum Kind { Terminal, Files, Editor, Media, Settings }

impl Kind {
    fn title(self, lang: Lang) -> &'static [u8] {
        match self {
            Kind::Terminal => t(lang, b"Terminal",     b"Terminal"),
            Kind::Files    => t(lang, b"Files",        b"Files"),
            Kind::Editor   => t(lang, b"Text Editor",  b"Text Editor"),
            Kind::Media    => t(lang, b"Media Player", b"Media Player"),
            Kind::Settings => t(lang, b"Settings",     b"Settings"),
        }
    }
}

#[derive(Copy, Clone)]
pub struct Window {
    pub x: i32, pub y: i32, pub w: i32, pub h: i32,
    pub kind: Kind,
    pub visible: bool,
}
impl Window {
    pub const fn empty() -> Self {
        Self { x: 0, y: 0, w: 0, h: 0, kind: Kind::Terminal, visible: false }
    }
    fn close_x(&self) -> i32 { self.x + self.w - theme::CLOSE_SZ - 4 }
    fn close_y(&self) -> i32 { self.y + 5 }
    fn in_close(&self, mx: i32, my: i32) -> bool {
        let bx = self.close_x(); let by = self.close_y();
        mx >= bx && mx < bx + theme::CLOSE_SZ && my >= by && my < by + theme::CLOSE_SZ
    }
    fn in_title(&self, mx: i32, my: i32) -> bool {
        mx >= self.x && mx < self.x + self.w - theme::CLOSE_SZ - 8 &&
        my >= self.y && my < self.y + theme::TITLE_H
    }
    fn in_body(&self, mx: i32, my: i32) -> bool {
        mx >= self.x && mx < self.x + self.w && my >= self.y && my < self.y + self.h
    }
}

const MAX_WIN: usize = 6;

pub struct Gui {
    show_welcome: bool,
    wins: [Window; MAX_WIN],
    nwin: usize,
    focused: i32,

    lang: Lang,

    term: Terminal,
    editor: Editor,
    files: Files,
    media: Media,
    settings: Settings,

    cc_state: [bool; 8],

    mouse_x: i32, mouse_y: i32,
    mouse_down: bool,
    drag_win: i32, drag_off_x: i32, drag_off_y: i32,

    last_w: i32, last_h: i32,
}

impl Gui {
    pub const fn new() -> Self {
        Self {
            show_welcome: true,
            wins: [Window::empty(); MAX_WIN],
            nwin: 0, focused: -1,
            lang: Lang::En,
            term: Terminal::new(),
            editor: Editor::new(),
            files: Files::new(),
            media: Media::new(),
            settings: Settings::new(),
            cc_state: [true, false, true, true, true, true, true, true],
            mouse_x: 0, mouse_y: 0,
            mouse_down: false,
            drag_win: -1, drag_off_x: 0, drag_off_y: 0,
            last_w: 0, last_h: 0,
        }
    }

    fn find_kind(&self, k: Kind) -> i32 {
        for i in 0..self.nwin {
            if self.wins[i].visible && self.wins[i].kind == k { return i as i32; }
        }
        -1
    }

    fn bring_to_front(&mut self, idx: usize) {
        if idx + 1 >= self.nwin { self.focused = idx as i32; return; }
        let w = self.wins[idx];
        for i in idx..self.nwin-1 { self.wins[i] = self.wins[i+1]; }
        self.wins[self.nwin-1] = w;
        self.focused = (self.nwin - 1) as i32;
    }

    fn open(&mut self, k: Kind) {
        let existing = self.find_kind(k);
        if existing >= 0 { self.bring_to_front(existing as usize); return; }
        if self.nwin >= MAX_WIN {
            for i in 0..self.nwin-1 { self.wins[i] = self.wins[i+1]; }
            self.nwin -= 1;
        }
        let idx = self.nwin;
        let c = idx as i32 * 30;
        self.wins[idx] = Window {
            x: 60 + c, y: 60 + c,
            w: 640, h: 400,
            kind: k, visible: true,
        };
        self.nwin += 1;
        self.focused = idx as i32;
    }

    fn close(&mut self, idx: usize) {
        if idx >= self.nwin { return; }
        self.wins[idx].visible = false;
        for i in idx..self.nwin-1 { self.wins[i] = self.wins[i+1]; }
        self.nwin -= 1;
        if self.focused as usize >= self.nwin { self.focused = self.nwin as i32 - 1; }
    }

    fn on_click(&mut self, x: i32, y: i32, buttons: i32) {
        if self.show_welcome {
            self.show_welcome = false;
            return;
        }
        let down = (buttons & 1) != 0;
        if down && !self.mouse_down {
            self.mouse_down = true;
            self.drag_win = -1;

            // Dock
            let d = self.hit_dock(x, y);
            if d >= 0 {
                let k = match d {
                    0 => Kind::Terminal, 1 => Kind::Files, 2 => Kind::Editor,
                    3 => Kind::Media,    4 => Kind::Settings, _ => return,
                };
                self.open(k);
                return;
            }

            // CC tiles
            let ct = self.hit_cc(x, y);
            if ct >= 0 {
                self.cc_state[ct as usize] = !self.cc_state[ct as usize];
                return;
            }

            // Windows (сверху вниз)
            let mut i = self.nwin as i32 - 1;
            while i >= 0 {
                let idx = i as usize;
                if !self.wins[idx].visible { i -= 1; continue; }
                let w = self.wins[idx];
                if w.in_body(x, y) {
                    self.bring_to_front(idx);
                    let ni = self.focused as usize;
                    if self.wins[ni].in_close(x, y) { self.close(ni); return; }
                    if self.wins[ni].in_title(x, y) {
                        self.drag_win = ni as i32;
                        self.drag_off_x = x - self.wins[ni].x;
                        self.drag_off_y = y - self.wins[ni].y;
                        return;
                    }
                    return;
                }
                i -= 1;
            }
            self.focused = -1;
        } else if !down && self.mouse_down {
            self.mouse_down = false;
            self.drag_win = -1;
        }
    }

    fn hit_dock(&self, x: i32, y: i32) -> i32 {
        let items = 5;
        let total_w = items * theme::DOCK_ICON + (items - 1) * theme::DOCK_GAP + theme::DOCK_PAD * 2;
        let dx = (self.last_w - total_w) / 2;
        let dy = self.last_h - theme::DOCK_H - 14;
        if y < dy || y >= dy + theme::DOCK_H { return -1; }
        if x < dx || x >= dx + total_w { return -1; }
        for i in 0..items {
            let ix = dx + theme::DOCK_PAD + i * (theme::DOCK_ICON + theme::DOCK_GAP);
            if x >= ix && x < ix + theme::DOCK_ICON { return i; }
        }
        -1
    }

    fn hit_cc(&self, x: i32, y: i32) -> i32 {
        let cols = 2; let rows = 4;
        let t = theme::CC_TILE; let g = theme::CC_GAP; let pad = theme::CC_PAD;
        let total_w = cols * t + (cols - 1) * g + pad * 2;
        let total_h = rows * t + (rows - 1) * g + pad * 2;
        let dx = self.last_w - total_w - 16;
        let dy = 16;
        if x < dx || y < dy || x >= dx + total_w || y >= dy + total_h { return -1; }
        let rel_x = x - dx - pad;
        let rel_y = y - dy - pad;
        if rel_x < 0 || rel_y < 0 { return -1; }
        let col = rel_x / (t + g);
        let row = rel_y / (t + g);
        let in_x = rel_x % (t + g);
        let in_y = rel_y % (t + g);
        if in_x >= t || in_y >= t { return -1; }
        let idx = row * cols + col;
        if idx < 0 || idx >= 8 { return -1; }
        idx
    }

    fn render(&mut self, s: &mut Surface) {
        s.gradient_v(0, s.h, theme::BG_TOP, theme::BG_BOT);
        {
            let rx = s.w - 220;
            let ry = s.h - 240;
            if rx > 100 && ry > 100 { draw_raccoon(s, rx, ry); }
        }
        for i in 0..self.nwin { self.render_win(s, i); }
        self.render_cc(s);
        self.render_dock(s);

        if self.show_welcome {
            draw_welcome_overlay(s, self.mouse_x, self.mouse_y);
        }

        render_cursor(s, self.mouse_x, self.mouse_y);
    }

    fn render_win(&mut self, s: &mut Surface, idx: usize) {
        let focused = self.focused == idx as i32;
        let w = self.wins[idx];
        if !w.visible { return; }

        s.panel_fill(w.x + 4, w.y + 4, w.w, w.h, theme::SHADOW);
        let border = if focused { theme::ACCENT_H } else { theme::BORDER };
        s.panel(w.x, w.y, w.w, w.h, theme::BG_WIN, border);
        s.hline(w.x + 6, w.x + w.w - 7, w.y + theme::TITLE_H, theme::BORDER);
        s.text_cstr(w.x + 12, w.y + 9, w.kind.title(self.lang), theme::TEXT);

        // close button
        let bx = w.close_x();
        let by = w.close_y();
        let hover = self.mouse_x >= bx && self.mouse_x < bx + theme::CLOSE_SZ &&
                    self.mouse_y >= by && self.mouse_y < by + theme::CLOSE_SZ;
        if hover { s.rect_fill(bx, by, theme::CLOSE_SZ, theme::CLOSE_SZ, theme::RED_H); }
        let cc = if hover { 0xFFFFFFFF } else { theme::TEXT_DIM };
        let cx = bx + theme::CLOSE_SZ / 2;
        let cy = by + theme::CLOSE_SZ / 2;
        s.line(cx-4, cy-4, cx+4, cy+4, cc);
        s.line(cx-4, cy+4, cx+4, cy-4, cc);

        let cx0 = w.x + 8;
        let cy0 = w.y + theme::TITLE_H + 8;
        let cw = w.w - 16;
        let ch = w.h - theme::TITLE_H - 16;

        // To satisfy borrow checker we work in a small scope
        match w.kind {
            Kind::Terminal => { self.term.render(s, cx0, cy0, cw, ch); }
            Kind::Files    => { self.files.render(s, cx0, cy0, cw, ch); }
            Kind::Editor   => { self.editor.render(s, cx0, cy0, cw, ch); }
            Kind::Media    => { self.media.render(s, cx0, cy0, cw, ch); }
            Kind::Settings => {
                let lang = self.lang;
                self.settings.render(s, cx0, cy0, cw, ch, lang);
            }
        }
    }

    fn render_dock(&self, s: &mut Surface) {
        let items = 5;
        let total_w = items * theme::DOCK_ICON + (items - 1) * theme::DOCK_GAP + theme::DOCK_PAD * 2;
        let x = (s.w - total_w) / 2;
        let y = s.h - theme::DOCK_H - 14;
        s.panel_fill(x + 3, y + 3, total_w, theme::DOCK_H, theme::SHADOW);
        s.panel(x, y, total_w, theme::DOCK_H, theme::BG_PANEL, theme::BORDER);
        let hover = self.hit_dock(self.mouse_x, self.mouse_y);
        for i in 0..items {
            let ix = x + theme::DOCK_PAD + i * (theme::DOCK_ICON + theme::DOCK_GAP);
            let iy = y + (theme::DOCK_H - theme::DOCK_ICON) / 2;
            let ak = match i {
                0 => Kind::Terminal, 1 => Kind::Files, 2 => Kind::Editor,
                3 => Kind::Media,    4 => Kind::Settings, _ => Kind::Terminal,
            };
            let active = self.find_kind(ak) >= 0;
            if hover == i as i32 {
                s.rect_fill(ix - 4, iy - 4, theme::DOCK_ICON + 8, theme::DOCK_ICON + 8, 0x33FFFFFF);
            }
            if active {
                s.rect_fill(ix + theme::DOCK_ICON/2 - 2, y + theme::DOCK_H - 5, 4, 2, theme::ACCENT_H);
            }
            match i {
                0 => icon::terminal(s, ix, iy, theme::DOCK_ICON, theme::TEXT),
                1 => icon::files(s, ix, iy, theme::DOCK_ICON, theme::TEXT),
                2 => icon::editor(s, ix, iy, theme::DOCK_ICON, theme::TEXT),
                3 => icon::media(s, ix, iy, theme::DOCK_ICON, theme::TEXT),
                4 => icon::settings(s, ix, iy, theme::DOCK_ICON, theme::TEXT),
                _ => {}
            }
        }
    }

    fn render_cc(&self, s: &mut Surface) {
        let cols = 2; let rows = 4;
        let t = theme::CC_TILE; let g = theme::CC_GAP; let pad = theme::CC_PAD;
        let total_w = cols * t + (cols - 1) * g + pad * 2;
        let total_h = rows * t + (rows - 1) * g + pad * 2;
        let x = s.w - total_w - 16;
        let y = 16;
        s.panel_fill(x + 3, y + 3, total_w, total_h, theme::SHADOW);
        s.panel(x, y, total_w, total_h, theme::BG_PANEL, theme::BORDER);
        let hover = self.hit_cc(self.mouse_x, self.mouse_y);
        for i in 0..8 {
            let col = (i as i32) % cols;
            let row = (i as i32) / cols;
            let tx = x + pad + col * (t + g);
            let ty = y + pad + row * (t + g);
            let on = self.cc_state[i as usize];
            let bg = if on { theme::ACCENT } else { theme::BORDER };
            let fg = theme::TEXT;
            let cx = tx + t/2; let cy = ty + t/2;
            s.rect_fill(tx, ty, t, t, bg);
            let brd = if hover == i as i32 { theme::ACCENT_H } else { theme::BORDER_H };
            s.rect(tx, ty, t, t, brd);
            match i {
                0 => icon::wifi(s, cx, cy, fg),
                1 => {
                    s.line(cx-6, cy-10, cx-6, cy+10, fg);
                    s.line(cx-6, cy-10, cx+6, cy, fg);
                    s.line(cx+6, cy, cx-6, cy+10, fg);
                }
                2 => {
                    s.rect_fill(cx-10, cy-4, 6, 8, fg);
                    s.line(cx-4, cy-4, cx+5, cy-10, fg);
                    s.line(cx+5, cy-10, cx+5, cy+10, fg);
                    s.line(cx+5, cy+10, cx-4, cy+4, fg);
                    s.circle(cx+3, cy, 8, fg);
                }
                3 => {
                    s.circle(cx, cy, 6, fg);
                    let d: [(i32,i32); 8] = [(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1),(0,-1),(1,-1)];
                    for &(dx,dy) in d.iter() {
                        s.line(cx+dx*8, cy+dy*8, cx+dx*13, cy+dy*13, fg);
                    }
                }
                4 => icon::battery(s, cx-14, cy-7, 28, 14, 74, fg),
                5 => {
                    s.circle_fill(cx, cy, 10, fg);
                    s.rect_fill(cx, cy-10, 11, 20, bg);
                    s.circle(cx, cy, 10, fg);
                }
                6 => {
                    s.rect_fill(cx-9, cy-2, 18, 12, fg);
                    s.rect(cx-6, cy-10, 12, 10, fg);
                }
                7 => {
                    s.rect(cx-12, cy-9, 24, 16, fg);
                    s.rect_fill(cx-4, cy+8, 8, 2, fg);
                }
                _ => {}
            }
        }
    }
}


fn draw_raccoon(s: &mut Surface, cx: i32, cy: i32) {
    let tx = cx + 60; let ty = cy + 55;
    s.circle_fill(tx, ty, 22, 0xFF4A4A4A);
    s.circle_fill(tx + 20, ty + 5, 18, 0xFF6B6B6B);
    s.circle_fill(tx + 38, ty + 8, 14, 0xFF4A4A4A);
    s.rect_fill(tx - 4, ty - 18, 6, 6, 0xFF1A1A1A);
    s.rect_fill(tx + 12, ty - 14, 6, 6, 0xFF1A1A1A);
    s.rect_fill(tx + 28, ty - 8, 5, 6, 0xFF1A1A1A);
    s.rect_fill(tx - 10, ty + 16, 6, 6, 0xFF1A1A1A);
    s.rect_fill(tx + 8, ty + 18, 6, 6, 0xFF1A1A1A);
    s.rect_fill(tx + 26, ty + 14, 5, 6, 0xFF1A1A1A);
    s.circle_fill(cx, cy + 60, 48, 0xFF5A5A5A);
    s.circle_fill(cx, cy + 62, 42, 0xFF6E6E6E);
    s.circle_fill(cx, cy + 70, 26, 0xFFB0B0B0);
    s.circle_fill(cx - 30, cy + 78, 12, 0xFF3A3A3A);
    s.circle_fill(cx + 30, cy + 78, 12, 0xFF3A3A3A);
    s.circle_fill(cx, cy, 52, 0xFF8C8C8C);
    s.circle_fill(cx, cy - 2, 48, 0xFF9E9E9E);
    let mut i = 0;
    while i < 22 {
        let h = i / 2;
        s.hline(cx - 42 + h, cx - 16 - h, cy - 45 - i, 0xFF4A4A4A);
        s.hline(cx + 16 + h, cx + 42 - h, cy - 45 - i, 0xFF4A4A4A);
        i += 1;
    }
    i = 0;
    while i < 12 {
        let h = i / 2;
        s.hline(cx - 38 + h, cx - 22 - h, cy - 42 - i, 0xFFAA8888);
        s.hline(cx + 22 + h, cx + 38 - h, cy - 42 - i, 0xFFAA8888);
        i += 1;
    }
    s.circle_fill(cx, cy + 5, 36, 0xFFE8E8E8);
    s.circle_fill(cx - 18, cy - 4, 18, 0xFF1E1E1E);
    s.circle_fill(cx + 18, cy - 4, 18, 0xFF1E1E1E);
    s.rect_fill(cx - 4, cy - 16, 8, 20, 0xFFE8E8E8);
    s.circle_fill(cx - 18, cy - 4, 8, 0xFFFFFFFF);
    s.circle_fill(cx + 18, cy - 4, 8, 0xFFFFFFFF);
    s.circle_fill(cx - 18, cy - 4, 4, 0xFF000000);
    s.circle_fill(cx + 18, cy - 4, 4, 0xFF000000);
    s.put(cx - 16, cy - 6, 0xFFFFFFFF);
    s.put(cx - 15, cy - 7, 0xFFFFFFFF);
    s.put(cx + 20, cy - 6, 0xFFFFFFFF);
    s.put(cx + 21, cy - 7, 0xFFFFFFFF);
    s.circle_fill(cx, cy + 14, 6, 0xFF1E1E1E);
    s.put(cx - 1, cy + 12, 0xFFFFFFFF);
    s.line(cx, cy + 20, cx, cy + 26, 0xFF1E1E1E);
    s.line(cx, cy + 26, cx - 6, cy + 30, 0xFF1E1E1E);
    s.line(cx, cy + 26, cx + 6, cy + 30, 0xFF1E1E1E);
    s.line(cx - 20, cy + 20, cx - 44, cy + 16, 0xFFD0D0D0);
    s.line(cx - 20, cy + 24, cx - 46, cy + 26, 0xFFD0D0D0);
    s.line(cx + 20, cy + 20, cx + 44, cy + 16, 0xFFD0D0D0);
    s.line(cx + 20, cy + 24, cx + 46, cy + 26, 0xFFD0D0D0);
}


fn draw_welcome_overlay(s: &mut Surface, mx: i32, my: i32) {
    // Тёмный фон
    let w = 720;
    let h = 420;
    let x = (s.w - w) / 2;
    let y = (s.h - h) / 2;

    // Затемнение
    s.rect_fill(0, 0, s.w, s.h, 0xAA000000);

    // Панель
    s.panel_fill(x + 6, y + 6, w, h, 0x80000000);
    s.panel(x, y, w, h, theme::BG_PANEL, theme::ACCENT_H);

    // Заголовок
    s.rect_fill(x + 1, y + 1, w - 2, 36, theme::ACCENT);
    s.text_cstr(x + 20, y + 12, b"Welcome to mUnix!", 0xFFFFFFFF);

    // Close X
    let bx = x + w - 30;
    let by = y + 10;
    let hover = mx >= bx && mx < bx + 20 && my >= by && my < by + 20;
    if hover { s.rect_fill(bx, by, 20, 20, theme::RED_H); }
    let cc = if hover { 0xFFFFFFFF } else { 0xFFFFFFFF };
    s.line(bx + 4, by + 4, bx + 16, by + 16, cc);
    s.line(bx + 4, by + 16, bx + 16, by + 4, cc);

    // Текст
    let mut yy = y + 60;
    s.text_cstr(x + 30, yy, b"Your new open-source operating system.", theme::TEXT); yy += 24;
    s.text_cstr(x + 30, yy, b"Running entirely in RAM. No installation required.", theme::TEXT); yy += 20;
    s.text_cstr(x + 30, yy, b"Built with C and Rust. Boots on any x86 machine via QEMU.", theme::TEXT); yy += 36;

    s.text_cstr(x + 30, yy, b"Components:", theme::TEXT_DIM); yy += 24;
    s.text_cstr(x + 45, yy, b"Custom Multiboot bootloader", theme::TEXT); yy += 18;
    s.text_cstr(x + 45, yy, b"32-bit protected mode kernel (C)", theme::TEXT); yy += 18;
    s.text_cstr(x + 45, yy, b"Rust GUI with window manager, dock, control center", theme::TEXT); yy += 18;
    s.text_cstr(x + 45, yy, b"RAMFS with real file operations", theme::TEXT); yy += 18;
    s.text_cstr(x + 45, yy, b"Apps: Terminal, Files, Editor, Media Player, Settings", theme::TEXT); yy += 36;

    s.text_cstr(x + 30, yy, b"To learn more about mUnix and follow updates, visit:", theme::TEXT); yy += 24;
    s.text_cstr(x + 30, yy, b"https://github.com/davidchekushka-hue/mUnix-os", theme::ACCENT_H); yy += 40;

    s.text_cstr(x + 30, yy, b"Click anywhere or press any key to continue.", theme::TEXT_DIM);
}

fn render_cursor(s: &mut Surface, x: i32, y: i32) {
    s.line(x+1, y+1, x+1, y+16, theme::SHADOW);
    s.line(x+1, y+16, x+6, y+11, theme::SHADOW);
    s.line(x+6, y+11, x+8, y+17, theme::SHADOW);
    s.line(x+8, y+17, x+10, y+16, theme::SHADOW);
    s.line(x+10, y+16, x+7, y+10, theme::SHADOW);
    s.line(x+7, y+10, x+13, y+10, theme::SHADOW);
    s.line(x+13, y+10, x+1, y+1, theme::SHADOW);
    s.line(x, y, x, y+15, theme::TEXT);
    s.line(x, y+15, x+5, y+10, theme::TEXT);
    s.line(x+5, y+10, x+7, y+16, theme::TEXT);
    s.line(x+7, y+16, x+9, y+15, theme::TEXT);
    s.line(x+9, y+15, x+6, y+9, theme::TEXT);
    s.line(x+6, y+9, x+12, y+9, theme::TEXT);
    s.line(x+12, y+9, x, y, theme::TEXT);
}

// ============================================================
// Global + ABI
// ============================================================
static mut GUI: Gui = Gui::new();

#[inline]
unsafe fn gui() -> &'static mut Gui { &mut *core::ptr::addr_of_mut!(GUI) }

#[no_mangle]
pub unsafe extern "C" fn munix_gui_init_dock() {
    let g = gui();
    g.nwin = 0;
    g.focused = -1;
    g.drag_win = -1;
    g.mouse_down = false;
    g.lang = Lang::En;
    g.term = Terminal::new();
    g.editor = Editor::new();
    g.files = Files::new();
    g.media = Media::new();
    g.settings = Settings::new();
    g.term.print(b"mUnix v0.8.0 -- Rust GUI, all apps functional.");
    g.term.print(b"Type 'help' for a list of commands.");
    g.term.print(b"");
    g.term.print(b"Welcome to mUnix!");
    g.term.print(b"");
    g.term.print(b"This is your new open-source operating system.");
    g.term.print(b"Running entirely in RAM. No installation required.");
    g.term.print(b"Built with C and Rust. Boots on any x86 machine.");
    g.term.print(b"");
    g.term.print(b"Components:");
    g.term.print(b"  - Multiboot bootloader + 32-bit kernel (C)");
    g.term.print(b"  - Rust GUI: windows, dock, control center");
    g.term.print(b"  - RAMFS: real file operations");
    g.term.print(b"  - Apps: Terminal, Files, Editor, Media, Settings");
    g.term.print(b"");
    g.term.print(b"More info and updates at:");
    g.term.print(b"  https://github.com/davidchekushka-hue/mUnix-os");
    g.term.print(b"");
    g.term.print(b"Dock: Terminal / Files / Editor / Media / Settings");
}

#[no_mangle]
pub unsafe extern "C" fn munix_gui_open_window(kind: u32, _t: *const u8, _l: usize) {
    let g = gui();
    let k = match kind {
        1 => Kind::Terminal, 2 => Kind::Files, 3 => Kind::Editor,
        4 => Kind::Media,    5 => Kind::Settings, _ => Kind::Terminal,
    };
    g.open(k);
}

#[no_mangle]
pub unsafe extern "C" fn munix_gui_render(px: *mut u32, w: i32, h: i32, mx: i32, my: i32) {
    if px.is_null() || w <= 0 || h <= 0 { return; }
    let len = (w as usize) * (h as usize);
    let slice = core::slice::from_raw_parts_mut(px, len);
    let mut s = Surface::new(slice, w);
    let g = gui();
    g.last_w = w; g.last_h = h;

    if g.drag_win >= 0 && g.mouse_down {
        let idx = g.drag_win as usize;
        if idx < g.nwin {
            let mut nx = mx - g.drag_off_x;
            let mut ny = my - g.drag_off_y;
            if nx < 0 { nx = 0; }
            if ny < 0 { ny = 0; }
            if nx > w - 80 { nx = w - 80; }
            if ny > h - 40 { ny = h - 40; }
            g.wins[idx].x = nx;
            g.wins[idx].y = ny;
        }
    }

    g.mouse_x = mx;
    g.mouse_y = my;
    g.render(&mut s);
}

#[no_mangle]
pub unsafe extern "C" fn munix_gui_key(k: i32) {
    let g = gui();
    // always tick media (so pause/music continues)
    g.media.tick();

    let f = g.focused;
    if f < 0 || (f as usize) >= g.nwin { return; }
    let kind = g.wins[f as usize].kind;
    match kind {
        Kind::Terminal => {
            let lang = g.lang;
            // We need to pass editor mutably. Split borrow manually:
            let gptr = g as *mut Gui;
            let term = &mut (*gptr).term;
            let editor = &mut (*gptr).editor;
            term.handle_key(k, lang, editor);
        }
        Kind::Files => {
            let gptr = g as *mut Gui;
            let files = &mut (*gptr).files;
            let editor = &mut (*gptr).editor;
            files.handle_key(k, editor);
        }
        Kind::Editor => {
            g.editor.handle_key(k);
        }
        Kind::Media => {
            g.media.handle_key(k);
        }
        Kind::Settings => {
            let gptr = g as *mut Gui;
            let settings = &mut (*gptr).settings;
            let lang = &mut (*gptr).lang;
            settings.handle_key(k, lang);
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn munix_gui_click(x: i32, y: i32, buttons: i32) {
    let g = gui();
    g.on_click(x, y, buttons);
}

#[no_mangle]
pub unsafe extern "C" fn munix_gui_ticks(_t: u32) {
    let g = gui();
    g.media.tick();
}
