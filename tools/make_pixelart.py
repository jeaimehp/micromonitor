#!/usr/bin/env python3
"""Generate the "motivation" pixel-art set: cute characters with encouraging captions.

Each image is drawn on a half-resolution grid and scaled 2x with nearest-neighbour, so pixels stay crisp.
Writes content/motivation/mNN_<name>_L.png (480x320) and _P.png (320x480), which µMonitor streams to the
device, and a contact sheet content/motivation_sheet.png.
"""
import os
import random
import textwrap

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "content", "motivation")
FONT = "/System/Library/Fonts/Supplemental/Silom.ttf"

PALETTE = {
    "K": (34, 30, 40), "W": (255, 255, 255), "O": (247, 152, 54), "P": (255, 140, 170), "G": (76, 175, 80),
    "g": (46, 125, 50), "R": (206, 102, 64), "B": (141, 104, 72), "T": (222, 190, 150), "D": (90, 62, 44),
    "C": (120, 72, 40), "M": (250, 240, 220), "s": (215, 215, 225), "S": (176, 190, 197), "c": (64, 224, 240),
    "r": (235, 64, 64), "b": (64, 140, 255), "L": (190, 225, 100), "N": (139, 90, 43), "a": (190, 230, 255),
    "Y": (255, 214, 0), "H": (120, 190, 90), "E": (60, 120, 60),
}

SPRITES = {
    "cat": """
..K..........K..
.KOK........KOK.
.KOOK......KOOK.
.KOOOKKKKKKOOOK.
.KOOOOOOOOOOOOK.
KOOOOOOOOOOOOOOK
KOOWKOOOOOOWKOOK
KOOKKOOOOOOKKOOK
KOPPOOOKKOOOPPOK
KOOOOOOOKOOOOOOK
.KOOOOOKOKOOOOK.
..KOOOOOOOOOOK..
...KKKKKKKKKK...
..KOOK....KOOK..
..KKKK....KKKK..""",
    "cactus": """
......GGGG......
.....GGGGGG.....
..G..GGGGGG..G..
.GGG.KKKKKK.GGG.
.GGG.KKGGKK.GGG.
.GGG.GGGGGG.GGG.
.GGGGGKGGKGGGGG.
..GGGGGKKGGGGG..
.....GGGGGG.....
.....GGGGGG.....
....RRRRRRRR....
....RRRRRRRR....
.....RRRRRR.....
.....RRRRRR.....""",
    "sloth": """
NNNNNNNNNNNNNNNN
NNBBNNNNNNNNBBNN
..BB.BBBBBB.BB..
..BBBBBBBBBBBB..
.BBBTTTTTTTTBBB.
.BBTTTTTTTTTTBB.
BBTDDDTTTTDDDTBB
BBTDKDTTTTDKDTBB
BBTDDDTTTTDDDTBB
BBTTTTTKKTTTTTBB
.BBTTTKTTKTTTBB.
..BBTTTTTTTTBB..
...BBBBBBBBBB...""",
    "coffee": """
.....s...s......
......s...s.....
.....s...s......
..KKKKKKKKKK....
..KCCCCCCCCK....
..KMMMMMMMMKKK..
..KMMKMMKMMK.K..
..KMMMMMMMMK.K..
..KMPMKKMPMKKK..
..KMMMMMMMMK....
...KMMMMMMK.....
....KKKKKK......""",
    "robot": """
.......rr.......
.......KK.......
...KKKKKKKKKK...
...KSSSSSSSSK...
.KKKScKSScKSKKK.
.KSKSccSSccSKSK.
.KKKSSSSSSSSKKK.
...KSSKKKKSSK...
...KSSSSSSSSK...
...KKKKKKKKKK...
.....KSSSSK.....
...KKKKKKKKKK...
...KSbSSSSGSK...
...KSSSSSSSSK...
...KKKKKKKKKK...""",
    "avocado": """
......DDDD......
....DDggggDD....
...DggggggggD...
..DggLLLLLLggD..
..DgLLLLLLLLgD..
.DgLLLLLLLLLLgD.
.DgLLKLLLLKLLgD.
.DgLLLLNNLLLLgD.
.DgLLLNNNNLLLgD.
.DgLLLNNNNLLLgD.
.DgLLLLNNLLLLgD.
..DgLLKKKKLLgD..
...DggggggggD...
....DDDDDDDD....""",
    "bee": """
...aaa....aaa...
..aaaaa..aaaaa..
..aaaaa..aaaaa..
...aaaYYYYaaa...
.....YYYYYY.....
....YKYYYYKY....
....YYYKKYYY....
...KKKKKKKKKK...
...YYYYYYYYYY...
...KKKKKKKKKK...
....YYYYYYYY....
.....KKKKKK.....
.......K........""",
    "turtle": """
.....EEEEEE.......
...EEHHHHHHEE.....
..EHHEEEEEEHHE....
.EHEEHHHHHHEEHE.GG
.EHHHHEEEEHHHHEGWK
.EEEEEEEEEEEEEEGGG
..GG........GG....
..GG........GG....""",
    "sprout": """
....LL....LL....
...LLLL..LLLL...
...LLLLL.LLLL...
....LLLLLLLL....
.......LL.......
.......LL.......
...RRRRRRRRRR...
...RKRRRRRRKR...
...RRRRKKRRRR...
....RRRRRRRR....
....RRRRRRRR....""",
}

