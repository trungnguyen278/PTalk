#!/usr/bin/env python3
import os
import argparse
from typing import Optional, Tuple, List
from PIL import Image, ImageSequence
import io

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
    f.write("#include \"emotion_types.hpp\"\n\n")

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
# ICON (PNG → .hpp as JPEG grayscale)
# ============================================================

def convert_icon(png_path, out_dir, target_w=None, target_h=None, max_dim=None):
    name = os.path.splitext(os.path.basename(png_path))[0].upper()
    img = Image.open(png_path).convert("L")  # Grayscale

    img, w, h = resize_with_aspect(img, target_w, target_h, max_dim)

    # Convert to JPEG bytes (grayscale)
    jpeg_buffer = io.BytesIO()
    img.save(jpeg_buffer, format="JPEG", quality=90, optimize=True)
    jpeg_data = jpeg_buffer.getvalue()

    out_path = os.path.join(out_dir, f"{name.lower()}.hpp")
    with open(out_path, "w", encoding="utf-8") as f:
        write_header_guard(f)
        f.write(f"namespace asset::icon {{\n\n")

        # JPEG data
        f.write(f"const uint8_t {name}_DATA[{len(jpeg_data)}] = {{\n")
        for i, byte in enumerate(jpeg_data):
            f.write(f"0x{byte:02X},")
            if (i + 1) % 16 == 0:
                f.write("\n")
        f.write("\n};\n\n")

        # Icon metadata
        f.write(f"// Icon: {w}x{h}, {len(jpeg_data)} bytes JPEG\n")
        f.write(f"struct Icon_{name} {{\n")
        f.write(f"    static constexpr int width = {w};\n")
        f.write(f"    static constexpr int height = {h};\n")
        f.write(f"    static constexpr int data_size = {len(jpeg_data)};\n")
        f.write(f"    static const uint8_t* data() {{ return {name}_DATA; }}\n")
        f.write(f"}};\n\n")

        f.write("} // namespace asset::icon\n")

    print(f"[ICON] {out_path} → {w}x{h}, {len(jpeg_data)} bytes")

# ============================================================
# EMOTION (GIF → 1-bit with diff encoding)
# ============================================================

def to_1bit_pixels(img: Image.Image, threshold=128) -> List[int]:
    """Convert grayscale image to 1-bit per pixel (white=1, black=0)."""
    pixels = list(img.getdata())
    return [1 if p >= threshold else 0 for p in pixels]

def compute_pixel_diff(prev_pixels: List[int], curr_pixels: List[int], w: int, h: int) -> List[dict]:
    """Compute rectangular diff blocks between two 1-bit frames.
    Returns: list of {x, y, width, height, data} dicts for changed regions
    """
    # Find all changed pixels
    changed_coords = []
    for i, (p, c) in enumerate(zip(prev_pixels, curr_pixels)):
        if p != c:
            x = i % w
            y = i // w
            changed_coords.append((x, y, c))
    
    if not changed_coords:
        return []
    
    # Find bounding box of all changes
    min_x = min(x for x, y, c in changed_coords)
    max_x = max(x for x, y, c in changed_coords)
    min_y = min(y for x, y, c in changed_coords)
    max_y = max(y for x, y, c in changed_coords)
    
    box_w = max_x - min_x + 1
    box_h = max_y - min_y + 1
    
    # Extract pixels in bounding box from current frame
    box_pixels = []
    for by in range(box_h):
        for bx in range(box_w):
            px = min_x + bx
            py = min_y + by
            idx = py * w + px
            box_pixels.append(curr_pixels[idx])
    
    # Pack into bytes (8 pixels per byte)
    packed = []
    for i in range(0, len(box_pixels), 8):
        byte = 0
        for j in range(8):
            if i + j < len(box_pixels) and box_pixels[i + j]:
                byte |= (1 << (7 - j))
        packed.append(byte)
    
    return [{
        'x': min_x,
        'y': min_y,
        'width': box_w,
        'height': box_h,
        'data': packed
    }]

