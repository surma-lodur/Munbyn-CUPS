#!/usr/bin/env python3
"""Generate CUPS raster files for testing.

This writes a valid CUPS raster v1 stream ("RaSt") with one or more pages.
Each page uses a 420-byte page header followed by uncompressed 8-bit grayscale
pixels so the Linux scaffold can consume it (it currently accepts 8/16 bpc).
"""

import struct
import sys

def create_page_header(width_pixels=576, height_pixels=864):
    """Create a 420-byte CUPS v1 page header.

    Layout matches cups_page_header_t from /usr/include/cups/raster.h.
    Fields are big-endian for "RaSt" stream order.
    """
    header = bytearray(420)
    
    # MediaClass (offset 0, 64 bytes)
    struct.pack_into('64s', header, 0, b'Media\x00')
    
    # MediaColor (offset 64, 64 bytes)
    struct.pack_into('64s', header, 64, b'White\x00')
    
    # MediaType (offset 128, 64 bytes)
    struct.pack_into('64s', header, 128, b'Plain\x00')
    
    # OutputType (offset 192, 64 bytes)
    struct.pack_into('64s', header, 192, b'Direct\x00')
    
    # AdvanceDistance (offset 256)
    struct.pack_into('>I', header, 256, 0)
    
    # AdvanceMedia (offset 260)
    struct.pack_into('>I', header, 260, 0)
    
    # Collate (offset 264)
    struct.pack_into('>I', header, 264, 0)
    
    # CutMedia (offset 268)
    struct.pack_into('>I', header, 268, 0)
    
    # Duplex (offset 272)
    struct.pack_into('>I', header, 272, 0)
    
    # HWResolution (offset 276-280): 203 DPI (thermal printer standard)
    struct.pack_into('>I', header, 276, 203)
    struct.pack_into('>I', header, 280, 203)
    
    # ImagingBoundingBox (offset 284-300)
    struct.pack_into('>I', header, 284, 0)
    struct.pack_into('>I', header, 288, 0)
    struct.pack_into('>I', header, 292, width_pixels)
    struct.pack_into('>I', header, 296, height_pixels)
    
    # InsertSheet (offset 300)
    struct.pack_into('>I', header, 300, 0)
    
    # Jog (offset 304)
    struct.pack_into('>I', header, 304, 0)
    
    # LeadingEdge (offset 308)
    struct.pack_into('>I', header, 308, 0)
    
    # Margins (offset 312-320)
    struct.pack_into('>I', header, 312, 0)
    struct.pack_into('>I', header, 316, 0)
    
    # MediaPosition (offset 320)
    struct.pack_into('>I', header, 320, 0)
    
    # MediaWeight (offset 324)
    struct.pack_into('>I', header, 324, 0)
    
    # MirrorPrint (offset 328)
    struct.pack_into('>I', header, 328, 0)
    
    # NegativePrint (offset 332)
    struct.pack_into('>I', header, 332, 0)
    
    # NumCopies (offset 336)
    struct.pack_into('>I', header, 336, 1)
    
    # Orientation (offset 340)
    struct.pack_into('>I', header, 340, 0)
    
    # OutputFaceUp (offset 344)
    struct.pack_into('>I', header, 344, 0)
    
    # OutputFaceUp (offset 348)
    struct.pack_into('>I', header, 348, 0)

    # PageSize (offset 352-356)
    struct.pack_into('>I', header, 352, width_pixels)
    struct.pack_into('>I', header, 356, height_pixels)

    # Separations (offset 360)
    struct.pack_into('>I', header, 360, 0)

    # TraySwitch (offset 364)
    struct.pack_into('>I', header, 364, 0)
    
    # Tumble (offset 368)
    struct.pack_into('>I', header, 368, 0)
    
    # cupsWidth / cupsHeight
    struct.pack_into('>I', header, 372, width_pixels)
    struct.pack_into('>I', header, 376, height_pixels)
    
    # cupsMediaType
    struct.pack_into('>I', header, 380, 0)
    
    # cupsBitsPerColor / cupsBitsPerPixel for 8-bit grayscale
    struct.pack_into('>I', header, 384, 8)
    struct.pack_into('>I', header, 388, 8)
    
    # cupsBytesPerLine
    struct.pack_into('>I', header, 392, width_pixels)
    
    # cupsColorOrder (chunked)
    struct.pack_into('>I', header, 396, 0)
    
    # cupsColorSpace (sW/sRGB-ish grayscale path)
    struct.pack_into('>I', header, 400, 18)
    
    # cupsCompression / cupsRowCount / cupsRowFeed / cupsRowStep
    struct.pack_into('>I', header, 404, 0)
    struct.pack_into('>I', header, 408, 0)
    struct.pack_into('>I', header, 412, 1)
    struct.pack_into('>I', header, 416, 1)
    
    return bytes(header)

def generate_page_data(width_pixels, height_pixels, pattern_name):
    """Generate 8-bit grayscale raster pixels."""
    data = bytearray()
    
    if pattern_name == "blank":
        # All white
        for _ in range(height_pixels):
            data.extend(b'\xff' * width_pixels)
    
    elif pattern_name == "solid":
        # All black
        for _ in range(height_pixels):
            data.extend(b'\x00' * width_pixels)
    
    elif pattern_name == "gradient":
        # Horizontal gradient (black -> white)
        for y in range(height_pixels):
            for x in range(width_pixels):
                data.append((x * 255) // max(1, (width_pixels - 1)))
    
    elif pattern_name == "checkerboard":
        # 8x8 checkerboard
        for y in range(height_pixels):
            for x in range(width_pixels):
                data.append(0x00 if (((y >> 3) + (x >> 3)) & 1) else 0xFF)
    
    elif pattern_name == "stripes":
        # Vertical stripes (8px)
        for y in range(height_pixels):
            for x in range(width_pixels):
                data.append(0x00 if ((x >> 3) & 1) else 0xFF)
    
    elif pattern_name == "lines":
        # Horizontal lines (8px)
        for y in range(height_pixels):
            if (y >> 3) & 1:
                data.extend(b'\x00' * width_pixels)
            else:
                data.extend(b'\xFF' * width_pixels)
    
    else:
        raise ValueError(f"Unknown pattern: {pattern_name}")
    
    return bytes(data)

def write_raster_file(filename, width_pixels=576, height_pixels=864, pattern_name="blank", num_pages=1):
    """Write a complete CUPS raster stream."""
    with open(filename, 'wb') as f:
        # Raster stream sync word (big-endian CUPS raster)
        f.write(b'RaSt')

        # Write each page
        for _ in range(num_pages):
            header = create_page_header(width_pixels, height_pixels)
            f.write(header)

            data = generate_page_data(width_pixels, height_pixels, pattern_name)
            f.write(data)

def main():
    if len(sys.argv) < 2:
        print("Usage: gen_raster.py <output.raster> [pattern] [width] [height] [pages]")
        print()
        print("Patterns: blank, solid, gradient, checkerboard, stripes, lines")
        print("Defaults: blank, 576x864, 1 page")
        sys.exit(1)
    
    filename = sys.argv[1]
    pattern = sys.argv[2] if len(sys.argv) > 2 else "blank"
    width = int(sys.argv[3]) if len(sys.argv) > 3 else 576
    height = int(sys.argv[4]) if len(sys.argv) > 4 else 864
    num_pages = int(sys.argv[5]) if len(sys.argv) > 5 else 1
    
    write_raster_file(filename, width, height, pattern, num_pages)
    print(f"Created {filename}: {pattern} pattern, {width}x{height}px, {num_pages} page(s)")

if __name__ == '__main__':
    main()
