//! Minimal x86 assembler for mUnix IDE.

pub const OUT_MAX: usize = 1024;
pub const LABEL_MAX: usize = 32;
pub const ERR_MAX: usize = 8;

pub struct Asm {
    pub out: [u8; OUT_MAX],
    pub out_len: usize,
    pub errs: [[u8; 48]; ERR_MAX],
    pub nerrs: usize,
    pub labels: [(u32, u32); LABEL_MAX],
    pub nlabels: usize,
    pub fixups: [(u32, u32, bool); LABEL_MAX],
    pub nfixups: usize,
    pub dangerous: bool,
    pub ok: bool,
}

fn ident(c: u8) -> bool {
    (c >= b'a' && c <= b'z') || (c >= b'A' && c <= b'Z') ||
    (c >= b'0' && c <= b'9') || c == b'_' || c == b'.'
}
fn trim(mut s: &[u8]) -> &[u8] {
    while !s.is_empty() && (s[0] == b' ' || s[0] == b'\t' || s[0] == b'\r') { s = &s[1..]; }
    while !s.is_empty() {
        let l = s.len(); let c = s[l-1];
        if c == b' ' || c == b'\t' || c == b'\r' { s = &s[..l-1]; } else { break; }
    }
    s
}
fn eqs(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() { return false; }
    for i in 0..a.len() { if a[i] != b[i] { return false; } }
    true
}
fn hash(s: &[u8]) -> u32 {
    let mut h: u32 = 5381;
    for &c in s { h = h.wrapping_mul(33).wrapping_add(c as u32); }
    h
}
fn parse_num(s: &[u8]) -> Option<u32> {
    if s.is_empty() { return None; }
    let mut v: u32 = 0;
    let mut i = 0;
    let hex = s.len() >= 2 && s[0] == b'0' && (s[1] == b'x' || s[1] == b'X');
    if hex { i = 2; if i == s.len() { return None; } }
    while i < s.len() {
        let c = s[i];
        let d = if hex {
            match c {
                b'0'..=b'9' => c - b'0',
                b'a'..=b'f' => c - b'a' + 10,
                b'A'..=b'F' => c - b'A' + 10,
                _ => return None,
            }
        } else {
            if c < b'0' || c > b'9' { return None; }
            c - b'0'
        };
        v = v.wrapping_mul(if hex {16} else {10}).wrapping_add(d as u32);
        i += 1;
    }
    Some(v)
}
fn reg(name: &[u8]) -> u8 {
    if name.len() != 3 || name[0] != b'e' { return 0xFF; }
    match (name[1], name[2]) {
        (b'a', b'x') => 0, (b'c', b'x') => 1, (b'd', b'x') => 2, (b'b', b'x') => 3,
        (b's', b'p') => 4, (b'b', b'p') => 5, (b's', b'i') => 6, (b'd', b'i') => 7,
        _ => 0xFF,
    }
}

impl Asm {
    pub const fn new() -> Self {
        Self {
            out: [0; OUT_MAX], out_len: 0,
            errs: [[0; 48]; ERR_MAX], nerrs: 0,
            labels: [(0,0); LABEL_MAX], nlabels: 0,
            fixups: [(0,0,false); LABEL_MAX], nfixups: 0,
            dangerous: false, ok: true,
        }
    }
    fn emit(&mut self, b: u8) {
        if self.out_len >= OUT_MAX { self.ok = false; return; }
        self.out[self.out_len] = b; self.out_len += 1;
    }
    fn emit32(&mut self, v: u32) {
        self.emit((v & 0xFF) as u8);
        self.emit(((v >> 8) & 0xFF) as u8);
        self.emit(((v >> 16) & 0xFF) as u8);
        self.emit(((v >> 24) & 0xFF) as u8);
    }
    fn err(&mut self, msg: &[u8]) {
        if self.nerrs >= ERR_MAX { return; }
        let slot = self.nerrs;
        let n = if msg.len() > 47 { 47 } else { msg.len() };
        for i in 0..n { self.errs[slot][i] = msg[i]; }
        self.errs[slot][n] = 0;
        self.nerrs += 1;
        self.ok = false;
    }
    fn label_find(&self, h: u32) -> Option<u32> {
        for i in 0..self.nlabels {
            if self.labels[i].0 == h { return Some(self.labels[i].1); }
        }
        None
    }
    fn label_add(&mut self, h: u32, off: u32) {
        if self.nlabels >= LABEL_MAX { return; }
        self.labels[self.nlabels] = (h, off);
        self.nlabels += 1;
    }
    fn fixup_add(&mut self, off: u32, h: u32, jcc: bool) {
        if self.nfixups >= LABEL_MAX { return; }
        self.fixups[self.nfixups] = (off, h, jcc);
        self.nfixups += 1;
    }

