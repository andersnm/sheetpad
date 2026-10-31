"""Generates src/app/app.ico (PNG-compressed, multiple sizes) using only the standard library.

Usage: python3 tools/make_icon.py [output.ico]
"""
import struct
import sys
import zlib

SIZES = [16, 24, 32, 48, 64, 128, 256]
SAMPLES = 4

GREEN = (31, 122, 77)
GREEN_LIGHT = (46, 158, 107)
WHITE = (255, 255, 255)
ACCENT = (255, 179, 0)


def in_round_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def shade(x, y):
    """Colour at unit-square position, or None when transparent."""
    if not in_round_rect(x, y, 0.04, 0.04, 0.96, 0.96, 0.2):
        return None
    colour = GREEN
    left, top, right, bottom = 0.17, 0.17, 0.83, 0.83
    if not in_round_rect(x, y, left, top, right, bottom, 0.05):
        return colour
    colour = WHITE
    cell = (right - left) / 3
    line = 0.022
    if y < top + cell * 0.8:
        colour = GREEN_LIGHT
    for k in range(1, 3):
        if abs(x - (left + cell * k)) < line or abs(y - (top + cell * k)) < line:
            colour = GREEN
    # Highlight the active cell.
    ax0, ay0 = left + cell, top + cell
    ax1, ay1 = ax0 + cell, ay0 + cell
    edge = 0.035
    if ax0 - edge < x < ax1 + edge and ay0 - edge < y < ay1 + edge:
        if not (ax0 + edge < x < ax1 - edge and ay0 + edge < y < ay1 - edge):
            colour = ACCENT
    return colour


def render(size):
    rows = []
    for py in range(size):
        row = bytearray([0])
        for px in range(size):
            r = g = b = a = 0
            for sy in range(SAMPLES):
                for sx in range(SAMPLES):
                    colour = shade((px + (sx + 0.5) / SAMPLES) / size, (py + (sy + 0.5) / SAMPLES) / size)
                    if colour is not None:
                        r += colour[0]
                        g += colour[1]
                        b += colour[2]
                        a += 1
            total = SAMPLES * SAMPLES
            if a:
                row += bytes((r // a, g // a, b // a, a * 255 // total))
            else:
                row += bytes((0, 0, 0, 0))
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    output = sys.argv[1] if len(sys.argv) > 1 else "src/app/app.ico"
    images = [render(size) for size in SIZES]
    data = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for size, image in zip(SIZES, images):
        dimension = 0 if size >= 256 else size
        data += struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(image), offset)
        offset += len(image)
    data += b"".join(images)
    with open(output, "wb") as handle:
        handle.write(data)


main()
