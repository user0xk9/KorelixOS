#!/usr/bin/env python3
"""Генератор kernel/drivers/video/font8x16.h из GNU Unifont (8x16).

Использование: tools/gen_font.py [путь/к/unifont.otf] > kernel/drivers/video/font8x16.h
Набор символов: ASCII, русская и украинская кириллица.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/opentype/unifont/unifont.otf"
W, H = 8, 16

cps = list(range(0x20, 0x7F))
cps += [0x0401] + list(range(0x0410, 0x0450)) + [0x0451]
cps += [0x0404, 0x0406, 0x0407, 0x0490, 0x0454, 0x0456, 0x0457, 0x0491]
cps = sorted(set(cps))      # порядок важен: в ядре используется бинарный поиск

font = ImageFont.truetype(FONT, 16)

def glyph(cp):
    img = Image.new("1", (W, H), 0)
    ImageDraw.Draw(img).text((0, 0), chr(cp), font=font, fill=1, anchor="la")
    rows = []
    for y in range(H):
        b = 0
        for x in range(W):
            if img.getpixel((x, y)):
                b |= 0x80 >> x
        rows.append(b)
    return rows

out = []
out.append("/* Автогенерация: tools/gen_font.py. Не править вручную.\n"
           " * Глифы: GNU Unifont (https://unifoundry.com/unifont/), GPL-2.0-or-later\n"
           " * с исключением для встраивания шрифтов (GNU Font Embedding Exception). */\n"
           "#pragma once\n#include <stdint.h>\n")
out.append("#define FONT_W 8\n#define FONT_H 16\n#define FONT_GLYPHS %d\n" % len(cps))
out.append("/* Кодпоинты по возрастанию: используется бинарный поиск */\n"
           "static const uint16_t font_codepoints[FONT_GLYPHS] = {")
for i in range(0, len(cps), 8):
    out.append("    " + ", ".join("0x%04X" % c for c in cps[i:i+8]) + ",")
out.append("};\n")
out.append("static const uint8_t font_data[FONT_GLYPHS][FONT_H] = {")
for cp in cps:
    g = glyph(cp)
    out.append("    {" + ",".join("0x%02X" % b for b in g) + "},  /* U+%04X */" % cp)
out.append("};")
print("\n".join(out))
