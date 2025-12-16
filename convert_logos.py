#!/usr/bin/env python3
"""
Convert PNG logos to JPEG, generate header files with base64 data URI
Usage: python convert_logos.py
"""

from PIL import Image
import base64
import io
import os

def convert_logo(input_path, output_header, size=120, quality=70):
    """
    Convert PNG to JPEG and generate C++ header with data URI
    
    Args:
        input_path: Path to input PNG
        output_header: Path to output .hpp file
        size: Max dimension (pixels)
        quality: JPEG quality 1-100 (70 is good default)
    """
    if not os.path.exists(input_path):
        print(f"❌ File not found: {input_path}")
        return False
    
    try:
        print(f"📷 Converting: {input_path}")
        
        # Open and convert to RGB (JPEG doesn't support transparency)
        img = Image.open(input_path)
        if img.mode != 'RGB':
            # If transparent, create white background
            if img.mode == 'RGBA':
                bg = Image.new('RGB', img.size, (255, 255, 255))
                bg.paste(img, mask=img.split()[3])
                img = bg
            else:
                img = img.convert('RGB')
        
        # Resize to max dimension
        img.thumbnail((size, size), Image.LANCZOS)
        print(f"   Size: {img.size}")
        
        # Convert to JPEG with optimization
        buffer = io.BytesIO()
        img.save(buffer, format='JPEG', quality=quality, optimize=True)
        jpeg_data = buffer.getvalue()
        jpeg_kb = len(jpeg_data) / 1024
        print(f"   JPEG size: {jpeg_kb:.2f} KB (quality={quality})")
        
        # Encode to base64
        b64 = base64.b64encode(jpeg_data).decode('ascii')
        b64_kb = len(b64) / 1024
        print(f"   Base64 size: {b64_kb:.2f} KB")
        
        # Generate C++ header
        header_name = os.path.basename(output_header).replace('.hpp', '').upper()
        guard = f"{header_name}_HPP"
        
        header_content = f'''#pragma once

// Auto-generated from {os.path.basename(input_path)} by convert_logos.py
// JPEG quality={quality}, size={size}x{size}, total={b64_kb:.1f}KB base64
const char {header_name.replace('.HPP', '')}_DATA[] = 
"data:image/jpeg;base64,{b64}";
'''
        
        with open(output_header, 'w') as f:
            f.write(header_content)
        
        print(f"✅ Generated: {output_header}")
        return True
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

if __name__ == '__main__':
    base_path = os.path.dirname(os.path.abspath(__file__))
    lib_path = os.path.join(base_path, 'lib', 'network')
    
    print("🎨 Logo Converter - PNG → JPEG base64 data URI\n")
    
    # Convert logos
    success = True
    success &= convert_logo(
        os.path.join(lib_path, 'logo1.png'),
        os.path.join(lib_path, 'logo1.hpp'),
        size=100,
        quality=70
    )
    print()
    success &= convert_logo(
        os.path.join(lib_path, 'logo2.png'),
        os.path.join(lib_path, 'logo2.hpp'),
        size=100,
        quality=70
    )
    
    if success:
        print("\n✅ All logos converted successfully!")
        print("📝 Next step: Upload firmware to test portal")
    else:
        print("\n❌ Some conversions failed")
