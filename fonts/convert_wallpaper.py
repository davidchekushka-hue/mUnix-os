#!/usr/bin/env python3
from PIL import Image
import struct, sys

SRC = "sever-nine.jpeg"
OUT = "fonts/wallpaper.raw"
TARGET_W = 1280
TARGET_H = 720

img = Image.open(SRC).convert('RGB')
w, h = img.size

# Crop center 16:9
target_ratio = TARGET_W / TARGET_H
src_ratio = w / h
if src_ratio > target_ratio:
    # шире — обрезаем по бокам
    new_w = int(h * target_ratio)
    left = (w - new_w) // 2
    img = img.crop((left, 0, left + new_w, h))
else:
    # выше — обрезаем сверху/снизу
    new_h = int(w / target_ratio)
    top = (h - new_h) // 2
    img = img.crop((0, top, w, top + new_h))

img = img.resize((TARGET_W, TARGET_H), Image.LANCZOS)
data = img.tobytes()

# Header: magic + w + h + reserved = 16 bytes
out = bytearray()
out += b'MWAL'
out += struct.pack('<I', TARGET_W)
out += struct.pack('<I', TARGET_H)
out += struct.pack('<I', 0)
out += data

open(OUT, 'wb').write(out)
print(f"{OUT}: {len(out)} bytes ({TARGET_W}x{TARGET_H})")
