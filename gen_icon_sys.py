#!/usr/bin/env python3
"""Generate a PS2 icon.sys file with the title 'PS2 Launcher'."""

import struct
import os

def generate_icon_sys(title_line1, title_line2, icon_filename, output_path):
    """
    Generate a PS2 memory card icon.sys file.
    
    The icon.sys format (964 bytes total):
    - 0x00-0x01: Magic "PS2D" header
    - 0x04-0x05: Offset for newline position (0x00 = no second line)
    - 0x06-0x07: Padding/unknown
    - 0x08-0x0B: Background transparency (0x00000060)
    - 0x0C-0x4B: Background color TL (4 ints RGBA)
    - 0x10-0x4F: Background color TR
    - 0x14-0x53: Background color BL 
    - 0x18-0x57: Background color BR
    - 0x40-0x7F: Light direction 1 (4 floats)
    - 0x50-0x5F: Light direction 2
    - 0x60-0x6F: Light direction 3
    - 0x70-0x7F: Light color 1 (4 floats RGBA)
    - 0x80-0x8F: Light color 2
    - 0x90-0x9F: Light color 3
    - 0xA0-0xAF: Ambient light (4 floats RGBA)
    - 0xC0-0x102: Title (Shift-JIS, null terminated, 68 bytes max)
    - 0x103-0x143: Icon filename for normal (null terminated, 64 bytes)
    - 0x143-0x183: Icon filename for copy (null terminated, 64 bytes)
    - 0x183-0x1C3: Icon filename for delete (null terminated, 64 bytes)
    """
    
    data = bytearray(964)
    
    # Magic header "PS2D"
    data[0:4] = b'PS2D'
    
    # Unknown/padding (standard values)
    struct.pack_into('<H', data, 0x04, 0)  # newline position
    struct.pack_into('<H', data, 0x06, 0)
    
    # Background transparency
    struct.pack_into('<I', data, 0x08, 0x00000060)
    
    # Background colors (dark blue/black gradient for premium look)
    # Top-left (RGBA as 4 ints)
    struct.pack_into('<IIII', data, 0x0C, 30, 30, 60, 0)
    # Top-right
    struct.pack_into('<IIII', data, 0x1C, 30, 30, 60, 0)
    # Bottom-left
    struct.pack_into('<IIII', data, 0x2C, 10, 10, 30, 0)
    # Bottom-right
    struct.pack_into('<IIII', data, 0x3C, 10, 10, 30, 0)
    
    # Light direction 1 (4 floats)
    struct.pack_into('<ffff', data, 0x40, 1.0, 1.0, 1.0, 0.0)
    # Light direction 2
    struct.pack_into('<ffff', data, 0x50, -1.0, 1.0, -1.0, 0.0)
    # Light direction 3
    struct.pack_into('<ffff', data, 0x60, 0.0, -1.0, 0.0, 0.0)
    
    # Light color 1 (RGBA floats)
    struct.pack_into('<ffff', data, 0x70, 0.5, 0.5, 0.5, 0.0)
    # Light color 2
    struct.pack_into('<ffff', data, 0x80, 0.5, 0.5, 0.5, 0.0)
    # Light color 3
    struct.pack_into('<ffff', data, 0x90, 0.5, 0.5, 0.5, 0.0)
    
    # Ambient light
    struct.pack_into('<ffff', data, 0xA0, 0.5, 0.5, 0.5, 0.0)
    
    # Title (Shift-JIS encoded, starts at 0xC0, 68 bytes max)
    title = title_line1
    if title_line2:
        title = title_line1 + "\n" + title_line2
    
    title_bytes = title.encode('ascii')  # ASCII is subset of Shift-JIS
    title_end = min(len(title_bytes), 67)
    data[0xC0:0xC0 + title_end] = title_bytes[:title_end]
    data[0xC0 + title_end] = 0  # null terminate
    
    # Icon filenames (64 bytes each, null terminated)
    # Normal icon
    icon_bytes = icon_filename.encode('ascii')
    icon_end = min(len(icon_bytes), 63)
    data[0x103:0x103 + icon_end] = icon_bytes[:icon_end]
    
    # Copy icon (same)
    data[0x143:0x143 + icon_end] = icon_bytes[:icon_end]
    
    # Delete icon (same)
    data[0x183:0x183 + icon_end] = icon_bytes[:icon_end]
    
    with open(output_path, 'wb') as f:
        f.write(data)
    
    print(f"Generated {output_path} ({len(data)} bytes)")
    print(f"Title: '{title}'")
    print(f"Icon: '{icon_filename}'")

if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    output = os.path.join(script_dir, 'gfx', 'icon.sys')
    generate_icon_sys("PS2 Launcher", "", "ps2l.icn", output)
