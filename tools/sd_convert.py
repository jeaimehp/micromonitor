#!/usr/bin/env python3
"""Convert images (JPG/PNG/HEIC) into the dashboard's .565 picture format.

File format: 8-byte header b"R565" + width (uint16 LE) + height (uint16 LE), then width*height pixels,
RGB565 big-endian, row-major. That is the byte order the TFT takes, so the firmware streams the pixel
data straight from the SD card to the display.

Every input becomes two files: <name>_L.565 (480x320, landscape) and <name>_P.565 (320x480, portrait),
each cover-cropped (scaled to fill, then centered crop). Inputs whose names already end in _L / _P
(e.g. the generated pixel art) are used for that orientation only, after a size check.

Usage:
  sd_convert.py --out sdcard/photos IMG [IMG ...] [--prefix p]   # numbered p01_L.565, p01_P.565, ...
  sd_convert.py --out sdcard/motivation sdcard_preview/motivation/*.png
  sd_convert.py --decode FILE.565 OUT.png                          # round-trip check
"""
import argparse
import os
import struct

from PIL import Image, ImageOps

try:
    import pillow_heif
    pillow_heif.register_heif_opener()
except ImportError:
    pass

SIZES = {"L": (480, 320), "P": (320, 480)}
MAGIC = b"R565"


def encode(img, path):
    img = img.convert("RGB")
    w, h = img.size
    out = bytearray(MAGIC + struct.pack("<HH", w, h))
    px = img.tobytes()
    for i in range(0, len(px), 3):
        r, g, b = px[i], px[i + 1], px[i + 2]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out += struct.pack(">H", v)
    with open(path, "wb") as f:
        f.write(out)


def decode(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] == MAGIC, "not a .565 file"
    w, h = struct.unpack("<HH", data[4:8])
    rgb = bytearray()
    for i in range(8, 8 + w * h * 2, 2):
        v = (data[i] << 8) | data[i + 1]
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        rgb += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return Image.frombytes("RGB", (w, h), bytes(rgb))


def cover(img, size):
    return ImageOps.fit(img, size, method=Image.LANCZOS, centering=(0.5, 0.5))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inputs", nargs="*")
    ap.add_argument("--out")
    ap.add_argument("--prefix", help="number the outputs <prefix>01, <prefix>02, ... instead of using input names")
    ap.add_argument("--decode", nargs=2, metavar=("IN", "OUT"))
    args = ap.parse_args()

    if args.decode:
        decode(args.decode[0]).save(args.decode[1])
        print("decoded", args.decode[1])
        return

    os.makedirs(args.out, exist_ok=True)
    n = 0
    for k, path in enumerate(args.inputs, 1):
        stem = os.path.splitext(os.path.basename(path))[0]
        img = ImageOps.exif_transpose(Image.open(path))
        fixed = stem[-2:] in ("_L", "_P")
        name = f"{args.prefix}{k:02d}" if args.prefix else (stem.split("_")[0] if fixed else stem)
        orients = [stem[-1]] if fixed else ["L", "P"]
        for o in orients:
            size = SIZES[o]
            if fixed and img.size != size:
                raise SystemExit(f"{path}: expected {size}, got {img.size}")
            out = os.path.join(args.out, f"{name}_{o}.565")
            encode(img if fixed else cover(img, size), out)
            n += 1
    print(f"wrote {n} files to {args.out}")


if __name__ == "__main__":
    main()
