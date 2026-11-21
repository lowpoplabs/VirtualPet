"""topng.py IN.bmp OUT.png [IN.bmp OUT.png ...]

Turns the simulator's 24-bit screenshots into PNGs for the web site, using
only what comes with Python.
"""
import struct
import sys
import zlib


def read_bmp(path):
    data = open(path, 'rb').read()
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    stride = (width * 3 + 3) & ~3
    rows = []
    for y in range(height):
        start = offset + (height - 1 - y) * stride      # stored bottom row first, as blue-green-red
        row = data[start:start + width * 3]
        rows.append(bytes(c for i in range(0, len(row), 3) for c in (row[i + 2], row[i + 1], row[i])))
    return width, height, rows


def write_png(path, width, height, rows):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    raw = b''.join(b'\x00' + row for row in rows)
    with open(path, 'wb') as out:
        out.write(b'\x89PNG\r\n\x1a\n')
        out.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)))
        out.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        out.write(chunk(b'IEND', b''))


if __name__ == '__main__':
    args = sys.argv[1:]
    if not args or len(args) % 2:
        sys.exit(__doc__)
    for source, target in zip(args[0::2], args[1::2]):
        write_png(target, *read_bmp(source))
        print(target)
