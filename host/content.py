"""Pictures and themes that µMonitor streams to the Xenon on request (replaces the SD card).

Folder 0 = photos (any JPG/PNG/HEIC in the configured photos folder, cover-fitted to the requested size),
folder 1 = motivation (the bundled pixel-art cards; mNN_name_L.png / _P.png, picked by requested orientation).
"""
import glob
import json
import os
import sys

from PIL import Image, ImageOps

try:
    import pillow_heif
    pillow_heif.register_heif_opener()
except ImportError:
    pass

IMAGE_EXTS = (".jpg", ".jpeg", ".png", ".heic", ".heif", ".gif", ".bmp", ".tif", ".tiff", ".webp")
THEME_KEYS = ["surface", "grid", "text", "text2", "series1", "series2", "good", "critical", "separator", "button",
              "accent"]
CONFIG = os.path.expanduser("~/Library/Application Support/micromonitor/config.json")


def content_dir():
    """content/ next to the repo in development; Contents/Resources/content inside the frozen app."""
    if getattr(sys, "frozen", False):
        return os.path.join(os.environ.get("RESOURCEPATH", ""), "content")
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "content")


def load_config():
    try:
        with open(CONFIG) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_config(cfg):
    os.makedirs(os.path.dirname(CONFIG), exist_ok=True)
    with open(CONFIG, "w") as f:
        json.dump(cfg, f, indent=2)


def photos_dir():
    return load_config().get("photos_dir") or os.path.join(content_dir(), "photos")


def set_photos_dir(path):
    cfg = load_config()
    cfg["photos_dir"] = path
    save_config(cfg)


def _images(folder):
    return sorted(p for p in glob.glob(os.path.join(folder, "*"))
                  if p.lower().endswith(IMAGE_EXTS) and not os.path.basename(p).startswith("."))


def picture_list(folder, portrait):
    if folder == 1:
        suffix = "_P.png" if portrait else "_L.png"
        return [p for p in _images(os.path.join(content_dir(), "motivation")) if p.endswith(suffix)]
    return _images(photos_dir())


def to_rgb565be(img):
    px = img.convert("RGB").tobytes()
    out = bytearray(len(px) // 3 * 2)
    j = 0
    for i in range(0, len(px), 3):
        v = ((px[i] & 0xF8) << 8) | ((px[i + 1] & 0xFC) << 3) | (px[i + 2] >> 3)
        out[j] = v >> 8
        out[j + 1] = v & 0xFF
        j += 2
    return bytes(out)


def rle_encode(be):
    """Pixel RLE: a byte c < 128 is followed by c+1 literal pixels; c >= 128 by one pixel repeated c-126 times
    (2..129). Pixel art shrinks 10-20x; photos grow by at most 1/128."""
    px = [be[i:i + 2] for i in range(0, len(be), 2)]
    out = bytearray()
    i, n = 0, len(px)
    while i < n:
        run = 1
        while i + run < n and run < 129 and px[i + run] == px[i]:
            run += 1
        if run >= 2:
            out.append(run + 126)
            out += px[i]
            i += run
            continue
        start = i
        i += 1
        while i < n and i - start < 128 and not (i + 1 < n and px[i + 1] == px[i]):
            i += 1
        out.append(i - start - 1)
        for p in px[start:i]:
            out += p
    return bytes(out)


def rle_decode(data, npx):
    out = bytearray()
    i = 0
    while len(out) < npx * 2:
        c = data[i]
        i += 1
        if c < 128:
            out += data[i:i + 2 * (c + 1)]
            i += 2 * (c + 1)
        else:
            out += data[i:i + 2] * (c - 126)
            i += 2
    return bytes(out)


def render_picture(folder, n, w, h):
    """Returns (count, payload): the n-th picture (wrapping) fitted to w x h as RGB565 big-endian."""
    files = picture_list(folder, portrait=h > w)
    if not files:
        return 0, b""
    path = files[n % len(files)]
    img = ImageOps.exif_transpose(Image.open(path))
    # Pixel art stays crisp with nearest-neighbour; photos use a high-quality filter.
    method = Image.NEAREST if folder == 1 else Image.LANCZOS
    img = ImageOps.fit(img, (w, h), method=method)
    return len(files), rle_encode(to_rgb565be(img))


def theme_lines():
    """One `thm name|layout|c1,...,c11` line per theme file, then `thm end`."""
    lines = []
    for path in sorted(glob.glob(os.path.join(content_dir(), "themes", "*.thm"))):
        kv = {}
        with open(path) as f:
            for line in f:
                if "=" in line and not line.startswith("#"):
                    k, v = line.strip().split("=", 1)
                    kv[k] = v.lstrip("#")
        if "name" in kv:
            colors = ",".join(kv.get(k, "000000") for k in THEME_KEYS)
            lines.append(f"thm {kv['name']}|{kv.get('layout', 'quad')}|{colors}")
    lines.append("thm end")
    return lines
