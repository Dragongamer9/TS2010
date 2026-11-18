"""Shrink an RGBA PNG (filter-0 rows, as PngWriter makes) by an integer factor, keeping
coloured (track) pixels; write a compressed PNG. Usage: shrink.py IN OUT FACTOR"""
import struct, sys, zlib
def read(path):
    d = open(path, 'rb').read(); pos = 8; idat = b''; w = h = 0
    while pos < len(d):
        n = struct.unpack_from('>I', d, pos)[0]; t = d[pos+4:pos+8]; b = d[pos+8:pos+8+n]
        if t == b'IHDR': w, h = struct.unpack_from('>II', b)
        if t == b'IDAT': idat += b
        pos += 12 + n
    raw = zlib.decompress(idat); s = 1 + 4*w
    return w, h, [raw[y*s+1:(y+1)*s] for y in range(h)]
def write(path, w, h, rows):
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
w, h, rows = read(sys.argv[1]); k = int(sys.argv[3]); W, H = w // k, h // k; out = []
for Y in range(H):
    row = bytearray()
    for X in range(W):
        best = None; best_rank = 0; acc = [0, 0, 0, 0]
        for y in range(Y*k, Y*k+k):
            r = rows[y]
            for x in range(X*k, X*k+k):
                p = r[4*x:4*x+4]
                if p[0] != p[1] or p[1] != p[2]:  # coloured = track overlay
                    rank = 3 if tuple(p[:3]) == (255, 140, 0) else (2 if p[2] > 200 else 1)  # trains, then junctions
                    if best is None or rank > best_rank: best, best_rank = p, rank
                for i in range(4): acc[i] += p[i]
        row += bytes(best) if best is not None else bytes(v // (k*k) for v in acc)
    out.append(row)
write(sys.argv[2], W, H, out)
print(sys.argv[2], W, 'x', H)
