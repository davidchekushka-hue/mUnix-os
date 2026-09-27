//! mUnix v0.8.0 — GUI core (Rust, no_std, x86_64-unknown-none)
//!
//! Vector-only UI. No bitmaps, no legacy icons. Every glyph
//! is drawn from primitives: line, rect, circle, arc.
//!
//! Backbuffer is `&mut [u32]` in 0xAARRGGBB format.
//! All drawing is done directly into the slice — no allocator,
//! no global state, no panics on the hot path.

#![no_std]
#![allow(dead_code)]

use core::cmp::{max, min};

// ============================================================
// Theme — flat minimalism
// ============================================================

pub mod theme {
    pub const BG_DESKTOP: u32 = 0xFF0E1B2A;
    pub const BG_WINDOW:  u32 = 0xFF141A26;
    pub const BG_PANEL:   u32 = 0xFF1B2333;
    pub const BORDER:     u32 = 0xFF2A3446;
    pub const BORDER_HI:  u32 = 0xFF3F4D66;
    pub const TEXT:       u32 = 0xFFE6EDF3;
    pub const TEXT_DIM:   u32 = 0xFF8A96A8;
    pub const ACCENT:     u32 = 0xFF3B82F6;
    pub const ACCENT_HI:  u32 = 0xFF60A5FA;
    pub const OK:         u32 = 0xFF22C55E;
    pub const WARN:       u32 = 0xFFFACC15;
    pub const ERR:        u32 = 0xFFEF4444;
    pub const SHADOW:     u32 = 0x60000000;

    pub const CORNER_R: i32 = 4;
    pub const DOCK_H:   i32 = 44;
    pub const CC_TILE:  i32 = 46;
    pub const CC_GAP:   i32 = 6;
}

// ============================================================
// Integer helpers (no libm, no float on the hot path)
// ============================================================

#[inline]
fn isqrt(n: i32) -> i32 {
    if n <= 0 { return 0; }
    let mut x = n;
    let mut y = (x + 1) / 2;
    while y < x {
        x = y;
        y = (x + n / x) / 2;
    }
    x
}

#[inline]
fn lerp8(dst: u32, src: u32, a: u32) -> u32 {
    (dst * (255 - a) + src * a) / 255
}

// 4x4 mask for the top-left corner of radius 4.
// 'true' = pixel belongs to the rounded shape.
const CORNER_TL: [[bool; 4]; 4] = [
    [false, false, false, true ],
    [false, true,  true,  true ],
    [false, true,  true,  true ],
    [true,  true,  true,  true ],
];

// ============================================================
// Surface — the drawing backend
// ============================================================

pub struct Surface<'a> {
    pub px: &'a mut [u32],
    pub w:  i32,
    pub h:  i32,
}

impl<'a> Surface<'a> {
    #[inline]
    pub fn new(px: &'a mut [u32], w: i32) -> Self {
        debug_assert!(w > 0);
        let h = (px.len() as i32) / w;
        Self { px, w, h }
    }

    #[inline]
    fn idx(&self, x: i32, y: i32) -> Option<usize> {
        if x < 0 || y < 0 || x >= self.w || y >= self.h {
            None
        } else {
            Some((y as usize) * (self.w as usize) + (x as usize))
        }
    }

    #[inline]
    pub fn put(&mut self, x: i32, y: i32, c: u32) {
        if let Some(i) = self.idx(x, y) { self.px[i] = c; }
    }

    #[inline]
    pub fn blend(&mut self, x: i32, y: i32, fg: u32) {
        let a = (fg >> 24) & 0xFF;
        if a == 0xFF { self.put(x, y, fg); return; }
        if a == 0    { return; }
        if let Some(i) = self.idx(x, y) {
            let bg = self.px[i];
            let r = lerp8((bg >> 16) & 0xFF, (fg >> 16) & 0xFF, a);
            let g = lerp8((bg >>  8) & 0xFF, (fg >>  8) & 0xFF, a);
            let b = lerp8( bg        & 0xFF,  fg        & 0xFF, a);
            self.px[i] = 0xFF00_0000 | (r << 16) | (g << 8) | b;
        }
    }

