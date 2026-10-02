#!/usr/bin/env python3
"""Single source of truth for dashboard themes.

Writes content/themes/*.thm (key=value files µMonitor streams to the device) and
firmware/dashboard/src/builtin_themes.h (the themes compiled into the firmware as a fallback).
Checks WCAG contrast of text and status colors against each surface.

Series pairs were checked with the dataviz skill's validate_palette.js (lightness band, chroma, CVD
separation, normal-vision floor, contrast); re-run it if you change a series color:
  node validate_palette.js "<series1>,<series2>" --mode dark|light --surface <surface>
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
KEYS = ["surface", "grid", "text", "text2", "series1", "series2", "good", "critical", "separator", "button", "accent"]
LAYOUTS = ["quad", "stacked", "focus", "tiles", "lcars"]
BUILTIN = ["Dark", "Light"]

THEMES = [
    dict(name="Dark", layout="quad", surface="1a1a19", grid="383835", text="ffffff", text2="c3c2b7",
         series1="3987e5", series2="d95926", good="0ca30c", critical="d03b3b", separator="000000",
         button="2c2c2a", accent="3987e5"),
    dict(name="Light", layout="quad", surface="fcfcfb", grid="d9d8d4", text="0b0b0b", text2="52514e",
         series1="2a78d6", series2="eb6834", good="0ca30c", critical="d03b3b", separator="e6e5e1",
         button="efeeea", accent="2a78d6"),
    dict(name="Retro Green", layout="stacked", surface="0a140a", grid="1f3a1f", text="8dff8d", text2="4fc04f",
         series1="22a050", series2="4a7fe0", good="0ca30c", critical="e05555", separator="000000",
         button="132613", accent="22a050"),
    dict(name="Amber", layout="tiles", surface="140c00", grid="3a2a10", text="ffcf80", text2="c8913a",
         series1="c98500", series2="3987e5", good="0ca30c", critical="d03b3b", separator="000000",
         button="241806", accent="c98500"),
    dict(name="Ocean", layout="focus", surface="0b1d2a", grid="1d3a4f", text="e6f4ff", text2="9cc3dd",
         series1="2f93c2", series2="d07a3a", good="0ca30c", critical="d03b3b", separator="04111a",
         button="12293a", accent="2f93c2"),
    dict(name="Synthwave", layout="tiles", surface="1a0b2e", grid="3b1f5c", text="fdf0ff", text2="d3a6ff",
         series1="e05a9a", series2="5b6ff0", good="0ca30c", critical="ff4d4d", separator="0d0418",
         button="2a1546", accent="e05a9a"),
    dict(name="Solarized", layout="stacked", surface="002b36", grid="0b4452", text="fdf6e3", text2="93a1a1",
         series1="268bd2", series2="cb4b16", good="859900", critical="dc322f", separator="001f27",
         button="073642", accent="268bd2"),
    # LCARS: black screen with the classic orange / lavender / peach frame (accent / button / text); the data colors
    # (series) are darker blue/orange variants that pass the validator on black.
    dict(name="LCARS", layout="lcars", surface="000000", grid="2b2b45", text="ffcc99", text2="cc99cc",
         series1="6f86f5", series2="d9772e", good="99cc66", critical="cc4444", separator="000000",
         button="cc99cc", accent="ff9900"),
    dict(name="High Contrast", layout="tiles", surface="000000", grid="666666", text="ffffff", text2="e6e6e6",
         series1="3987e5", series2="c98500", good="0ca30c", critical="ff4040", separator="4d4d4d",
         button="1a1a1a", accent="ffffff"),
]


def luminance(hex6):
    def chan(c):
        c = c / 255
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (int(hex6[i:i + 2], 16) for i in (0, 2, 4))
    return 0.2126 * chan(r) + 0.7152 * chan(g) + 0.0722 * chan(b)


def contrast(a, b):
    la, lb = sorted((luminance(a), luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def check(t):
    problems = []
    for key, minimum in [("text", 7.0), ("text2", 4.5), ("series1", 3.0), ("series2", 3.0), ("good", 3.0),
                         ("critical", 3.0)]:
        c = contrast(t[key], t["surface"])
        if c < minimum:
            problems.append(f"{key} {c:.2f}:1 < {minimum}:1")
    if t["layout"] not in LAYOUTS:
        problems.append(f"unknown layout {t['layout']}")
    return problems


def slug(name):
    return name.lower().replace(" ", "_")


def main():
    failed = False
    for t in THEMES:
        problems = check(t)
        print(f"{t['name']:14} {t['layout']:8} " + ("OK" if not problems else "FAIL: " + "; ".join(problems)))
        failed |= bool(problems)
    if failed:
        sys.exit(1)

    out = os.path.join(ROOT, "content", "themes")
    os.makedirs(out, exist_ok=True)
    for old in os.listdir(out):  # theme numbering shifts when themes are added; don't leave stale files behind
        if old.endswith(".thm"):
            os.remove(os.path.join(out, old))
    for i, t in enumerate(THEMES):
        with open(os.path.join(out, f"{i:02d}_{slug(t['name'])}.thm"), "w") as f:
            f.write(f"# Xenon dashboard theme. Colors are RGB hex; layout is one of {', '.join(LAYOUTS)}.\n")
            f.write(f"name={t['name']}\nlayout={t['layout']}\n")
            for k in KEYS:
                f.write(f"{k}=#{t[k]}\n")

    with open(os.path.join(ROOT, "firmware", "dashboard", "src", "builtin_themes.h"), "w") as f:
        f.write("// Generated by tools/make_themes.py - do not edit by hand.\n#pragma once\n\n")
        f.write("static const ThemeSpec BUILTIN_THEMES[] = {\n")
        for t in THEMES:
            if t["name"] not in BUILTIN:
                continue
            colors = ", ".join(f"0x{t[k]}" for k in KEYS)
            f.write(f"    {{\"{t['name']}\", LAYOUT_{t['layout'].upper()}, {{{colors}}}}},\n")
        f.write("};\n")
    print(f"wrote {len(THEMES)} theme files to content/themes and builtin_themes.h ({', '.join(BUILTIN)})")


if __name__ == "__main__":
    main()
