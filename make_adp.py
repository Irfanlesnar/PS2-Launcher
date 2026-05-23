#!/usr/bin/env python3
import sys
import os

def main():
    if len(sys.argv) < 3:
        print("Usage: make_adp.py <input_ss2> <output_adp>")
        sys.exit(1)

    in_path = sys.argv[1]
    out_path = sys.argv[2]

    if not os.path.exists(in_path):
        print(f"Error: input file '{in_path}' does not exist.")
        sys.exit(1)

    size = os.path.getsize(in_path)

    # Let's check if the file already has the APCM header
    with open(in_path, 'rb') as f:
        magic = f.read(4)
        if magic == b'APCM':
            # Already has header, just copy
            f.seek(0)
            data = f.read()
            with open(out_path, 'wb') as out_f:
                out_f.write(data)
            print(f"APCM header already present in '{in_path}', copied directly to '{out_path}'.")
            return

    # If it is raw, let's determine if it's stereo or mono based on file size/interleave
    # By default, we will assume 2 channels (Stereo) to match OPL's defaults.
    # A stereo block has 32 bytes (16 bytes left, 16 bytes right) representing 28 samples per channel.
    channels = 2
    num_blocks = size // 32
    num_samples = num_blocks * 28

    # Build the 16-byte OPL APCM header
    header = bytearray(16)
    
    # 0-3: Magic bytes "APCM"
    header[0:4] = b'APCM'
    
    # 4-7: Version (1) and Channels (2) in little-endian
    header[4] = 0x01
    header[5] = channels
    header[6] = 0x00
    header[7] = 0x00
    
    # 8-11: Fixed constant value from default OPL sound effects (0x0eb3)
    header[8] = 0xb3
    header[9] = 0x0e
    header[10] = 0x00
    header[11] = 0x00
    
    # 12-15: Number of samples in little-endian
    header[12] = num_samples & 0xFF
    header[13] = (num_samples >> 8) & 0xFF
    header[14] = (num_samples >> 16) & 0xFF
    header[15] = (num_samples >> 24) & 0xFF

    with open(in_path, 'rb') as f:
        raw_data = f.read()

    with open(out_path, 'wb') as out_f:
        out_f.write(header)
        out_f.write(raw_data)

    print(f"Successfully processed raw file '{in_path}' -> '{out_path}' (Prepended APCM header, {num_samples} samples, Stereo).")

if __name__ == '__main__':
    main()
