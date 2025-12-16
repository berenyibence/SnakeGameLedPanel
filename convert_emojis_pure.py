#!/usr/bin/env python3
"""
convert_emojis_pure.py
=====================

Pure-Python OpenMoji SVG converter for this project.

- Input:  `openmoji-svg-color/<UNICODE>.svg` (OpenMoji color SVGs)
- Output: `EmojisData.h` containing 64x64 RGB565 arrays for the ESP32 LED matrix

Goals / constraints:
- Works on Windows without installing system Cairo libraries.
- Produces 64x64 bitmaps centered in the canvas.
- Optional preview PNGs for quick visual verification.

Dependencies:
    pip install svglib pillow

Notes:
- We intentionally do NOT use ReportLab's renderPM / Cairo backends.
- SVG support is "good enough" for OpenMoji at 64x64, but SVG is huge:
  if you find an emoji that renders oddly, send it and we can extend the renderer.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import Iterable, Optional, Tuple

from PIL import Image, ImageDraw
from svglib.svglib import svg2rlg

from reportlab.graphics.shapes import (
    Circle,
    Ellipse,
    Group,
    Line,
    Path as RLPath,
    Polygon,
    PolyLine,
    Rect,
)


# ---------------------------------------------------------------------------
# Emoji selection (seed list)
# ---------------------------------------------------------------------------
# OpenMoji filenames are the unicode codepoint(s) separated by '-', without ".svg".
# Examples:
# - "1F602"                  -> 😂
# - "2764-FE0F"              -> ❤️ (heart + variation selector)
# - "1F468-200D-1F4BB"       -> 👨‍💻 (ZWJ sequence)
#
# We split "faces" from everything else so we can cap face emojis to keep the
# set diverse (requested: at most 10 faces; the rest should be "fun" emojis).
FACE_SEED = [
    "1F602",  # 😂
    "1F923",  # 🤣
    "1F60D",  # 😍
    "1F970",  # 🥰
    "1F60A",  # 😊
    "1F60E",  # 😎
    "1F609",  # 😉
    "1F618",  # 😘
    "1F61C",  # 😜
    "1F62D",  # 😭
    "1F914",  # 🤔
    "1F644",  # 🙄
]

FUN_SEED = [
    # Hearts / symbols (requested)
    "2764-FE0F", "1F49B", "1F49A", "1F499", "1F49C", "1F5A4", "1F496", "1F495", "1F49E", "1F494",
    "1F48B", "1F4A5", "1F4A3", "1F4A9",  # 💩
    "2705", "274C", "2753", "2757", "26A0-FE0F", "1F6AB", "1F6A8", "1F4A1", "2728", "1F525", "1F4AF",

    # Hands / gestures (not faces)
    "1F44D", "1F44E", "1F44F", "1F64F", "1F91D", "1F918", "270A", "1F44C", "1F91E",

    # Sports / balls / games (requested: balls)
    "26BD", "1F3C0", "1F3C8", "26BE", "1F3BE", "1F3D0", "1F94E", "1F94F",
    "1F3B2", "1F3AE", "1F3AF", "1F3C6", "1F947",

    # Party / fun
    "1F389", "1F38A", "1F973", "1F37E", "1F381", "1F38E", "1F3B6", "1F3A7",

    # Food / drink
    "1F355", "1F354", "1F35F", "1F357", "1F372", "1F366", "1F36A", "1F370", "1F36D", "1F37F",
    "1F96A", "1F9C0", "2615", "1F37A", "1F377", "1F378",

    # Animals / nature
    "1F436", "1F431", "1F98A", "1F981", "1F984", "1F438", "1F41F", "1F433", "1F40D",
    "1F33B", "1F339", "1F340",

    # Transport / places
    "1F697", "1F695", "1F6F4", "1F6A2", "2708-FE0F", "1F680", "1F681",
    "1F3E0", "1F30D", "1F30E", "1F30F",

    # Objects / tech
    "1F4F1", "1F4BB", "1F4FA", "1F4F7", "1F50A", "1F4A4", "23F0", "1F514",
    "1F4B0", "1F511",
]


def _list_available_emoji_codes(svg_dir: Path) -> list[str]:
    """Return available OpenMoji emoji codes (filenames without suffix), sorted."""
    # Keep only files directly under svg_dir; this repo uses a flat directory.
    codes = [p.stem.upper() for p in svg_dir.glob("*.svg")]
    codes = sorted(set(codes))
    return codes


def _first_segment_hex(code: str) -> int:
    """Parse the first hex segment of an OpenMoji code (best-effort)."""
    try:
        first = code.split("-")[0]
        return int(first, 16)
    except Exception:
        return 0


def _is_face_like(code: str) -> bool:
    """
    Heuristic: treat classic face blocks as "faces".

    We intentionally keep this narrow to avoid classifying unrelated emojis as faces.
    """
    v = _first_segment_hex(code)
    # 😀..🙏 (Emoticons) and 🤐..🤯 (Supplemental Symbols and Pictographs subset of faces)
    return (0x1F600 <= v <= 0x1F64F) or (0x1F910 <= v <= 0x1F92F) or (0x1F930 <= v <= 0x1F9AF)


def _is_reasonable_autofill(code: str) -> bool:
    """
    Filter out low-value / tiny symbols (e.g. '-' or other ASCII-like entries)
    that would waste space in the firmware.
    """
    v = _first_segment_hex(code)
    return v >= 0x2300  # keep common emoji blocks; drop ASCII-ish entries like 002D


def _select_emoji_codes(svg_dir: Path, target_count: int, seed: list[str]) -> list[str]:
    """
    Select emoji codes to convert.

    Strategy:
    - Start with the curated seed list (most common / requested emojis)
    - Auto-fill up to target_count from what's available in the OpenMoji folder
      preferring "simpler" single-codepoint emojis (fewer '-' segments).
    """
    available = set(_list_available_emoji_codes(svg_dir))

    selected: list[str] = []
    seen: set[str] = set()

    def add(code: str) -> None:
        c = code.upper().strip()
        if not c or c in seen:
            return
        if c not in available:
            return
        seen.add(c)
        selected.append(c)

    for c in seed:
        add(c)
        if len(selected) >= target_count:
            return selected[:target_count]

    # Fill remaining slots.
    # Prefer emojis with fewer segments (e.g., "1F602" over complex ZWJ sequences).
    def sort_key(c: str) -> tuple[int, int, str]:
        return (c.count("-"), len(c), c)

    for c in sorted(available, key=sort_key):
        add(c)
        if len(selected) >= target_count:
            break

    return selected


# ---------------------------------------------------------------------------
# RGB helpers
# ---------------------------------------------------------------------------
def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    """Convert RGB888 to RGB565 integer."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def _safe_int(x) -> int:
    try:
        return int(x)
    except Exception:
        return 0


