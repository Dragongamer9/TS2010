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
    name = b'route/mapfile.gnd'.ljust(260, b'\0')
    metadata = name + u32(len(ground)) * 2 + bytes(20)
    assert len(metadata) == 288
    payload = b'FIL ' + u32(288 + len(ground)) + metadata + ground
    index = u32(340) + u32(len(ground)) + metadata
    header = b'ARCHINFO' + u32(32) + u32(0x41524330) + u32(0x102) + u32(1)
    header += u32(44 + len(payload)) + bytes(16)
    path.write_bytes(header + payload + b'IDX ' + u32(296) + index)


def pixels(path):
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
            assert struct.unpack_from('>II', chunk) == (64, 64)
        if tag == b'IDAT':
            compressed += chunk
        pos += size + 12
    raw = zlib.decompress(compressed)
    assert len(raw) == 64 * 257
    for row in range(64):
        assert raw[row * 257] == 0
        assert raw[row * 257 + 1:(row + 1) * 257] == bytes([128, 128, 128, 255]) * 64


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
    print('route heights CLI checks passed')


if __name__ == '__main__':
    main()
