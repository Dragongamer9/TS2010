"""Synthetic CLI regression checks; run with a built route-tool path."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


def u32(value):
    return struct.pack('<I', value)


def archive(path, height, x=0):
    # No textures/cells; each vertex uses the explicit-height empty layer list.
    body = u32(0) + u32(0) + (b'\xff' + struct.pack('<f', height)) * (76 * 76)
    start = 28
    end = start + len(body)
    ground = b'GND\r' + u32(1) + struct.pack('<ii', x, 0) + b'sceS' + u32(start) + u32(end)
    ground += body + bytes(8192)
    write_archive(path, ground)


def write_archive(path, ground):
    name = b'route/mapfile.gnd'.ljust(260, b'\0')
    metadata = name + u32(len(ground)) * 2 + bytes(20)
    assert len(metadata) == 288
    payload = b'FIL ' + u32(288 + len(ground)) + metadata + ground
    index = u32(340) + u32(len(ground)) + metadata
    header = b'ARCHINFO' + u32(32) + u32(0x41524330) + u32(0x102) + u32(1)
    header += u32(44 + len(payload)) + bytes(16)
    path.write_bytes(header + payload + b'IDX ' + u32(296) + index)


def read_pixels(path, width, height):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    pos, compressed = 8, b''
    while pos < len(data):
        size = struct.unpack_from('>I', data, pos)[0]
        tag = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + size]
        crc = struct.unpack_from('>I', data, pos + 8 + size)[0]
        assert zlib.crc32(tag + chunk) == crc
        if tag == b'IHDR':
            assert struct.unpack_from('>II', chunk) == (width, height)
            assert chunk[8:] == bytes([8, 6, 0, 0, 0])
        if tag == b'IDAT':
            compressed += chunk
        pos += size + 12
    raw = zlib.decompress(compressed)
    stride = width * 4 + 1
    assert len(raw) == height * stride
    rows = []
    for row in range(height):
        assert raw[row * stride] == 0
        rows.append(raw[row * stride + 1:(row + 1) * stride])
    return rows


def pixels(path):
    rows = read_pixels(path, 64, 64)
    for row in range(64):
        assert rows[row] == bytes([128, 128, 128, 255]) * 64


def rising_route(path, mixed):
    # Heights are world x + 2*y. Each baseboard spans 72 units; Sec5 has
    # twice the sampling density. Padding continues the slope beyond the edge,
    # so sampling side-1 instead of side-4 cannot accidentally pass.
    sections = []
    maximum = 0.0
    for y in range(2):
        for x in range(2):
            side = 148 if mixed and (x, y) == (1, 1) else 76
            scale = 0.5 if side == 148 else 1.0
            body = bytearray(u32(0) + u32(0))
            for row in range(side):
                for col in range(side):
                    value = 72 * x + col * scale + 2 * (72 * y + row * scale)
                    maximum = max(maximum, value)
                    body += b'\xff' + struct.pack('<f', value)
            sections.append((x, y, b'5ceS' if side == 148 else b'sceS', body))
    offset = 8 + 20 * len(sections)
    index, payload = bytearray(), bytearray()
    for x, y, tag, body in sections:
        end = offset + len(body)
        index += struct.pack('<ii', x, y) + tag + u32(offset) + u32(end)
        payload += body + bytes(8192)
        offset = end + 8192
    write_archive(path, b'GND\r' + u32(len(sections)) + index + payload)
    return maximum


def check_rising_pixels(path, maximum, mixed):
    rows = read_pixels(path, 128, 128)

    def pixel(x, y):
        return tuple(rows[y][x * 4:x * 4 + 4])

    def equal_at(a, b, label):
        actual, expected = pixel(*a), pixel(*b)
        assert actual == expected, (
            f'{label}: pixel {a} = {actual}, pixel {b} = {expected}')

    for tile_y in range(2):
        for tile_x in range(2):
            left = (64 * tile_x, 64 * tile_y + 32)
            right = (64 * tile_x + 63, left[1])
            top = (64 * tile_x + 32, 64 * tile_y)
            bottom = (top[0], 64 * tile_y + 63)
            assert pixel(*left)[0] < pixel(*right)[0], (
                f'+x slope: pixel {left} = {pixel(*left)}, pixel {right} = {pixel(*right)}')
            assert pixel(*top)[0] > pixel(*bottom)[0], (
                f'+y slope: pixel {top} = {pixel(*top)}, pixel {bottom} = {pixel(*bottom)}')
    # Mixed resolutions round some pixel samples to different world positions.
    # Compare shared coordinates (0, 24, 48, 72) on those edges; the uniform
    # route can additionally compare every pixel along both full seams.
    edge_coordinates = ([tile * 64 + p for tile in range(2) for p in (0, 21, 42, 63)]
                        if mixed else range(128))
    for coordinate in edge_coordinates:
        equal_at((63, coordinate), (64, coordinate), 'vertical shared edge')
        equal_at((coordinate, 63), (coordinate, 64), 'horizontal shared edge')

    # Fixed sample positions include both edges and interior points. At pixel
    # 2, Sec5 samples vertex 5 (2.5 units), while Secs samples vertex 2.
    # This distinguishes the 144-cell grid from an incorrectly reused 72.
    for tile_y in range(2):
        for tile_x in range(2):
            dense = mixed and (tile_x, tile_y) == (1, 0)
            samples = {0: 0, 2: 2.5 if dense else 2, 21: 24, 42: 48, 63: 72}
            for px, world_dx in samples.items():
                for from_bottom, world_dy in samples.items():
                    position = (64 * tile_x + px, 64 * tile_y + 63 - from_bottom)
                    value = 72 * tile_x + world_dx + 2 * (72 * (1 - tile_y) + world_dy)
                    grey = int(value / maximum * 255 + 0.5)
                    expected = (grey, grey, grey, 255)
                    actual = pixel(*position)
                    assert actual == expected, (
                        f'{"Sec5" if dense else "Secs"} pixel {position}: '
                        f'got {actual}, expected {expected} for height {value}')


def main():
    tool = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        env = dict(os.environ, HOME=tmp, XDG_DATA_HOME=str(root / 'data'))
        (root / 'game').mkdir()
        source = root / 'game/fixture.ja'
        archive(source, 12.0, x=-2147483648)
        before = source.read_bytes()
        def run():
            return subprocess.run([tool, 'heights', str(source), 'route'], env=env,
                                  capture_output=True, text=True, timeout=30)
        result = run()
        assert result.returncode == 0, result.stderr
        output = root / 'data/trainz-native/routes/route.heights.png'
        pixels(output)
        assert source.read_bytes() == before
        assert run().returncode != 0  # never overwrite
        output.unlink()
        archive(source, float('nan'))
        assert run().returncode != 0
        assert not output.exists()
        archive(source, 12.0, x=-2147483648)
        assert source.read_bytes() == before
        for mixed in (False, True):
            maximum = rising_route(source, mixed)
            before = source.read_bytes()
            result = run()
            assert result.returncode == 0, result.stderr
            check_rising_pixels(output, maximum, mixed)
            assert source.read_bytes() == before
            output.unlink()
    print('route heights CLI checks passed')


if __name__ == '__main__':
    main()