def color_to_rgb(color) -> Optional[Tuple[int, int, int]]:
    """
    Convert ReportLab/svglib colors to RGB tuple.
    Returns None if no color / transparent.
    """
    if color is None:
        return None

    # svglib often uses reportlab.lib.colors.Color with .red/.green/.blue in [0..1]
    for attrs in (("red", "green", "blue"), ("r", "g", "b")):
        if all(hasattr(color, a) for a in attrs):
            r = getattr(color, attrs[0])
            g = getattr(color, attrs[1])
            b = getattr(color, attrs[2])
            try:
                # Normalize floats to 0..255
                if isinstance(r, float) and 0.0 <= r <= 1.0:
                    return (int(r * 255), int(g * 255), int(b * 255))
                return (_safe_int(r), _safe_int(g), _safe_int(b))
            except Exception:
                return None

    # Fallback: try hex string-ish (rare)
    try:
        s = str(color).strip()
        if s.startswith("#") and len(s) in (7, 9):
            rr = int(s[1:3], 16)
            gg = int(s[3:5], 16)
            bb = int(s[5:7], 16)
            return (rr, gg, bb)
    except Exception:
        pass

    return None


# ---------------------------------------------------------------------------
# Drawing traversal + rendering
# ---------------------------------------------------------------------------
def _children(el) -> Iterable:
    """
    Best-effort retrieval of children from svglib/reportlab elements.
    """
    if el is None:
        return []
    if hasattr(el, "getContents"):
        try:
            c = el.getContents()
            if c:
                return c
        except Exception:
            pass
    if hasattr(el, "contents"):
        try:
            c = el.contents
            if c:
                return c
        except Exception:
            pass
    return []


