#!/usr/bin/env python3
"""Génère layer/src/font_data.h : atlas de glyphes (couverture 8 bits) à partir de DejaVu Sans Mono.

Usage : python tools/gen_font.py [dossier_des_polices]
À relancer uniquement si l'on veut changer de police ou de taille.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

FONT_DIR = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu"
OUT = os.path.join(os.path.dirname(__file__), "..", "layer", "src", "font_data.h")

# (nom, fichier, taille px, premier code, nombre de glyphes)
# Le code 127 est remplacé par le signe degré.
ATLASES = [
    ("Label", "DejaVuSansMono.ttf", 17, 32, 96),
    ("Value", "DejaVuSansMono-Bold.ttf", 23, 32, 96),
    ("Big", "DejaVuSansMono-Bold.ttf", 58, 43, 15),  # + , - . / 0-9
]


def build(name, file, size, first, count):
    font = ImageFont.truetype(os.path.join(FONT_DIR, file), size)
    ascent, descent = font.getmetrics()
    cell_w = int(round(font.getlength("0")))
    cell_h = ascent + descent
    data = bytearray()
    for code in range(first, first + count):
        ch = "°" if code == 127 else chr(code)
        img = Image.new("L", (cell_w, cell_h), 0)
        ImageDraw.Draw(img).text((0, 0), ch, font=font, fill=255)
        data += img.tobytes()
    return cell_w, cell_h, ascent, data


with open(OUT, "w", newline="\n") as f:
    f.write("// Fichier généré par tools/gen_font.py - ne pas modifier à la main.\n")
    f.write("// Glyphes issus de DejaVu Sans Mono (licence Bitstream Vera / DejaVu, voir THIRD-PARTY.md).\n")
    f.write("#pragma once\n\nnamespace xrpo {\n\n")
    f.write("struct FontAtlas {\n    int cellW, cellH, ascent, first, count;\n    const unsigned char* data;\n};\n\n")
    for name, file, size, first, count in ATLASES:
        cell_w, cell_h, ascent, data = build(name, file, size, first, count)
        f.write(f"static const unsigned char kFont{name}Data[{len(data)}] = {{\n")
        for i in range(0, len(data), 32):
            f.write(",".join(str(b) for b in data[i:i + 32]) + ",\n")
        f.write("};\n")
        f.write(f"static const FontAtlas kFont{name} = {{{cell_w}, {cell_h}, {ascent}, {first}, {count}, kFont{name}Data}};\n\n")
        print(name, cell_w, cell_h, ascent, len(data))
    f.write("}  // namespace xrpo\n")