    pub fn assemble(&mut self, src: &[u8]) {
        self.out_len = 0;
        self.nlabels = 0;
        self.nfixups = 0;
        self.nerrs = 0;
        self.dangerous = false;
        self.ok = true;

        let mut start = 0;
        let mut i = 0;
        while i <= src.len() {
            if i == src.len() || src[i] == b'\n' {
                self.line(&src[start..i]);
                if !self.ok { return; }
                start = i + 1;
            }
            i += 1;
        }

        let mut k = 0;
        while k < self.nfixups {
            let (ofs, h, is_jcc) = self.fixups[k];
            if let Some(target) = self.label_find(h) {
                if is_jcc {
                    let here = (ofs + 6) as i32;
                    let rel = target as i32 - here;
                    let v = rel as u32;
                    self.out[(ofs + 2) as usize] = (v & 0xFF) as u8;
                    self.out[(ofs + 3) as usize] = ((v >> 8) & 0xFF) as u8;
                    self.out[(ofs + 4) as usize] = ((v >> 16) & 0xFF) as u8;
                    self.out[(ofs + 5) as usize] = ((v >> 24) & 0xFF) as u8;
                } else {
                    let here = (ofs + 5) as i32;
                    let rel = target as i32 - here;
                    let v = rel as u32;
                    self.out[(ofs + 1) as usize] = (v & 0xFF) as u8;
                    self.out[(ofs + 2) as usize] = ((v >> 8) & 0xFF) as u8;
                    self.out[(ofs + 3) as usize] = ((v >> 16) & 0xFF) as u8;
                    self.out[(ofs + 4) as usize] = ((v >> 24) & 0xFF) as u8;
                }
            } else {
                self.err(b"undefined label");
                return;
            }
            k += 1;
        }

        if self.out_len == 0 { self.err(b"empty output"); }
    }

    fn line(&mut self, src: &[u8]) {
        let mut s = trim(src);
        let mut i = 0;
        while i < s.len() { if s[i] == b';' { s = &s[..i]; break; } i += 1; }
        s = trim(s);
        if s.is_empty() { return; }

        let mut j = 0;
        while j < s.len() && ident(s[j]) { j += 1; }
        if j > 0 && j < s.len() && s[j] == b':' {
            let h = hash(&s[..j]);
            let off = self.out_len as u32;
            self.label_add(h, off);
            s = trim(&s[j+1..]);
            if s.is_empty() { return; }
        }

        let mut k = 0;
        while k < s.len() && s[k] != b' ' && s[k] != b'\t' { k += 1; }
        let mnem = &s[..k];
        let ops = trim(&s[k..]);

        let mut comma: Option<usize> = None;
        let mut m = 0;
        while m < ops.len() { if ops[m] == b',' { comma = Some(m); break; } m += 1; }
        let (op1, op2) = if let Some(c) = comma {
            (trim(&ops[..c]), trim(&ops[c+1..]))
        } else {
            (trim(ops), &b""[..])
        };

        self.insn(mnem, op1, op2);
    }