def render_drawing_to_pil(
    drawing,
    size: int = 64,
    background_key: Tuple[int, int, int] = (255, 0, 255),
) -> Image.Image:
    """
    Render a ReportLab drawing into a PIL Image without Cairo.

    Important:
    - SVGs are generally Y-down; svglib already maps them for reportlab. In practice,
      OpenMoji renders correctly for the LED matrix WITHOUT an extra Y flip here.
    """
    img = Image.new("RGB", (size, size), background_key)
    draw = ImageDraw.Draw(img)

    # Robust bounds: use getBounds() when possible (min/max in drawing space).
    try:
        b = drawing.getBounds()
        if not b or len(b) < 4:
            raise ValueError("no bounds")
        min_x, min_y, max_x, max_y = b[0], b[1], b[2], b[3]
    except Exception:
        min_x, min_y, max_x, max_y = 0.0, 0.0, float(getattr(drawing, "width", size)), float(getattr(drawing, "height", size))

    dw = max(1e-6, max_x - min_x)
    dh = max(1e-6, max_y - min_y)

    scale = min(size / dw, size / dh)
    offset_x = (size - dw * scale) / 2.0
    offset_y = (size - dh * scale) / 2.0

    def tx(x: float) -> float:
        return offset_x + (x - min_x) * scale

    def ty(y: float) -> float:
        # Do NOT flip Y here; OpenMoji renders correctly in practice for this pipeline.
        return offset_y + (y - min_y) * scale

    def render_element(el):
        if el is None:
            return

        # Groups: recurse
        if isinstance(el, Group):
            for c in _children(el):
                render_element(c)
            return

        # Rect
        if isinstance(el, Rect):
            fill = color_to_rgb(getattr(el, "fillColor", None))
            stroke = color_to_rgb(getattr(el, "strokeColor", None))
            x1, y1 = tx(el.x), ty(el.y)
            x2, y2 = tx(el.x + el.width), ty(el.y + el.height)
            if fill is not None:
                draw.rectangle([x1, y1, x2, y2], fill=fill)
            if stroke is not None and getattr(el, "strokeWidth", 0) > 0:
                draw.rectangle([x1, y1, x2, y2], outline=stroke, width=max(1, int(el.strokeWidth)))
            return

        # Circle / Ellipse
        if isinstance(el, Circle):
            fill = color_to_rgb(getattr(el, "fillColor", None))
            stroke = color_to_rgb(getattr(el, "strokeColor", None))
            cx, cy = tx(el.cx), ty(el.cy)
            r = float(el.r) * scale
            box = [cx - r, cy - r, cx + r, cy + r]
            if fill is not None:
                draw.ellipse(box, fill=fill)
            if stroke is not None and getattr(el, "strokeWidth", 0) > 0:
                draw.ellipse(box, outline=stroke, width=max(1, int(el.strokeWidth)))
            return

        if isinstance(el, Ellipse):
            fill = color_to_rgb(getattr(el, "fillColor", None))
            stroke = color_to_rgb(getattr(el, "strokeColor", None))
            cx, cy = tx(el.cx), ty(el.cy)
            rx = float(el.rx) * scale
            ry = float(el.ry) * scale
            box = [cx - rx, cy - ry, cx + rx, cy + ry]
            if fill is not None:
                draw.ellipse(box, fill=fill)
            if stroke is not None and getattr(el, "strokeWidth", 0) > 0:
                draw.ellipse(box, outline=stroke, width=max(1, int(el.strokeWidth)))
            return

        # Line
        if isinstance(el, Line):
            stroke = color_to_rgb(getattr(el, "strokeColor", None)) or (255, 255, 255)
            w = max(1, int(getattr(el, "strokeWidth", 1)))
            draw.line([(tx(el.x1), ty(el.y1)), (tx(el.x2), ty(el.y2))], fill=stroke, width=w)
            return

        # Polygon / PolyLine
        if isinstance(el, (Polygon, PolyLine)):
            pts = getattr(el, "points", None)
            if callable(pts):
                try:
                    pts = pts()
                except Exception:
                    pts = None
            if not isinstance(pts, (list, tuple)) or len(pts) < 4:
                return

            points = [(tx(pts[i]), ty(pts[i + 1])) for i in range(0, len(pts) - 1, 2)]
            if len(points) < 2:
                return

            fill = color_to_rgb(getattr(el, "fillColor", None))
            stroke = color_to_rgb(getattr(el, "strokeColor", None))
            w = max(1, int(getattr(el, "strokeWidth", 1)))

            if isinstance(el, Polygon) and fill is not None and len(points) >= 3:
                draw.polygon(points, fill=fill)
            if stroke is not None:
                # Ensure closed stroke for polygons
                if isinstance(el, Polygon) and points:
                    draw.line(points + [points[0]], fill=stroke, width=w)
                else:
                    draw.line(points, fill=stroke, width=w)
            return

        # Path (dominant in OpenMoji): we approximate by connecting point sequences.
        if isinstance(el, RLPath):
            fill = color_to_rgb(getattr(el, "fillColor", None))
            stroke = color_to_rgb(getattr(el, "strokeColor", None))
            w = max(1, int(getattr(el, "strokeWidth", 1)))

            pts = getattr(el, "points", None)
            if callable(pts):
                try:
                    pts = pts()
                except Exception:
                    pts = None
            if not isinstance(pts, (list, tuple)) or len(pts) < 4:
                return

            points = [(tx(pts[i]), ty(pts[i + 1])) for i in range(0, len(pts) - 1, 2)]
            if len(points) < 2:
                return

            # Best-effort fill: treat as polygon. At 64x64 this is usually acceptable.
            if fill is not None and len(points) >= 3:
                draw.polygon(points, fill=fill)
            if stroke is not None:
                draw.line(points, fill=stroke, width=w)
            return

        # Recurse into anything else that has children
        for c in _children(el):
            render_element(c)

    # Render root contents
    for c in _children(drawing):
        render_element(c)

    return img


