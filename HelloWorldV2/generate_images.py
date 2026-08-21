import math, os

W, H = 200, 200
BLACK, WHITE, YELLOW, RED = 0, 1, 2, 3

def pack_pixels(row):
    """Pack a row of 200 pixels (4 colors) into 50 bytes, MSB-first."""
    out = bytearray(50)
    for i in range(W):
        px = row[i]
        byte_idx = i // 4
        shift = (3 - (i % 4)) * 2
        out[byte_idx] |= (px & 0x03) << shift
    return out

def make_image(pixel_func):
    rows = []
    for y in range(H):
        row = [pixel_func(x, y) for x in range(W)]
        rows.append(pack_pixels(row))
    return b''.join(rows)

def gen_image_0(x, y):
    """Black bg, white circle center=(100,100) r=60"""
    dx, dy = x - 100, y - 100
    if dx*dx + dy*dy <= 60*60:
        return WHITE
    return BLACK

def gen_image_1(x, y):
    """White bg, red circle center=(100,100) r=50"""
    dx, dy = x - 100, y - 100
    if dx*dx + dy*dy <= 50*50:
        return RED
    return WHITE

def gen_image_2(x, y):
    """White bg, yellow filled rectangle (40,60)-(160,140)"""
    if 40 <= x <= 160 and 60 <= y <= 140:
        return YELLOW
    return WHITE

images = [
    ("image_0", gen_image_0),
    ("image_1", gen_image_1),
    ("image_2", gen_image_2),
]

lines = []
lines.append('#include "images.h"\n')

for name, func in images:
    data = make_image(func)
    lines.append(f'const unsigned char {name}[10000] = {{')
    # Write 16 bytes per line
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_vals = ','.join(f'0x{b:02x}' for b in chunk)
        lines.append(hex_vals + ',')
    lines.append('};\n')

# image pointers array
lines.append('const unsigned char* const images[IMAGE_COUNT] = {')
for name, _ in images:
    lines.append(f'    {name},')
lines.append('};\n')

# audio filenames
lines.append('const char* const audio_files[IMAGE_COUNT] = {')
lines.append('    "/audio/0.wav",')
lines.append('    "/audio/1.wav",')
lines.append('    "/audio/2.wav",')
lines.append('};\n')

outpath = os.path.join(os.path.dirname(__file__), 'images', 'images.cpp')
with open(outpath, 'w') as f:
    f.write('\n'.join(lines))

print(f"Generated {outpath} ({os.path.getsize(outpath)} bytes)")