def convert_emotion(gif_path, out_dir, target_w=None, target_h=None, fps=20, loop=True):
    name = os.path.splitext(os.path.basename(gif_path))[0].upper()
    img = Image.open(gif_path)

    frames_pixels = []
    for frame in ImageSequence.Iterator(img):
        resized, w, h = resize_with_aspect(frame.convert("L"), target_w, target_h)
        pixels = to_1bit_pixels(resized)
        frames_pixels.append(pixels)

    if not frames_pixels:
        print("No frames found!")
        return

    frame_count = len(frames_pixels)
    
    # Pack frame 0 into bytes (8 pixels per byte)
    bits_frame0 = frames_pixels[0]
    packed_frame0 = []
    for i in range(0, len(bits_frame0), 8):
        byte = 0
        for j in range(8):
            if i + j < len(bits_frame0) and bits_frame0[i + j]:
                byte |= (1 << (7 - j))
        packed_frame0.append(byte)

    out_path = os.path.join(out_dir, f"{name.lower()}.hpp")
    with open(out_path, "w", encoding="utf-8") as f:
        write_header_guard(f)
        f.write(f"namespace asset::emotion {{\n\n")

        # Frame 0: Full frame (packed 1-bit)
        f.write(f"// Frame 0: Full 1-bit bitmap ({len(packed_frame0)} bytes, {w}x{h})\n")
        f.write(f"const uint8_t {name}_FRAME0[{len(packed_frame0)}] = {{\n")
        for i, byte in enumerate(packed_frame0):
            f.write(f"0x{byte:02X},")
            if (i + 1) % 16 == 0:
                f.write("\n")
        f.write("\n};\n\n")

        total_size = len(packed_frame0)
        total_diff_blocks = 0

        # Frames 1+: Block-level diff
        diff_data = []
        for idx in range(1, frame_count):
            diff = compute_pixel_diff(frames_pixels[idx - 1], frames_pixels[idx], w, h)
            diff_data.append(diff)
            
            if len(diff) == 0:
                f.write(f"// Frame {idx}: No changes\n\n")
            else:
                total_diff_blocks += len(diff)
                
                for block_idx, block in enumerate(diff):
                    data_len = len(block['data'])
                    f.write(f"// Frame {idx}: Diff block at ({block['x']},{block['y']}) size {block['width']}x{block['height']}\n")
                    f.write(f"const uint8_t {name}_FRAME{idx}_DATA[{data_len}] = {{\n")
                    for i, byte in enumerate(block['data']):
                        f.write(f"0x{byte:02X},")
                        if (i + 1) % 16 == 0:
                            f.write("\n")
                    f.write("\n};\n")
                    
                    total_size += 4 + data_len  # x,y,w,h + data
                
                f.write(f"const DiffBlock {name}_FRAME{idx}_DIFF = {{\n")
                f.write(f"    {diff[0]['x']}, {diff[0]['y']},\n")
                f.write(f"    {diff[0]['width']}, {diff[0]['height']},\n")
                f.write(f"    {name}_FRAME{idx}_DATA\n")
                f.write(f"}};\n\n")

        # Animation metadata
        f.write(f"// Animation: {w}x{h}, {frame_count} frames\n")
        f.write(f"// Total size: {total_size} bytes ({total_diff_blocks} diff blocks)\n\n")

        f.write(f"const FrameInfo {name}_FRAMES[{frame_count}] = {{\n")
        f.write(f"    {{nullptr}},  // Frame 0: full bitmap\n")
        for idx in range(1, frame_count):
            if len(diff_data[idx - 1]) == 0:
                f.write(f"    {{nullptr}},  // Frame {idx}: no change\n")
            else:
                f.write(f"    {{&{name}_FRAME{idx}_DIFF}},\n")
        f.write(f"}};\n\n")

        # Create animation instance using static variables
        f.write(f"// Animation instance: {name}\n")
        f.write(f"constexpr int {name}_WIDTH = {w};\n")
        f.write(f"constexpr int {name}_HEIGHT = {h};\n")
        f.write(f"constexpr int {name}_FRAME_COUNT = {frame_count};\n")
        f.write(f"constexpr int {name}_FPS = {fps};\n")
        f.write(f"constexpr bool {name}_LOOP = {'true' if loop else 'false'};\n\n")
        
        f.write(f"const Animation {name} = {{\n")
        f.write(f"    {name}_WIDTH,\n")
        f.write(f"    {name}_HEIGHT,\n")
        f.write(f"    {name}_FRAME_COUNT,\n")
        f.write(f"    {name}_FPS,\n")
        f.write(f"    {name}_LOOP,\n")
        f.write(f"    []() {{ return {name}_FRAME0; }},\n")
        f.write(f"    []() {{ return {name}_FRAMES; }}\n")
        f.write(f"}};\n\n")

        f.write("} // namespace asset::emotion\n")

    print(f"[EMOTION] {out_path} → {w}x{h}, {frame_count} frames, {total_size} bytes")

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
