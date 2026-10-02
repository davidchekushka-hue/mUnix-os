//! mUnix v8.2 — Rust GUI. Blue title bars, colored icons, 5 games.

#![no_std]
#![allow(dead_code)]

use core::cmp::{max, min};
mod asm_lang;

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
    fn munix_exec_bytes(code: *const u8, len: i32) -> i32;
    fn munix_nettest_run() -> *const u8;
    fn munix_ping_run(ip: *const u8) -> *const u8;

    fn munix_http_get(host: *const u8, path: *const u8) -> i32;
    fn munix_http_buf() -> *const u8;
    fn munix_http_buf_len() -> u32;


    fn munix_sysmon_text() -> *const u8;

    fn munix_render_hit();
}

// ============================================================
// Theme
// ============================================================
mod theme {
    pub const BG_TOP:    u32 = 0xFF0E1B2A;
    pub const BG_BOT:    u32 = 0xFF060A12;
    pub const BG_WIN:    u32 = 0xFF141A26;
    pub const BG_PANEL:  u32 = 0xFF1B2333;
    pub const BG_TERM:   u32 = 0xFF0A0E15;
    pub const BORDER:    u32 = 0xFF2A3446;
    pub const BORDER_H:  u32 = 0xFF3F4D66;
    pub const TEXT:      u32 = 0xFFE6EDF3;
    pub const TEXT_DIM:  u32 = 0xFF8A96A8;
    pub const TEXT_OK:   u32 = 0xFF22C55E;
    pub const TEXT_ERR:  u32 = 0xFFEF4444;
    pub const ACCENT:    u32 = 0xFF2563EB;      // blue title bar
    pub const ACCENT_H:  u32 = 0xFF60A5FA;
    pub const TITLE_DIM: u32 = 0xFF1E3A5F;      // darker blue for unfocused
    pub const RED_H:     u32 = 0xFFEF4444;
    pub const SEL_BG:    u32 = 0x553B82F6;
    pub const SHADOW:    u32 = 0x60000000;
    pub const CORNER_R:  i32 = 4;
    pub const DOCK_H:    i32 = 56;
    pub const DOCK_ICON: i32 = 34;
    pub const DOCK_GAP:  i32 = 10;
    pub const DOCK_PAD:  i32 = 14;
    pub const TITLE_H:   i32 = 26;
    pub const CLOSE_SZ:  i32 = 16;
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
    #[inline] fn idx(&self, x: i32, y: i32) -> Option<usize> {
        if x < 0 || y < 0 || x >= self.w || y >= self.h { None }
        else { Some((y as usize) * (self.w as usize) + (x as usize)) }
    }
    #[inline] pub fn put(&mut self, x: i32, y: i32, c: u32) {
        if let Some(i) = self.idx(x, y) { self.px[i] = c; }
    }
    pub fn hline(&mut self, x0: i32, x1: i32, y: i32, c: u32) {
        if y < 0 || y >= self.h { return; }
        let (a, b) = if x0 <= x1 { (x0, x1) } else { (x1, x0) };
        let a = max(a, 0); let b = min(b, self.w - 1);
        let row = (y as usize) * (self.w as usize);
        for x in a..=b { self.px[row + x as usize] = c; }
    }
    pub fn vline(&mut self, x: i32, y0: i32, y1: i32, c: u32) {
        if x < 0 || x >= self.w { return; }
        let (a, b) = if y0 <= y1 { (y0, y1) } else { (y1, y0) };
        let a = max(a, 0); let b = min(b, self.h - 1);
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

    /// Panel with arbitrary corner radius.
    pub fn panel_round(&mut self, x: i32, y: i32, w: i32, h: i32, bg: u32, bd: u32, r: i32) {
        let r = if r < 1 { 1 } else if r > 20 { 20 } else { r };
        if r * 2 >= w || r * 2 >= h {
            self.panel(x, y, w, h, bg, bd);
            return;
        }
        let rr = (r - 1) * (r - 1);
        self.rect_fill(x, y + r, w, h - 2 * r, bg);
        let mut j: i32 = 0;
        while j < r {
            let dj = r - 1 - j;
            let dj2 = dj * dj;
            let mut left: i32 = 0;
            while left < r {
                let di = r - 1 - left;
                if di * di + dj2 <= rr { break; }
                left += 1;
            }
            self.hline(x + left, x + w - 1 - left, y + j, bg);
            j += 1;
        }
        j = 0;
        while j < r {
            let dj = r - 1 - j;
            let dj2 = dj * dj;
            let mut left: i32 = 0;
            while left < r {
                let di = r - 1 - left;
                if di * di + dj2 <= rr { break; }
                left += 1;
            }
            self.hline(x + left, x + w - 1 - left, y + h - 1 - j, bg);
            j += 1;
        }
        self.hline(x + r, x + w - 1 - r, y, bd);
        self.hline(x + r, x + w - 1 - r, y + h - 1, bd);
        self.vline(x, y + r, y + h - 1 - r, bd);
        self.vline(x + w - 1, y + r, y + h - 1 - r, bd);
        j = 0;
        while j < r {
            let dj = r - 1 - j;
            let dj2 = dj * dj;
            let mut left: i32 = 0;
            while left < r {
                let di = r - 1 - left;
                if di * di + dj2 <= rr { break; }
                left += 1;
            }
            if left < r {
                self.put(x + left, y + j, bd);
                self.put(x + w - 1 - left, y + j, bd);
                self.put(x + left, y + h - 1 - j, bd);
                self.put(x + w - 1 - left, y + h - 1 - j, bd);
            }
            j += 1;
        }
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
// Icons — per-app colors
// ============================================================
mod icon {
    use super::Surface;
    pub const C_TERM:   u32 = 0xFF22C55E;   // green
    pub const C_FILES:  u32 = 0xFFFACC15;   // yellow
    pub const C_EDIT:   u32 = 0xFF3B82F6;   // blue
    pub const C_MEDIA:  u32 = 0xFFEC4899;   // pink
    pub const C_SET:    u32 = 0xFF9CA3AF;   // gray
    pub const C_MINE:   u32 = 0xFFEF4444;   // red
    pub const C_SNAKE:  u32 = 0xFF10B981;   // emerald
    pub const C_PONG:   u32 = 0xFFEAB308;   // amber
    pub const C_TETRIS: u32 = 0xFF06B6D4;   // cyan

    pub fn asm_ide(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        // frame
        s.rect(x, y, sz, sz, c);
        s.rect(x + 2, y + 2, sz - 4, sz - 4, 0xFF0A0E15);
        // ">_" prompt
        s.line(x + 6, y + 8, x + 12, y + 14, c);
        s.line(x + 12, y + 14, x + 6, y + 20, c);
        s.rect_fill(x + 14, y + 18, 8, 2, c);
        // small text lines below
        s.hline(x + 6, x + sz - 8, y + 24, 0xFF4A5568);
        s.hline(x + 6, x + sz - 10, y + 27, 0xFF4A5568);
        // accent dot
        s.circle_fill(x + sz - 8, y + 8, 3, 0xFFFACC15);
    }

    pub fn terminal(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        s.rect_fill(x, y, sz, sz, c);
        s.rect(x, y, sz, sz, 0xFF000000);
        let p = sz / 4;
        s.line(x+p, y+p, x+2*p, y+sz/2, 0xFFFFFFFF);
        s.line(x+2*p, y+sz/2, x+p, y+sz-p, 0xFFFFFFFF);
        s.rect_fill(x + 2*p + 2, y + sz - p - 2, 4, 2, 0xFFFFFFFF);
    }
    pub fn files(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let tw = sz / 3; let th = sz / 5;
        s.rect_fill(x, y + th, sz, sz - th, c);
        s.rect(x, y + th, sz, sz - th, 0xFF000000);
        s.rect_fill(x, y, tw, th + 1, c);
        s.rect(x, y, tw, th + 1, 0xFF000000);
    }
    pub fn editor(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let l = x + sz / 6; let r = x + sz - sz / 6;
        s.rect_fill(l, y, r - l, sz, c);
        s.rect(l, y, r - l, sz, 0xFF000000);
        for i in 1..=3 { s.hline(l + 3, r - 3, y + (sz * i) / 4, 0xFFFFFFFF); }
    }
    pub fn media(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let cx = x + sz / 2; let cy = y + sz / 2; let r = sz / 2 - 2;
        s.circle_fill(cx, cy, r, c);
        s.circle(cx, cy, r, 0xFF000000);
        s.rect_fill(cx - 2, cy - r + 3, 4, r, 0xFFFFFFFF);
    }
    pub fn settings(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let cx = x + sz/2; let cy = y + sz/2; let r = sz/4;
        s.circle_fill(cx, cy, r, c);
        s.circle_fill(cx, cy, r/2, 0xFF0A0E15);
        let d: [(i32, i32); 8] = [(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1),(0,-1),(1,-1)];
        for &(dx, dy) in d.iter() {
            s.line(cx + dx*r, cy + dy*r, cx + dx*(r+3), cy + dy*(r+3), c);
        }
    }
    pub fn mine(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let cx = x + sz/2; let cy = y + sz/2;
        s.circle_fill(cx, cy, sz/3, c);
        s.circle(cx, cy, sz/3, 0xFF000000);
        s.rect_fill(cx - 1, y, 3, sz/4, c);
        s.rect_fill(cx - 1, y + sz - sz/4, 3, sz/4, c);
        s.rect_fill(x, cy - 1, sz/4, 3, c);
        s.rect_fill(x + sz - sz/4, cy - 1, sz/4, 3, c);
        s.circle_fill(cx - 3, cy - 3, 2, 0xFFFFFFFF);
    }
    pub fn snake(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let seg = sz / 5;
        s.rect_fill(x + 1*seg, y + 3*seg, seg, seg, c);
        s.rect_fill(x + 2*seg, y + 3*seg, seg, seg, c);
        s.rect_fill(x + 3*seg, y + 2*seg, seg, seg, c);
        s.rect_fill(x + 3*seg, y + 1*seg, seg, seg, c);
        s.rect_fill(x + 3*seg, y + 1*seg, 2, 2, 0xFFFFFFFF);
        s.circle_fill(x + 1*seg + seg/2, y + 1*seg, 3, 0xFFEF4444);
    }
    pub fn pong(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        s.rect_fill(x, y, sz, sz, 0xFF000000);
        s.rect_fill(x, y, sz, 1, c);
        s.rect_fill(x, y + sz - 1, sz, 1, c);
        s.rect_fill(x, y, 1, sz, c);
        s.rect_fill(x + sz - 1, y, 1, sz, c);
        s.rect_fill(x + 2, y + 3, 3, 12, 0xFFFFFFFF);
        s.rect_fill(x + sz - 5, y + sz - 15, 3, 12, 0xFFFFFFFF);
        s.circle_fill(x + sz/2, y + sz/2, 3, c);
    }
    pub fn tetris(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let b = sz / 4;
        let cols = [c, 0xFFEF4444, 0xFFFACC15, 0xFF22C55E];
        for i in 0..4 {
            s.rect_fill(x + i * b, y + b, b - 1, b - 1, cols[i as usize % 4]);
        }
        s.rect_fill(x, y + 2*b, b - 1, b - 1, 0xFFEC4899);
        s.rect_fill(x + b, y + 2*b, b - 1, b - 1, 0xFF06B6D4);
    }
}

// ============================================================
// Helpers
// ============================================================
fn eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() { return false; }
    for i in 0..a.len() { if a[i] != b[i] { return false; } }
    true
}
fn to_buf(src: &[u8], dst: &mut [u8]) -> usize {
    let n = min(src.len(), dst.len() - 1);
    for i in 0..n { dst[i] = src[i]; }
    dst[n] = 0;
    n
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
        Self { lines: [[0u8; TERM_COLS]; TERM_ROWS], count: 0,
               input: [0u8; 96], input_len: 0 }
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
    fn exec(&mut self, _lang: u8, editor: &mut Editor) {
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
            self.print(b"Built-in commands:");
            self.print(b"  help ls cat echo clear version about whoami");
            self.print(b"  pwd meminfo cpuid date edit write touch mkdir rm");
            self.print(b"  games lang nettest ping http browser asm reboot");
        } else if eq(cmd, b"ls") {
            unsafe {
                let n = munix_ramfs_count();
                let mut i = 0;
                let mut printed = 0;
                while i < 32 {
                    let mut name = [0u8; 32];
                    let mut isdir = 0; let mut sz = 0; let mut par = 0;
                    if munix_ramfs_get(i, name.as_mut_ptr(), 32,
                                       &mut isdir, &mut sz, &mut par) == 1 {
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
                                else { while v > 0 && dn < 11 {
                                    dig[dn] = b'0' + (v % 10) as u8; v /= 10; dn += 1; } }
                                for j in 0..dn { line[kk] = dig[dn - 1 - j]; kk += 1; }
                                line[kk] = b'b'; kk += 1;
                            }
                            self.push_line_raw(&line[..kk]);
                            printed += 1;
                        }
                    }
                    i += 1;
                    if i >= n { break; }
                }
                if printed == 0 { self.print(b"(empty)"); }
            }
        } else if eq(cmd, b"cat") {
            if args.is_empty() { self.print(b"cat: missing operand"); return; }
            let mut name = [0u8; 32];
            to_buf(args, &mut name);
            let mut out = [0u8; 256];
            let rc = unsafe {
                munix_ramfs_read(name.as_ptr(), munix_ramfs_root(),
                                 out.as_mut_ptr(), 256)
            };
            if rc < 0 { self.print(b"cat: file not found"); }
            else {
                let len = out.iter().position(|&c| c == 0).unwrap_or(256);
                self.push_line(&out[..len]);
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
            to_buf(args, &mut name);
            unsafe { munix_ramfs_create(name.as_ptr(), munix_ramfs_root(), 0); }
            self.print(b"created");
        } else if eq(cmd, b"mkdir") {
            if args.is_empty() { self.print(b"mkdir: missing name"); return; }
            let mut name = [0u8; 32];
            to_buf(args, &mut name);
            unsafe { munix_ramfs_create(name.as_ptr(), munix_ramfs_root(), 1); }
            self.print(b"directory created");
        } else if eq(cmd, b"rm") {
            if args.is_empty() { self.print(b"rm: missing name"); return; }
            let mut name = [0u8; 32];
            to_buf(args, &mut name);
            unsafe { munix_ramfs_remove(name.as_ptr(), munix_ramfs_root()); }
            self.print(b"removed");
        } else if eq(cmd, b"echo") {
            let mut tmp = [0u8; 96];
            let n = if args.len() > 95 { 95 } else { args.len() };
            for j in 0..n { tmp[j] = args[j]; }
            self.push_line(&tmp[..n]);
        } else if eq(cmd, b"clear") {
            self.clear();
        } else if eq(cmd, b"version") {
            self.print(b"mUnix v8.2 (release)");
            self.print(b"  Arch  : x86_64 (long mode)");
            self.print(b"  Load  : GRUB2 + Multiboot2");
            self.print(b"  GUI   : Rust (no_std, ARGB)");
            self.print(b"  Video : GOP framebuffer, 32-bit");
        } else if eq(cmd, b"about") {
            self.print(b"opening About window...");
            unsafe { crate::PENDING_OPEN_ABOUT = true; }
        } else if eq(cmd, b"whoami") { self.print(b"root"); }
        else if eq(cmd, b"pwd")    { self.print(b"/"); }
        else if eq(cmd, b"date") {
            let t = unsafe { munix_ticks() } / 18;
            let hh = (t / 3600) % 24;
            let mm = (t / 60) % 60;
            let ss = t % 60;
            let mut buf = [0u8; 10];
            buf[0] = b'0' + ((hh / 10) % 10) as u8;
            buf[1] = b'0' + (hh % 10) as u8;
            buf[2] = b':';
            buf[3] = b'0' + ((mm / 10) % 10) as u8;
            buf[4] = b'0' + (mm % 10) as u8;
            buf[5] = b':';
            buf[6] = b'0' + ((ss / 10) % 10) as u8;
            buf[7] = b'0' + (ss % 10) as u8;
            self.push_line_raw(&buf[..8]);
        } else if eq(cmd, b"meminfo") {
            unsafe {
                let used = munix_ramfs_pool_used();
                let total = munix_ramfs_pool_size();
                self.print(b"RAMFS:");
                self.print(b"  used  : ");
                self.print_num(used as u32);
                self.print(b"  total : ");
                self.print_num(total as u32);
            }
        } else if eq(cmd, b"cpuid") {
            self.print(b"CPUID:");
            self.print(b"  Vendor : GenuineIntel");
            self.print(b"  Family : i686");
        } else if eq(cmd, b"edit") {
            if args.is_empty() { self.print(b"edit: missing filename"); return; }
            let mut name = [0u8; 32];
            let nl = to_buf(args, &mut name);
            editor.load(&name[..nl]);
            self.print(b"opened in editor");
        } else if eq(cmd, b"run") {
            if args.is_empty() { self.print(b"run: usage: run <app>"); return; }
            let mut full = [0u8; 40];
            let n = if args.len() > 31 { 31 } else { args.len() };
            let mut k = 0;
            let mut j = 0;
            while j < n { full[k] = args[j]; k += 1; j += 1; }
            let has_ext = n >= 4 && args[n-1] == b'n' && args[n-2] == b'i'
                       && args[n-3] == b'b' && args[n-4] == b'.';
            if !has_ext {
                let ext = b".bin";
                let mut e = 0;
                while e < 4 { full[k] = ext[e]; k += 1; e += 1; }
            }
            full[k] = 0;
            let mut buf = [0u8; 1024];
            let rc = unsafe {
                munix_ramfs_read(full.as_ptr(), munix_ramfs_root(),
                                 buf.as_mut_ptr(), 1024)
            };
            if rc < 0 {
                let mut line = [0u8; 48]; let mut kk = 0;
                let pfx = b"run: no such app: ";
                let mut pp = 0;
                while pp < pfx.len() { line[kk] = pfx[pp]; kk += 1; pp += 1; }
                let mut jj = 0;
                while jj < n { line[kk] = args[jj]; kk += 1; jj += 1; }
                self.push_line_raw(&line[..kk]);
            } else {
                self.print(b"running...");
                let r = unsafe { munix_exec_bytes(buf.as_ptr(), rc) };
                if r == 0 { self.print(b"done (rc=0)"); }
                else { self.print(b"done (rc!=0)"); }
            }
        } else if eq(cmd, b"games") {
            self.print(b"Games (open from dock):");
            self.print(b"  Minesweeper  Snake  Pong  Shapes  Tetris");
        } else if eq(cmd, b"lang") {
            self.print(b"language: en (only English supported)");
        } else if eq(cmd, b"ping") {
            if args.is_empty() {
                self.print(b"ping: usage: ping <ip>");
                self.print(b"example: ping 10.0.2.2");
                return;
            }
            let mut ip_buf = [0u8; 32];
            let n = min(args.len(), 31);
            let mut i = 0;
            while i < n { ip_buf[i] = args[i]; i += 1; }
            ip_buf[n] = 0;

            let p = unsafe { munix_ping_run(ip_buf.as_ptr()) };
            if p.is_null() {
                self.print(b"ping: no result");
            } else {
                let mut k = 0;
                while k < 511 && unsafe { *p.add(k) } != 0 { k += 1; }
                let s = unsafe { core::slice::from_raw_parts(p, k) };
                let mut start = 0;
                let mut j = 0;
                while j <= k {
                    if j == k || s[j] == b'\n' {
                        self.push_line_raw(&s[start..j]);
                        start = j + 1;
                    }
                    j += 1;
                }
            }
        } else if eq(cmd, b"http") {
            if args.is_empty() {
                self.print(b"http: usage: http <host> [path]");
                self.print(b"  example: http example.com /");
                return;
            }
            let mut host_buf = [0u8; 128];
            let mut path_buf = [0u8; 128];
            let mut i = 0;
            let mut hi = 0;
            while i < args.len() && args[i] != b' ' && hi < 127 {
                host_buf[hi] = args[i]; hi += 1; i += 1;
            }
            host_buf[hi] = 0;
            while i < args.len() && args[i] == b' ' { i += 1; }
            let mut pi = 0;
            if i >= args.len() {
                path_buf[0] = b'/'; pi = 1;
            } else {
                while i < args.len() && pi < 127 {
                    path_buf[pi] = args[i]; pi += 1; i += 1;
                }
            }
            path_buf[pi] = 0;

            self.print(b"http: GET ");
            self.print(&host_buf[..hi]);
            self.print(&path_buf[..pi]);
            self.print(b" ...");

            let rc = unsafe { munix_http_get(host_buf.as_ptr(), path_buf.as_ptr()) };
            if rc != 0 {
                let mut msg = [0u8; 32];
                let mut k = 0;
                for c in b"http: failed rc=" { msg[k] = *c; k += 1; }
                let n = rc.unsigned_abs();
                msg[k] = b'0' + (n % 10) as u8;
                self.push_line_raw(&msg[..k+1]);
                self.print(b"  (see serial for details)");
                return;
            }

            let n = unsafe { munix_http_buf_len() } as usize;
            let ptr = unsafe { munix_http_buf() };
            if ptr.is_null() || n == 0 {
                self.print(b"http: empty response");
                return;
            }
            let buf = unsafe { core::slice::from_raw_parts(ptr, n) };

            let mut lines = 0;
            let mut start = 0;
            let mut k = 0;
            self.print(b"--- response (first 40 lines) ---");
            while k <= n && lines < 40 {
                if k == n || buf[k] == b'\n' {
                    let mut end = k;
                    if end > start && buf[end - 1] == b'\r' { end -= 1; }
                    self.push_line_raw(&buf[start..end]);
                    start = k + 1;
                    lines += 1;
                }
                k += 1;
            }
            if start < n {
                let mut msg = [0u8; 40];
                let mut j = 0;
                for c in b"... (" { msg[j] = *c; j += 1; }
                let rest = n - start;
                let mut d = [0u8; 8];
                let mut dn = 0;
                let mut v = rest as u32;
                if v == 0 { d[dn] = b'0'; dn = 1; }
                else { while v > 0 { d[dn] = b'0' + (v % 10) as u8; v /= 10; dn += 1; } }
                while dn > 0 { dn -= 1; msg[j] = d[dn]; j += 1; }
                for c in b" more bytes)" { msg[j] = *c; j += 1; }
                self.push_line_raw(&msg[..j]);
            }
        } else if eq(cmd, b"nettest") {
            self.print(b"running network diagnostics...");
            let p = unsafe { munix_nettest_run() };
            if p.is_null() {
                self.print(b"nettest: no result");
            } else {
                let mut n = 0;
                while n < 767 && unsafe { *p.add(n) } != 0 { n += 1; }
                let s = unsafe { core::slice::from_raw_parts(p, n) };
                /* print line by line */
                let mut start = 0;
                let mut i = 0;
                while i <= n {
                    if i == n || s[i] == b'\n' {
                        self.push_line_raw(&s[start..i]);
                        start = i + 1;
                    }
                    i += 1;
                }
            }
        } else if eq(cmd, b"browser") {
            self.print(b"opening browser window...");
            unsafe { crate::PENDING_OPEN_BROWSER = true; }
        } else if eq(cmd, b"asm") {
            self.print(b"opening ASM IDE...");
            self.print(b"F5=assemble F6=run F7=save F8=sample");
            unsafe { crate::PENDING_OPEN_ASM = true; }
        } else if eq(cmd, b"moffice") || eq(cmd, b"office") {
            self.print(b"opening mOffice...");
            self.print(b"F1=Writer F2=Calc F3=Impress F7=save F8=load");
            unsafe { crate::PENDING_OPEN_OFFICE = true; }
        } else if eq(cmd, b"sysmon") || eq(cmd, b"top") || eq(cmd, b"htop") {
            self.print(b"opening task manager...");
            unsafe { crate::PENDING_OPEN_SYSMON = true; }
        } else if eq(cmd, b"sysinfo") {
            let p = unsafe { munix_sysmon_text() };
            if p.is_null() {
                self.print(b"sysinfo: no data");
            } else {
                let mut n = 0;
                while n < 4095 && unsafe { *p.add(n) } != 0 { n += 1; }
                let slice = unsafe { core::slice::from_raw_parts(p, n) };
                let mut start = 0;
                let mut j = 0;
                let mut lines = 0;
                while j <= n && lines < 60 {
                    if j == n || slice[j] == b'\n' {
                        self.push_line_raw(&slice[start..j]);
                        start = j + 1;
                        lines += 1;
                    }
                    j += 1;
                }
            }
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
    fn handle_key(&mut self, k: i32, lang: u8, editor: &mut Editor) -> bool {
        if k >= 32 && k < 127 {
            if self.input_len < 95 {
                self.input[self.input_len] = k as u8; self.input_len += 1;
            }
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
            let color = if len >= 6 && &line[..6] == b"mUnix>" {
                theme::TEXT_OK
            } else { theme::TEXT };
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

// ============================================================
// Editor
// ============================================================
const ED_COLS: usize = 60;
const ED_ROWS: usize = 14;

pub struct Editor {
    name: [u8; 32], name_len: usize,
    buf: [[u8; ED_COLS]; ED_ROWS],
    cx: i32, cy: i32,
    dirty: bool, has_file: bool,
}
impl Editor {
    pub const fn new() -> Self {
        Self { name: [0u8; 32], name_len: 0,
               buf: [[b' '; ED_COLS]; ED_ROWS],
               cx: 0, cy: 0, dirty: false, has_file: false }
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
                    self.buf[y][x] = c; x += 1;
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
            munix_ramfs_write(nm.as_ptr(), munix_ramfs_root(),
                              out.as_ptr(), n as i32);
        }
        self.dirty = false;
    }
    fn handle_key(&mut self, k: i32) {
        if k >= 32 && k < 127 {
            if (self.cx as usize) < ED_COLS {
                self.buf[self.cy as usize][self.cx as usize] = k as u8;
                self.cx += 1; self.dirty = true;
            }
        } else if k == 8 {
            if self.cx > 0 {
                self.cx -= 1;
                self.buf[self.cy as usize][self.cx as usize] = b' ';
                self.dirty = true;
            }
        } else if k == 10 || k == 13 {
            if (self.cy as usize) < ED_ROWS - 1 { self.cy += 1; self.cx = 0; }
        } else if k == 0x100 { if self.cy > 0 { self.cy -= 1; } }
        else if k == 0x101 { if (self.cy as usize) < ED_ROWS - 1 { self.cy += 1; } }
        else if k == 0x102 { if self.cx > 0 { self.cx -= 1; } }
        else if k == 0x103 { if (self.cx as usize) < ED_COLS { self.cx += 1; } }
        else if k == 0x107 { self.save(); }
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, w: i32, h: i32) {
        s.rect_fill(x, y, w, h, theme::BG_TERM);
        let line_h = 12; let pad = 6;
        let start_y = y + pad + 14;
        for row in 0..ED_ROWS {
            let yy = start_y + (row as i32) * line_h;
            if yy > y + h - 14 { break; }
            let line = &self.buf[row];
            let mut len = ED_COLS;
            while len > 0 && line[len - 1] == b' ' { len -= 1; }
            if len > 0 { s.text_cstr(x + pad + 24, yy, &line[..len], theme::TEXT); }
        }
        let cy = start_y + self.cy * line_h;
        let cx = x + pad + 24 + self.cx * 8;
        s.rect_fill(cx, cy, 7, 9, theme::ACCENT_H);
        let mut hdr = [0u8; 40]; let mut k = 0;
        for c in b"File: " { hdr[k] = *c; k += 1; }
        for i in 0..self.name_len { hdr[k] = self.name[i]; k += 1; }
        if self.dirty { hdr[k] = b'*'; k += 1; }
        s.text_cstr(x + pad, y + 2, &hdr[..k], theme::TEXT_DIM);
        s.text_cstr(x + pad, y + h - 12, b"F2=save", theme::TEXT_DIM);
    }
}

// ============================================================
// Files
// ============================================================
pub struct Files { sel: i32, scroll: i32, count: i32 }
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
                if munix_ramfs_get(i, name.as_mut_ptr(), 32,
                                   &mut isdir, &mut sz, &mut par) == 1 {
                    if par == -1 { self.count += 1; }
                }
                i += 1;
                if i >= n { break; }
            }
        }
    }
    fn get_at(&self, idx: i32) -> Option<([u8; 32], usize, i32, i32)> {
        let mut i = 0; let mut found = 0;
        unsafe {
            while i < 32 {
                let mut name = [0u8; 32];
                let mut isdir = 0; let mut sz = 0; let mut par = 0;
                if munix_ramfs_get(i, name.as_mut_ptr(), 32,
                                   &mut isdir, &mut sz, &mut par) == 1 {
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
        if k == 0x100 {
            if self.sel > 0 { self.sel -= 1; }
            if self.sel < self.scroll { self.scroll = self.sel; }
        } else if k == 0x101 {
            if self.sel < self.count - 1 { self.sel += 1; }
            if self.sel >= self.scroll + 12 { self.scroll = self.sel - 11; }
        } else if k == 10 || k == 13 {
            if let Some((name, nl, isdir, _)) = self.get_at(self.sel) {
                if isdir == 0 { editor.load(&name[..nl]); }
            }
        } else if k == b'd' as i32 || k == b'D' as i32 {
            if let Some((name, nl, _, _)) = self.get_at(self.sel) {
                let mut nm = [0u8; 32];
                for j in 0..nl { nm[j] = name[j]; }
                unsafe { munix_ramfs_remove(nm.as_ptr(), munix_ramfs_root()); }
                self.refresh_count();
                if self.sel >= self.count {
                    self.sel = self.count - 1;
                    if self.sel < 0 { self.sel = 0; }
                }
            }
        } else if k == b'n' as i32 || k == b'N' as i32 {
            static NAME_SEQ: [&[u8]; 5] = [
                b"new.txt\0", b"new2.txt\0", b"new3.txt\0",
                b"new4.txt\0", b"new5.txt\0"
            ];
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
        s.text_cstr(x + 6, y + 2,
                    b"Name                Size   [Enter=open D=del N=new]",
                    theme::TEXT_DIM);
        let mut row = 0; let mut i = 0;
        unsafe {
            let n = munix_ramfs_count();
            while i < 32 {
                let mut name = [0u8; 32];
                let mut isdir = 0; let mut sz = 0; let mut par = 0;
                if munix_ramfs_get(i, name.as_mut_ptr(), 32,
                                   &mut isdir, &mut sz, &mut par) == 1 {
                    if par == -1 {
                        if row >= self.scroll && row < self.scroll + 12 {
                            let yy = y + 20 + (row - self.scroll) * 14;
                            if row == self.sel {
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
                                else { while v > 0 && dn < 11 {
                                    dig[dn] = b'0' + (v % 10) as u8; v /= 10; dn += 1; } }
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
// Media
// ============================================================
const MP_NOTES: usize = 8;
const MP_TRACKS: usize = 4;
const MP_MELODIES: [[u16; MP_NOTES]; MP_TRACKS] = [
    [392, 392, 392, 329, 493, 392, 329, 493],
    [262, 330, 392, 523, 392, 330, 262, 0],
    [440, 494, 523, 494, 440, 392, 330, 294],
    [659, 494, 523, 587, 523, 494, 440, 440],
];
const MP_NAMES: [&[u8]; MP_TRACKS] = [
    b"Imperial March", b"Chime", b"Silent Night", b"Tetris Theme"
];
pub struct Media { track: u8, playing: bool, note: u8, last_tick: u32 }
impl Media {
    pub const fn new() -> Self { Self { track: 0, playing: false, note: 0, last_tick: 0 } }
    fn tick(&mut self) {
        if !self.playing { return; }
        let now = unsafe { munix_ticks() };
        if now - self.last_tick < 4 { return; }
        self.last_tick = now;
        let freq = MP_MELODIES[self.track as usize][self.note as usize];
        if freq == 0 { unsafe { munix_speaker_off(); } }
        else { unsafe { munix_speaker_set(freq as u32); } }
        self.note = (self.note + 1) % MP_NOTES as u8;
    }
    fn handle_key(&mut self, k: i32) {
        if k == b' ' as i32 {
            self.playing = !self.playing;
            if !self.playing { unsafe { munix_speaker_off(); } }
            else { self.note = 0; self.last_tick = unsafe { munix_ticks() }; }
        } else if k == b'n' as i32 || k == b'N' as i32 {
            self.track = (self.track + 1) % MP_TRACKS as u8; self.note = 0;
        } else if k == b'p' as i32 || k == b'P' as i32 {
            self.track = (self.track + MP_TRACKS as u8 - 1) % MP_TRACKS as u8;
            self.note = 0;
        }
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, w: i32, _h: i32) {
        s.rect_fill(x, y, w, 250, theme::BG_TERM);
        s.text_cstr(x + 8, y + 8, b"Now playing:", theme::TEXT_DIM);
        s.text_cstr(x + 8, y + 24, MP_NAMES[self.track as usize], theme::ACCENT_H);
        let state: &[u8] = if self.playing { b"PLAYING" } else { b"PAUSED" };
        s.text_cstr(x + 8, y + 44, state,
                    if self.playing { theme::TEXT_OK } else { theme::TEXT_DIM });
        let bx = x + 8; let by = y + 64; let bw = w - 16;
        s.rect(bx, by, bw, 10, theme::BORDER);
        let fill = ((self.note as i32) * (bw - 4)) / MP_NOTES as i32;
        s.rect_fill(bx + 2, by + 2, fill, 6, theme::ACCENT_H);
        s.text_cstr(x + 8, y + 90, b"SPACE: play/pause  N/P: track", theme::TEXT_DIM);
        let mut yy = y + 120;
        for i in 0..MP_TRACKS {
            if i as u8 == self.track { s.text_cstr(x + 8, yy, b">", theme::TEXT_OK); }
            s.text_cstr(x + 24, yy, MP_NAMES[i], theme::TEXT);
            yy += 14;
        }
    }
}

// ============================================================
// Settings
// ============================================================
pub struct Settings { sel: u8 }
impl Settings {
    pub const fn new() -> Self { Self { sel: 0 } }
    fn handle_key(&mut self, k: i32) {
        if k == 0x100 { if self.sel > 0 { self.sel -= 1; } }
        else if k == 0x101 { if self.sel < 2 { self.sel += 1; } }
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, w: i32, _h: i32) {
        s.rect_fill(x, y, w, 250, theme::BG_TERM);
        s.text_cstr(x + 8, y + 8, b"Settings", theme::TEXT_DIM);
        let mut yy = y + 32;
        if self.sel == 0 { s.rect_fill(x + 6, yy - 2, w - 12, 14, theme::SEL_BG); }
        s.text_cstr(x + 10, yy, b"Language ......... EN", theme::TEXT);
        yy += 24;
        if self.sel == 1 { s.rect_fill(x + 6, yy - 2, w - 12, 14, theme::SEL_BG); }
        s.text_cstr(x + 10, yy, b"Theme ............ Dark", theme::TEXT);
        yy += 24;
        if self.sel == 2 { s.rect_fill(x + 6, yy - 2, w - 12, 14, theme::SEL_BG); }
        s.text_cstr(x + 10, yy, b"About ............ mUnix v8.2", theme::TEXT);
        yy += 28;
        s.text_cstr(x + 8, yy, b"Up/Down: navigate", theme::TEXT_DIM);
    }
}

// ============================================================
// GAMES
// ============================================================

// ---------- MINESWEEPER ----------
const MS_W: i32 = 9;
const MS_H: i32 = 9;
const MS_MINES: i32 = 10;
const MS_CELL: i32 = 24;

pub struct Minesweeper {
    grid: [[u8; 9]; 9],      // 0..8 = count, 9 = mine
    state: [[u8; 9]; 9],     // 0 = hidden, 1 = open, 2 = flag
    cx: i32, cy: i32,
    over: bool, win: bool,
    opened: i32, flags: i32,
    seed: u32,
}
impl Minesweeper {
    pub const fn new() -> Self {
        Self {
            grid: [[0; 9]; 9], state: [[0; 9]; 9],
            cx: 4, cy: 4, over: false, win: false,
            opened: 0, flags: 0, seed: 0x12345,
        }
    }
    fn rng(&mut self) -> u32 {
        self.seed = self.seed.wrapping_mul(1103515245).wrapping_add(12345);
        (self.seed >> 16) & 0x7FFF
    }
    fn reset(&mut self) {
        self.seed = unsafe { munix_ticks() } ^ 0xC0FFEE;
        for y in 0..9 { for x in 0..9 { self.grid[y][x] = 0; self.state[y][x] = 0; } }
        let mut placed = 0;
        while placed < MS_MINES {
            let x = (self.rng() % 9) as usize;
            let y = (self.rng() % 9) as usize;
            if self.grid[y][x] != 9 { self.grid[y][x] = 9; placed += 1; }
        }
        for y in 0..9 { for x in 0..9 {
            if self.grid[y][x] == 9 { continue; }
            let mut c = 0u8;
            for dy in -1..=1i32 { for dx in -1..=1i32 {
                let nx = x as i32 + dx; let ny = y as i32 + dy;
                if nx < 0 || ny < 0 || nx >= 9 || ny >= 9 { continue; }
                if self.grid[ny as usize][nx as usize] == 9 { c += 1; }
            }}
            self.grid[y][x] = c;
        }}
        self.cx = 4; self.cy = 4;
        self.over = false; self.win = false;
        self.opened = 0; self.flags = 0;
    }
    fn reveal(&mut self, x: i32, y: i32) {
        if x < 0 || y < 0 || x >= 9 || y >= 9 { return; }
        if self.state[y as usize][x as usize] != 0 { return; }
        if self.grid[y as usize][x as usize] == 9 {
            self.state[y as usize][x as usize] = 1;
            self.over = true; return;
        }
        self.state[y as usize][x as usize] = 1;
        self.opened += 1;
        if self.grid[y as usize][x as usize] == 0 {
            for dy in -1..=1i32 { for dx in -1..=1i32 {
                if dx == 0 && dy == 0 { continue; }
                self.reveal(x + dx, y + dy);
            }}
        }
        if self.opened >= 81 - MS_MINES { self.win = true; }
    }
    fn toggle_flag(&mut self, x: i32, y: i32) {
        let s = &mut self.state[y as usize][x as usize];
        if *s == 0 { *s = 2; self.flags += 1; }
        else if *s == 2 { *s = 0; self.flags -= 1; }
    }
    fn handle_key(&mut self, k: i32) {
        if k == 0x100 { if self.cy > 0 { self.cy -= 1; } }
        else if k == 0x101 { if self.cy < 8 { self.cy += 1; } }
        else if k == 0x102 { if self.cx > 0 { self.cx -= 1; } }
        else if k == 0x103 { if self.cx < 8 { self.cx += 1; } }
        else if k == b' ' as i32 {
            if !self.over && !self.win
                && self.state[self.cy as usize][self.cx as usize] == 0 {
                self.reveal(self.cx, self.cy);
            }
        } else if k == b'f' as i32 || k == b'F' as i32 {
            if !self.over && !self.win {
                self.toggle_flag(self.cx, self.cy);
            }
        } else if k == b'r' as i32 || k == b'R' as i32 {
            self.reset();
        }
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, w: i32, _h: i32) {
        s.rect_fill(x, y, w, 300, 0xFF1A1A1A);
        // HUD
        let rem = MS_MINES - self.flags;
        let mut buf = [0u8; 32]; let mut k = 0;
        for c in b"Mines: " { buf[k] = *c; k += 1; }
        buf[k] = b'0' + ((rem / 10) % 10) as u8; k += 1;
        buf[k] = b'0' + (rem % 10) as u8; k += 1;
        s.text_cstr(x + 8, y + 4, &buf[..k], theme::TEXT);
        if self.over { s.text_cstr(x + 200, y + 4, b"BOOM!  R=restart", theme::TEXT_ERR); }
        else if self.win { s.text_cstr(x + 200, y + 4, b"YOU WIN!  R=restart", theme::TEXT_OK); }

        let gx = x + (w - 9*MS_CELL) / 2;
        let gy = y + 24;
        for row in 0..9 {
            for col in 0..9 {
                let px = gx + col as i32 * MS_CELL;
                let py = gy + row as i32 * MS_CELL;
                let st = self.state[row][col];
                if st == 0 {
                    s.rect_fill(px, py, MS_CELL - 1, MS_CELL - 1, 0xFFB0B0B0);
                    s.rect(px, py, MS_CELL, MS_CELL, 0xFF606060);
                    s.hline(px, px + MS_CELL - 2, py, 0xFFE0E0E0);
                    s.vline(px, py, py + MS_CELL - 2, 0xFFE0E0E0);
                } else if st == 2 {
                    s.rect_fill(px, py, MS_CELL - 1, MS_CELL - 1, 0xFFB0B0B0);
                    s.rect(px, py, MS_CELL, MS_CELL, 0xFF606060);
                    s.rect_fill(px + 6, py + 4, 12, 12, 0xFFEF4444);
                    s.rect_fill(px + 10, py + 16, 4, 4, 0xFF000000);
                } else {
                    s.rect_fill(px, py, MS_CELL - 1, MS_CELL - 1, 0xFFD0D0D0);
                    s.rect(px, py, MS_CELL, MS_CELL, 0xFF808080);
                    if self.grid[row][col] == 9 {
                        s.circle_fill(px + MS_CELL/2, py + MS_CELL/2, 8, 0xFF1A1A1A);
                    } else if self.grid[row][col] > 0 {
                        let n = self.grid[row][col];
                        let col_c = match n {
                            1 => 0xFF0000FF, 2 => 0xFF00AA00, 3 => 0xFFFF0000,
                            4 => 0xFF000080, 5 => 0xFF800000, 6 => 0xFF008080,
                            _ => 0xFF000000,
                        };
                        s.text_cstr(px + 8, py + 8, &[b'0' + n], col_c);
                    }
                }
            }
        }
        // cursor
        let cx = gx + self.cx * MS_CELL;
        let cy = gy + self.cy * MS_CELL;
        s.rect(cx - 1, cy - 1, MS_CELL + 1, MS_CELL + 1, 0xFFFACC15);
        s.rect(cx - 2, cy - 2, MS_CELL + 3, MS_CELL + 3, 0xFFFACC15);
        s.text_cstr(x + 8, y + 250, b"Arrows=move  SPACE=open  F=flag  R=restart", theme::TEXT_DIM);
    }
}

// ---------- SNAKE ----------
const SN_W: i32 = 30;
const SN_H: i32 = 16;
const SN_CELL: i32 = 16;

pub struct Snake {
    body_x: [i32; 480], body_y: [i32; 480], len: i32,
    dx: i32, dy: i32,
    food_x: i32, food_y: i32,
    dead: bool,
    seed: u32,
    last_tick: u32,
    speed: u32,
}
impl Snake {
    pub const fn new() -> Self {
        Self {
            body_x: [0; 480], body_y: [0; 480], len: 3,
            dx: 1, dy: 0, food_x: 15, food_y: 8,
            dead: false, seed: 0xCAFE, last_tick: 0, speed: 8,
        }
    }
    fn rng(&mut self) -> u32 {
        self.seed = self.seed.wrapping_mul(1103515245).wrapping_add(12345);
        (self.seed >> 16) & 0x7FFF
    }
    fn reset(&mut self) {
        self.len = 3;
        self.body_x[0] = 7; self.body_y[0] = 8;
        self.body_x[1] = 6; self.body_y[1] = 8;
        self.body_x[2] = 5; self.body_y[2] = 8;
        self.dx = 1; self.dy = 0;
        self.dead = false;
        self.seed = unsafe { munix_ticks() } ^ 0xBEEF;
        self.place_food();
        self.last_tick = unsafe { munix_ticks() };
    }
    fn place_food(&mut self) {
        let mut tries = 200;
        while tries > 0 {
            let fx = (self.rng() % SN_W as u32) as i32;
            let fy = (self.rng() % SN_H as u32) as i32;
            let mut hit = false;
            for i in 0..self.len {
                if self.body_x[i as usize] == fx && self.body_y[i as usize] == fy {
                    hit = true; break;
                }
            }
            if !hit { self.food_x = fx; self.food_y = fy; return; }
            tries -= 1;
        }
    }
    fn step(&mut self) {
        if self.dead { return; }
        let nx = self.body_x[0] + self.dx;
        let ny = self.body_y[0] + self.dy;
        if nx < 0 || ny < 0 || nx >= SN_W || ny >= SN_H { self.dead = true; return; }
        for i in 0..self.len {
            if self.body_x[i as usize] == nx && self.body_y[i as usize] == ny {
                self.dead = true; return;
            }
        }
        let grew = nx == self.food_x && ny == self.food_y;
        if grew && self.len < 480 { self.len += 1; }
        let mut i = self.len - 1;
        while i > 0 {
            self.body_x[i as usize] = self.body_x[(i-1) as usize];
            self.body_y[i as usize] = self.body_y[(i-1) as usize];
            i -= 1;
        }
        self.body_x[0] = nx; self.body_y[0] = ny;
        if grew { self.place_food(); }
    }
    fn tick(&mut self) {
        if self.dead { return; }
        let now = unsafe { munix_ticks() };
        if now - self.last_tick < self.speed { return; }
        self.last_tick = now;
        self.step();
    }
    fn handle_key(&mut self, k: i32) {
        if k == 0x100 && self.dy == 0 { self.dx = 0; self.dy = -1; }
        else if k == 0x101 && self.dy == 0 { self.dx = 0; self.dy = 1; }
        else if k == 0x102 && self.dx == 0 { self.dx = -1; self.dy = 0; }
        else if k == 0x103 && self.dx == 0 { self.dx = 1; self.dy = 0; }
        else if k == b'r' as i32 || k == b'R' as i32 { self.reset(); }
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, _w: i32, _h: i32) {
        let gx = x + 20;
        let gy = y + 30;
        s.rect_fill(x, y, SN_W * SN_CELL + 40, SN_H * SN_CELL + 50, 0xFF0A0A0A);
        s.rect(gx - 1, gy - 1, SN_W * SN_CELL + 2, SN_H * SN_CELL + 2, 0xFF4ADE80);
        // food
        s.circle_fill(gx + self.food_x * SN_CELL + SN_CELL/2,
                      gy + self.food_y * SN_CELL + SN_CELL/2,
                      SN_CELL/2 - 3, 0xFFEF4444);
        // body
        for i in 0..self.len {
            let bx = gx + self.body_x[i as usize] * SN_CELL + 1;
            let by = gy + self.body_y[i as usize] * SN_CELL + 1;
            let col = if i == 0 { 0xFFFACC15 } else { 0xFF22C55E };
            s.rect_fill(bx, by, SN_CELL - 2, SN_CELL - 2, col);
        }
        // HUD
        let mut buf = [0u8; 24]; let mut k = 0;
        for c in b"Score: " { buf[k] = *c; k += 1; }
        let sc = (self.len - 3) as u32;
        buf[k] = b'0' + ((sc / 10) % 10) as u8; k += 1;
        buf[k] = b'0' + (sc % 10) as u8; k += 1;
        s.text_cstr(x + 20, y + 8, &buf[..k], 0xFFFFFFFF);
        if self.dead {
            s.text_cstr(x + 200, y + 8, b"GAME OVER  R=restart", 0xFFEF4444);
        }
    }
}

// ---------- PONG ----------
pub struct Pong {
    py: i32, ay: i32,
    bx: i32, by: i32, bdx: i32, bdy: i32,
    sp: i32, sa: i32,
    over: bool,
    last_tick: u32,
    ai_tick: u32,
}
impl Pong {
    pub const fn new() -> Self {
        Self { py: 120, ay: 120, bx: 250, by: 150, bdx: 4, bdy: 2,
               sp: 0, sa: 0, over: false, last_tick: 0, ai_tick: 0 }
    }
    fn reset(&mut self) {
        self.py = 120; self.ay = 120;
        self.bx = 250; self.by = 150;
        self.bdx = 4; self.bdy = 2;
        self.sp = 0; self.sa = 0;
        self.over = false;
    }
    fn step(&mut self) {
        if self.over { return; }
        self.bx += self.bdx; self.by += self.bdy;
        if self.by <= 0 { self.by = 0; self.bdy = -self.bdy; }
        if self.by >= 290 { self.by = 290; self.bdy = -self.bdy; }
        // left paddle (player)
        if self.bdx < 0 && self.bx <= 10 && self.bx >= 0 {
            if self.by + 8 >= self.py && self.by <= self.py + 60 {
                self.bdx = -self.bdx; self.bx = 12;
            }
        }
        // right paddle (AI)
        if self.bdx > 0 && self.bx + 8 >= 490 && self.bx <= 500 {
            if self.by + 8 >= self.ay && self.by <= self.ay + 60 {
                self.bdx = -self.bdx; self.bx = 480;
            }
        }
        if self.bx >= 500 { self.sp += 1; if self.sp >= 5 { self.over = true; } else { self.reset_ball(); } return; }
        if self.bx <= -8 { self.sa += 1; if self.sa >= 5 { self.over = true; } else { self.reset_ball(); } return; }
        // AI moves slowly, with error
        self.ai_tick += 1;
        if self.bdx > 0 && self.ai_tick % 3 == 0 {
            let target = (self.by - 22).clamp(0, 240);
            let err = ((self.ai_tick / 40) % 9) as i32 - 4;
            let t2 = (target + err).clamp(0, 240);
            if self.ay < t2 { self.ay += 2; }
            else if self.ay > t2 { self.ay -= 2; }
        }
    }
    fn reset_ball(&mut self) {
        self.bx = 250; self.by = 150;
        self.bdx = if self.bdx >= 0 { -4 } else { 4 };
        self.bdy = if self.bdy >= 0 { 2 } else { -2 };
        self.ai_tick = 0;
    }
    fn tick(&mut self) {
        let now = unsafe { munix_ticks() };
        if now - self.last_tick < 2 { return; }
        self.last_tick = now;
        self.step();
    }
    fn handle_key(&mut self, k: i32) {
        if k == 0x100 { if self.py > 0 { self.py -= 8; } }
        else if k == 0x101 { if self.py < 240 { self.py += 8; } }
        else if k == b'r' as i32 || k == b'R' as i32 { self.reset(); }
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, _w: i32, _h: i32) {
        s.rect_fill(x, y, 520, 360, 0xFF050505);
        let gx = x + 10;
        let gy = y + 30;
        // mid dashed line
        let mut i = 0;
        while i < 300 { s.rect_fill(gx + 249, gy + i, 2, 8, 0xFF444444); i += 20; }
        // paddles
        s.rect_fill(gx + 0, gy + self.py, 8, 60, 0xFF60A5FA);
        s.rect_fill(gx + 490, gy + self.ay, 8, 60, 0xFFEF4444);
        // ball
        s.rect_fill(gx + self.bx, gy + self.by, 8, 8, 0xFFFFFFFF);
        // score
        let mut buf = [0u8; 24]; let mut k = 0;
        buf[k] = b'0' + (self.sp as u8); k += 1;
        buf[k] = b' '; k += 1; buf[k] = b':'; k += 1; buf[k] = b' '; k += 1;
        buf[k] = b'0' + (self.sa as u8); k += 1;
        s.text_cstr(gx + 220, y + 6, &buf[..k], 0xFFFFFFFF);
        if self.over { s.text_cstr(x + 200, y + 180, b"GAME OVER  R=restart", 0xFFEF4444); }
        s.text_cstr(x + 10, y + 340, b"Arrows: up/down  R=restart", 0xFF888888);
    }
}

// ---------- SHAPES ----------
pub struct Shapes {
    placed: [(i32, i32, u8, u32); 96],
    n: usize,
    current: u8,
    color_step: u8,
}
impl Shapes {
    pub const fn new() -> Self {
        Self { placed: [(0,0,0,0); 96], n: 0, current: 0, color_step: 0 }
    }
    fn reset(&mut self) { self.n = 0; self.current = 0; self.color_step = 0; }
    fn next_color(&mut self) -> u32 {
        const COLS: [u32; 8] = [
            0xFFEF4444, 0xFF3B82F6, 0xFF22C55E, 0xFFFACC15,
            0xFFEC4899, 0xFF22D3EE, 0xFFF97316, 0xFF8B5CF6,
        ];
        let c = COLS[self.color_step as usize % 8];
        self.color_step = (self.color_step + 1) % 8;
        c
    }
    fn draw_shape(s: &mut Surface, cx: i32, cy: i32, t: u8, c: u32) {
        match t {
            0 => s.rect_fill(cx - 16, cy - 16, 32, 32, c),
            1 => s.circle_fill(cx, cy, 18, c),
            2 => { let mut i = 0; while i <= 16 {
                s.hline(cx - i, cx + i, cy - 16 + i, c); i += 1; } },
            3 => { let mut y: i32 = -16; while y <= 16 {
                let w = 16 - y.abs();
                s.hline(cx - w, cx + w, cy + y, c); y += 1; } },
            4 => s.line(cx - 16, cy + 16, cx + 16, cy - 16, c),
            5 => { // star
                s.line(cx - 18, cy, cx + 18, cy, c);
                s.line(cx, cy - 18, cx, cy + 18, c);
                s.line(cx - 12, cy - 12, cx + 12, cy + 12, c);
                s.line(cx - 12, cy + 12, cx + 12, cy - 12, c);
            },
            _ => {}
        }
    }
    fn handle_key(&mut self, k: i32) {
        if k >= b'1' as i32 && k <= b'6' as i32 { self.current = (k - b'1' as i32) as u8; }
        else if k == b'c' as i32 || k == b'C' as i32 { self.reset(); }
    }
    fn click(&mut self, x: i32, y: i32, wx: i32, wy: i32) {
        // if inside canvas (bottom half of the window), place
        if x >= wx + 10 && x <= wx + 500 && y >= wy + 100 && y <= wy + 320 {
            if self.n >= 96 {
                for i in 0..95 { self.placed[i] = self.placed[i+1]; }
                self.n = 95;
            }
            let c = self.next_color();
            self.placed[self.n] = (x, y, self.current, c);
            self.n += 1;
        } else if y >= wy + 40 && y < wy + 100 {
            // palette
            let mut i = 0;
            while i < 6 {
                let bx = wx + 20 + i * 60;
                if x >= bx && x < bx + 50 { self.current = i as u8; break; }
                i += 1;
            }
        }
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, _w: i32, _h: i32) {
        s.rect_fill(x, y, 520, 340, 0xFFFFFFFF);
        // palette
        let mut i = 0u8;
        while i < 6 {
            let bx = x + 20 + i as i32 * 60;
            let by = y + 40;
            let bg = if self.current == i { 0xFF2563EB } else { 0xFFE0E0E0 };
            s.rect_fill(bx, by, 50, 50, bg);
            s.rect(bx, by, 50, 50, 0xFF000000);
            Self::draw_shape(s, bx + 25, by + 25, i, 0xFF1A1A1A);
            i += 1;
        }
        // separator
        s.hline(x + 10, x + 510, y + 100, 0xFF000000);
        // placed
        for j in 0..self.n {
            let (px, py, pt, pc) = self.placed[j];
            Self::draw_shape(s, px, py, pt, pc);
        }
        s.text_cstr(x + 10, y + 8, b"1-6 = pick shape.  Click = place.  C = clear.", 0xFF1A1A1A);
    }
}

// ---------- TETRIS ----------
const TT_W: i32 = 10;
const TT_H: i32 = 20;
const TT_CELL: i32 = 14;

const PIECES: [[[(i8,i8);4];4];7] = [
    // I
    [[(0,1),(1,1),(2,1),(3,1)],[(2,0),(2,1),(2,2),(2,3)],
     [(0,2),(1,2),(2,2),(3,2)],[(1,0),(1,1),(1,2),(1,3)]],
    // O
    [[(1,0),(2,0),(1,1),(2,1)],[(1,0),(2,0),(1,1),(2,1)],
     [(1,0),(2,0),(1,1),(2,1)],[(1,0),(2,0),(1,1),(2,1)]],
    // T
    [[(0,1),(1,1),(2,1),(1,0)],[(1,0),(1,1),(1,2),(2,1)],
     [(0,1),(1,1),(2,1),(1,2)],[(1,0),(1,1),(1,2),(0,1)]],
    // S
    [[(1,0),(2,0),(0,1),(1,1)],[(1,0),(1,1),(2,1),(2,2)],
     [(1,0),(2,0),(0,1),(1,1)],[(1,0),(1,1),(2,1),(2,2)]],
    // Z
    [[(0,0),(1,0),(1,1),(2,1)],[(2,0),(1,1),(2,1),(1,2)],
     [(0,0),(1,0),(1,1),(2,1)],[(2,0),(1,1),(2,1),(1,2)]],
    // J
    [[(0,0),(0,1),(1,1),(2,1)],[(1,0),(2,0),(1,1),(1,2)],
     [(0,1),(1,1),(2,1),(2,2)],[(1,0),(1,1),(0,2),(1,2)]],
    // L
    [[(2,0),(0,1),(1,1),(2,1)],[(1,0),(1,1),(1,2),(2,2)],
     [(0,1),(1,1),(2,1),(0,2)],[(0,0),(1,0),(1,1),(1,2)]],
];
const PIECE_COLORS: [u32; 7] = [
    0xFF06B6D4, 0xFFFACC15, 0xFF8B5CF6, 0xFF22C55E,
    0xFFEF4444, 0xFF3B82F6, 0xFFF97316,
];

pub struct Tetris {
    board: [[u8; 10]; 20],
    cur_piece: u8,
    cur_rot: u8,
    cur_x: i32, cur_y: i32,
    next_piece: u8,
    score: u32, lines: u32,
    over: bool,
    last_tick: u32,
    speed: u32,
    seed: u32,
}
impl Tetris {
    pub const fn new() -> Self {
        Self {
            board: [[0; 10]; 20], cur_piece: 0, cur_rot: 0,
            cur_x: 3, cur_y: 0, next_piece: 1,
            score: 0, lines: 0, over: false,
            last_tick: 0, speed: 12, seed: 0x777,
        }
    }
    fn rng(&mut self) -> u32 {
        self.seed = self.seed.wrapping_mul(1103515245).wrapping_add(12345);
        (self.seed >> 16) & 0x7FFF
    }
    fn reset(&mut self) {
        for y in 0..20 { for x in 0..10 { self.board[y][x] = 0; } }
        self.score = 0; self.lines = 0; self.over = false;
        self.seed = unsafe { munix_ticks() } ^ 0xABCD;
        self.cur_piece = (self.rng() % 7) as u8;
        self.cur_rot = 0;
        self.cur_x = 3; self.cur_y = 0;
        self.next_piece = (self.rng() % 7) as u8;
        self.last_tick = unsafe { munix_ticks() };
    }
    fn can_move(&self, dx: i32, dy: i32, rot: u8) -> bool {
        for &(ox, oy) in PIECES[self.cur_piece as usize][rot as usize].iter() {
            let nx = self.cur_x + ox as i32 + dx;
            let ny = self.cur_y + oy as i32 + dy;
            if nx < 0 || nx >= 10 || ny >= 20 { return false; }
            if ny >= 0 && self.board[ny as usize][nx as usize] != 0 { return false; }
        }
        true
    }
    fn lock_piece(&mut self) {
        let col = self.cur_piece + 1;
        for &(ox, oy) in PIECES[self.cur_piece as usize][self.cur_rot as usize].iter() {
            let nx = self.cur_x + ox as i32;
            let ny = self.cur_y + oy as i32;
            if ny >= 0 && ny < 20 && nx >= 0 && nx < 10 {
                self.board[ny as usize][nx as usize] = col;
            }
        }
        // clear lines
        let mut cleared = 0;
        let mut y = 19i32;
        while y >= 0 {
            let mut full = true;
            for x in 0..10 { if self.board[y as usize][x] == 0 { full = false; break; } }
            if full {
                // shift down
                let mut yy = y;
                while yy > 0 {
                    self.board[yy as usize] = self.board[(yy-1) as usize];
                    yy -= 1;
                }
                for x in 0..10 { self.board[0][x] = 0; }
                cleared += 1;
                // don't decrement y — check same row again
            } else {
                y -= 1;
            }
        }
        if cleared > 0 {
            self.lines += cleared;
            self.score += (cleared as u32) * 100;
            if self.speed > 4 { self.speed -= 1; }
        }
        // spawn next
        self.cur_piece = self.next_piece;
        self.next_piece = (self.rng() % 7) as u8;
        self.cur_rot = 0;
        self.cur_x = 3; self.cur_y = 0;
        if !self.can_move(0, 0, 0) { self.over = true; }
    }
    fn step(&mut self) {
        if self.over { return; }
        if self.can_move(0, 1, self.cur_rot) {
            self.cur_y += 1;
        } else {
            self.lock_piece();
        }
    }
    fn tick(&mut self) {
        if self.over { return; }
        let now = unsafe { munix_ticks() };
        if now - self.last_tick < self.speed { return; }
        self.last_tick = now;
        self.step();
    }
    fn handle_key(&mut self, k: i32) {
        if self.over {
            if k == b'r' as i32 || k == b'R' as i32 { self.reset(); }
            return;
        }
        if k == 0x100 {
            if self.can_move(0, 0, self.cur_rot) && self.cur_y > 0 { self.cur_y -= 1; }
            // actually just move up if allowed
            if self.can_move(0, -1, self.cur_rot) { self.cur_y -= 1; }
        } else if k == 0x101 {
            if self.can_move(0, 1, self.cur_rot) { self.cur_y += 1; }
        } else if k == 0x102 {
            if self.can_move(-1, 0, self.cur_rot) { self.cur_x -= 1; }
        } else if k == 0x103 {
            if self.can_move(1, 0, self.cur_rot) { self.cur_x += 1; }
        } else if k == b' ' as i32 {
            let new_rot = (self.cur_rot + 1) % 4;
            if self.can_move(0, 0, new_rot) { self.cur_rot = new_rot; }
        } else if k == b'r' as i32 || k == b'R' as i32 {
            self.reset();
        }
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, _w: i32, _h: i32) {
        s.rect_fill(x, y, 320, 340, 0xFF0A0E15);
        let gx = x + 10;
        let gy = y + 10;
        // board border
        s.rect(gx - 1, gy - 1, TT_W * TT_CELL + 2, TT_H * TT_CELL + 2, 0xFF4ADE80);
        // board cells
        for by_u in 0..20usize {
            for bx_u in 0..10usize {
                let bx: i32 = bx_u as i32;
                let by: i32 = by_u as i32;
                let c = self.board[by_u][bx_u];
                if c != 0 {
                    let col = PIECE_COLORS[(c - 1) as usize];
                    s.rect_fill(gx + bx * TT_CELL, gy + by * TT_CELL,
                                TT_CELL - 1, TT_CELL - 1, col);
                } else {
                    s.rect_fill(gx + bx * TT_CELL, gy + by * TT_CELL,
                                TT_CELL - 1, TT_CELL - 1, 0xFF161B22);
                }
            }
        }
        // current piece
        if !self.over {
            let col = PIECE_COLORS[self.cur_piece as usize];
            for &(ox, oy) in PIECES[self.cur_piece as usize][self.cur_rot as usize].iter() {
                let nx = self.cur_x + ox as i32;
                let ny = self.cur_y + oy as i32;
                if ny >= 0 && ny < 20 && nx >= 0 && nx < 10 {
                    s.rect_fill(gx + nx * TT_CELL, gy + ny * TT_CELL,
                                TT_CELL - 1, TT_CELL - 1, col);
                }
            }
        }
        // HUD right side
        let hx = gx + TT_W * TT_CELL + 20;
        s.text_cstr(hx, gy, b"Score", theme::TEXT_DIM);
        let mut buf = [0u8; 16]; let mut k = 0;
        let mut v = self.score;
        if v == 0 { buf[k] = b'0'; k += 1; }
        else {
            let mut dig = [0u8; 12]; let mut dn = 0;
            while v > 0 { dig[dn] = b'0' + (v % 10) as u8; v /= 10; dn += 1; }
            while dn > 0 { dn -= 1; buf[k] = dig[dn]; k += 1; }
        }
        s.text_cstr(hx, gy + 14, &buf[..k], theme::TEXT);
        s.text_cstr(hx, gy + 44, b"Lines", theme::TEXT_DIM);
        let mut buf2 = [0u8; 16]; let mut k2 = 0;
        let mut v2 = self.lines;
        if v2 == 0 { buf2[k2] = b'0'; k2 += 1; }
        else {
            let mut dig = [0u8; 12]; let mut dn = 0;
            while v2 > 0 { dig[dn] = b'0' + (v2 % 10) as u8; v2 /= 10; dn += 1; }
            while dn > 0 { dn -= 1; buf2[k2] = dig[dn]; k2 += 1; }
        }
        s.text_cstr(hx, gy + 58, &buf2[..k2], theme::TEXT);
        s.text_cstr(hx, gy + 100, b"Next:", theme::TEXT_DIM);
        let ncol = PIECE_COLORS[self.next_piece as usize];
        let mut nb = [(0i32, 0i32); 4];
        for j in 0..4 { let (a, b) = PIECES[self.next_piece as usize][0][j]; nb[j] = (a as i32, b as i32); }
        for &(ox, oy) in nb.iter() {
            s.rect_fill(hx + ox * TT_CELL, gy + 120 + oy * TT_CELL,
                        TT_CELL - 1, TT_CELL - 1, ncol);
        }
        s.text_cstr(hx, gy + 220, b"Arrows move", theme::TEXT_DIM);
        s.text_cstr(hx, gy + 234, b"SPACE rotate", theme::TEXT_DIM);
        s.text_cstr(hx, gy + 248, b"R restart", theme::TEXT_DIM);
        if self.over { s.text_cstr(x + 60, y + 150, b"GAME OVER", 0xFFEF4444); }
    }
}

// ============================================================
// Window / Gui
// ============================================================
#[derive(Copy, Clone, PartialEq, Eq)]
pub enum Kind {
    Terminal, Files, Editor, Media, Settings,
    Minesweeper, Snake, Pong, Shapes, Tetris, Ide, Browser, Office, SysMon,
    About,}
impl Kind {
    fn title(self) -> &'static [u8] {
        match self {
            Kind::Terminal    => b"Terminal",
            Kind::Files       => b"Files",
            Kind::Editor      => b"Text Editor",
            Kind::Media       => b"Media Player",
            Kind::Settings    => b"Settings",
            Kind::Minesweeper => b"Minesweeper",
            Kind::Snake       => b"Snake",
            Kind::Pong        => b"Pong",
            Kind::Shapes      => b"Shapes",
            Kind::Tetris      => b"Tetris",
            Kind::Ide         => b"ASM IDE",
            Kind::SysMon      => b"Task Manager",
            Kind::Office      => b"mOffice",
            Kind::Browser     => b"Browser",
            Kind::About       => b"About",
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


// ============================================================
// Browser
// ============================================================
const BR_COLS: usize = 100;

pub struct Browser {
    url: [u8; 128],
    url_len: usize,
    response: [u8; 16384],
    response_len: usize,
    scroll: i32,
    status: [u8; 64],
    status_len: usize,
    cursor_visible: bool,
}

impl Browser {
    pub const fn new() -> Self {
        Self {
            url: [0; 128], url_len: 0,
            response: [0; 16384], response_len: 0,
            scroll: 0,
            status: [0; 64], status_len: 0,
            cursor_visible: true,
        }
    }

    fn set_status(&mut self, s: &[u8]) {
        let n = if s.len() > 63 { 63 } else { s.len() };
        let mut i = 0;
        while i < n { self.status[i] = s[i]; i += 1; }
        self.status[n] = 0;
        self.status_len = n;
    }

    fn default_url(&mut self) {
        let d = b"example.com";
        for i in 0..d.len() { self.url[i] = d[i]; }
        self.url_len = d.len();
    }

    fn fetch(&mut self) {
        if self.url_len == 0 {
            self.set_status(b"enter a URL first");
            return;
        }

        // split url into host + path
        let mut host = [0u8; 128];
        let mut path = [0u8; 128];
        let mut hi = 0;
        let mut pi = 0;

        // skip "http://" prefix
        let mut start = 0;
        if self.url_len > 7 {
            if &self.url[start..start+7] == b"http://" {
                start = 7;
            }
        }

        let mut i = start;
        while i < self.url_len && self.url[i] != b'/' {
            if hi < 127 { host[hi] = self.url[i]; hi += 1; }
            i += 1;
        }
        host[hi] = 0;

        if i < self.url_len {
            while i < self.url_len && pi < 127 {
                path[pi] = self.url[i]; pi += 1; i += 1;
            }
        } else {
            path[0] = b'/'; pi = 1;
        }
        path[pi] = 0;

        self.set_status(b"fetching...");
        self.scroll = 0;

        extern "C" {
            fn munix_http_get(host: *const u8, path: *const u8) -> i32;
            fn munix_http_buf() -> *const u8;
            fn munix_http_buf_len() -> u32;
        }

        let rc = unsafe { munix_http_get(host.as_ptr(), path.as_ptr()) };
        if rc != 0 {
            let mut msg = [0u8; 32];
            let pre = b"fetch failed rc=";
            let mut k = 0;
            while k < pre.len() { msg[k] = pre[k]; k += 1; }
            let n = rc.unsigned_abs();
            let mut d = [0u8; 4]; let mut dn = 0;
            let mut v = n;
            if v == 0 { d[dn] = b'0'; dn = 1; }
            else { while v > 0 { d[dn] = b'0' + (v % 10) as u8; v /= 10; dn += 1; } }
            while dn > 0 { dn -= 1; msg[k] = d[dn]; k += 1; }
            self.set_status(&msg[..k]);
            self.response_len = 0;
            return;
        }

        let n = unsafe { munix_http_buf_len() } as usize;
        let p = unsafe { munix_http_buf() };
        if p.is_null() || n == 0 {
            self.set_status(b"empty response");
            self.response_len = 0;
            return;
        }

        let src = unsafe { core::slice::from_raw_parts(p, n) };

        // strip HTTP headers (skip until \r\n\r\n)
        let mut body_start = 0;
        let mut i = 0;
        while i + 3 < n {
            if src[i] == b'\r' && src[i+1] == b'\n' && src[i+2] == b'\r' && src[i+3] == b'\n' {
                body_start = i + 4;
                break;
            }
            i += 1;
        }
        if body_start == 0 { body_start = 0; }

        // strip HTML tags → plain text into response buffer
        let max = if 16384 < self.response.len() { 16384 } else { self.response.len() };
        let mut out = 0;
        let mut in_tag = false;
        let mut in_script = false;
        let mut last_space = false;
        let mut i = body_start;
        while i < n && out < max - 1 {
            let c = src[i];
            // detect <script / <style
            if c == b'<' && i + 7 < n && &src[i..i+7] == b"<script" {
                in_script = true;
            }
            if c == b'<' && i + 7 < n && &src[i..i+7] == b"<script " {
                in_script = true;
            }
            if c == b'<' && i + 6 < n && &src[i..i+6] == b"<style" {
                in_script = true;
            }
            if c == b'<' && i + 8 < n && &src[i..i+8] == b"</script" {
                in_script = false;
                // skip to '>'
                while i < n && src[i] != b'>' { i += 1; }
                i += 1;
                continue;
            }
            if c == b'<' && i + 7 < n && &src[i..i+7] == b"</style" {
                in_script = false;
                while i < n && src[i] != b'>' { i += 1; }
                i += 1;
                continue;
            }
            if in_script {
                i += 1;
                continue;
            }
            if c == b'<' { in_tag = true; i += 1; continue; }
            if c == b'>' { in_tag = false; i += 1; continue; }
            if in_tag { i += 1; continue; }
            // decode common entities
            if c == b'&' {
                if i + 5 < n && &src[i..i+5] == b"&amp;" { self.response[out] = b'&'; out += 1; i += 5; continue; }
                if i + 4 < n && &src[i..i+4] == b"&lt;"  { self.response[out] = b'<'; out += 1; i += 4; continue; }
                if i + 4 < n && &src[i..i+4] == b"&gt;"  { self.response[out] = b'>'; out += 1; i += 4; continue; }
                if i + 6 < n && &src[i..i+6] == b"&nbsp;"{ self.response[out] = b' '; out += 1; i += 6; continue; }
                if i + 5 < n && &src[i..i+5] == b"&quot;"{ self.response[out] = b'"'; out += 1; i += 5; continue; }
            }
            // collapse whitespace
            let is_ws = c == b' ' || c == b'\t' || c == b'\r' || c == b'\n';
            if is_ws {
                if !last_space {
                    self.response[out] = b' '; out += 1; last_space = true;
                }
                i += 1;
                continue;
            }
            last_space = false;
            self.response[out] = c;
            out += 1;
            i += 1;
        }
        self.response[out] = 0;
        self.response_len = out;

        // count lines
        let mut lines = 0;
        let mut k = 0;
        while k < out {
            if self.response[k] == b' ' { lines += 1; }
            k += 1;
        }
        // crude: wrap by chars
        let _ = lines;

        self.set_status(b"loaded");
    }

    fn handle_key(&mut self, k: i32) {
        if k == 0x100 { // up
            if self.scroll > 0 { self.scroll -= 1; }
            return;
        }
        if k == 0x101 { // down
            self.scroll += 1;
            return;
        }
        if k == 0x102 { // left — edit url
            if self.url_len > 0 { self.url_len -= 1; self.url[self.url_len] = 0; }
            return;
        }
        if k == 10 || k == 13 { // enter — fetch
            self.fetch();
            return;
        }
        if k == 8 { // backspace
            if self.url_len > 0 { self.url_len -= 1; self.url[self.url_len] = 0; }
            return;
        }
        if k >= 32 && k < 127 {
            if self.url_len < 127 {
                self.url[self.url_len] = k as u8;
                self.url_len += 1;
                self.url[self.url_len] = 0;
            }
            return;
        }
    }

    fn render(&self, s: &mut Surface, x: i32, y: i32, w: i32, h: i32,
              _mx: i32, _my: i32) {
        s.rect_fill(x, y, w, h, 0xFF0A0E15);

        // URL bar
        let url_h = 22;
        s.rect_fill(x + 4, y + 4, w - 8, url_h, 0xFF1B2333);
        s.rect(x + 4, y + 4, w - 8, url_h, 0xFF3F4D66);
        s.text_cstr(x + 8, y + 9, &self.url[..self.url_len], theme::TEXT);
        // cursor in url
        let cx = x + 8 + (self.url_len as i32) * 8;
        s.rect_fill(cx, y + 8, 6, 10, theme::ACCENT_H);

        // status line
        s.text_cstr(x + 8, y + 30, &self.status[..self.status_len], theme::TEXT_DIM);

        // content area
        let cy = y + 46;
        let ch = h - 50;
        s.rect_fill(x + 4, cy, w - 8, ch, 0xFF0E131B);

        // wrap text into lines of BR_COLS chars
        // count wrapped lines
        let mut line_starts = [0usize; 512];
        let mut line_count = 0;
        let mut pos = 0;
        let cols = ((w - 16) / 8) as usize;
        if cols == 0 { return; }
        while pos < self.response_len && line_count < 512 {
            line_starts[line_count] = pos;
            line_count += 1;
            let mut col = 0;
            while pos < self.response_len && col < cols {
                if self.response[pos] == b' ' { pos += 1; col += 1; }
                else { pos += 1; col += 1; }
            }
        }

        // draw visible lines
        let vis_lines = ((ch - 8) / 12) as i32;
        let first = self.scroll;
        let mut li = 0i32;
        while li < vis_lines {
            let idx = first + li;
            if idx < 0 || idx as usize >= line_count { break; }
            let start = line_starts[idx as usize];
            let end = if (idx as usize + 1) < line_count { line_starts[idx as usize + 1] } else { self.response_len };
            let mut len = end - start;
            if len > cols { len = cols; }
            let yy = cy + 4 + li * 12;
            if len > 0 {
                s.text_cstr(x + 8, yy, &self.response[start..start+len], theme::TEXT);
            }
            li += 1;
        }

        // scroll hint
        if line_count > 0 {
            let mut hint = [0u8; 32];
            let mut k = 0;
            for c in b"line " { hint[k] = *c; k += 1; }
            let mut v = (self.scroll + 1) as u32;
            let mut dig = [0u8; 8]; let mut dn = 0;
            if v == 0 { dig[dn] = b'0'; dn = 1; }
            else { while v > 0 { dig[dn] = b'0' + (v % 10) as u8; v /= 10; dn += 1; } }
            while dn > 0 { dn -= 1; hint[k] = dig[dn]; k += 1; }
            for c in b"/" { hint[k] = *c; k += 1; }
            let mut v2 = line_count as u32;
            let mut dig2 = [0u8; 8]; let mut dn2 = 0;
            if v2 == 0 { dig2[dn2] = b'0'; dn2 = 1; }
            else { while v2 > 0 { dig2[dn2] = b'0' + (v2 % 10) as u8; v2 /= 10; dn2 += 1; } }
            while dn2 > 0 { dn2 -= 1; hint[k] = dig2[dn2]; k += 1; }
            s.text_cstr(x + w - 100, y + h - 14, &hint[..k], theme::TEXT_DIM);
        }

        s.text_cstr(x + 8, y + h - 14, b"UP/DOWN scroll, ENTER=go", theme::TEXT_DIM);
    }
}


// ============================================================
// mOffice — Writer / Calc / Impress
// ============================================================
const OFC_COLS: usize = 80;
const OFC_ROWS: usize = 30;

#[derive(Copy, Clone, PartialEq, Eq)]
pub enum OfcMode { Writer, Calc, Impress }

pub struct Office {
    mode: OfcMode,
    // Unified text buffer for Writer, cell content for Calc, slide lines for Impress
    buf: [[u8; OFC_COLS]; OFC_ROWS],
    cx: i32,
    cy: i32,
    status: [u8; 64],
    status_len: usize,
    save_mode: bool,
    save_name: [u8; 32],
    save_name_len: usize,
    load_mode: bool,
    load_name: [u8; 32],
    load_name_len: usize,
    slide: u32,
    calc_result: [u8; 32],
    calc_result_len: usize,
}

impl Office {
    pub const fn new() -> Self {
        Self {
            mode: OfcMode::Writer,
            buf: [[b' '; OFC_COLS]; OFC_ROWS],
            cx: 0, cy: 0,
            status: [0; 64], status_len: 0,
            save_mode: false, save_name: [0; 32], save_name_len: 0,
            load_mode: false, load_name: [0; 32], load_name_len: 0,
            slide: 0,
            calc_result: [0; 32], calc_result_len: 0,
        }
    }

    fn set_status(&mut self, s: &[u8]) {
        let n = if s.len() > 63 { 63 } else { s.len() };
        let mut i = 0;
        while i < n { self.status[i] = s[i]; i += 1; }
        self.status[n] = 0;
        self.status_len = n;
    }

    fn clear_buf(&mut self) {
        let mut y = 0;
        while y < OFC_ROWS {
            let mut x = 0;
            while x < OFC_COLS { self.buf[y][x] = b' '; x += 1; }
            y += 1;
        }
    }

    fn load_sample(&mut self) {
        self.clear_buf();
        match self.mode {
            OfcMode::Writer => {
                let lines: [&[u8]; 7] = [
                    b"# mUnix Release Notes",
                    b"",
                    b"## v8.2",
                    b"",
                    b"- 64-bit long mode",
                    b"- UEFI + GRUB + Multiboot2",
                    b"- HTTP browser + ASM IDE",
                ];
                let mut y = 0;
                while y < lines.len() && y < OFC_ROWS {
                    let l = lines[y];
                    let mut x = 0;
                    while x < l.len() && x < OFC_COLS { self.buf[y][x] = l[x]; x += 1; }
                    y += 1;
                }
                self.set_status(b"sample: release notes (Writer)");
            }
            OfcMode::Calc => {
                let lines: [&[u8]; 6] = [
                    b"A1=10  B1=20  C1=A1+B1",
                    b"A2=5   B2=7   C2=A2*B2",
                    b"",
                    b"Formula in C1: =A1+B1   -> 30",
                    b"Formula in C2: =A2*B2   -> 35",
                    b"",
                ];
                let mut y = 0;
                while y < lines.len() && y < OFC_ROWS {
                    let l = lines[y];
                    let mut x = 0;
                    while x < l.len() && x < OFC_COLS { self.buf[y][x] = l[x]; x += 1; }
                    y += 1;
                }
                self.set_status(b"sample: calc sheet");
            }
            OfcMode::Impress => {
                let lines: [&[u8]; 6] = [
                    b"=========================",
                    b"  mUnix v8.2 (release)",
                    b"=========================",
                    b"",
                    b"  64-bit UEFI operating system",
                    b"  Written in C + Rust + NASM",
                ];
                let mut y = 0;
                while y < lines.len() && y < OFC_ROWS {
                    let l = lines[y];
                    let mut x = 0;
                    while x < l.len() && x < OFC_COLS { self.buf[y][x] = l[x]; x += 1; }
                    y += 1;
                }
                self.slide = 0;
                self.set_status(b"slide 1/1");
            }
        }
        self.cx = 0;
        self.cy = 0;
    }

    fn src_bytes(&self, out: &mut [u8]) -> usize {
        let mut k = 0;
        let mut y = 0;
        while y < OFC_ROWS {
            let mut last = 0;
            let mut x = 0;
            while x < OFC_COLS {
                if self.buf[y][x] != b' ' { last = x + 1; }
                x += 1;
            }
            x = 0;
            while x < last {
                if k < out.len() - 1 { out[k] = self.buf[y][x]; k += 1; }
                x += 1;
            }
            if k < out.len() - 1 { out[k] = b'\n'; k += 1; }
            y += 1;
        }
        if k < out.len() { out[k] = 0; }
        k
    }

    fn do_save(&mut self) {
        if self.save_name_len == 0 { self.set_status(b"empty name"); return; }
        let mut full = [0u8; 40];
        let mut n = 0;
        let mut i = 0;
        while i < self.save_name_len && n < 35 {
            full[n] = self.save_name[i]; n += 1; i += 1;
        }
        let ext: &[u8] = match self.mode {
            OfcMode::Writer => b".mw",
            OfcMode::Calc => b".mc",
            OfcMode::Impress => b".mi",
        };
        let mut j = 0;
        while j < ext.len() && n < 39 { full[n] = ext[j]; n += 1; j += 1; }
        full[n] = 0;

        let mut data = [0u8; 4096];
        let dn = self.src_bytes(&mut data);

        extern "C" {
            fn munix_ramfs_create(name: *const u8, parent: i32, isdir: i32) -> i32;
            fn munix_ramfs_lookup(name: *const u8, parent: i32) -> i32;
            fn munix_ramfs_write(name: *const u8, parent: i32, data: *const u8, len: i32) -> i32;
            fn munix_ramfs_root() -> i32;
        }

        unsafe {
            let parent = munix_ramfs_root();
            if munix_ramfs_lookup(full.as_ptr(), parent) < 0 {
                munix_ramfs_create(full.as_ptr(), parent, 0);
            }
            munix_ramfs_write(full.as_ptr(), parent, data.as_ptr(), dn as i32);
        }
        self.set_status(b"saved");
        self.save_mode = false;
        self.save_name_len = 0;
    }

    fn do_load(&mut self) {
        if self.load_name_len == 0 {
            self.set_status(b"empty name");
            self.load_mode = false;
            return;
        }
        let mut base = [0u8; 40];
        let n = if self.load_name_len > 31 { 31 } else { self.load_name_len };
        let mut i = 0;
        while i < n { base[i] = self.load_name[i]; i += 1; }
        base[n] = 0;

        extern "C" {
            fn munix_ramfs_read(name: *const u8, parent: i32, out: *mut u8, cap: i32) -> i32;
            fn munix_ramfs_root() -> i32;
        }

        let mut data = [0u8; 4096];
        let exts: [&[u8]; 4] = [b"", b".mw", b".mc", b".mi"];
        let mut rc: i32 = -1;
        let mut matched_ext: u8 = 0;

        for ext in exts.iter() {
            let mut full = [0u8; 40];
            let mut k = 0;
            while k < n { full[k] = base[k]; k += 1; }
            let mut j = 0;
            while j < ext.len() && k < 39 { full[k] = ext[j]; k += 1; j += 1; }
            full[k] = 0;
            rc = unsafe {
                munix_ramfs_read(full.as_ptr(), munix_ramfs_root(),
                                 data.as_mut_ptr(), 4096)
            };
            if rc > 0 {
                // set mode based on ext
                if ext.len() == 3 && ext[0] == b'.' {
                    match ext[1] {
                        b'm' => match ext[2] {
                            b'w' => { self.mode = OfcMode::Writer; matched_ext = 1; }
                            b'c' => { self.mode = OfcMode::Calc;   matched_ext = 2; }
                            b'i' => { self.mode = OfcMode::Impress;matched_ext = 3; }
                            _ => {}
                        },
                        _ => {}
                    }
                }
                break;
            }
        }

        if rc <= 0 {
            self.set_status(b"file not found (tried .mw .mc .mi)");
            self.load_mode = false;
            self.load_name_len = 0;
            return;
        }

        // Wipe buffer, load text
        self.clear_buf();
        let mut x = 0usize;
        let mut y = 0usize;
        let mut idx = 0usize;
        while idx < rc as usize {
            let c = data[idx];
            if c == 0 { break; }
            if c == b'\n' { y += 1; x = 0; if y >= OFC_ROWS { break; } }
            else if x < OFC_COLS { self.buf[y][x] = c; x += 1; }
            idx += 1;
        }

        let mut msg = [0u8; 48];
        let mut kk = 0;
        for c in b"loaded " { msg[kk] = *c; kk += 1; }
        let mut jj = 0;
        while jj < n { msg[kk] = base[jj]; kk += 1; jj += 1; }
        let suffix: &[u8] = if matched_ext == 1 { b" (.mw)" }
                            else if matched_ext == 2 { b" (.mc)" }
                            else if matched_ext == 3 { b" (.mi)" }
                            else { b"" };
        let mut q = 0;
        while q < suffix.len() { msg[kk] = suffix[q]; kk += 1; q += 1; }
        self.set_status(&msg[..kk]);

        self.load_mode = false;
        self.load_name_len = 0;
    }

    fn eval_calc(&mut self) {
        // Trivial: evaluate what's on current line after '='
        let row = &self.buf[self.cy as usize];
        let mut start = 0;
        let mut found = 0;
        let mut i = 0;
        while i < OFC_COLS {
            if row[i] == b'=' { start = i + 1; found = 1; break; }
            i += 1;
        }
        if found == 0 {
            self.calc_result_len = 0;
            self.set_status(b"no '=' in line");
            return;
        }
        // Parse very simple: number op number OR SUM(A1:A5) is skipped
        let mut a: i32 = 0;
        let mut b: i32 = 0;
        let mut op: u8 = 0;
        let mut j = start;
        // read a
        while j < OFC_COLS && row[j] == b' ' { j += 1; }
        while j < OFC_COLS && row[j] >= b'0' && row[j] <= b'9' {
            a = a * 10 + (row[j] - b'0') as i32;
            j += 1;
        }
        while j < OFC_COLS && row[j] == b' ' { j += 1; }
        if j < OFC_COLS && (row[j] == b'+' || row[j] == b'-' || row[j] == b'*' || row[j] == b'/') {
            op = row[j]; j += 1;
        }
        while j < OFC_COLS && row[j] == b' ' { j += 1; }
        while j < OFC_COLS && row[j] >= b'0' && row[j] <= b'9' {
            b = b * 10 + (row[j] - b'0') as i32;
            j += 1;
        }
        let r: i32 = match op {
            b'+' => a + b,
            b'-' => a - b,
            b'*' => a * b,
            b'/' => if b != 0 { a / b } else { 0 },
            _ => a,
        };
        // Format result
        let mut msg = [0u8; 32];
        let mut k = 0;
        for c in b"= " { msg[k] = *c; k += 1; }
        let neg = r < 0;
        let mut v = if neg { -r } else { r } as u32;
        let mut d = [0u8; 12]; let mut dn = 0;
        if v == 0 { d[dn] = b'0'; dn = 1; }
        else { while v > 0 { d[dn] = b'0' + (v % 10) as u8; v /= 10; dn += 1; } }
        if neg { msg[k] = b'-'; k += 1; }
        while dn > 0 { dn -= 1; msg[k] = d[dn]; k += 1; }
        self.calc_result[..k].copy_from_slice(&msg[..k]);
        self.calc_result_len = k;
        self.set_status(&msg[..k]);
    }

    pub fn on_click(&mut self, mx: i32, my: i32, wx: i32, wy: i32) {
        extern "C" { fn serial_puts(s: *const u8); fn serial_hex(v: u64); }
        unsafe {
            serial_puts(b"ofc_click: mx=\0".as_ptr()); serial_hex(mx as u64);
            serial_puts(b" my=\0".as_ptr()); serial_hex(my as u64);
            serial_puts(b" wx=\0".as_ptr()); serial_hex(wx as u64);
            serial_puts(b" wy=\0".as_ptr()); serial_hex(wy as u64);
            serial_puts(b"\n\0".as_ptr());
        }
        let ty = wy + 30;
        let tab_h = 22;
        if my < ty || my >= ty + tab_h {
            unsafe { serial_puts(b"ofc_click: not in tab band\n\0".as_ptr()); }
            return;
        }
        let tx = wx + 10;
        let tab_w = 110;
        let gap = 4;
        if mx >= tx && mx < tx + tab_w {
            self.mode = OfcMode::Writer;
            self.set_status(b"Writer tab");
            self.cx = 0; self.cy = 0;
            unsafe { serial_puts(b"ofc_click: -> Writer\n\0".as_ptr()); }
            return;
        }
        if mx >= tx + tab_w + gap && mx < tx + 2*tab_w + gap {
            self.mode = OfcMode::Calc;
            self.set_status(b"Calc tab");
            self.cx = 0; self.cy = 0;
            unsafe { serial_puts(b"ofc_click: -> Calc\n\0".as_ptr()); }
            return;
        }
        if mx >= tx + 2*(tab_w + gap) && mx < tx + 3*tab_w + 2*gap {
            self.mode = OfcMode::Impress;
            self.set_status(b"Impress tab");
            self.cx = 0; self.cy = 0;
            unsafe { serial_puts(b"ofc_click: -> Impress\n\0".as_ptr()); }
            return;
        }
        unsafe { serial_puts(b"ofc_click: x outside tabs\n\0".as_ptr()); }
    }

    pub fn handle_key(&mut self, k: i32) {
        extern "C" { fn serial_puts(s: *const u8); fn serial_hex(v: u64); }
        unsafe {
            serial_puts(b"ofc_key: k=0x\0".as_ptr());
            serial_hex(k as u64);
            serial_puts(b" mode=\0".as_ptr());
            let m = match self.mode { OfcMode::Writer => 0, OfcMode::Calc => 1, OfcMode::Impress => 2 };
            serial_hex(m);
            serial_puts(b" save_mode=\0".as_ptr());
            serial_hex(self.save_mode as u64);
            serial_puts(b" load_mode=\0".as_ptr());
            serial_hex(self.load_mode as u64);
            serial_puts(b"\n\0".as_ptr());
        }
        // ===== Save dialog — intercepts EVERYTHING =====
        if self.save_mode {
            if k == 0x104 || k == 27 {
                self.save_mode = false;
                self.save_name_len = 0;
                self.set_status(b"save cancelled");
                return;
            }
            if k == 8 {
                if self.save_name_len > 0 {
                    self.save_name_len -= 1;
                    self.save_name[self.save_name_len] = 0;
                }
                return;
            }
            if k == 10 || k == 13 {
                self.do_save();
                return;
            }
            if k >= 32 && k < 127 {
                if self.save_name_len < 31 {
                    self.save_name[self.save_name_len] = k as u8;
                    self.save_name_len += 1;
                    self.save_name[self.save_name_len] = 0;
                }
                return;
            }
            return;
        }

        // ===== Load dialog — intercepts EVERYTHING =====
        if self.load_mode {
            if k == 0x104 || k == 27 {
                self.load_mode = false;
                self.load_name_len = 0;
                self.set_status(b"load cancelled");
                return;
            }
            if k == 8 {
                if self.load_name_len > 0 {
                    self.load_name_len -= 1;
                    self.load_name[self.load_name_len] = 0;
                }
                return;
            }
            if k == 10 || k == 13 {
                self.do_load();
                return;
            }
            if k >= 32 && k < 127 {
                if self.load_name_len < 31 {
                    self.load_name[self.load_name_len] = k as u8;
                    self.load_name_len += 1;
                    self.load_name[self.load_name_len] = 0;
                }
                return;
            }
            return;
        }

        // ===== Normal mode =====
        // F1=Writer F2=Calc F3=Impress
        if k == 0x105 { self.mode = OfcMode::Writer; self.set_status(b"F1: Writer"); self.cx=0; self.cy=0; return; }
        if k == 0x106 { self.mode = OfcMode::Calc;   self.set_status(b"F2: Calc - IN DEVELOPMENT"); self.cx=0; self.cy=0; return; }
        if k == 0x107 { self.mode = OfcMode::Impress;self.set_status(b"F3: Impress - IN DEVELOPMENT"); self.cx=0; self.cy=0; return; }

        // F5 = evaluate (Calc)
        if k == 0x108 {
            if self.mode == OfcMode::Calc {
                self.eval_calc();
            } else {
                self.set_status(b"F5 = evaluate (only in Calc)");
            }
            return;
        }
        // F6 = load sample
        if k == 0x109 { self.load_sample(); return; }
        // F7 = save dialog
        if k == 0x10A {
            self.save_mode = true;
            self.save_name_len = 0;
            self.save_name[0] = 0;
            self.set_status(b"SAVE: type name, ENTER=ok, ESC=cancel");
            return;
        }
        // F8 = load dialog
        if k == 0x10B {
            self.load_mode = true;
            self.load_name_len = 0;
            self.load_name[0] = 0;
            self.set_status(b"OPEN: type name, ENTER=ok, ESC=cancel");
            return;
        }

        // Impress slide nav
        if self.mode == OfcMode::Impress {
            if k == 0x100 { if self.slide > 0 { self.slide -= 1; } return; }
            if k == 0x101 { self.slide += 1; return; }
        }

        // Text editing
        if k >= 32 && k < 127 {
            if (self.cx as usize) < OFC_COLS {
                self.buf[self.cy as usize][self.cx as usize] = k as u8;
                self.cx += 1;
            }
            return;
        }
        if k == 8 {
            if self.cx > 0 {
                self.cx -= 1;
                self.buf[self.cy as usize][self.cx as usize] = b' ';
            }
            return;
        }
        if k == 10 || k == 13 {
            if (self.cy as usize) < OFC_ROWS - 1 { self.cy += 1; self.cx = 0; }
            return;
        }
        if k == 0x100 { if self.cy > 0 { self.cy -= 1; } return; }
        if k == 0x101 { if (self.cy as usize) < OFC_ROWS - 1 { self.cy += 1; } return; }
        if k == 0x102 { if self.cx > 0 { self.cx -= 1; } return; }
        if k == 0x103 { if (self.cx as usize) < OFC_COLS { self.cx += 1; } return; }
    }

    fn draw_tab(s: &mut Surface, x: i32, y: i32, w: i32, h: i32, label: &[u8], active: bool) {
        let bg = if active { 0xFF2563EB } else { 0xFF1B2333 };
        let border = if active { 0xFF60A5FA } else { 0xFF2A3446 };
        let txt = if active { 0xFFFFFFFF } else { 0xFF9CA3AF };
        s.rect_fill(x, y, w, h, bg);
        s.rect(x, y, w, h, border);
        let n = label.len() as i32;
        s.text_cstr(x + (w - n * 8) / 2, y + (h - 8) / 2, label, txt);
    }

    pub fn render(&self, s: &mut Surface, x: i32, y: i32, w: i32, h: i32,
                  _mx: i32, _my: i32) {
        extern "C" { fn serial_puts(s: *const u8); }
        unsafe {
            static mut CNT: u32 = 0;
            CNT += 1;
            if CNT < 4 {
                serial_puts(b"ofc: render called\n\0".as_ptr());
            }
        }
        s.rect_fill(x, y, w, h, 0xFF0A0E15);

        // Top bar
        s.text_cstr(x + 10, y + 4, b"mOffice v0.1", theme::ACCENT_H);

        // Tabs
        let ty = y + 30;
        let tab_w = 110;
        let tab_h = 22;
        Self::draw_tab(s, x + 10, ty, tab_w, tab_h, b"Writer",  self.mode == OfcMode::Writer);
        Self::draw_tab(s, x + 10 + tab_w + 4, ty, tab_w, tab_h, b"Calc",    self.mode == OfcMode::Calc);
        Self::draw_tab(s, x + 10 + 2*(tab_w + 4), ty, tab_w, tab_h, b"Impress", self.mode == OfcMode::Impress);

        // Content area
        let cy = ty + tab_h + 8;
        let ch = h - (cy - y) - 26;
        s.rect_fill(x + 4, cy, w - 8, ch, 0xFF0E131B);

        // WIP placeholder for Calc / Impress
        if self.mode == OfcMode::Calc || self.mode == OfcMode::Impress {
            let msg: &[u8] = if self.mode == OfcMode::Calc {
                b"Calc is in development"
            } else {
                b"Impress is in development"
            };
            let sub: &[u8] = b"Coming soon in a future release";
            let cx_mid = x + w / 2;
            let cy_mid = cy + ch / 2;
            let tw = (msg.len() as i32) * 8;
            s.text_cstr(cx_mid - tw / 2, cy_mid - 20, msg, theme::ACCENT_H);
            let sw = (sub.len() as i32) * 8;
            s.text_cstr(cx_mid - sw / 2, cy_mid + 4, sub, theme::TEXT_DIM);
        }

        // Render buffer
        let line_h = 12;
        let pad = 8;
        let mut row = 0usize;
        while row < OFC_ROWS {
            let yy = cy + pad + (row as i32) * line_h;
            if yy > cy + ch - 12 { break; }
            // Row number
            let mut num = [0u8; 3];
            num[0] = b'0' + ((row / 10) % 10) as u8;
            num[1] = b'0' + (row % 10) as u8;
            num[2] = b' ';
            s.text_cstr(x + 8, yy, &num[..], theme::TEXT_DIM);

            let line = &self.buf[row];
            let mut len = OFC_COLS;
            while len > 0 && line[len - 1] == b' ' { len -= 1; }
            if len > 0 {
                let color = if self.mode == OfcMode::Writer && line[0] == b'#' {
                    theme::ACCENT_H
                } else { theme::TEXT };
                s.text_cstr(x + 36, yy, &line[..len], color);
            }
            row += 1;
        }

        // Cursor
        if !self.save_mode && !self.load_mode {
            let cyy = cy + pad + self.cy * line_h;
            let cxx = x + 36 + self.cx * 8;
            s.rect_fill(cxx, cyy, 7, 10, theme::ACCENT_H);
        }

        // Bottom status bar
        s.rect_fill(x, y + h - 20, w, 20, 0xFF1B2333);
        s.text_cstr(x + 8, y + h - 14, &self.status[..self.status_len], theme::TEXT_OK);

        // Calc result
        if self.mode == OfcMode::Calc && self.calc_result_len > 0 {
            s.text_cstr(x + w - 200, y + h - 14,
                        &self.calc_result[..self.calc_result_len], theme::ACCENT_H);
        }

        // Impress slide indicator
        if self.mode == OfcMode::Impress {
            let mut msg = [0u8; 32];
            let mut k = 0;
            for c in b"slide " { msg[k] = *c; k += 1; }
            let s_slide = self.slide + 1;
            let mut v = s_slide;
            let mut d = [0u8; 8]; let mut dn = 0;
            if v == 0 { d[dn] = b'0'; dn = 1; }
            else { while v > 0 { d[dn] = b'0' + (v % 10) as u8; v /= 10; dn += 1; } }
            while dn > 0 { dn -= 1; msg[k] = d[dn]; k += 1; }
            s.text_cstr(x + w - 100, y + h - 14, &msg[..k], theme::TEXT_DIM);
        }

        // Save/Load dialogs (modal, big, on top)
        if self.save_mode || self.load_mode {
            let dw = 440; let dh = 110;
            let dx = x + (w - dw) / 2;
            let dy = y + (h - dh) / 2;
            // backdrop dim
            s.rect_fill(x, y, w, h, 0x80000000);
            s.panel_fill(dx, dy, dw, dh, theme::BG_PANEL);
            s.panel_border(dx, dy, dw, dh, theme::ACCENT_H);
            s.rect_fill(dx + 1, dy + 1, dw - 2, 24, theme::ACCENT);
            let title: &[u8] = if self.save_mode {
                b"Save file (F7)"
            } else {
                b"Open file (F8)"
            };
            s.text_cstr(dx + 10, dy + 8, title, 0xFFFFFFFF);

            let hint: &[u8] = if self.save_mode {
                b"File name (without extension):"
            } else {
                b"File name (tries .mw / .mc / .mi):"
            };
            s.text_cstr(dx + 12, dy + 36, hint, theme::TEXT_DIM);

            // input field
            let fx = dx + 12;
            let fy = dy + 52;
            let fw = dw - 24;
            s.rect_fill(fx, fy, fw, 22, 0xFF0E131B);
            s.rect(fx, fy, fw, 22, theme::BORDER_H);

            let name_slice: &[u8] = if self.save_mode {
                &self.save_name[..self.save_name_len]
            } else {
                &self.load_name[..self.load_name_len]
            };
            s.text_cstr(fx + 6, fy + 7, name_slice, theme::TEXT);

            // cursor blink
            let cx = fx + 6 + (name_slice.len() as i32) * 8;
            s.rect_fill(cx, fy + 6, 7, 11, theme::ACCENT_H);

            s.text_cstr(dx + 12, dy + 82,
                        b"ENTER = confirm   ESC = cancel",
                        theme::TEXT_DIM);
        }
    }
}


// ============================================================
// Task Manager / SysMon
// ============================================================
pub struct SysMon {
    buf: [u8; 4096],
    len: usize,
    last_refresh: u32,
    scroll: i32,
}

impl SysMon {
    pub const fn new() -> Self {
        Self { buf: [0; 4096], len: 0, last_refresh: 0, scroll: 0 }
    }

    pub fn refresh(&mut self) {
        extern "C" {
            fn munix_sysmon_text() -> *const u8;
        }
        let p = unsafe { munix_sysmon_text() };
        if p.is_null() { self.len = 0; return; }
        let mut n = 0usize;
        while n < 4095 && unsafe { *p.add(n) } != 0 { n += 1; }
        let src = unsafe { core::slice::from_raw_parts(p, n) };
        let mut i = 0;
        while i < n { self.buf[i] = src[i]; i += 1; }
        self.len = n;
    }

    pub fn handle_key(&mut self, k: i32) {
        if k == 0x100 && self.scroll > 0 { self.scroll -= 1; return; }
        if k == 0x101 { self.scroll += 1; return; }
        if k == 0x100 || k == 0x101 || k == 10 || k == 13 || k == b'r' as i32 || k == b'R' as i32 {
            self.scroll = 0;
            self.refresh();
        }
    }

    pub fn render(&mut self, s: &mut Surface, x: i32, y: i32, w: i32, h: i32) {
        self.refresh();

        s.rect_fill(x, y, w, h, 0xFF0A0E15);
        s.text_cstr(x + 10, y + 4, b"Task Manager / System Information",
                    theme::ACCENT_H);
        s.hline(x + 10, x + w - 10, y + 20, theme::BORDER);

        let line_h = 12;
        let start_y = y + 26;
        let max_lines = ((h - 40) / line_h) as i32;

        let mut row = 0i32;
        let mut start = 0usize;
        let mut j = 0usize;
        let mut vis_row = 0i32;
        while j <= self.len {
            if j == self.len || self.buf[j] == b'\n' {
                if row >= self.scroll && vis_row < max_lines {
                    let yy = start_y + vis_row * line_h;
                    let mut end = j;
                    if end > start && self.buf[end - 1] == b'\r' { end -= 1; }
                    // color by section
                    let color = if end - start > 4 && self.buf[start] == b'-' {
                        theme::TEXT_DIM
                    } else if end - start > 10 && &self.buf[start..start+4] == b"===" {
                        theme::ACCENT_H
                    } else {
                        theme::TEXT
                    };
                    if end > start {
                        s.text_cstr(x + 12, yy, &self.buf[start..end], color);
                    }
                    vis_row += 1;
                }
                row += 1;
                start = j + 1;
                if vis_row >= max_lines { break; }
            }
            j += 1;
        }

        s.rect_fill(x, y + h - 20, w, 20, 0xFF1B2333);
        s.text_cstr(x + 8, y + h - 14, b"R=refresh  UP/DOWN=scroll",
                    theme::TEXT_DIM);
    }
}


// ============================================================
// About Window
// ============================================================
fn draw_raccoon_face(s: &mut Surface, cx: i32, cy: i32, r: i32) {
    let ear1_x = cx - r * 11 / 10;
    let ear1_y = cy - r * 9 / 10;
    s.circle_fill(ear1_x, ear1_y, r * 4 / 10, 0xFF4A4A4A);
    s.circle_fill(ear1_x + r / 2, ear1_y + r / 8, r * 35 / 100, 0xFF6B6B6B);
    s.circle_fill(ear1_x + r * 7 / 10, ear1_y + r / 5, r * 27 / 100, 0xFF4A4A4A);
    s.circle_fill(cx, cy, r, 0xFF8C8C8C);
    s.circle_fill(cx, cy - 2, r * 92 / 100, 0xFF9E9E9E);
    let mut i: i32 = 0;
    while i < r * 42 / 100 {
        let h = i / 2;
        s.hline(cx - r * 8 / 10 + h, cx - r * 31 / 100 - h, cy - r * 86 / 100 - i, 0xFF4A4A4A);
        s.hline(cx + r * 31 / 100 + h, cx + r * 8 / 10 - h, cy - r * 86 / 100 - i, 0xFF4A4A4A);
        i += 1;
    }
    i = 0;
    while i < r * 23 / 100 {
        let h = i / 2;
        s.hline(cx - r * 73 / 100 + h, cx - r * 42 / 100 - h, cy - r * 81 / 100 - i, 0xFFAA8888);
        s.hline(cx + r * 42 / 100 + h, cx + r * 73 / 100 - h, cy - r * 81 / 100 - i, 0xFFAA8888);
        i += 1;
    }
    s.circle_fill(cx, cy + r / 10, r * 69 / 100, 0xFFE8E8E8);
    s.circle_fill(cx - r * 35 / 100, cy - r * 8 / 100, r * 35 / 100, 0xFF1E1E1E);
    s.circle_fill(cx + r * 35 / 100, cy - r * 8 / 100, r * 35 / 100, 0xFF1E1E1E);
    s.rect_fill(cx - r * 8 / 100, cy - r * 31 / 100, r * 16 / 100, r * 39 / 100, 0xFFE8E8E8);
    s.circle_fill(cx - r * 35 / 100, cy - r * 8 / 100, r * 15 / 100, 0xFFFFFFFF);
    s.circle_fill(cx + r * 35 / 100, cy - r * 8 / 100, r * 15 / 100, 0xFFFFFFFF);
    s.circle_fill(cx - r * 35 / 100, cy - r * 8 / 100, r * 8 / 100, 0xFF000000);
    s.circle_fill(cx + r * 35 / 100, cy - r * 8 / 100, r * 8 / 100, 0xFF000000);
    s.put(cx - r * 38 / 100, cy - r * 15 / 100, 0xFFFFFFFF);
    s.put(cx + r * 32 / 100, cy - r * 15 / 100, 0xFFFFFFFF);
    s.circle_fill(cx, cy + r * 27 / 100, r * 11 / 100, 0xFF1E1E1E);
    s.put(cx - 1, cy + r * 23 / 100, 0xFFFFFFFF);
    s.line(cx, cy + r * 38 / 100, cx, cy + r / 2, 0xFF1E1E1E);
    s.line(cx, cy + r / 2, cx - r * 11 / 100, cy + r * 58 / 100, 0xFF1E1E1E);
    s.line(cx, cy + r / 2, cx + r * 11 / 100, cy + r * 58 / 100, 0xFF1E1E1E);
    s.line(cx - r * 38 / 100, cy + r * 38 / 100, cx - r * 85 / 100, cy + r * 31 / 100, 0xFFD0D0D0);
    s.line(cx - r * 38 / 100, cy + r * 46 / 100, cx - r * 88 / 100, cy + r / 2, 0xFFD0D0D0);
    s.line(cx + r * 38 / 100, cy + r * 38 / 100, cx + r * 85 / 100, cy + r * 31 / 100, 0xFFD0D0D0);
    s.line(cx + r * 38 / 100, cy + r * 46 / 100, cx + r * 88 / 100, cy + r / 2, 0xFFD0D0D0);
}

pub struct AboutWin {}

impl AboutWin {
    pub const fn new() -> Self { Self {} }
    pub fn handle_key(&mut self, _k: i32) {}
    pub fn render(&self, s: &mut Surface, x: i32, y: i32, w: i32, h: i32) {
        s.rect_fill(x, y, w, h, 0xFF0E1B2A);
        let cx = x + w / 2;
        let cy = y + 100;
        draw_raccoon_face(s, cx, cy, 70);
        let mut yy = cy + 100;
        let pad = 30;
        let lines: [&[u8]; 6] = [
            b"OS      : mUnix 8.2",
            b"Build   : 02.10.26",
            b"Github  : github.com/davidchekushka-hue/mUnix-os",
            b"License : MIT",
            b"Kernel  : kernel.c9",
            b"By Larp Dev!",
        ];
        let mut i = 0;
        while i < lines.len() {
            s.text_cstr(x + pad, yy, lines[i], theme::TEXT);
            yy += 18;
            i += 1;
        }
    }
}

pub struct Gui {
    show_welcome: bool,
    wins: [Window; MAX_WIN],
    nwin: usize,
    focused: i32,
    term: Terminal,
    editor: Editor,
    files: Files,
    media: Media,
    settings: Settings,
    ms: Minesweeper,
    sn: Snake,
    pg: Pong,
    sh: Shapes,
    tt: Tetris,
    ide: AsmIde,
    sysmon: SysMon,
    office: Office,
    browser: Browser,
    about_win: AboutWin,
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
            term: Terminal::new(),
            editor: Editor::new(),
            files: Files::new(),
            media: Media::new(),
            settings: Settings::new(),
            ms: Minesweeper::new(),
            sn: Snake::new(),
            pg: Pong::new(),
            sh: Shapes::new(),
            tt: Tetris::new(),
            ide: AsmIde::new(),
            sysmon: SysMon::new(),
            office: Office::new(),
            browser: Browser::new(),
            about_win: AboutWin::new(),
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
        self.wins[idx] = Window { x: 60 + c, y: 60 + c,
                                  w: 560, h: 380,
                                  kind: k, visible: true };
        self.nwin += 1;
        self.focused = idx as i32;
    }
    fn close(&mut self, idx: usize) {
        if idx >= self.nwin { return; }
        self.wins[idx].visible = false;
        for i in idx..self.nwin-1 { self.wins[i] = self.wins[i+1]; }
        self.nwin -= 1;
        if self.focused as usize >= self.nwin {
            self.focused = self.nwin as i32 - 1;
        }
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
            let d = self.hit_dock(x, y);
            if d >= 0 { self.dock_action(d); return; }

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
                    // Office tab click
                    if self.wins[ni].kind == Kind::Office {
                        extern "C" { fn serial_puts(s: *const u8); fn serial_hex(v: u64); }
                        unsafe {
                            serial_puts(b"gui_click: Office routing x=\0".as_ptr());
                            serial_hex(x as u64);
                            serial_puts(b" y=\0".as_ptr());
                            serial_hex(y as u64);
                            serial_puts(b"\n\0".as_ptr());
                        }
                        let wx = self.wins[ni].x;
                        let wy = self.wins[ni].y;
                        let gptr = self as *mut Gui;
                        unsafe { (*gptr).office.on_click(x, y, wx, wy); }
                    }
                    // Shapes canvas click
                    if self.wins[ni].kind == Kind::Shapes {
                        let wx = self.wins[ni].x;
                        let wy = self.wins[ni].y;
                        let gptr = self as *mut Gui;
                        unsafe { (*gptr).sh.click(x, y, wx, wy); }
                    }
                    // Installer buttons                    return;
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
        let items = 11;
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
    fn dock_action(&mut self, i: i32) {
        let k = match i {
            0 => Kind::Terminal, 1 => Kind::Files, 2 => Kind::Editor,
            3 => Kind::Media,    4 => Kind::Settings,
            5 => Kind::Minesweeper, 6 => Kind::Snake, 7 => Kind::Pong,
            8 => Kind::Shapes,   9 => Kind::Tetris,
            10 => Kind::Ide,
            _ => return,
        };
        if k == Kind::Minesweeper { self.ms.reset(); }
        if k == Kind::Snake { self.sn.reset(); }
        if k == Kind::Pong { self.pg.reset(); }
        if k == Kind::Shapes { self.sh.reset(); }
        if k == Kind::Tetris { self.tt.reset(); }
        self.open(k);
    }
    fn render(&mut self, s: &mut Surface) {
        extern "C" {
            fn munix_wallpaper_blit(px: *mut u32, w: i32, h: i32) -> i32;
        }
        let rc = unsafe { munix_wallpaper_blit(s.px.as_mut_ptr(), s.w, s.h) };
        if rc != 0 {
            s.rect_fill(0, 0, s.w, s.h, 0xFF000000);
        }
        for i in 0..self.nwin { self.render_win(s, i); }
        self.render_dock(s);
        if self.show_welcome { draw_welcome_overlay(s); }
        render_cursor(s, self.mouse_x, self.mouse_y);
    }
    fn render_win(&mut self, s: &mut Surface, idx: usize) {
        let focused = self.focused == idx as i32;
        let w = self.wins[idx];
        if !w.visible { return; }
        s.panel_fill(w.x + 4, w.y + 4, w.w, w.h, theme::SHADOW);
        let border = if focused { theme::ACCENT_H } else { theme::BORDER };
        s.panel(w.x, w.y, w.w, w.h, theme::BG_WIN, border);
        // Blue title bar (bright for focused, dim for unfocused)
        let tb = if focused { theme::ACCENT } else { theme::TITLE_DIM };
        s.rect_fill(w.x + 1, w.y + 1, w.w - 2, theme::TITLE_H, tb);
        // reapply top corners (rounded)
        for dx in 0..theme::CORNER_R {
            for dy in 0..theme::CORNER_R {
                if !CORNER_TL[dy as usize][dx as usize] {
                    s.put(w.x + dx, w.y + dy, theme::BG_WIN);
                    s.put(w.x + w.w - 1 - dx, w.y + dy, theme::BG_WIN);
                }
            }
        }
        s.text_cstr(w.x + 12, w.y + 9, w.kind.title(), 0xFFFFFFFF);

        let bx = w.close_x(); let by = w.close_y();
        let hover = self.mouse_x >= bx && self.mouse_x < bx + theme::CLOSE_SZ &&
                    self.mouse_y >= by && self.mouse_y < by + theme::CLOSE_SZ;
        if hover { s.rect_fill(bx, by, theme::CLOSE_SZ, theme::CLOSE_SZ, theme::RED_H); }
        let cc = 0xFFFFFFFF;
        let cx = bx + theme::CLOSE_SZ / 2;
        let cy = by + theme::CLOSE_SZ / 2;
        s.line(cx-4, cy-4, cx+4, cy+4, cc);
        s.line(cx-4, cy+4, cx+4, cy-4, cc);

        let cx0 = w.x + 8;
        let cy0 = w.y + theme::TITLE_H + 8;
        let cw = w.w - 16;
        let ch = w.h - theme::TITLE_H - 16;

        let gptr = self as *mut Gui;
        match w.kind {
            Kind::Terminal    => unsafe { (*gptr).term.render(s, cx0, cy0, cw, ch); }
            Kind::Files       => unsafe { (*gptr).files.render(s, cx0, cy0, cw, ch); }
            Kind::Editor      => unsafe { (*gptr).editor.render(s, cx0, cy0, cw, ch); }
            Kind::Media       => unsafe { (*gptr).media.render(s, cx0, cy0, cw, ch); }
            Kind::Settings    => unsafe { (*gptr).settings.render(s, cx0, cy0, cw, ch); }
            Kind::Minesweeper => unsafe { (*gptr).ms.render(s, cx0, cy0, cw, ch); }
            Kind::Snake       => unsafe { (*gptr).sn.render(s, cx0, cy0, cw, ch); }
            Kind::Pong        => unsafe { (*gptr).pg.render(s, cx0, cy0, cw, ch); }
            Kind::Shapes      => unsafe { (*gptr).sh.render(s, cx0, cy0, cw, ch); }
            Kind::Tetris      => unsafe { (*gptr).tt.render(s, cx0, cy0, cw, ch); }
            Kind::Ide         => unsafe { (*gptr).ide.render(s, cx0, cy0, cw, ch); },
            Kind::SysMon      => unsafe { (*gptr).sysmon.render(s, cx0, cy0, cw, ch); },
            Kind::Office      => unsafe { (*gptr).office.render(s, cx0, cy0, cw, ch, (*gptr).mouse_x, (*gptr).mouse_y); },
            Kind::Browser     => unsafe { (*gptr).browser.render(s, cx0, cy0, cw, ch, (*gptr).mouse_x, (*gptr).mouse_y); },
            Kind::About       => unsafe { (*gptr).about_win.render(s, cx0, cy0, cw, ch); }
        }
    }
    fn render_dock(&self, s: &mut Surface) {
        let items = 11;
        let total_w = items * theme::DOCK_ICON + (items - 1) * theme::DOCK_GAP + theme::DOCK_PAD * 2;
        let x = (s.w - total_w) / 2;
        let y = s.h - theme::DOCK_H - 14;
        s.panel_round(x + 3, y + 3, total_w, theme::DOCK_H, theme::SHADOW, theme::SHADOW, 16);
        s.panel_round(x, y, total_w, theme::DOCK_H, theme::BG_PANEL, theme::BORDER, 16);
        let hover = self.hit_dock(self.mouse_x, self.mouse_y);
        for i in 0..items {
            let ix = x + theme::DOCK_PAD + i * (theme::DOCK_ICON + theme::DOCK_GAP);
            let iy = y + (theme::DOCK_H - theme::DOCK_ICON) / 2;
            let ak = match i {
                0 => Kind::Terminal, 1 => Kind::Files, 2 => Kind::Editor,
                3 => Kind::Media,    4 => Kind::Settings,
                5 => Kind::Minesweeper, 6 => Kind::Snake, 7 => Kind::Pong,
                8 => Kind::Shapes,   9 => Kind::Tetris,
                10 => Kind::Ide,
                _ => Kind::Terminal,
            };
            let active = self.find_kind(ak) >= 0;
            if hover == i as i32 {
                s.rect_fill(ix - 4, iy - 4, theme::DOCK_ICON + 8,
                            theme::DOCK_ICON + 8, 0x33FFFFFF);
            }
            if active {
                s.rect_fill(ix + theme::DOCK_ICON/2 - 2,
                            y + theme::DOCK_H - 5, 4, 2, theme::ACCENT_H);
            }
            match i {
                0 => icon::terminal(s, ix, iy, theme::DOCK_ICON, icon::C_TERM),
                1 => icon::files(s, ix, iy, theme::DOCK_ICON, icon::C_FILES),
                2 => icon::editor(s, ix, iy, theme::DOCK_ICON, icon::C_EDIT),
                3 => icon::media(s, ix, iy, theme::DOCK_ICON, icon::C_MEDIA),
                4 => icon::settings(s, ix, iy, theme::DOCK_ICON, icon::C_SET),
                5 => icon::mine(s, ix, iy, theme::DOCK_ICON, icon::C_MINE),
                6 => icon::snake(s, ix, iy, theme::DOCK_ICON, icon::C_SNAKE),
                7 => icon::pong(s, ix, iy, theme::DOCK_ICON, icon::C_PONG),
                8 => icon::settings(s, ix, iy, theme::DOCK_ICON, icon::C_SET),
                9 => icon::tetris(s, ix, iy, theme::DOCK_ICON, icon::C_TETRIS),
                10 => icon::asm_ide(s, ix, iy, theme::DOCK_ICON, 0xFF06B6D4),
                _ => {}
            }
        }
    }
}

// ============================================================
// Cursor
// ============================================================

// ============================================================
// ASM IDE
// ============================================================
const IDE_COLS: usize = 56;
const IDE_ROWS: usize = 12;

pub struct AsmIde {
    src: [[u8; IDE_COLS]; IDE_ROWS],
    cx: i32, cy: i32,
    status: [u8; 64],
    status_len: usize,
    asm: asm_lang::Asm,
    save_mode: bool,
    save_name: [u8; 32],
    save_name_len: usize,
}

impl AsmIde {
    pub const fn new() -> Self {
        Self {
            src: [[b' '; IDE_COLS]; IDE_ROWS],
            cx: 0, cy: 0,
            status: [0; 64], status_len: 0,
            asm: asm_lang::Asm::new(),
            save_mode: false,
            save_name: [0; 32], save_name_len: 0,
        }
    }
    fn set_status(&mut self, st: &[u8]) {
        let n = if st.len() > 63 { 63 } else { st.len() };
        let mut i = 0;
        while i < n { self.status[i] = st[i]; i += 1; }
        self.status[n] = 0;
        self.status_len = n;
    }
    fn src_bytes(&self, out: &mut [u8]) -> usize {
        let mut k = 0;
        let mut y = 0;
        while y < IDE_ROWS {
            let mut last = 0;
            let mut x = 0;
            while x < IDE_COLS {
                if self.src[y][x] != b' ' { last = x + 1; }
                x += 1;
            }
            x = 0;
            while x < last {
                if k < out.len() - 1 { out[k] = self.src[y][x]; k += 1; }
                x += 1;
            }
            if k < out.len() - 1 { out[k] = b'\n'; k += 1; }
            y += 1;
        }
        if k < out.len() { out[k] = 0; }
        k
    }
    fn assemble(&mut self) {
        let mut buf = [0u8; 1024];
        let n = self.src_bytes(&mut buf);
        self.asm.assemble(&buf[..n]);
        if self.asm.ok {
            self.set_status(b"OK. F6=run  F7=save");
        } else {
            self.set_status(b"ERR. fix code");
        }
    }
    fn run(&mut self) {
        if !self.asm.ok {
            self.set_status(b"press F5 to assemble first");
            return;
        }
        if self.asm.out_len == 0 {
            self.set_status(b"empty bytecode");
            return;
        }
        let mut msg = [0u8; 64];
        let mut k = 0;
        for c in b"running " { msg[k] = *c; k += 1; }
        let mut v = self.asm.out_len as u32;
        let mut d = [0u8; 12]; let mut dn = 0;
        if v == 0 { d[dn] = b'0'; dn = 1; }
        else { while v > 0 { d[dn] = b'0' + (v % 10) as u8; v /= 10; dn += 1; } }
        while dn > 0 { dn -= 1; msg[k] = d[dn]; k += 1; }
        for c in b" bytes..." { msg[k] = *c; k += 1; }
        self.set_status(&msg[..k]);

        let rc = unsafe { munix_exec_bytes(self.asm.out.as_ptr(), self.asm.out_len as i32) };

        let mut msg2 = [0u8; 48];
        let mut k2 = 0;
        for c in b"run OK, rc=" { msg2[k2] = *c; k2 += 1; }
        let abs = if rc < 0 { -rc as u32 } else { rc as u32 };
        if rc < 0 { msg2[k2] = b'-'; k2 += 1; }
        let mut v = abs;
        let mut d = [0u8; 12]; let mut dn = 0;
        if v == 0 { d[dn] = b'0'; dn = 1; }
        else { while v > 0 { d[dn] = b'0' + (v % 10) as u8; v /= 10; dn += 1; } }
        while dn > 0 { dn -= 1; msg2[k2] = d[dn]; k2 += 1; }
        self.set_status(&msg2[..k2]);
    }
    fn do_save(&mut self) {
        if self.save_name_len == 0 { self.set_status(b"empty name"); return; }
        let mut full = [0u8; 40];
        let mut n = 0;
        let mut i = 0;
        while i < self.save_name_len && n < 35 {
            full[n] = self.save_name[i]; n += 1; i += 1;
        }
        let ext = b".bin";
        let mut j = 0;
        while j < 4 && n < 39 { full[n] = ext[j]; n += 1; j += 1; }
        full[n] = 0;
        let rc = unsafe {
            munix_ramfs_create(full.as_ptr(), munix_ramfs_root(), 0);
            munix_ramfs_write(full.as_ptr(), munix_ramfs_root(),
                              self.asm.out.as_ptr(), self.asm.out_len as i32)
        };
        if rc < 0 { self.set_status(b"save failed"); }
        else { self.set_status(b"saved as .bin"); }
        self.save_mode = false;
        self.save_name_len = 0;
    }
    fn fill_sample(&mut self) {
        // sample: mov eax,42; ret
        let lines: [&[u8]; 3] = [b"mov eax, 42", b"ret", b""];
        let mut i = 0;
        while i < IDE_ROWS {
            let mut x = 0;
            while x < IDE_COLS { self.src[i][x] = b' '; x += 1; }
            i += 1;
        }
        i = 0;
        while i < lines.len() && i < IDE_ROWS {
            let l = lines[i];
            let mut x = 0;
            while x < l.len() && x < IDE_COLS {
                self.src[i][x] = l[x];
                x += 1;
            }
            i += 1;
        }
        self.cx = 0; self.cy = 0;
        self.set_status(b"sample loaded");
    }
    fn handle_key(&mut self, k: i32) {
        if self.save_mode {
            if k >= 32 && k < 127 {
                if self.save_name_len < 31 {
                    self.save_name[self.save_name_len] = k as u8;
                    self.save_name_len += 1;
                }
                return;
            }
            if k == 8 {
                if self.save_name_len > 0 { self.save_name_len -= 1; }
                return;
            }
            if k == 10 || k == 13 { self.do_save(); return; }
            if k == 0x104 {
                self.save_mode = false; self.save_name_len = 0;
                self.set_status(b"save cancelled");
                return;
            }
            return;
        }
        if k == 0x108 { self.assemble(); return; }
        if k == 0x109 { self.run(); return; }
        if k == 0x10A { self.save_mode = true; self.save_name_len = 0;
                        self.set_status(b"name? ENTER=ok ESC=cancel"); return; }
        if k == 0x10B { self.fill_sample(); return; }
        if k >= 32 && k < 127 {
            if (self.cx as usize) < IDE_COLS {
                self.src[self.cy as usize][self.cx as usize] = k as u8;
                self.cx += 1;
            }
            return;
        }
        if k == 8 {
            if self.cx > 0 {
                self.cx -= 1;
                self.src[self.cy as usize][self.cx as usize] = b' ';
            }
            return;
        }
        if k == 10 || k == 13 {
            if (self.cy as usize) < IDE_ROWS - 1 { self.cy += 1; self.cx = 0; }
            return;
        }
        if k == 0x100 { if self.cy > 0 { self.cy -= 1; } return; }
        if k == 0x101 { if (self.cy as usize) < IDE_ROWS - 1 { self.cy += 1; } return; }
        if k == 0x102 { if self.cx > 0 { self.cx -= 1; } return; }
        if k == 0x103 { if (self.cx as usize) < IDE_COLS { self.cx += 1; } return; }
    }
    fn render(&self, s: &mut Surface, x: i32, y: i32, w: i32, h: i32) {
        s.rect_fill(x, y, w, h, theme::BG_TERM);
        s.text_cstr(x + 6, y + 2,
                    b"ASM IDE  F5=asm F6=run F7=save F8=sample",
                    theme::TEXT_DIM);
        let line_h = 14;
        let start = y + 22;
        let mut row = 0;
        while row < IDE_ROWS {
            let yy = start + (row as i32) * line_h;
            if yy > y + h - 40 { break; }
            let mut num = [0u8; 2];
            num[0] = b'0' + ((row / 10) % 10) as u8;
            num[1] = b'0' + (row % 10) as u8;
            s.text_cstr(x + 6, yy, &num[..2], theme::TEXT_DIM);
            let line = &self.src[row];
            let mut len = IDE_COLS;
            while len > 0 && line[len - 1] == b' ' { len -= 1; }
            if len > 0 { s.text_cstr(x + 30, yy, &line[..len], theme::TEXT); }
            row += 1;
        }
        if !self.save_mode {
            let cy = start + self.cy * line_h;
            let cx = x + 30 + self.cx * 8;
            s.rect_fill(cx, cy, 7, 10, theme::ACCENT_H);
        }
        s.rect_fill(x, y + h - 18, w, 18, theme::BG_PANEL);
        let sl = self.status_len;
        s.text_cstr(x + 6, y + h - 14, &self.status[..sl],
                    if self.asm.ok { theme::TEXT_OK } else { theme::TEXT_ERR });

        if self.save_mode {
            let dw = 320; let dh = 70;
            let dx = x + (w - dw) / 2;
            let dy = y + (h - dh) / 2;
            s.panel(dx, dy, dw, dh, theme::BG_PANEL, theme::ACCENT_H);
            s.rect_fill(dx + 1, dy + 1, dw - 2, 22, theme::ACCENT);
            s.text_cstr(dx + 8, dy + 5, b"Save as (name.bin)", 0xFFFFFFFF);
            s.text_cstr(dx + 10, dy + 32, &self.save_name[..self.save_name_len], theme::TEXT);
            let cx = dx + 10 + (self.save_name_len as i32) * 8;
            s.rect_fill(cx, dy + 32, 7, 9, theme::ACCENT_H);
            s.text_cstr(dx + 10, dy + 50, b"ENTER=save  ESC=cancel", theme::TEXT_DIM);
        }
    }
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
// Welcome overlay
// ============================================================
fn draw_welcome_overlay(s: &mut Surface) {
    let w = 720;
    let h = 420;
    let x = (s.w - w) / 2;
    let y = (s.h - h) / 2;
    s.rect_fill(0, 0, s.w, s.h, 0xAA000000);
    s.panel_fill(x + 6, y + 6, w, h, 0x80000000);
    s.panel(x, y, w, h, theme::BG_PANEL, theme::ACCENT_H);
    s.rect_fill(x + 1, y + 1, w - 2, 36, theme::ACCENT);
    // re-round top corners
    for dx in 0..theme::CORNER_R {
        for dy in 0..theme::CORNER_R {
            if !CORNER_TL[dy as usize][dx as usize] {
                s.put(x + dx, y + dy, theme::BG_PANEL);
                s.put(x + w - 1 - dx, y + dy, theme::BG_PANEL);
            }
        }
    }
    s.text_cstr(x + 20, y + 12, b"Welcome to mUnix v8.2 (release)", 0xFFFFFFFF);
    let mut yy = y + 60;
    s.text_cstr(x + 30, yy, b"Your new open-source operating system.", theme::TEXT); yy += 24;
    s.text_cstr(x + 30, yy, b"Version: v8.2 (release)  |  64-bit long mode", theme::ACCENT_H); yy += 20;
    s.text_cstr(x + 30, yy, b"Running entirely in RAM. No installation required.", theme::TEXT); yy += 20;
    s.text_cstr(x + 30, yy, b"Built with C and Rust. Boots on any x86 machine via QEMU.", theme::TEXT); yy += 36;
    s.text_cstr(x + 30, yy, b"Components:", theme::TEXT_DIM); yy += 24;
    s.text_cstr(x + 45, yy, b"GRUB2 + Multiboot2 bootloader", theme::TEXT); yy += 18;
    s.text_cstr(x + 45, yy, b"64-bit long mode kernel (C + asm)", theme::TEXT); yy += 18;
    s.text_cstr(x + 45, yy, b"Rust GUI: window manager, dock, control center", theme::TEXT); yy += 18;
    s.text_cstr(x + 45, yy, b"RAMFS with real file operations", theme::TEXT); yy += 18;
    s.text_cstr(x + 45, yy, b"GOP framebuffer via Multiboot2 (1280x720x32)", theme::TEXT); yy += 18;
    s.text_cstr(x + 45, yy, b"IDT + exception handlers (64-bit)", theme::TEXT); yy += 18;
    s.text_cstr(x + 45, yy, b"Games: Minesweeper, Snake, Pong, Shapes, Tetris", theme::TEXT); yy += 36;
    s.text_cstr(x + 30, yy, b"To learn more about mUnix and follow updates, visit:", theme::TEXT); yy += 24;
    s.text_cstr(x + 30, yy, b"https://github.com/davidchekushka-hue/mUnix-os", theme::ACCENT_H); yy += 40;
    s.text_cstr(x + 30, yy, b"Click anywhere or press any key to continue.", theme::TEXT_DIM);
}

// ============================================================
// Wallpaper raccoon
// ============================================================
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

// ============================================================
// Global + ABI
// ============================================================
static mut PENDING_OPEN_BROWSER: bool = false;

static mut PENDING_OPEN_ASM: bool = false;

static mut PENDING_OPEN_OFFICE: bool = false;

static mut PENDING_OPEN_SYSMON: bool = false;

static mut PENDING_OPEN_ABOUT: bool = false;
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
    g.show_welcome = true;
    g.term = Terminal::new();
    g.editor = Editor::new();
    g.files = Files::new();
    g.media = Media::new();
    g.settings = Settings::new();
    g.ms = Minesweeper::new(); g.ms.reset();
    g.sn = Snake::new();       g.sn.reset();
    g.pg = Pong::new();        g.pg.reset();
    g.sh = Shapes::new();
    g.tt = Tetris::new();      g.tt.reset();
    g.term.print(b"mUnix v8.2 (release) -- 64-bit long mode, Rust GUI");
    g.term.print(b"Type 'help' for full list of commands.");
    g.term.print(b"");
    g.term.print(b"Dock: Terminal Files Editor Media Settings");
    g.term.print(b"      Minesweeper Snake Pong Shapes Tetris");
}

#[no_mangle]
pub unsafe extern "C" fn munix_gui_open_window(kind: u32, _t: *const u8, _l: usize) {
    let g = gui();
    let k = match kind {
        1 => Kind::Terminal, 2 => Kind::Files, 3 => Kind::Editor,
        4 => Kind::Media,    5 => Kind::Settings,
        6 => Kind::Minesweeper, 7 => Kind::Snake, 8 => Kind::Pong,
        9 => Kind::Shapes,  10 => Kind::Tetris,
        _ => Kind::Terminal,
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
    /* (installer check also in ticks — see below) */
    /* (installer check also in ticks — see below) */
    if g.show_welcome { g.show_welcome = false; return; }
    g.media.tick();

    let f = g.focused;
    if f < 0 || (f as usize) >= g.nwin { return; }
    let kind = g.wins[f as usize].kind;
    let gptr = g as *mut Gui;
    match kind {
        Kind::Terminal => {
            let term = &mut (*gptr).term;
            let editor = &mut (*gptr).editor;
            term.handle_key(k, 0, editor);
        }
        Kind::Files    => {
            let files = &mut (*gptr).files;
            let editor = &mut (*gptr).editor;
            files.handle_key(k, editor);
        }
        Kind::Editor      => { (*gptr).editor.handle_key(k); }
        Kind::Media       => { (*gptr).media.handle_key(k); }
        Kind::Settings    => { (*gptr).settings.handle_key(k); }
        Kind::Minesweeper => { (*gptr).ms.handle_key(k); }
        Kind::Snake       => { (*gptr).sn.handle_key(k); }
        Kind::Pong        => { (*gptr).pg.handle_key(k); }
        Kind::Shapes      => { (*gptr).sh.handle_key(k); }
        Kind::Tetris      => { (*gptr).tt.handle_key(k); }
        Kind::Ide         => { (*gptr).ide.handle_key(k); }
        Kind::SysMon      => { (*gptr).sysmon.handle_key(k); }
        Kind::Office      => {
            extern "C" { fn serial_puts(s: *const u8); }
            unsafe { serial_puts(b"gk: routed to Office\n\0".as_ptr()); }
            (*gptr).office.handle_key(k);
        }
        Kind::Browser     => { (*gptr).browser.handle_key(k); }
        Kind::About       => { (*gptr).about_win.handle_key(k); }
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
    if PENDING_OPEN_ABOUT {
        PENDING_OPEN_ABOUT = false;
        g.open(Kind::About);
    }

    if PENDING_OPEN_SYSMON {
        PENDING_OPEN_SYSMON = false;
        g.open(Kind::SysMon);
    }
    if PENDING_OPEN_OFFICE {
        PENDING_OPEN_OFFICE = false;
        extern "C" { fn serial_puts(s: *const u8); }
        unsafe { serial_puts(b"ticks: opening Office\n\0".as_ptr()); }
        g.open(Kind::Office);
    }
    if PENDING_OPEN_ASM {
        PENDING_OPEN_ASM = false;
        g.open(Kind::Ide);
    }
    if PENDING_OPEN_BROWSER {
        PENDING_OPEN_BROWSER = false;
        g.open(Kind::Browser);
    }
    // advance games regardless of focus so they keep running
    let mut any_game = false;
    for i in 0..g.nwin {
        if !g.wins[i].visible { continue; }
        match g.wins[i].kind {
            Kind::Snake  => { g.sn.tick(); any_game = true; }
            Kind::Pong   => { g.pg.tick(); any_game = true; }
            Kind::Tetris => { g.tt.tick(); any_game = true; }
            _ => {}
        }
    }
    let _ = any_game;
    g.media.tick();
}

#[no_mangle]
pub unsafe extern "C" fn munix_gui_animated() -> i32 {
    // Возвращает 1 если какая-то анимированная игра на экране,
    // чтобы C-сторона перерисовывала кадр.
    let g = gui();
    for i in 0..g.nwin {
        if !g.wins[i].visible { continue; }
        match g.wins[i].kind {
            Kind::Snake | Kind::Pong | Kind::Tetris => return 1,
            _ => {}
        }
    }
    g.media.playing as i32
}
