def to_2bit(img: Image.Image) -> List[int]:
    """Convert grayscale image to 2-bit per pixel (4 levels: 0-3)."""
    pixels = list(img.getdata())
    return [p // 64 for p in pixels]  # 0-63:0, 64-127:1, 128-191:2, 192-255:3

def encode_rle_2bit(pixels: List[int]) -> List[int]:
    """RLE encode 2-bit grayscale pixels. Format: [count, value]"""
    if not pixels:
        return []
    encoded = []
    i = 0
    while i < len(pixels):
        value = pixels[i] & 0x03
        count = 1
        while i + count < len(pixels) and pixels[i + count] == value and count < 255:
            count += 1
        encoded.append(count)
        encoded.append(value)
        i += count
    return encoded
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
#   python scripts/convert_assets.py emotion path/to/emotion.gif output/directory --width 128 --height 128 --fps 10 --loop


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
# EMOTION (GIF → 4-bit grayscale with RLE encoding)
# ============================================================

def to_4bit_grayscale(img: Image.Image) -> List[int]:
    """Convert grayscale image to 4-bit per pixel (16 levels: 0-15).
    Maps 0-255 → 0-15 (divide by 16)"""
    pixels = list(img.getdata())
    return [p >> 4 for p in pixels]  # Divide by 16 to get 4-bit value

def encode_rle_4bit(pixels: List[int]) -> List[int]:
    """RLE encode 4-bit grayscale pixels.
    Format: [count_byte, value_byte, count_byte, value_byte, ...]
    count_byte: 1-255 (number of consecutive pixels with same value)
    value_byte: 0-15 (grayscale value)
    """
    if not pixels:
        return []
    
    encoded = []
    i = 0
    while i < len(pixels):
        value = pixels[i]
        count = 1
        # Count consecutive pixels with same value (max 255)
        while i + count < len(pixels) and pixels[i + count] == value and count < 255:
            count += 1
        
        encoded.append(count)
        encoded.append(value)
        i += count
    
    return encoded

def compute_pixel_diff(prev_pixels: List[int], curr_pixels: List[int], w: int, h: int) -> List[dict]:
    """Compute rectangular diff blocks between two 4-bit grayscale frames.
    Returns: list of {x, y, width, height, data} dicts for changed regions
    Data is RLE encoded (2 bytes per token: count_byte, value_byte)
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
    
    # RLE encode the diff block
    rle_data = encode_rle_1bit(box_pixels)
    
    return [{
        'x': min_x,
        'y': min_y,
        'width': box_w,
        'height': box_h,
        'data': rle_data
    }]

def convert_emotion(gif_path, out_dir, target_w=None, target_h=None, fps=10, loop=True):
    name_upper = os.path.splitext(os.path.basename(gif_path))[0].upper()
    name_lower = name_upper.lower()
    img = Image.open(gif_path)

    frames_pixels = []
    for frame in ImageSequence.Iterator(img):
        resized, w, h = resize_with_aspect(frame.convert("L"), target_w, target_h)
        pixels = to_2bit(resized)
        frames_pixels.append(pixels)

    if not frames_pixels:
        print("No frames found!")
        return

    frame_count = len(frames_pixels)
    
    # File paths
    out_hpp = os.path.join(out_dir, f"{name_lower}.hpp")
    out_cpp = os.path.join(out_dir, f"{name_lower}.cpp")
    
    # Encode each frame as full RLE (no diff)
    frame_data = []
    total_size = 0
    max_frame_size = 0

    for idx, pixels in enumerate(frames_pixels):
        # RLE encode full frame (2-bit grayscale)
        rle_data = encode_rle_2bit(pixels)
        frame_data.append(rle_data)
        total_size += len(rle_data)
        max_frame_size = max(max_frame_size, len(rle_data))
    
    # Calculate max packed size (decoded buffer size for one frame)
    max_packed_size = (w * h + 7) // 8

    # =====================
    # Write header (.hpp)
    # =====================
    with open(out_hpp, "w", encoding="utf-8") as f:
        write_header_guard(f)
        f.write("namespace asset::emotion {\n\n")
        f.write(f"extern const Animation {name_upper};\n\n")
        f.write("} // namespace asset::emotion\n")

    # =====================
    # Write implementation (.cpp)
    # =====================
    # Write header (.hpp)
    # =====================
    out_hpp = os.path.join(out_dir, f"{name_lower}.hpp")
    out_cpp = os.path.join(out_dir, f"{name_lower}.cpp")
    
    with open(out_hpp, "w", encoding="utf-8") as f:
        write_header_guard(f)
        f.write("namespace asset::emotion {\n\n")
        f.write(f"extern const Animation {name_upper};\n\n")
        f.write("} // namespace asset::emotion\n")

    # =====================
    # Write implementation (.cpp)
    # =====================
    with open(out_cpp, "w", encoding="utf-8") as f:
        f.write(f"#include \"{name_lower}.hpp\"\n\n")
        f.write("namespace asset::emotion {\n\n")

        # Emit frame data arrays (full RLE, not diff)
        for idx, rle_data in enumerate(frame_data):
            data_len = len(rle_data)
            f.write(f"// Frame {idx}: Full frame RLE ({data_len} bytes)\n")
            f.write(f"static const uint8_t {name_upper}_FRAME{idx}_DATA[{data_len}] = {{\n")
            for i, byte in enumerate(rle_data):
                f.write(f"0x{byte:02X},")
                if (i + 1) % 16 == 0:
                    f.write("\n")
            f.write("\n};\n")

            # Store as DiffBlock for compatibility (x=0, y=0, width=w, height=h)
            f.write(f"static const DiffBlock {name_upper}_FRAME{idx}_BLOCK = {{\n")
            f.write(f"    0, 0,  // Full frame starts at (0,0)\n")
            f.write(f"    {w}, {h},  // Full frame dimensions\n")
            f.write(f"    {name_upper}_FRAME{idx}_DATA\n")
            f.write(f"}};\n\n")

        # FrameInfo array
        f.write(f"static const FrameInfo {name_upper}_FRAMES[{frame_count}] = {{\n")
        for idx in range(frame_count):
            f.write(f"    {{&{name_upper}_FRAME{idx}_BLOCK}},\n")
        f.write("};\n\n")

        # Animation instance
        f.write(f"const Animation {name_upper} = {{\n")
        f.write(f"    {w},\n")
        f.write(f"    {h},\n")
        f.write(f"    {frame_count},\n")
        f.write(f"    {fps},\n")
        f.write(f"    {'true' if loop else 'false'},\n")
        f.write(f"    {max_packed_size},  // max packed size (1 full frame)\n")
        f.write(f"    nullptr,  // no separate base frame\n")
        f.write(f"    []() {{ return {name_upper}_FRAMES; }}\n")
        f.write("};\n\n")

        f.write("} // namespace asset::emotion\n")

    print(f"[EMOTION] {out_hpp} (+cpp) → {w}x{h}, {frame_count} frames, {total_size} bytes RLE")
    print(f"          Max RLE frame: {max_frame_size} bytes, packed buffer: {max_packed_size} bytes")

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
    emo_p.add_argument("--fps", type=int, default=10, help="Frames per second")
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
