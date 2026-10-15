#!/usr/bin/env python3
"""Dessine l'icône de XRPerfSensors et écrit sensors/XRPerfSensors.ico.

Chaque taille est dessinée séparément (et non réduite depuis la grande) pour rester lisible à
16 pixels dans la zone de notification. Dépendance : Pillow.  Usage : python tools/gen_icon.py
"""
import io
import os
import struct

from PIL import Image, ImageDraw

BG_TOP = (30, 37, 48)
BG_BOTTOM = (14, 17, 22)      # fond du panneau
BORDER = (70, 78, 94)
BODY = (150, 190, 240)        # bleu des intitulés du panneau
BODY_DARK = (104, 140, 196)
SCREEN = (16, 21, 30)
GREEN = (110, 225, 140)       # vert "cadence tenue"
SIZES = [16, 20, 24, 32, 40, 48, 64, 256]


def draw_icon(size):
    """Retourne l'icône en RGBA à la taille demandée (dessin suréchantillonné puis réduit)."""
    ss = 8 if size <= 64 else 4
    n = size * ss
    u = n / 256.0  # le dessin est décrit sur une grille de 256
    small = size <= 24

    def box(x0, y0, x1, y1):
        return [round(x0 * u), round(y0 * u), round(x1 * u) - 1, round(y1 * u) - 1]

    # Fond : carré arrondi, léger dégradé vertical.
    gradient = Image.new("RGBA", (n, n))
    gd = ImageDraw.Draw(gradient)
    for y in range(n):
        t = y / (n - 1)
        gd.line([(0, y), (n, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOTTOM)) + (255,))
    mask = Image.new("L", (n, n), 0)
    ImageDraw.Draw(mask).rounded_rectangle(box(6, 6, 250, 250), radius=round(54 * u), fill=255)
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    img.paste(gradient, (0, 0), mask)
    d = ImageDraw.Draw(img)
    if not small:
        d.rounded_rectangle(box(6, 6, 250, 250), radius=round(54 * u), outline=BORDER + (255,), width=max(1, round(3 * u)))

    # Casque vu de face : sangles sur les côtés, coque, encoche du nez.
    if small:
        body = (22, 62, 234, 194)
        screen = (44, 84, 212, 172)
        d.rounded_rectangle(box(*body), radius=round(40 * u), fill=BODY + (255,))
        d.rounded_rectangle(box(*screen), radius=round(22 * u), fill=SCREEN + (255,))
        bars = [(58, 136), (106, 116), (154, 98)]  # (x, haut) : trois barres qui montent
        width = 44
    else:
        body = (40, 74, 216, 182)
        screen = (58, 92, 198, 164)
        d.rounded_rectangle(box(20, 104, 52, 152), radius=round(10 * u), fill=BODY_DARK + (255,))
        d.rounded_rectangle(box(204, 104, 236, 152), radius=round(10 * u), fill=BODY_DARK + (255,))
        d.rounded_rectangle(box(*body), radius=round(36 * u), fill=BODY + (255,))
        d.ellipse(box(110, 168, 146, 204), fill=tuple(round(a + (b - a) * 0.72) for a, b in zip(BG_TOP, BG_BOTTOM)) + (255,))
        d.rounded_rectangle(box(*screen), radius=round(20 * u), fill=SCREEN + (255,))
        bars = [(72, 138), (102, 124), (132, 130), (162, 106)]  # quatre barres, la dernière la plus haute
        width = 22
    bottom = screen[3] - (10 if small else 10)
    for x, top in bars:
        d.rounded_rectangle(box(x, top, x + width, bottom), radius=round((3 if small else 5) * u), fill=GREEN + (255,))

    return img.resize((size, size), Image.LANCZOS)


def dib(image):
    """Image d'icône au format BMP (BGRA de bas en haut, suivi du masque 1 bit)."""
    w, h = image.size
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    rows = [image.crop((0, y, w, y + 1)).tobytes("raw", "BGRA") for y in range(h - 1, -1, -1)]
    mask_row = b"\x00" * (((w + 31) // 32) * 4)  # transparence portée par le canal alpha
    return header + b"".join(rows) + mask_row * h


def write_ico(path, images):
    entries, blobs = [], []
    offset = 6 + 16 * len(images)
    for image in images:
        w, h = image.size
        if w >= 256:
            buffer = io.BytesIO()
            image.save(buffer, "PNG", optimize=True)
            data = buffer.getvalue()
        else:
            data = dib(image)
        entries.append(struct.pack("<BBBBHHII", w & 255, h & 255, 0, 0, 1, 32, len(data), offset))
        blobs.append(data)
        offset += len(data)
    with open(path, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, len(images)))
        f.write(b"".join(entries))
        f.write(b"".join(blobs))


if __name__ == "__main__":
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    images = [draw_icon(size) for size in SIZES]
    write_ico(os.path.join(root, "sensors", "XRPerfSensors.ico"), images)
    images[-1].save(os.path.join(root, "docs", "logo.png"))
    print("sensors/XRPerfSensors.ico :", ", ".join(str(s) for s in SIZES))