def center_by_visible_pixels(
    img: Image.Image,
    background_key: Tuple[int, int, int] = (255, 0, 255),
) -> Image.Image:
    """
    Re-center an image by its visible pixels (non-background_key).

    This is a *second-pass* centering step that fixes cases where SVG bounds /
    renderer quirks shift the visible content inside the 64x64 canvas.
    """
    img = img.convert("RGB")
    w, h = img.size

    min_x, min_y = w, h
    max_x, max_y = -1, -1
    for y in range(h):
        for x in range(w):
            if img.getpixel((x, y)) == background_key:
                continue
            if x < min_x:
                min_x = x
            if x > max_x:
                max_x = x
            if y < min_y:
                min_y = y
            if y > max_y:
                max_y = y

    # Fully transparent / background-only: nothing to center.
    if max_x < min_x or max_y < min_y:
        return img

    bbox_cx = (min_x + max_x) / 2.0
    bbox_cy = (min_y + max_y) / 2.0
    canvas_cx = (w - 1) / 2.0
    canvas_cy = (h - 1) / 2.0

    dx = int(round(canvas_cx - bbox_cx))
    dy = int(round(canvas_cy - bbox_cy))

    centered = Image.new("RGB", (w, h), background_key)
    # Paste with an offset; PIL will clip automatically.
    centered.paste(img, (dx, dy))
    return centered


def pil_to_rgb565_array(
    img: Image.Image,
    background_key: Tuple[int, int, int] = (255, 0, 255),
    outline_boost_threshold: int = 10,
    outline_boost_rgb: Tuple[int, int, int] = (40, 40, 40),
) -> list[int]:
    """
    Convert PIL RGB image to 64x64 RGB565 array.

    - background_key becomes 0x0000 (transparent)
    - very dark pixels are lifted to a dark gray so OpenMoji outlines are visible on LED matrix
    """
    img = img.convert("RGB")
    w, h = img.size
    out: list[int] = []
    for y in range(h):
        for x in range(w):
            r, g, b = img.getpixel((x, y))
            if (r, g, b) == background_key:
                out.append(0x0000)
                continue
            if r <= outline_boost_threshold and g <= outline_boost_threshold and b <= outline_boost_threshold:
                r, g, b = outline_boost_rgb
            out.append(rgb888_to_rgb565(r, g, b))
    return out


