#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_assets.py - genere les 3 fichiers d'assets requis pour un CIA 3DS :

    assets/icon.png    48x48    icone du menu HOME
    assets/banner.png  256x128  banniere (fond de jailbreak / FBI)
    assets/audio.wav   16 bits PCM mono, ~1.2 s  (son de la banniere)

Aucune dependance externe : stdlib uniquement (zlib, struct, wave, random).
Usage :  python3 tools/gen_assets.py
"""

import math
import os
import random
import struct
import wave
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(os.path.dirname(HERE), "assets")


# ---------------------------------------------------------------------------
# PNG minimal (RGB 8 bits) - stdlib only
# ---------------------------------------------------------------------------

def write_png(path, width, height, get_pixel):
    """get_pixel(x, y) -> (r, g, b)"""
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type 0
        for x in range(width):
            r, g, b = get_pixel(x, y)
            raw += bytes((r & 0xFF, g & 0xFF, b & 0xFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
    print("ecrit", path, "(%dx%d)" % (width, height))


def lerp(a, b, t):
    return a + (b - a) * t


# ---------------------------------------------------------------------------
# Icône 48x48 : ciel + soleil, colline, personnage blocky style Roblox
# ---------------------------------------------------------------------------

def make_icon():
    W, H = 48, 48
    sky0 = (92, 169, 232)
    sky1 = (140, 200, 245)

    def px(x, y):
        # ciel degrade
        t = y / H
        r = lerp(sky0[0], sky1[0], t)
        g = lerp(sky0[1], sky1[1], t)
        b = lerp(sky0[2], sky1[2], t)

        # soleil (cercle jaune en haut a gauche)
        dx, dy = x - 10, y - 10
        if dx * dx + dy * dy <= 36:
            r, g, b = 255, 224, 90
            if dx * dx + dy * dy > 25:
                r, g, b = 255, 246, 160

        # nuages
        if (18 <= x <= 40) and (6 <= y <= 10) and not (x > 20 and x < 38 and y in (6, 10)):
            r, g, b = 255, 255, 255

        # sol : herbe + terre
        if y >= 34:
            r, g, b = 92, 184, 92
            if y >= 40:
                r, g, b = 146, 108, 74

        # tour mini en briques a droite
        if 34 <= x <= 44 and 16 <= y <= 34:
            r, g, b = (188, 72, 62) if (y != 16) else (150, 138, 140)
            if y in (16,):
                r, g, b = 138, 138, 146

        # personnage blocky style Roblox (t=tete)
        hx, hy = 24, 14
        if hx <= x < hx + 8 and hy <= y < hy + 8:
            r, g, b = 250, 205, 110            # tete
        if 26 <= x <= 35 and 22 <= y <= 33:
            r, g, b = 66, 120, 214             # torse
        if 21 <= x <= 25 and 22 <= y <= 33:
            r, g, b = 250, 205, 110            # bras
        if 36 <= x <= 40 and 22 <= y <= 33:
            r, g, b = 250, 205, 110            # bras
        if (27 <= x <= 30 or 32 <= x <= 35) and 34 <= y <= 42:
            r, g, b = 74, 160, 72              # jambes

        return (int(r), int(g), int(b))

    write_png(os.path.join(ASSETS, "icon.png"), W, H, px)


# ---------------------------------------------------------------------------
# Bannière 256x128 : titre blocky + paysage
# ---------------------------------------------------------------------------

FONT5x7 = {
    "R": ["01110", "10001", "10001", "01110", "10010", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "3": ["01110", "10001", "00001", "00110", "00001", "10001", "01110"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
}


def make_banner():
    W, H = 256, 128
    sky0 = (60, 120, 200)
    sky1 = (150, 200, 240)

    text = "ROBLO3DS"
    glyph_w, glyph_h, scale = 5, 7, 6
    spacing = 2
    total_w = len(text) * (glyph_w * scale + spacing) - spacing
    tx0 = (W - total_w) // 2
    ty0 = 22

    def px(x, y):
        t = y / H
        r, g, b = lerp(sky0[0], sky1[0], t), lerp(sky0[1], sky1[1], t), lerp(sky0[2], sky1[2], t)

        # soleil
        dx, dy = x - 34, y - 26
        if dx * dx + dy * dy <= 144:
            r, g, b = 255, 224, 90

        # nuage
        if (150 <= x <= 210) and (18 <= y <= 26):
            r, g, b = 255, 255, 255

        # terrain bas : herbe
        if y >= 96:
            r, g, b = 92, 184, 92
            if y >= 112:
                r, g, b = 146, 108, 74

        # colline blocky
        if (y >= 78) and (8 <= x <= 60) and (y >= 78 + (x - 8) or x > 50):
            r, g, b = 120, 128, 130

        # tour a droite
        if 210 <= x <= 244 and 48 <= y <= 96:
            r, g, b = 188, 72, 54
            if y == 94:
                r, g, b = 138, 138, 146

        # titre texte pixel
        for i, ch in enumerate(text):
            gx0 = tx0 + i * (glyph_w * scale + spacing)
            if not (gx0 <= x < gx0 + glyph_w * scale):
                continue
            if ch in FONT5x7:
                rows = FONT5x7[ch]
                gx = (x - gx0) // scale
                for ry, row in enumerate(rows):
                    inside = row[gx] == "1"
                    if not inside:
                        continue
                    gy0 = ty0 + ry * scale
                    if gy0 <= y < gy0 + scale:
                        r, g, b = 255, 255, 255
                        break
        return (int(r), int(g), int(b))

    write_png(os.path.join(ASSETS, "banner.png"), W, H, px)


# ---------------------------------------------------------------------------
# Son : petit arpeggio + "boom" (16 bits mono, 44100 Hz)
# ---------------------------------------------------------------------------

def make_wav():
    rate = 44100
    notes = [523.25, 659.25, 783.99, 1046.50]   # do mi sol do
    note_len = 0.12
    boom_len = 0.65
    total_len = note_len * len(notes) + boom_len

    samples = []
    t = 0.0
    for f in notes:
        for i in range(int(rate * note_len)):
            tt = i / rate
            env = min(1.0, tt / 0.01) * math.exp(-tt * 9.0)
            s = 0.55 * math.sin(2 * math.pi * f * tt) * env
            samples.append(int(max(-1.0, min(1.0, s)) * 32767))

    random.seed(42)
    boom_len_s = int(rate * boom_len)
    for i in range(boom_len_s):
        tt = i / rate
        env = math.exp(-tt * 6.5)
        noise = random.uniform(-1.0, 1.0) * 0.55
        low = math.sin(2 * math.pi * 92.0 * tt) * 0.7
        s = (noise + low) * env
        samples.append(int(max(-1.0, min(1.0, s)) * 32767))

    path = os.path.join(ASSETS, "audio.wav")
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)       # 16 bits
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", s) for s in samples))
    print("ecrit", path, "(%.2f s, 16 bits mono)" % total_len)


if __name__ == "__main__":
    os.makedirs(ASSETS, exist_ok=True)
    make_icon()
    make_banner()
    make_wav()
    print("OK - assets generes dans", ASSETS)
