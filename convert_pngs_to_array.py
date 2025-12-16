#!/usr/bin/env python3
"""
Convert PNG files to 64x64 RGB565 C++ arrays
Use this if you convert SVGs to PNGs first (e.g., using online converter)
Requires: pip install pillow
"""

import os
import sys
from pathlib import Path
from PIL import Image

def rgb888_to_rgb565(r, g, b):
    """Convert RGB888 to RGB565"""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def png_to_rgb565_array(png_path, size=64):
    """Convert PNG to RGB565 array"""
    img = Image.open(png_path)
    img = img.convert('RGB')
    img = img.resize((size, size), Image.Resampling.LANCZOS)
    
    rgb565_array = []
    for y in range(size):
        for x in range(size):
            r, g, b = img.getpixel((x, y))
            rgb565 = rgb888_to_rgb565(r, g, b)
            rgb565_array.append(rgb565)
    
    return rgb565_array

def generate_cpp_array(name, rgb565_array):
    """Generate C++ array declaration"""
    lines = [f"const uint16_t {name}[{len(rgb565_array)}] = {{"]
    
    for i in range(0, len(rgb565_array), 16):
        chunk = rgb565_array[i:i+16]
        hex_values = ', '.join(f'0x{val:04X}' for val in chunk)
        lines.append(f"    {hex_values},")
    
    lines.append("};")
    return '\n'.join(lines)

# Popular emoji Unicode codes
POPULAR_EMOJIS = [
    "1F600", "1F601", "1F602", "1F603", "1F604", "1F605", "1F606",
    "1F609", "1F60A", "1F60D", "1F60E", "1F60F", "1F618", "1F61A",
    "1F61B", "1F61C", "1F61D", "1F61E", "1F620", "1F621", "1F622",
    "1F623", "1F624", "1F625", "1F628", "1F629", "1F62A", "1F62D",
    "1F631", "1F632"
]

def main():
    # Look for PNG files in openmoji-png-color or current directory
    png_dir = Path("openmoji-png-color")
    if not png_dir.exists():
        png_dir = Path(".")
        print("Note: Looking for PNG files in current directory")
        print("Expected naming: 1F600.png, 1F601.png, etc.")
    else:
        print(f"Looking for PNG files in {png_dir}")
    
    output_file = Path("EmojisData.h")
    arrays = []
    
    print("\nConverting PNG files to RGB565 arrays...\n")
    
    for i, emoji_code in enumerate(POPULAR_EMOJIS):
        # Try different extensions
        for ext in ['.png', '.PNG']:
            png_file = png_dir / f"{emoji_code}{ext}"
            if png_file.exists():
                try:
                    print(f"Converting {i+1}/30: {emoji_code}{ext}...", end=" ")
                    rgb565_array = png_to_rgb565_array(str(png_file), size=64)
                    array_name = f"emoji_{emoji_code.lower()}"
                    arrays.append((array_name, rgb565_array))
                    print("✓")
                    break
                except Exception as e:
                    print(f"✗ Error: {e}")
        else:
            print(f"Warning: {emoji_code}.png not found, skipping...")
    
    if not arrays:
        print("\nError: No PNG files found!")
        print("\nTo use this script:")
        print("1. Convert SVG files to PNG (64x64 or larger)")
        print("2. Place PNG files in 'openmoji-png-color' folder")
        print("3. Name them: 1F600.png, 1F601.png, etc.")
        sys.exit(1)
    
    # Generate C++ header file
    print(f"\nGenerating {output_file}...")
    with open(output_file, 'w') as f:
        f.write("#pragma once\n")
        f.write("#include <Arduino.h>\n\n")
        f.write("// OpenMoji emoji data converted from PNG to 64x64 RGB565\n")
        f.write("// Source: https://openmoji.org (CC BY-SA 4.0)\n\n")
        
        for name, array in arrays:
            f.write(generate_cpp_array(name, array))
            f.write("\n\n")
        
        # Generate array of pointers
        f.write(f"const uint16_t* emoji_arrays[{len(arrays)}] = {{\n")
        for name, _ in arrays:
            f.write(f"    {name},\n")
        f.write("};\n\n")
        f.write(f"const int NUM_EMOJIS = {len(arrays)};\n")
    
    print(f"\n✓ Successfully converted {len(arrays)} emojis!")
    print(f"✓ Output written to {output_file}")

if __name__ == "__main__":
    main()