    pub fn clear(&mut self, c: u32) {
        for p in self.px.iter_mut() { *p = c; }
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

    pub fn polyline(&mut self, pts: &[(i32, i32)], c: u32) {
        for i in 1..pts.len() {
            self.line(pts[i - 1].0, pts[i - 1].1, pts[i].0, pts[i].1, c);
        }
    }

    pub fn circle(&mut self, cx: i32, cy: i32, r: i32, c: u32) {
        if r <= 0 { return; }
        let mut x = 0;
        let mut y = r;
        let mut d = 1 - r;
        while x <= y {
            self.put(cx + x, cy + y, c); self.put(cx - x, cy + y, c);
            self.put(cx + x, cy - y, c); self.put(cx - x, cy - y, c);
            self.put(cx + y, cy + x, c); self.put(cx - y, cy + x, c);
            self.put(cx + y, cy - x, c); self.put(cx - y, cy - x, c);
            if d < 0 { d += 2 * x + 3; }
            else     { d += 2 * (x - y) + 5; y -= 1; }
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

    // -------- Flat panel with 1px border and 4px top corners --------

    pub fn panel(&mut self, x: i32, y: i32, w: i32, h: i32, bg: u32, border: u32) {
        self.panel_fill(x, y, w, h, bg);
        self.panel_border(x, y, w, h, border);
    }

    pub fn panel_fill(&mut self, x: i32, y: i32, w: i32, h: i32, c: u32) {
        let r = theme::CORNER_R;
        if h <= r {
            self.rect_fill(x, y, w, h, c);
            return;
        }
        // body below corners
        self.rect_fill(x, y + r, w, h - r, c);
        // top strip with corner mask
        for dy in 0..r {
            for dx in 0..w {
                let inside = if dx < r {
                    CORNER_TL[dy as usize][dx as usize]
                } else if dx >= w - r {
                    CORNER_TL[dy as usize][(w - 1 - dx) as usize]
                } else {
                    true
                };
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
        // corner arcs — 4-point bezier-ish polyline for r=4
        let arc: [(i32, i32); 4] = [(3, 0), (1, 1), (1, 2), (0, 3)];
        for &(dx, dy) in arc.iter() {
            if dx < r && dy < r {
                self.put(x + dx, y + dy, c);
                self.put(x + w - 1 - dx, y + dy, c);
            }
        }
    }

    // Vertical gradient, useful for desktop background
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
}

// ============================================================
// Procedural icons — pure geometry
// ============================================================

pub mod icon {
    use super::{Surface, theme};

    pub fn terminal(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        s.rect(x, y, sz, sz, c);
        let p = sz / 4;
        s.line(x + p, y + p, x + 2 * p, y + sz / 2, c);
        s.line(x + 2 * p, y + sz / 2, x + p, y + sz - p, c);
        s.rect_fill(x + 2 * p + 2, y + sz - p - 2, 4, 2, c);
    }

    pub fn files(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let tab_w = sz / 3;
        let tab_h = sz / 5;
        s.rect(x, y + tab_h, sz, sz - tab_h, c);
        s.rect(x, y, tab_w, tab_h + 1, c);
    }

    pub fn editor(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let l = x + sz / 6;
        let r = x + sz - sz / 6;
        s.rect(l, y, r - l, sz, c);
        for i in 1..=3 {
            let ly = y + (sz * i) / 4;
            s.hline(l + 3, r - 3, ly, c);
        }
    }

    pub fn media(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let cx = x + sz / 2;
        let cy = y + sz / 2;
        let r = sz / 2 - 2;
        s.line(cx - r + 3, cy - r, cx + r, cy, c);
        s.line(cx + r, cy, cx - r + 3, cy + r, c);
        s.line(cx - r + 3, cy + r, cx - r + 3, cy - r, c);
    }

    pub fn settings(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let cx = x + sz / 2;
        let cy = y + sz / 2;
        let r = sz / 4;
        s.circle(cx, cy, r, c);
        s.circle(cx, cy, r / 2, c);
        let dirs: [(i32, i32); 8] = [
            (1, 0), (1, 1), (0, 1), (-1, 1),
            (-1, 0), (-1, -1), (0, -1), (1, -1),
        ];
        for (dx, dy) in dirs.iter() {
            s.line(cx + dx * r, cy + dy * r,
                   cx + dx * (r + 3), cy + dy * (r + 3), c);
        }
    }

    pub fn snake(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let pts: [(i32, i32); 5] = [
            (x + 2,               y + sz - 3),
            (x + sz / 4,          y + sz / 2),
            (x + sz / 2,          y + sz - 3),
            (x + 3 * sz / 4,      y + sz / 2),
            (x + sz - 2,          y + sz - 3),
        ];
        s.polyline(&pts, c);
    }

    pub fn pong(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        s.rect_fill(x + 1,             y + sz / 4, 2, sz / 2, c);
        s.rect_fill(x + sz - 3,        y + sz / 3, 2, sz / 2, c);
        s.rect_fill(x + sz / 2 - 1,    y + sz / 2 - 1, 3, 3, c);
    }

    pub fn shapes(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        let h = sz / 2;
        s.circle(x + h, y + h, h - 4, c);
        s.rect(x + h, y + h, h, h, c);
    }

    pub fn trash(s: &mut Surface, x: i32, y: i32, sz: i32, c: u32) {
        s.rect(x + 3, y + 4, sz - 6, sz - 6, c);
        s.hline(x + 1, x + sz - 2, y + 3, c);
        s.vline(x + sz / 2 - 2, y, y + 3, c);
        s.vline(x + sz / 2 + 2, y, y + 3, c);
        s.hline(x + sz / 2 - 2, x + sz / 2 + 2, y, c);
    }

    pub fn wifi(s: &mut Surface, cx: i32, cy: i32, c: u32) {
        s.circle(cx, cy, 4, c);
        s.circle(cx, cy, 8, c);
        s.circle(cx, cy, 12, c);
        // cut lower half
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
// Window manager
// ============================================================

pub const MAX_WINDOWS: usize = 8;

#[derive(Copy, Clone, PartialEq, Eq)]
pub enum WindowKind {
    None,
    Terminal,
    Files,
    Editor,
    Media,
    Settings,
}

#[derive(Copy, Clone)]
pub struct Window {
    pub x: i32, pub y: i32,
    pub w: i32, pub h: i32,
    pub kind: WindowKind,
    pub title: [u8; 32],
    pub title_len: usize,
    pub visible: bool,
    pub focused: bool,
}

impl Window {
    pub const fn empty() -> Self {
        Self {
            x: 0, y: 0, w: 0, h: 0,
            kind: WindowKind::None,
            title: [0u8; 32], title_len: 0,
            visible: false, focused: false,
        }
    }
}

pub struct WindowManager {
    pub windows: [Window; MAX_WINDOWS],
    pub n: usize,
    pub focused: Option<usize>,
}

impl WindowManager {
    pub const fn new() -> Self {
        Self {
            windows: [Window::empty(); MAX_WINDOWS],
            n: 0,
            focused: None,
        }
    }

    pub fn open(&mut self, kind: WindowKind, title: &[u8]) -> Option<usize> {
        if self.n >= MAX_WINDOWS { return None; }
        let i = self.n;
        let mut w = Window::empty();
        w.kind    = kind;
        w.visible = true;
        w.x = 40 + (i as i32) * 24;
        w.y = 30 + (i as i32) * 24;
        w.w = 320;
        w.h = 200;
        let len = min(title.len(), 32);
        w.title[..len].copy_from_slice(&title[..len]);
        w.title_len = len;
        self.windows[i] = w;
        self.n += 1;
        self.focused = Some(i);
        Some(i)
    }

    pub fn close(&mut self, i: usize) {
        if i >= self.n { return; }
        self.windows[i].visible = false;
        if self.focused == Some(i) { self.focused = None; }
    }

    pub fn render(&self, s: &mut Surface) {
        for i in 0..self.n {
            let w = self.windows[i];
            if !w.visible { continue; }
            self.render_one(s, &w, i);
        }
    }

    fn render_one(&self, s: &mut Surface, w: &Window, idx: usize) {
        let border = if self.focused == Some(idx) {
            theme::ACCENT_HI
        } else {
            theme::BORDER
        };
        // flat drop shadow (2px offset)
        s.panel_fill(w.x + 2, w.y + 2, w.w, w.h, theme::SHADOW);
        // panel
        s.panel(w.x, w.y, w.w, w.h, theme::BG_WINDOW, border);
        // thin separator under the (implicit) title strip
        s.hline(w.x + 6, w.x + w.w - 7, w.y + 22, theme::BORDER);
        // minimal close glyph top-right
        let cx = w.x + w.w - 14;
        let cy = w.y + 11;
        s.line(cx - 4, cy - 4, cx + 4, cy + 4, theme::TEXT_DIM);
        s.line(cx - 4, cy + 4, cx + 4, cy - 4, theme::TEXT_DIM);
    }
}

// ============================================================
// Floating dock
// ============================================================

pub const MAX_DOCK_ITEMS: usize = 6;

#[derive(Copy, Clone)]
pub struct DockItem {
    pub kind: WindowKind,
    pub active: bool,
}

pub struct Dock {
    pub items: [DockItem; MAX_DOCK_ITEMS],
    pub n: usize,
    pub hover: Option<usize>,
}

impl Dock {
    pub const fn new() -> Self {
        Self {
            items: [DockItem { kind: WindowKind::None, active: false }; MAX_DOCK_ITEMS],
            n: 0,
            hover: None,
        }
    }

    pub fn push(&mut self, k: WindowKind) -> Option<usize> {
        if self.n >= MAX_DOCK_ITEMS { return None; }
        self.items[self.n] = DockItem { kind: k, active: false };
        let i = self.n;
        self.n += 1;
        Some(i)
    }

    pub fn render(&self, s: &mut Surface) {
        if self.n == 0 { return; }
        let icon_sz: i32 = 28;
        let pad: i32 = 8;
        let gap: i32 = 4;
        let total_w = (self.n as i32) * icon_sz
                    + ((self.n as i32) - 1) * gap
                    + pad * 2;
        let x = (s.w - total_w) / 2;
        let y = s.h - theme::DOCK_H - 8;

        s.panel_fill(x + 2, y + 2, total_w, theme::DOCK_H, theme::SHADOW);
        s.panel(x, y, total_w, theme::DOCK_H, theme::BG_PANEL, theme::BORDER);

        for i in 0..self.n {
            let ix = x + pad + (i as i32) * (icon_sz + gap);
            let iy = y + (theme::DOCK_H - icon_sz) / 2;

            if self.hover == Some(i) {
                s.rect_fill(ix - 4, iy - 4, icon_sz + 8, icon_sz + 8, 0x33FFFFFF);
            }
            if self.items[i].active {
                s.rect_fill(ix + icon_sz / 2 - 2, y + theme::DOCK_H - 4,
                            4, 2, theme::ACCENT);
            }
            draw_icon(s, self.items[i].kind, ix, iy, icon_sz, theme::TEXT);
        }
    }
}

fn draw_icon(s: &mut Surface, kind: WindowKind, x: i32, y: i32, sz: i32, c: u32) {
    match kind {
        WindowKind::Terminal => icon::terminal(s, x, y, sz, c),
        WindowKind::Files    => icon::files(s, x, y, sz, c),
        WindowKind::Editor   => icon::editor(s, x, y, sz, c),
        WindowKind::Media    => icon::media(s, x, y, sz, c),
        WindowKind::Settings => icon::settings(s, x, y, sz, c),
        WindowKind::None     => {}
    }
}

// ============================================================
// HyperOS-style control center (top-right grid)
// ============================================================

#[derive(Copy, Clone)]
pub enum TileKind {
    Wifi,
    Bluetooth,
    Volume,
    Brightness,
    Battery,
    Theme,
    Lock,
    Screen,
}

#[derive(Copy, Clone)]
pub struct Tile {
    pub label: TileKind,
    pub on: bool,
    pub value: u8,
}

pub const CC_TILES: usize = 8;

pub struct ControlCenter {
    pub open: bool,
    pub tiles: [Tile; CC_TILES],
    pub hover: Option<usize>,
}

impl ControlCenter {
    pub const fn new() -> Self {
        Self {
            open: true,
            tiles: [
                Tile { label: TileKind::Wifi,       on: true,  value: 80 },
                Tile { label: TileKind::Bluetooth,  on: false, value: 0  },
                Tile { label: TileKind::Volume,     on: true,  value: 60 },
                Tile { label: TileKind::Brightness, on: true,  value: 90 },
                Tile { label: TileKind::Battery,    on: true,  value: 74 },
                Tile { label: TileKind::Theme,      on: true,  value: 0  },
                Tile { label: TileKind::Lock,       on: true,  value: 0  },
                Tile { label: TileKind::Screen,     on: true,  value: 0  },
            ],
            hover: None,
        }
    }

    pub fn render(&self, s: &mut Surface) {
        if !self.open { return; }
        let cols: i32 = 2;
        let rows: i32 = 4;
        let t = theme::CC_TILE;
        let g = theme::CC_GAP;
        let pad: i32 = 10;
        let w = cols * t + (cols - 1) * g + pad * 2;
        let h = rows * t + (rows - 1) * g + pad * 2;
        let x = s.w - w - 12;
        let y = 12;

        s.panel_fill(x + 2, y + 2, w, h, theme::SHADOW);
        s.panel(x, y, w, h, theme::BG_PANEL, theme::BORDER);

        for i in 0..CC_TILES {
            let col = (i as i32) % cols;
            let row = (i as i32) / cols;
            let tx = x + pad + col * (t + g);
            let ty = y + pad + row * (t + g);
            self.render_tile(s, &self.tiles[i], tx, ty, t);
        }
    }

    fn render_tile(&self, s: &mut Surface, tile: &Tile, x: i32, y: i32, sz: i32) {
        let bg = if tile.on { theme::ACCENT } else { theme::BORDER };
        let fg = theme::TEXT;
        let cx = x + sz / 2;
        let cy = y + sz / 2;

        s.rect_fill(x, y, sz, sz, bg);
        s.rect(x, y, sz, sz, theme::BORDER_HI);

        match tile.label {
            TileKind::Wifi => {
                icon::wifi(s, cx, cy, fg);
            }
            TileKind::Bluetooth => {
                s.line(cx - 5, cy - 8, cx - 5, cy + 8, fg);
                s.line(cx - 5, cy - 8, cx + 5, cy,     fg);
                s.line(cx + 5, cy,     cx - 5, cy + 8, fg);
            }
            TileKind::Volume => {
                s.rect_fill(cx - 8, cy - 3, 5, 6, fg);
                s.line(cx - 3, cy - 3, cx + 4, cy - 8, fg);
                s.line(cx + 4, cy - 8, cx + 4, cy + 8, fg);
                s.line(cx + 4, cy + 8, cx - 3, cy + 3, fg);
                s.circle(cx + 2, cy, 6, fg);
            }
            TileKind::Brightness => {
                s.circle(cx, cy, 4, fg);
                let dirs: [(i32, i32); 8] = [
                    (1, 0), (1, 1), (0, 1), (-1, 1),
                    (-1, 0), (-1, -1), (0, -1), (1, -1),
                ];
                for (dx, dy) in dirs.iter() {
                    s.line(cx + dx * 6, cy + dy * 6, cx + dx * 10, cy + dy * 10, fg);
                }
            }
            TileKind::Battery => {
                let bw = 22;
                let bh = 10;
                icon::battery(s, cx - bw / 2, cy - bh / 2, bw, bh, tile.value, fg);
            }
            TileKind::Theme => {
                s.circle_fill(cx, cy, 8, fg);
                s.rect_fill(cx, cy - 8, 9, 16, bg);
                s.circle(cx, cy, 8, fg);
            }
            TileKind::Lock => {
                s.rect_fill(cx - 7, cy - 2, 14, 10, fg);
                s.rect(cx - 5, cy - 8, 10, 8, fg);
            }
            TileKind::Screen => {
                s.rect(cx - 10, cy - 8, 20, 14, fg);
                s.rect_fill(cx - 3, cy + 6, 6, 2, fg);
            }
        }
    }
}

// ============================================================
// Top-level GUI facade
// ============================================================

pub struct Gui {
    pub wm:  WindowManager,
    pub dock: Dock,
    pub cc:  ControlCenter,
    pub mouse_x: i32,
    pub mouse_y: i32,
    pub cursor_visible: bool,
}

impl Gui {
    pub const fn new() -> Self {
        Self {
            wm: WindowManager::new(),
            dock: Dock::new(),
            cc: ControlCenter::new(),
            mouse_x: 0,
            mouse_y: 0,
            cursor_visible: true,
        }
    }

    /// Full frame. Caller must give us a backbuffer sized `w*h`.
    pub fn render(&mut self, s: &mut Surface) {
        // 1) desktop background — flat with a subtle vertical gradient
        s.gradient_v(0, s.h, theme::BG_DESKTOP, 0xFF060A12);
        // 2) windows
        self.wm.render(s);
        // 3) control center
        self.cc.render(s);
        // 4) dock
        self.dock.render(s);
        // 5) cursor
        if self.cursor_visible {
            draw_cursor(s, self.mouse_x, self.mouse_y);
        }
    }
}

fn draw_cursor(s: &mut Surface, x: i32, y: i32) {
    // shadow
    let sh = theme::SHADOW;
    s.line(x,     y,     x,     y + 13, sh);
    s.line(x,     y + 13, x + 4, y + 9,  sh);
    s.line(x + 4, y + 9,  x + 6, y + 14, sh);
    s.line(x + 6, y + 14, x + 8, y + 13, sh);
    s.line(x + 8, y + 13, x + 5, y + 8,  sh);
    s.line(x + 5, y + 8,  x + 10, y + 8, sh);
    s.line(x + 10, y + 8, x,     y,      sh);
    // foreground
    let c = theme::TEXT;
    s.line(x,     y,     x,     y + 12, c);
    s.line(x,     y + 12, x + 4, y + 8,  c);
    s.line(x + 4, y + 8,  x + 6, y + 13, c);
    s.line(x + 6, y + 13, x + 8, y + 12, c);
    s.line(x + 8, y + 12, x + 5, y + 7,  c);
    s.line(x + 5, y + 7,  x + 10, y + 7, c);
    s.line(x + 10, y + 7, x,     y,      c);
}

// ============================================================
// C ABI — call into this from the existing kernel
// ============================================================

static mut GUI: Gui = Gui::new();

/// Entry point for the C kernel. Rust owns the buffer as `&mut [u32]`.
#[no_mangle]
pub unsafe extern "C" fn munix_gui_render(
    pixels: *mut u32,
    width: i32,
    height: i32,
    mouse_x: i32,
    mouse_y: i32,
) {
    if pixels.is_null() || width <= 0 || height <= 0 { return; }
    let len = (width as usize) * (height as usize);
    let slice = core::slice::from_raw_parts_mut(pixels, len);
    let mut surface = Surface::new(slice, width);
    // (safe: single-threaded kernel, GUI is owned here)
    let gui = &mut *core::ptr::addr_of_mut!(GUI);
    gui.mouse_x = mouse_x;
    gui.mouse_y = mouse_y;
    gui.render(&mut surface);
}

#[no_mangle]
pub unsafe extern "C" fn munix_gui_open_window(kind: u32, title: *const u8, len: usize) {
    let gui = &mut *core::ptr::addr_of_mut!(GUI);
    let k = match kind {
        1 => WindowKind::Terminal,
        2 => WindowKind::Files,
        3 => WindowKind::Editor,
        4 => WindowKind::Media,
        5 => WindowKind::Settings,
        _ => return,
    };
    if title.is_null() { return; }
    let t = core::slice::from_raw_parts(title, len);
    let _ = gui.wm.open(k, t);
}

#[no_mangle]
pub unsafe extern "C" fn munix_gui_init_dock() {
    let gui = &mut *core::ptr::addr_of_mut!(GUI);
    gui.dock.n = 0;
    let _ = gui.dock.push(WindowKind::Terminal);
    let _ = gui.dock.push(WindowKind::Files);
    let _ = gui.dock.push(WindowKind::Editor);
    let _ = gui.dock.push(WindowKind::Media);
    let _ = gui.dock.push(WindowKind::Settings);
}