# (sprite, caption, sky color, ground color)
CARDS = [
    ("cat", "You got this!", (255, 214, 224), (240, 160, 180)),
    ("cactus", "Stay sharp, stay cool", (255, 236, 180), (230, 190, 110)),
    ("sloth", "Slow progress is still progress", (200, 235, 210), (110, 170, 120)),
    ("coffee", "One sip at a time", (235, 220, 205), (170, 130, 100)),
    ("robot", "Debugging life, one bug at a time", (205, 225, 255), (120, 150, 200)),
    ("avocado", "Avo great day!", (225, 245, 200), (150, 190, 110)),
    ("bee", "Bee proud of tiny wins", (255, 245, 200), (150, 200, 100)),
    ("turtle", "Steady wins the race", (190, 235, 245), (90, 170, 190)),
    ("sprout", "Grow at your own pace", (230, 245, 230), (160, 120, 90)),
]

SCALE = 2  # grid -> screen


def shade(c, f):
    return tuple(max(0, min(255, int(v * f))) for v in c)


def draw_sprite(img, name, x, y, cell):
    rows = [r for r in SPRITES[name].strip("\n").splitlines()]
    w = max(len(r) for r in rows)
    d = ImageDraw.Draw(img)
    for j, row in enumerate(rows):
        for i, ch in enumerate(row.ljust(w, ".")):
            if ch != ".":
                d.rectangle([x + i * cell, y + j * cell, x + (i + 1) * cell - 1, y + (j + 1) * cell - 1],
                            fill=PALETTE[ch])
    return w * cell, len(rows) * cell


def sprite_size(name, cell):
    rows = SPRITES[name].strip("\n").splitlines()
    return max(len(r) for r in rows) * cell, len(rows) * cell