    fn insn(&mut self, m: &[u8], op1: &[u8], op2: &[u8]) {
        if m.is_empty() { return; }

        if eqs(m, b"nop") { self.emit(0x90); return; }
        if eqs(m, b"hlt") { self.emit(0xF4); return; }
        if eqs(m, b"ret") { self.emit(0xC3); return; }
        if eqs(m, b"cli") { self.emit(0xFA); return; }
        if eqs(m, b"sti") { self.emit(0xFB); return; }

        if eqs(m, b"int") {
            match parse_num(op1) {
                Some(v) => { self.emit(0xCD); self.emit(v as u8); self.dangerous = true; }
                None => self.err(b"int: expects imm8"),
            }
            return;
        }

        if eqs(m, b"db") {
            for tok in op1.split(|&c| c == b',') {
                let t = trim(tok);
                if t.is_empty() { continue; }
                if t[0] == b'\'' || t[0] == b'"' {
                    let q = t[0];
                    let mut ii = 1;
                    while ii < t.len() && t[ii] != q { self.emit(t[ii]); ii += 1; }
                } else if let Some(v) = parse_num(t) {
                    self.emit(v as u8);
                }
            }
            return;
        }

        if eqs(m, b"push") {
            let r = reg(op1);
            if r != 0xFF { self.emit(0x50 + r); return; }
            self.err(b"push: expects reg32"); return;
        }
        if eqs(m, b"pop") {
            let r = reg(op1);
            if r != 0xFF { self.emit(0x58 + r); return; }
            self.err(b"pop: expects reg32"); return;
        }
        if eqs(m, b"inc") {
            let r = reg(op1);
            if r != 0xFF { self.emit(0x40 + r); return; }
            self.err(b"inc: expects reg32"); return;
        }
        if eqs(m, b"dec") {
            let r = reg(op1);
            if r != 0xFF { self.emit(0x48 + r); return; }
            self.err(b"dec: expects reg32"); return;
        }

        if eqs(m, b"mov") {
            let r1 = reg(op1);
            if r1 == 0xFF { self.err(b"mov: 1st not reg32"); return; }
            if let Some(v) = parse_num(op2) {
                self.emit(0xB8 + r1); self.emit32(v); return;
            }
            let r2 = reg(op2);
            if r2 != 0xFF {
                self.emit(0x89); self.emit(0xC0 | (r2 << 3) | r1); return;
            }
            self.err(b"mov: bad 2nd op"); return;
        }

        let rr: u8 = if eqs(m, b"add") { 0x01 }
            else if eqs(m, b"sub") { 0x29 }
            else if eqs(m, b"xor") { 0x31 }
            else if eqs(m, b"and") { 0x21 }
            else if eqs(m, b"or")  { 0x09 }
            else if eqs(m, b"cmp") { 0x39 }
            else { 0 };
        let ext: u8 = if eqs(m, b"add") { 0 }
            else if eqs(m, b"or")  { 1 }
            else if eqs(m, b"and") { 4 }
            else if eqs(m, b"sub") { 5 }
            else if eqs(m, b"xor") { 6 }
            else if eqs(m, b"cmp") { 7 }
            else { 0xFF };

        if rr != 0 {
            let r1 = reg(op1);
            if r1 == 0xFF { self.err(b"bad 1st op"); return; }
            let r2 = reg(op2);
            if r2 != 0xFF { self.emit(rr); self.emit(0xC0 | (r2 << 3) | r1); return; }
            if let Some(v) = parse_num(op2) {
                self.emit(0x81); self.emit(0xC0 | (ext << 3) | r1);
                self.emit32(v); return;
            }
            self.err(b"bad 2nd op"); return;
        }

        let jcc: u8 = if eqs(m, b"jz") || eqs(m, b"je") { 0x84 }
            else if eqs(m, b"jnz") || eqs(m, b"jne") { 0x85 }
            else if eqs(m, b"jl")  { 0x8C }
            else if eqs(m, b"jge") { 0x8D }
            else if eqs(m, b"jle") { 0x8E }
            else if eqs(m, b"jg")  { 0x8F }
            else { 0 };

        if jcc != 0 {
            let h = hash(op1);
            if let Some(target) = self.label_find(h) {
                let here = (self.out_len + 6) as i32;
                let rel = target as i32 - here;
                self.emit(0x0F); self.emit(jcc); self.emit32(rel as u32);
            } else {
                let off = self.out_len as u32;
                self.emit(0x0F); self.emit(jcc); self.emit32(0);
                self.fixup_add(off, h, true);
            }
            return;
        }

        if eqs(m, b"jmp") {
            let h = hash(op1);
            if let Some(target) = self.label_find(h) {
                let here = (self.out_len + 5) as i32;
                let rel = target as i32 - here;
                self.emit(0xE9); self.emit32(rel as u32);
            } else {
                let off = self.out_len as u32;
                self.emit(0xE9); self.emit32(0);
                self.fixup_add(off, h, false);
            }
            return;
        }

        self.err(b"unknown mnemonic");
    }
}
