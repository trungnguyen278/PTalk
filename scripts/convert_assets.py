#!/usr/bin/env python3
import os
import argparse
from typing import List, Optional, Tuple
from PIL import Image, ImageSequence

# ============================================================
# Utils
# ============================================================

def ensure_dir(path: str):
    if not os.path.exists(path):
        os.makedirs(path)

def resize_with_aspect(
    img: Image.Image,
    target_w: Optional[int] = None,
    target_h: Optional[int] = None,
) -> Tuple[Image.Image, int, int]:

    if target_w and target_h:
        img = img.resize((target_w, target_h), Image.LANCZOS)
    elif target_w:
        h = int(img.height * (target_w / img.width))
        img = img.resize((target_w, h), Image.LANCZOS)
    elif target_h:
        w = int(img.width * (target_h / img.height))
        img = img.resize((w, target_h), Image.LANCZOS)

    return img, img.width, img.height


# ============================================================
# RLE – 2-bit grayscale
# ============================================================

def to_2bit(img: Image.Image) -> List[int]:
    pixels = list(img.getdata())
    return [p >> 6 for p in pixels]  # 0–3

def encode_rle_2bit(pixels: List[int]) -> List[int]:
    if not pixels:
        return []

    out = []
    i = 0
    while i < len(pixels):
        val = pixels[i] & 0x03
        cnt = 1
        while i + cnt < len(pixels) and pixels[i + cnt] == val and cnt < 255:
            cnt += 1
        out.append(cnt)
        out.append(val)
        i += cnt
    return out


# ============================================================
# ICON (PNG → RLE → hpp + cpp)
# ============================================================

def convert_icon(png_path, out_root, w=None, h=None):
    name = os.path.splitext(os.path.basename(png_path))[0].upper()
    name_l = name.lower()

    img = Image.open(png_path).convert("L")
    img, iw, ih = resize_with_aspect(img, w, h)

    pixels = to_2bit(img)
    rle = encode_rle_2bit(pixels)

    out_dir = os.path.join(out_root, "icons")
    ensure_dir(out_dir)

    hpp = os.path.join(out_dir, f"{name_l}.hpp")
    cpp = os.path.join(out_dir, f"{name_l}.cpp")

    # ---------- HPP ----------
    with open(hpp, "w", encoding="utf-8") as f:
        f.write("#pragma once\n")
        f.write("#include <cstdint>\n\n")
        f.write("namespace asset::icon {\n\n")
        f.write("struct Icon {\n")
        f.write("    int w;\n")
        f.write("    int h;\n")
        f.write("    const uint8_t* rle_data;\n")
        f.write("};\n\n")
        f.write(f"extern const Icon {name};\n\n")
        f.write("} // namespace asset::icon\n")

    # ---------- CPP ----------
    with open(cpp, "w", encoding="utf-8") as f:
        f.write(f"#include \"{name_l}.hpp\"\n\n")
        f.write("namespace asset::icon {\n\n")

        f.write(f"static const uint8_t {name}_RLE_DATA[{len(rle)}] = {{\n")
        for i, b in enumerate(rle):
            f.write(f"0x{b:02X}, ")
            if (i + 1) % 16 == 0:
                f.write("\n")
        f.write("\n};\n\n")

        f.write(f"const Icon {name} = {{\n")
        f.write(f"    {iw},\n")
        f.write(f"    {ih},\n")
        f.write(f"    {name}_RLE_DATA\n")
        f.write("};\n\n")

        f.write("} // namespace asset::icon\n")

    print(f"[ICON] {name_l} → {iw}x{ih}, {len(rle)} bytes RLE")


# ============================================================
# EMOTION (GIF → RLE frames → hpp + cpp)
# ============================================================

def convert_emotion(gif_path, out_root, w=None, h=None, fps=10, loop=True):
    name = os.path.splitext(os.path.basename(gif_path))[0].upper()
    name_l = name.lower()

    img = Image.open(gif_path)

    frames = []
    for frame in ImageSequence.Iterator(img):
        f, fw, fh = resize_with_aspect(frame.convert("L"), w, h)
        frames.append(encode_rle_2bit(to_2bit(f)))

    out_dir = os.path.join(out_root, "emotions")
    ensure_dir(out_dir)

    hpp = os.path.join(out_dir, f"{name_l}.hpp")
    cpp = os.path.join(out_dir, f"{name_l}.cpp")

    # ---------- HPP ----------
    with open(hpp, "w", encoding="utf-8") as f:
        f.write("#pragma once\n")
        f.write("#include <cstdint>\n")
        f.write("#include \"emotion_types.hpp\"\n\n")
        f.write("namespace asset::emotion {\n")
        f.write(f"extern const Animation {name};\n")
        f.write("}\n")

    # ---------- CPP ----------
    with open(cpp, "w", encoding="utf-8") as f:
        f.write(f"#include \"{name_l}.hpp\"\n\n")
        f.write("namespace asset::emotion {\n\n")

        for i, rle in enumerate(frames):
            f.write(f"static const uint8_t {name}_FRAME{i}_DATA[{len(rle)}] = {{\n")
            for j, b in enumerate(rle):
                f.write(f"0x{b:02X}, ")
                if (j + 1) % 16 == 0:
                    f.write("\n")
            f.write("\n};\n")

            f.write(f"static const DiffBlock {name}_FRAME{i} = {{\n")
            f.write(f"    0, 0, {fw}, {fh}, {name}_FRAME{i}_DATA\n")
            f.write("};\n\n")

        f.write(f"static const FrameInfo {name}_FRAMES[] = {{\n")
        for i in range(len(frames)):
            f.write(f"    {{ &{name}_FRAME{i} }},\n")
        f.write("};\n\n")

        f.write(f"const Animation {name} = {{\n")
        f.write(f"    {fw}, {fh}, {len(frames)}, {fps}, {str(loop).lower()},\n")
        f.write(f"    nullptr,\n")
        f.write(f"    []() {{ return {name}_FRAMES; }}\n")
        f.write("};\n\n")

        f.write("}\n")

    print(f"[EMOTION] {name_l} → {fw}x{fh}, {len(frames)} frames")


# ============================================================
# MAIN
# ============================================================

def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="mode", required=True)

    ic = sub.add_parser("icon")
    ic.add_argument("input")
    ic.add_argument("output")
    ic.add_argument("--width", type=int)
    ic.add_argument("--height", type=int)

    em = sub.add_parser("emotion")
    em.add_argument("input")
    em.add_argument("output")
    em.add_argument("--width", type=int)
    em.add_argument("--height", type=int)
    em.add_argument("--fps", type=int, default=10)
    em.add_argument("--loop", action="store_true")

    args = ap.parse_args()

    if args.mode == "icon":
        convert_icon(args.input, args.output, args.width, args.height)
    else:
        convert_emotion(args.input, args.output, args.width, args.height, args.fps, args.loop)

if __name__ == "__main__":
    main()