def background(w, h, sky, ground, seed):
    img = Image.new("RGB", (w, h), sky)
    d = ImageDraw.Draw(img)
    # Banded sky (darker toward the top), like a dithered gradient.
    bands = 5
    for b in range(bands):
        d.rectangle([0, b * h // 12, w, (b + 1) * h // 12 - 1], fill=shade(sky, 0.90 + 0.02 * b))
    # Sparkles.
    rnd = random.Random(seed)
    for _ in range(9):
        sx, sy = rnd.randrange(4, w - 4), rnd.randrange(4, h * 2 // 3)
        c = (255, 255, 255)
        d.point([(sx, sy), (sx - 1, sy), (sx + 1, sy), (sx, sy - 1), (sx, sy + 1)], fill=c)
    # Ground with a darker top edge and a few tufts.
    gy = h - h // 7
    d.rectangle([0, gy, w, h], fill=ground)
    d.rectangle([0, gy, w, gy + 1], fill=shade(ground, 0.75))
    for _ in range(12):
        tx = rnd.randrange(0, w)
        ty = gy + 4 + rnd.randrange(0, h // 9)
        d.rectangle([tx, ty, tx + 2, ty + 1], fill=shade(ground, 0.85))
    return img, gy


def bubble(img, box, text, font, tail_to=None):
    """Pixel speech bubble with a 1px outline and a stepped tail toward tail_to (x, y)."""
    d = ImageDraw.Draw(img)
    x0, y0, x1, y1 = box
    ink = (34, 30, 40)
    d.rectangle([x0 + 1, y0, x1 - 1, y1], fill=(255, 255, 255))
    d.rectangle([x0, y0 + 1, x1, y1 - 1], fill=(255, 255, 255))
    d.line([x0 + 1, y0, x1 - 1, y0], fill=ink)
    d.line([x0 + 1, y1, x1 - 1, y1], fill=ink)
    d.line([x0, y0 + 1, x0, y1 - 1], fill=ink)
    d.line([x1, y0 + 1, x1, y1 - 1], fill=ink)
    if tail_to:
        tx, ty = tail_to
        white = (255, 255, 255)
        if tx < x0:
            # Speaker to the left: a stepped tail out of the left edge, two thirds of the way down.
            by = y0 + (y1 - y0) * 2 // 3
            for k in range(1, 7):
                half = (6 - k) // 2
                d.line([x0 - k, by - half, x0 - k, by + half], fill=white)
                d.point([(x0 - k, by - half - 1), (x0 - k, by + half + 1)], fill=ink)
            d.line([x0, by - 3, x0, by + 3], fill=white)
        else:
            # Speaker below or above: a stepped tail out of the bottom (or top) edge.
            bx = min(max(tx, x0 + 8), x1 - 8)
            by = y1 if ty > y1 else y0
            step = 1 if ty > y1 else -1
            for k in range(1, 6):
                half = (5 - k)
                d.line([bx - half, by + step * k, bx + half, by + step * k], fill=white)
                d.point([(bx - half - 1, by + step * k), (bx + half + 1, by + step * k)], fill=ink)
            d.line([bx - 4, by, bx + 4, by], fill=white)
    # Text, centered, crisp (no anti-aliasing).
    d.fontmode = "1"
    lines = text
    lh = font.getbbox("Ag")[3] + 2
    total = lh * len(lines)
    ty0 = y0 + (y1 - y0 - total) // 2 + 1
    for i, line in enumerate(lines):
        lw = d.textlength(line, font=font)
        d.text((x0 + (x1 - x0 - lw) // 2, ty0 + i * lh), line, font=font, fill=ink)


def wrap(text, font, width):
    d = ImageDraw.Draw(Image.new("1", (1, 1)))
    for cols in range(len(text), 4, -1):
        lines = textwrap.wrap(text, cols)
        if all(d.textlength(line, font=font) <= width for line in lines):
            return lines
    return textwrap.wrap(text, 8)


def card(idx, sprite, caption, sky, ground, portrait):
    gw, gh = (160, 240) if portrait else (240, 160)
    img, gy = background(gw, gh, sky, ground, seed=idx)
    cell = 8 if portrait else 6
    sw, sh = sprite_size(sprite, cell)
    font = ImageFont.truetype(FONT, 14 if portrait else 13)
    if portrait:
        sx, sy = (gw - sw) // 2, gy - sh + 8
        lines = wrap(caption, font, gw - 28)
        bh = len(lines) * 18 + 16
        box = (10, 22, gw - 11, 22 + bh)
    else:
        sx, sy = 12, gy - sh + 7
        lines = wrap(caption, font, gw - sw - 46)
        bh = len(lines) * 17 + 14
        bx0 = sx + sw + 10
        box = (bx0, 28, gw - 10, 28 + bh)
    draw_sprite(img, sprite, sx, sy, cell)
    bubble(img, box, lines, font, tail_to=(sx + sw // 2, sy))
    return img.resize((gw * SCALE, gh * SCALE), Image.NEAREST)


def main():
    os.makedirs(OUT, exist_ok=True)
    land, port = [], []
    for i, (sprite, caption, sky, ground) in enumerate(CARDS, 1):
        for portrait, bucket in ((False, land), (True, port)):
            img = card(i, sprite, caption, sky, ground, portrait)
            img.save(os.path.join(OUT, f"m{i:02d}_{sprite}_{'P' if portrait else 'L'}.png"))
            bucket.append(img)
    # Contact sheet: landscape cards (5 per row) above the portrait set (10 in a row), at half size.
    thumbs_l = [im.resize((240, 160), Image.NEAREST) for im in land]
    thumbs_p = [im.resize((120, 180), Image.NEAREST) for im in port]
    sheet = Image.new("RGB", (5 * 248 + 4, 2 * 168 + 188 + 4), (40, 40, 40))
    for k, t in enumerate(thumbs_l):
        sheet.paste(t, (4 + (k % 5) * 248, 4 + (k // 5) * 168))
    for k, t in enumerate(thumbs_p):
        sheet.paste(t, (4 + k * 124, 4 + 2 * 168))
    sheet.save(os.path.join(ROOT, "content", "motivation_sheet.png"))
    print(f"wrote {len(land)} landscape + {len(port)} portrait cards to {OUT}")


if __name__ == "__main__":
    main()
