---
layout: default
title: Themes
permalink: /themes/
description: The 12 µMonitor themes, and how to make and share your own.
---

# Themes

µMonitor ships with **12 themes**. Pick one from the display's touch menu (**Theme**) or from the µMonitor menu bar
app (**Display → Theme**). Each theme also picks a suggested layout, which you can change under **Layout**.

There are two kinds:

- **Color themes** restyle the standard layouts (Quad, Stacked, Focus, Tiles) with their own palette.
- **Layout themes** (LCARS, Tron, Windows XP, System 7) come with a full-screen layout of their own, drawn by the
  firmware.

Every screenshot below is a pixel-exact capture read back from the display's memory.

## Color themes

<div class="theme-grid">
{% for t in site.data.themes.color %}
  <figure class="theme-card">
    <img src="{{ '/images/' | append: t.image | relative_url }}" alt="{{ t.name }} theme" width="480" height="320" loading="lazy">
    <figcaption>
      <h3>{{ t.name }}{% if t.builtin %} <span class="tag">built in</span>{% endif %}</h3>
      <p class="meta">Layout: {{ t.layout }}</p>
      <p>{{ t.description }}</p>
      <div class="swatches">
        {% for c in t.colors %}<span title="{{ c[0] }} #{{ c[1] }}" style="background:#{{ c[1] }}"></span>{% endfor %}
      </div>
    </figcaption>
  </figure>
{% endfor %}
</div>

## Layout themes

<div class="theme-grid">
{% for t in site.data.themes.layout %}
  <figure class="theme-card">
    <img src="{{ '/images/' | append: t.image | relative_url }}" alt="{{ t.name }} theme" width="480" height="320" loading="lazy">
    <figcaption>
      <h3>{{ t.name }}</h3>
      <p>{{ t.description }}</p>
    </figcaption>
  </figure>
{% endfor %}
</div>

The [documentation]({{ '/documentation/#layouts-and-themes' | relative_url }}) has portrait screenshots and more
detail on each layout theme.

## How themes work

A theme is a name, a suggested layout and **11 colors**. The colors live in a small text file in `content/themes/`:

```ini
# content/themes/05_synthwave.thm
name=Synthwave
layout=tiles
surface=#1a0b2e
grid=#3b1f5c
text=#fdf0ff
text2=#d3a6ff
series1=#e05a9a
series2=#5b6ff0
good=#0ca30c
critical=#ff4d4d
separator=#0d0418
button=#2a1546
accent=#e05a9a
```

When the display connects, it asks the Mac for its themes and µMonitor streams every `.thm` file over USB. Only
**Dark** and **Light** are compiled into the firmware, so the display always has something to draw before the Mac
connects.

| Key | What it colors |
|---|---|
| `surface` | Card backgrounds |
| `grid` | Graph grid lines and frames, table dividers, the track behind the process bars |
| `text` | Main text, big numbers and the clock |
| `text2` | Labels, units and secondary text |
| `series1` | First graph line (CPU, disk read, network rx) and the process CPU bars |
| `series2` | Second graph line (GPU, disk write, network tx) |
| `good` | The LIVE dot and LINK ACTIVE |
| `critical` | LINK LOST / no data, and the flashing TIME'S UP timer |
| `separator` | The gaps between cards and behind album photos |
| `button` | Touch menu buttons (and the LCARS frame pieces) |
| `accent` | The selected menu button's outline (and the LCARS frame) |

`layout` is one of `quad`, `stacked`, `focus`, `tiles`, `lcars`, `tron`, `xp` or `system 7`. A color theme can
suggest a layout theme's layout: for example, LCARS colors with the Quad layout, or your own colors with LCARS.

## Make your own theme

Every theme is defined in one place, `THEMES` in
[`tools/make_themes.py`]({{ site.repo }}/blob/main/tools/make_themes.py). The `.thm` files are generated from it.

**1. Add an entry** to the `THEMES` list:

```python
dict(name="Dracula", layout="quad", surface="282a36", grid="44475a", text="f8f8f2", text2="c0c4d6",
     series1="8be9fd", series2="ff79c6", good="50fa7b", critical="ff5555", separator="1e1f29",
     button="343746", accent="bd93f9"),
```

Names can be up to **15 characters**. Colors are 6-digit hex without the `#`.

**2. Generate and check it:**

```bash
.venv/bin/python tools/make_themes.py
```

The script checks contrast against `surface` before writing anything, using WCAG ratios:

| Color | Minimum contrast |
|---|---|
| `text` | 7:1 |
| `text2` | 4.5:1 |
| `series1`, `series2`, `good`, `critical` | 3:1 |

If a color fails, the script names it and exits, so a theme can't ship with unreadable text. Pick
`series1` and `series2` so they stay distinguishable for colorblind viewers; the comment at the top of the script
explains how the built-in pairs were validated.

**3. Try it on the display.** Quit µMonitor and run `./run.sh -v`. It streams themes straight from
`content/themes/`, so your new theme shows up in the Theme menu right away. When you're happy with it, rebuild the
app so it's bundled:

```bash
rm -rf build dist && .venv/bin/python setup.py py2app
```

**4. Take a screenshot** for your pull request (with µMonitor quit):

```bash
.venv/bin/python tools/uitest.py "wait 5, shot theme_dracula, wait 30" --snapdir docs/images
```

> **Theme limit:** the firmware keeps up to 12 themes streamed from the Mac (`MAX_HOST_THEMES` in
> `firmware/dashboard/src/app.h`), and 10 are already used. To add more than two new themes, raise that number and
> reflash; each theme takes only about 64 bytes of RAM.

### Going further: a new layout

The LCARS, Tron, XP and System 7 themes are more than colors: each has its own drawing code in
`firmware/dashboard/src/dashboard_view.cpp`, in both landscape and portrait. Adding a layout means adding it to the
`Layout` enum in `app.h` and to `LAYOUTS` in `make_themes.py`, writing the draw functions, and reflashing. It's a
bigger project, but the existing four are good templates.

## Build one and share it

**I'd love to see what the community builds.** A favorite editor palette, a movie computer, a retro OS, your team's
colors: if it fits on a 480×320 screen, it's fair game.

Some ideas nobody has made yet:

- **Color themes:** Dracula, Nord, Gruvbox, Monokai, Catppuccin, Game Boy green, Commodore 64 blue, Fallout
  Pip-Boy
- **Layout themes:** a NeXTSTEP or BeOS desktop, Mac OS X Aqua, Windows 95, a Matrix rain terminal, a car
  dashboard, a 2001 HAL panel

To share a theme:

1. **Open a pull request** on [GitHub]({{ site.repo }}/pulls) with your `THEMES` entry, the generated `.thm` file,
   and a screenshot in `docs/images/`. Tell us the theme's story in the description.
2. **No GitHub?** Email the `dict(...)` line and a photo of your display to
   [{{ site.email }}](mailto:{{ site.email }}?subject=%C2%B5Monitor%20theme).

Good community themes will be added to this page with credit.