def generate_cpp_array(name: str, rgb565_array: list[int]) -> str:
    """Generate a C++ array declaration."""
    lines = [f"const uint16_t {name}[{len(rgb565_array)}] = {{"]
    for i in range(0, len(rgb565_array), 16):
        chunk = rgb565_array[i : i + 16]
        hex_values = ", ".join(f"0x{val:04X}" for val in chunk)
        lines.append(f"    {hex_values},")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert OpenMoji SVGs to 64x64 RGB565 C++ arrays (pure Python).")
    parser.add_argument("--svg-dir", default="openmoji-svg-color", help="Directory containing OpenMoji color SVGs")
    parser.add_argument("--out", default="EmojisData.h", help="Output header file path")
    parser.add_argument("--size", type=int, default=64, help="Output size in pixels (default 64)")
    parser.add_argument("--count", type=int, default=100, help="How many emojis to convert (default 100)")
    parser.add_argument("--max-faces", type=int, default=10, help="Maximum number of face emojis to include (default 10)")
    parser.add_argument("--previews", action="store_true", help="Write PNG previews to previews/ directory")
    args = parser.parse_args()

    svg_dir = Path(args.svg_dir)
    if not svg_dir.exists():
        print(f"Error: {svg_dir} directory not found!")
        return 1

    out_file = Path(args.out)
    size = int(args.size)

    print("\nConverting OpenMoji SVGs to RGB565 arrays...\n")
    print("Using pure Python rendering (no Cairo required)\n")

    arrays: list[Tuple[str, list[int]]] = []
    preview_dir = Path("previews")
    if args.previews:
        preview_dir.mkdir(parents=True, exist_ok=True)

    target_count = max(1, int(args.count))
    max_faces = max(0, int(args.max_faces))

    # Build selection with a hard cap on faces, and bias toward "fun" emojis.
    available = set(_list_available_emoji_codes(svg_dir))
    selected: list[str] = []
    seen: set[str] = set()
    face_count = 0

    def add(code: str) -> None:
        nonlocal face_count
        c = code.upper().strip()
        if not c or c in seen:
            return
        if c not in available:
            return
        if _is_face_like(c):
            if face_count >= max_faces:
                return
            face_count += 1
        seen.add(c)
        selected.append(c)

    # 1) Add faces up to the cap.
    for c in FACE_SEED:
        add(c)
        if len(selected) >= target_count:
            break

    # 2) Add fun/common emojis.
    if len(selected) < target_count:
        for c in FUN_SEED:
            add(c)
            if len(selected) >= target_count:
                break

    # 3) Auto-fill: prefer simple codes, skip ASCII-ish, and enforce face cap.
    if len(selected) < target_count:
        def sort_key(c: str) -> tuple[int, int, str]:
            # fewer ZWJ/VS sequences first, then shorter codes first.
            return (c.count("-"), len(c), c)

        for c in sorted(available, key=sort_key):
            if len(selected) >= target_count:
                break
            if not _is_reasonable_autofill(c):
                continue
            add(c)

    emoji_codes = selected[:target_count]
    if not emoji_codes:
        print("Error: No emojis found to convert. Check your --svg-dir path.")
        return 1

    for i, emoji_code in enumerate(emoji_codes):
        svg_file = svg_dir / f"{emoji_code}.svg"
        if not svg_file.exists():
            print(f"Converting {i+1}/{len(emoji_codes)}: {emoji_code}.svg... (missing) ✗")
            continue

        try:
            print(f"Converting {i+1}/{len(emoji_codes)}: {emoji_code}.svg... ", end="")
            drawing = svg2rlg(str(svg_file))
            img = render_drawing_to_pil(drawing, size=size)
            img = center_by_visible_pixels(img)  # robust centering pass
            if args.previews:
                img.save(preview_dir / f"{emoji_code}.png")
            rgb565 = pil_to_rgb565_array(img)
            array_name = f"emoji_{emoji_code.lower()}"
            arrays.append((array_name, rgb565))
            print("✓")
        except Exception as e:
            print(f"✗ ({e})")

    print("\nGenerating EmojisData.h...\n")
    with out_file.open("w", encoding="utf-8") as f:
        f.write("#pragma once\n")
        f.write("#include <Arduino.h>\n\n")
        f.write("// OpenMoji emoji data converted from SVG to 64x64 RGB565\n")
        f.write("// Source: https://openmoji.org (CC BY-SA 4.0)\n\n")

        for name, arr in arrays:
            f.write(generate_cpp_array(name, arr))
            f.write("\n\n")

        f.write(f"const uint16_t* emoji_arrays[{len(arrays)}] = {{\n")
        for name, _ in arrays:
            f.write(f"    {name},\n")
        f.write("};\n\n")
        f.write(f"const int NUM_EMOJIS = {len(arrays)};\n")

    print(f"✓ Successfully converted {len(arrays)} emojis!")
    print(f"✓ Output written to {out_file}")
    if args.previews:
        print(f"✓ PNG previews written to {preview_dir}/")
    print("\nNext step: Recompile your Arduino project to use the new emojis!\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())


