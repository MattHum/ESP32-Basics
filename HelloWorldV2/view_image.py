"""
View 4-color ePaper image data (200x200, 2bpp, MSB-first).
Usage: python view_image.py [image_index]
Generates a PNG for visual inspection.
"""
import sys, os

W, H = 200, 200
COLORS = {
    0: (0, 0, 0),       # BLACK
    1: (255, 255, 255),  # WHITE
    2: (255, 255, 0),    # YELLOW
    3: (255, 0, 0),      # RED
}

def unpack_image(data):
    """Unpack 10000 bytes into 200x200 pixel grid."""
    pixels = []
    for y in range(H):
        row = []
        for bx in range(50):  # 50 bytes per row
            byte = data[y * 50 + bx]
            for px in range(4):  # 4 pixels per byte, MSB first
                color = (byte >> (6 - px * 2)) & 0x03
                row.append(color)
        pixels.append(row)
    return pixels

def save_png(pixels, filename):
    """Save as PNG without PIL – writes a minimal BMP instead."""
    # Write as BMP (easier, no dependencies)
    bmp_path = filename.replace('.png', '.bmp')
    with open(bmp_path, 'wb') as f:
        row_size = (W * 3 + 3) & ~3  # rows padded to 4 bytes
        pixel_data_size = row_size * H
        file_size = 54 + pixel_data_size

        # BMP Header
        f.write(b'BM')
        f.write(file_size.to_bytes(4, 'little'))
        f.write(b'\x00\x00\x00\x00')
        f.write((54).to_bytes(4, 'little'))

        # DIB Header
        f.write((40).to_bytes(4, 'little'))
        f.write(W.to_bytes(4, 'little'))
        f.write(H.to_bytes(4, 'little'))
        f.write((1).to_bytes(2, 'little'))   # planes
        f.write((24).to_bytes(2, 'little'))  # bpp
        f.write(b'\x00' * 24)

        # Pixel data (bottom-up, BGR)
        for y in range(H - 1, -1, -1):
            row = pixels[y]
            for x in range(W):
                r, g, b = COLORS[row[x]]
                f.write(bytes([b, g, r]))  # BGR
            # Pad row to 4-byte boundary
            padding = (4 - (W * 3) % 4) % 4
            f.write(b'\x00' * padding)

    print(f"Saved: {bmp_path}")

def print_ascii(pixels, scale=4):
    """Print ASCII representation to terminal."""
    sym = {0: '##', 1: '..', 2: 'YY', 3: 'RR'}
    for y in range(0, H, scale):
        line = ''
        for x in range(0, W, scale):
            line += sym[pixels[y][x]]
        print(line)

# Import the image data
script_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, script_dir)

# Read the .cpp file and extract image data
cpp_file = os.path.join(script_dir, 'images', 'images.cpp')
with open(cpp_file, 'r') as f:
    content = f.read()

# Parse each image array
import re
images_data = {}
for match in re.finditer(r'const unsigned char (image_\d+)\[10000\]\s*=\s*\{([^}]+)\}', content):
    name = match.group(1)
    hex_vals = re.findall(r'0x([0-9a-f]{2})', match.group(2))
    data = bytes(int(h, 16) for h in hex_vals)
    if len(data) == 10000:
        images_data[name] = data

if len(sys.argv) > 1:
    idx = int(sys.argv[1])
    name = f'image_{idx}'
    if name in images_data:
        print(f"=== {name} (ASCII preview) ===")
        pixels = unpack_image(images_data[name])
        print_ascii(pixels, scale=4)
        save_png(pixels, os.path.join(script_dir, f'{name}.png'))
    else:
        print(f"Available: {list(images_data.keys())}")
else:
    print(f"Available images: {list(images_data.keys())}")
    print("Usage: python view_image.py <index>")
    print("e.g.:  python view_image.py 0")
    for name in sorted(images_data.keys()):
        pixels = unpack_image(images_data[name])
        save_png(pixels, os.path.join(script_dir, f'{name}.png'))
