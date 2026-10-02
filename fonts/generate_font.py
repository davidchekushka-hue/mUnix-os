#!/usr/bin/env python3
from PIL import Image, ImageDraw, ImageFont
import struct, sys, os

SIZE = 12
FONT_PATH = "/usr/share/fonts/default/TTF/LiberationSans-Regular.ttf"
FIRST = 32
LAST = 126
OUT = "fonts/DejaVu14.mfnt"

font = ImageFont.truetype(FONT_PATH, SIZE)
ascent, descent = font.getmetrics()
line_height = ascent + descent

glyphs = []
for c in range(FIRST, LAST + 1):
    ch = chr(c)
    bbox = font.getbbox(ch)
    if bbox:
        bx0, by0, bx1, by1 = bbox
    else:
        bx0 = by0 = bx1 = by1 = 0

    w = max(0, bx1 - bx0)
    h = max(0, by1 - by0)

    if w <= 0 or h <= 0:
        adv = int(font.getlength(ch) + 0.5)
        if adv < 1: adv = 1
        glyphs.append((b'', 0, 0, adv, 0, 0))
        continue

    img = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(img)
    d.text((-bx0, -by0), ch, font=font, fill=255)
    data = img.tobytes()

    # FIX: adv = max(round(getlength), w). NO +1
    adv = int(font.getlength(ch) + 0.5)
    if adv < w:
        adv = w

    glyphs.append((data, w, h, adv, bx0, by0))

out = bytearray()
out += b'MFNT'
out += struct.pack('<I', 1)
out += struct.pack('<I', SIZE)
out += struct.pack('<I', FIRST)
out += struct.pack('<I', LAST)
out += struct.pack('<i', line_height)
out += struct.pack('<i', ascent)
out += struct.pack('<I', len(glyphs))

HDR_SIZE = 32
ENT_SIZE = 14
data_base = HDR_SIZE + len(glyphs) * ENT_SIZE

data_blob = bytearray()
for (px, w, h, adv, bx, by) in glyphs:
    entry = struct.pack('<IHHhhH', data_base + len(data_blob), w, h, bx, by, adv)
    out += entry
    data_blob += px

out += data_blob
open(OUT, 'wb').write(out)
print(f"{OUT}: {len(out)} bytes, {len(glyphs)} glyphs, lh={line_height}, ascent={ascent}")

# Диагностика advance
print("\nadvance check:")
for c in "Welcome to mUnix":
    adv = int(font.getlength(c) + 0.5)
    bbox = font.getbbox(c)
    w = bbox[2] - bbox[0] if bbox else 0
    if adv < w: adv = w
    print(f"  {c!r}: w={w}, adv={adv}")
