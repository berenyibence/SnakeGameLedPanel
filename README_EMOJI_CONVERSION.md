# OpenMoji Integration Guide

This guide explains how to convert OpenMoji SVG files to embedded C++ arrays for the ESP32 LED matrix.

## Prerequisites

### Recommended: Pure Python Method (Works on Windows/Linux/Mac)
Install pure Python libraries (no system dependencies):
```bash
pip install svglib pillow
```

Then run:
```bash
python convert_emojis_pure.py
```

**Note:** This method manually renders SVG elements and may not perfectly handle very complex paths, but works without any system library dependencies.

### Alternative: If Pure Python Doesn't Work
If the pure Python method has issues with complex SVGs, you can:
1. Use an online SVG to PNG converter (e.g., https://convertio.co/svg-png/)
2. Convert PNG files to 64x64 RGB565 using a PNG-to-array converter
3. Or manually edit the script to use PNG files instead of SVG files

## Conversion Steps

1. **Run the conversion script:**
   ```bash
   python convert_emojis_pure.py
   ```

2. **The script will:**
   - Read SVG files from `openmoji-svg-color/` folder
   - Convert **100 popular emojis** (seeded list + auto-fill) to 64x64 RGB565 bitmaps
   - Generate `EmojisData.h` with embedded C++ arrays

3. **The generated file includes:**
   - Individual emoji arrays (64x64 = 4096 pixels each)
   - Array of pointers to all emojis
   - `NUM_EMOJIS` constant

## Emoji Selection

The script uses a curated seed list of common emojis (faces, hearts, gestures,
objects, food, etc.) and then auto-fills to the requested count using the SVGs
present in `openmoji-svg-color/`.

To change the number converted, use:
```bash
python convert_emojis_pure.py --count 100 --max-faces 10
```

## Customization

To tune the selection:
- Edit `convert_emojis_pure.py` and adjust `EMOJI_SEED`
- Or change the amount with `--count`

## License

OpenMoji emojis are licensed under CC BY-SA 4.0
Source: https://openmoji.org

