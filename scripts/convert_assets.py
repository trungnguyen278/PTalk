#!/usr/bin/env python3
import os
import argparse
from typing import Optional, Tuple
from PIL import Image, ImageSequence

#How to use
# To convert a PNG icon:
#   python scripts/convert_assets.py icon path/to/icon.png output/directory --width 64 --height 64
# To convert a GIF emotion:
#   python scripts/convert_assets.py emotion path/to/emotion.gif output/directory --width 128 --height 128 --fps 20 --loop


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


def resize_with_aspect(
    img: Image.Image,
    target_w: Optional[int] = None,
    target_h: Optional[int] = None,
    max_dim: Optional[int] = None,
) -> Tuple[Image.Image, int, int]:
    """Resize image preserving aspect ratio when only one side is provided.

    If both target_w and target_h are set, resize to that exact size.
    If only one side is provided, scale the other side to preserve aspect ratio.
    If neither is provided but max_dim is set, fit into a square box.
    Returns the resized image and its new dimensions.
    """

    if target_w is not None or target_h is not None:
        if target_w is not None and target_h is not None:
            resized = img.resize((target_w, target_h), Image.LANCZOS)
        elif target_w is not None:
            new_h = max(1, int(round(img.height * (target_w / img.width))))
            resized = img.resize((target_w, new_h), Image.LANCZOS)
        else:
            new_w = max(1, int(round(img.width * (target_h / img.height))))
            resized = img.resize((new_w, target_h), Image.LANCZOS)
    elif max_dim is not None:
        resized = img.copy()
        resized.thumbnail((max_dim, max_dim), Image.LANCZOS)
    else:
        resized = img

    w, h = resized.size
    return resized, w, h

# ============================================================
# ICON (PNG → .hpp)
# ============================================================

def convert_icon(png_path, out_dir, target_w=None, target_h=None, max_dim=None):
    name = os.path.splitext(os.path.basename(png_path))[0].upper()
    img = Image.open(png_path).convert("RGB")

    img, w, h = resize_with_aspect(img, target_w, target_h, max_dim)
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

def convert_emotion(gif_path, out_dir, target_w=None, target_h=None, fps=20, loop=True):
    name = os.path.splitext(os.path.basename(gif_path))[0].upper()
    img = Image.open(gif_path)

    frames = []
    for frame in ImageSequence.Iterator(img):
        resized, w, h = resize_with_aspect(frame.convert("RGB"), target_w, target_h)
        frames.append(resized)

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


def parse_args():
    parser = argparse.ArgumentParser(description="Convert PNG/GIF assets to C++ headers")
    sub = parser.add_subparsers(dest="mode", required=True)

    icon_p = sub.add_parser("icon", help="Convert PNG icon")
    icon_p.add_argument("input_path", help="Input PNG file")
    icon_p.add_argument("output_dir", help="Output directory for generated header")
    icon_p.add_argument("--width", type=int, default=None, help="Force width (height keeps aspect if unset)")
    icon_p.add_argument("--height", type=int, default=None, help="Force height (width keeps aspect if unset)")
    icon_p.add_argument(
        "--max-dim",
        type=int,
        default=None,
        dest="max_dim",
        help="Fit inside NxN box when width/height not provided",
    )

    emo_p = sub.add_parser("emotion", help="Convert GIF animation")
    emo_p.add_argument("input_path", help="Input GIF file")
    emo_p.add_argument("output_dir", help="Output directory for generated header")
    emo_p.add_argument("--width", type=int, default=None, help="Force width (preserve aspect if height unset)")
    emo_p.add_argument("--height", type=int, default=None, help="Force height (preserve aspect if width unset)")
    emo_p.add_argument("--fps", type=int, default=20, help="Frames per second")
    emo_p.add_argument(
        "--loop",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Enable/disable looping",
    )

    return parser.parse_args()


def main():
    args = parse_args()
    ensure_dir(args.output_dir)

    if args.mode == "icon":
        convert_icon(args.input_path, args.output_dir, args.width, args.height, args.max_dim)

    elif args.mode == "emotion":
        convert_emotion(args.input_path, args.output_dir, args.width, args.height, args.fps, args.loop)


if __name__ == "__main__":
    main()
