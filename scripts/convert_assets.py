#!/usr/bin/env python3
import sys
import os
from PIL import Image, ImageSequence

# ============================================================
# Utils
# ============================================================

def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def write_header_guard(f):
    f.write("#pragma once\n")
    f.write("#include <cstdint>\n")
    f.write("#include <vector>\n")
    f.write("#include \"AnimationPlayer.hpp\"\n\n")

def ensure_dir(path):
    if not os.path.exists(path):
        os.makedirs(path)

# ============================================================
# ICON (PNG → .hpp)
# ============================================================

def convert_icon(png_path, out_dir, max_dim=None):
    name = os.path.splitext(os.path.basename(png_path))[0].upper()
    img = Image.open(png_path).convert("RGB")

    if max_dim is not None:
        # Fit inside max_dim x max_dim, preserve aspect
        img.thumbnail((max_dim, max_dim), Image.LANCZOS)

    w, h = img.size
    pixels = list(img.getdata())

    out_path = os.path.join(out_dir, f"{name.lower()}.hpp")
    with open(out_path, "w", encoding="utf-8") as f:
        write_header_guard(f)
        f.write(f"namespace asset::icon {{\n\n")
        
        # Raw pixel data
        f.write(f"static const uint16_t {name}_DATA[{w*h}] = {{\n")
        for i, (r, g, b) in enumerate(pixels):
            val = rgb888_to_rgb565(r, g, b)
            f.write(f"0x{val:04X}, ")
            if (i + 1) % 12 == 0:
                f.write("\n")
        f.write("\n};\n\n")
        
        # Icon object ready to use
        f.write(f"// Icon object ready for DisplayManager::registerIcon()\n")
        f.write(f"static const struct {{\n")
        f.write(f"    int w = {w};\n")
        f.write(f"    int h = {h};\n")
        f.write(f"    const uint16_t* rgb = {name}_DATA;\n")
        f.write(f"}} {name};\n\n")
        
        f.write("} // namespace asset::icon\n")

    print(f"[ICON] Generated: {out_path}")

# ============================================================
# EMOTION (GIF → .hpp)
# ============================================================

def convert_emotion(gif_path, out_dir, fps=20, loop=True):
    name = os.path.splitext(os.path.basename(gif_path))[0].upper()
    img = Image.open(gif_path)

    frames = []
    for frame in ImageSequence.Iterator(img):
        frames.append(frame.convert("RGB"))

    if not frames:
        print("No frames found!")
        return

    w, h = frames[0].size
    frame_count = len(frames)

    out_path = os.path.join(out_dir, f"{name.lower()}.hpp")
    with open(out_path, "w", encoding="utf-8") as f:
        write_header_guard(f)
        f.write(f"namespace asset::emotion {{\n\n")

        # Frames data
        for idx, frame in enumerate(frames):
            pixels = list(frame.getdata())
            f.write(f"static const uint16_t {name}_FRAME{idx}_DATA[{w*h}] = {{\n")
            for i, (r, g, b) in enumerate(pixels):
                val = rgb888_to_rgb565(r, g, b)
                f.write(f"0x{val:04X}, ")
                if (i + 1) % 12 == 0:
                    f.write("\n")
            f.write("\n};\n\n")

        # Animation object ready to use
        f.write(f"// Animation object ready for DisplayManager::registerEmotion()\n")
        f.write(f"static Animation {name} = {{\n")
        f.write(f"    .frames = {{\n")
        
        for idx in range(frame_count):
            f.write(f"        AnimationFrame({w}, {h}, {name}_FRAME{idx}_DATA, nullptr)")
            if idx < frame_count - 1:
                f.write(",\n")
            else:
                f.write("\n")
        
        f.write(f"    }},\n")
        f.write(f"    .fps = {fps},\n")
        f.write(f"    .loop = {'true' if loop else 'false'}\n")
        f.write(f"}};\n\n")

        f.write("} // namespace asset::emotion\n")

    print(f"[EMOTION] Generated: {out_path}")

# ============================================================
# MAIN
# ============================================================

def main():
    if len(sys.argv) < 3:
        print("Usage:")
        print("  Icon:    python convert_asset.py icon input.png output_dir/ [max_dim]")
        print("  Emotion: python convert_asset.py emotion input.gif output_dir/ [fps] [loop]")
        return

    mode = sys.argv[1]
    input_path = sys.argv[2]
    output_dir = sys.argv[3] if len(sys.argv) >= 4 else "."

    ensure_dir(output_dir)

    if mode == "icon":
        max_dim = int(sys.argv[4]) if len(sys.argv) >= 5 else None
        convert_icon(input_path, output_dir, max_dim)

    elif mode == "emotion":
        fps = int(sys.argv[4]) if len(sys.argv) >= 5 else 20
        loop = (sys.argv[5].lower() != "false") if len(sys.argv) >= 6 else True
        convert_emotion(input_path, output_dir, fps, loop)

    else:
        print("Unknown mode:", mode)

if __name__ == "__main__":
    main()
