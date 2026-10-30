#!/usr/bin/env python3
"""Generates the Monitone icons: SVG sources, PNG renditions and Windows .ico files.

Design: monitor + tone. A rounded tile with a cyan-indigo-magenta gradient, a
white monitor and a glowing sine tone burst on its dark screen. Sizes up to
32 px use a simplified drawing (thicker strokes, no glow or shading), 16 and
20 px a single wave period, so they stay crisp in the notification area.

Requires rsvg-convert (librsvg) and icotool (icoutils).
Usage: tools/icon/make_icon.py   (writes into res/)
"""

import math
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "res"
TINY_SIZES = [16, 20]
SMALL_SIZES = [24, 32]
LARGE_SIZES = [40, 48, 64, 96, 128, 256]

PALETTES = {
    "Monitone": {
        "tile": ["#22D3EE", "#6366F1", "#C026D3"],
        "screen": ["#0B1026", "#1E1B4B"],
        "wave": ["#67E8F9", "#F0ABFC"],
        "bezel": "#F8FAFC",
        "stand": "#E2E8F0",
    },
    "MonitonePaused": {
        "tile": ["#CBD5E1", "#94A3B8", "#64748B"],
        "screen": ["#1E293B", "#334155"],
        "wave": ["#E2E8F0", "#CBD5E1"],
        "bezel": "#F8FAFC",
        "stand": "#E2E8F0",
    },
}


def tone_path(x0, x1, cy, amplitude, periods, steps=160):
    """Sine burst with a raised-cosine envelope, as an SVG polyline path."""
    points = []
    for i in range(steps + 1):
        t = i / steps
        envelope = math.sin(math.pi * t) ** 1.2
        x = x0 + (x1 - x0) * t
        y = cy - amplitude * envelope * math.sin(2 * math.pi * periods * t)
        points.append(f"{x:.2f},{y:.2f}")
    return "M" + " L".join(points)


def gradient(name, colors, x2="1", y2="1"):
    stops = "".join(
        f'<stop offset="{i / (len(colors) - 1):.2f}" stop-color="{c}"/>' for i, c in enumerate(colors))
    return f'<linearGradient id="{name}" x1="0" y1="0" x2="{x2}" y2="{y2}">{stops}</linearGradient>'


def large_svg(p):
    wave = tone_path(64, 192, 116, 30, 2.5)
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">
  <defs>
    {gradient("tile", p["tile"])}
    {gradient("screen", p["screen"], x2="0")}
    {gradient("wave", p["wave"], y2="0")}
    <linearGradient id="gloss" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#FFFFFF" stop-opacity="0.35"/>
      <stop offset="1" stop-color="#FFFFFF" stop-opacity="0"/>
    </linearGradient>
    <filter id="glow" x="-20%" y="-40%" width="140%" height="180%">
      <feGaussianBlur stdDeviation="5" result="blur"/>
      <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>
    <filter id="shadow" x="-20%" y="-20%" width="140%" height="150%">
      <feDropShadow dx="0" dy="6" stdDeviation="7" flood-color="#0B1026" flood-opacity="0.35"/>
    </filter>
    <clipPath id="tileClip"><rect x="8" y="8" width="240" height="240" rx="56"/></clipPath>
  </defs>
  <rect x="8" y="8" width="240" height="240" rx="56" fill="url(#tile)"/>
  <ellipse cx="128" cy="8" rx="190" ry="118" fill="url(#gloss)" clip-path="url(#tileClip)"/>
  <g filter="url(#shadow)">
    <path d="M112 174 h32 l7 24 h-46 z" fill="{p["stand"]}"/>
    <rect x="84" y="194" width="88" height="14" rx="7" fill="{p["bezel"]}"/>
    <rect x="38" y="52" width="180" height="126" rx="18" fill="{p["bezel"]}"/>
  </g>
  <rect x="50" y="64" width="156" height="102" rx="9" fill="url(#screen)"/>
  <path d="{wave}" fill="none" stroke="url(#wave)" stroke-width="9" stroke-linecap="round"
        stroke-linejoin="round" filter="url(#glow)"/>
</svg>
"""


def small_svg(p, periods=1.5, stroke=22):
    wave = tone_path(58, 198, 114, 34, periods)
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">
  <defs>
    {gradient("tile", p["tile"])}
    {gradient("wave", p["wave"], y2="0")}
  </defs>
  <rect x="0" y="0" width="256" height="256" rx="52" fill="url(#tile)"/>
  <rect x="24" y="36" width="208" height="152" rx="22" fill="{p["bezel"]}"/>
  <rect x="42" y="54" width="172" height="116" rx="8" fill="{p["screen"][0]}"/>
  <path d="{wave}" fill="none" stroke="url(#wave)" stroke-width="{stroke}" stroke-linecap="round"
        stroke-linejoin="round"/>
  <rect x="104" y="186" width="48" height="22" fill="{p["stand"]}"/>
  <rect x="68" y="204" width="120" height="26" rx="13" fill="{p["bezel"]}"/>
</svg>
"""


def render(svg_path, size, png_path):
    subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), "-o", str(png_path), str(svg_path)],
                   check=True)


def main():
    OUT.mkdir(exist_ok=True)
    for name, palette in PALETTES.items():
        large = OUT / f"{name}.svg"
        small = OUT / f"{name}-small.svg"
        large.write_text(large_svg(palette), encoding="utf-8")
        tiny = OUT / f"{name}-tiny.svg"
        small.write_text(small_svg(palette), encoding="utf-8")
        tiny.write_text(small_svg(palette, periods=1.0, stroke=30), encoding="utf-8")
        with tempfile.TemporaryDirectory() as tmp:
            pngs = []
            for size in TINY_SIZES + SMALL_SIZES + LARGE_SIZES:
                png = pathlib.Path(tmp) / f"{size}.png"
                source = tiny if size in TINY_SIZES else small if size in SMALL_SIZES else large
                render(source, size, png)
                pngs.append(png)
            # 256 px is stored PNG-compressed, the others as BMP for old consumers.
            command = ["icotool", "-c", "-o", str(OUT / f"{name}.ico")]
            for png in pngs:
                command += (["-r", str(png)] if png.stem == "256" else [str(png)])
            subprocess.run(command, check=True)
            if name == "Monitone":
                render(large, 256, OUT / "Monitone-256.png")
        print(f"wrote {name}.ico")


if __name__ == "__main__":
    main()
