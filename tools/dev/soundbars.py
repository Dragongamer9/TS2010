# Draws the loudness of a mono 16-bit WAV (bars per 0.25 s) as a PNG; marks a span in orange.
import math, struct, sys, wave, zlib
src, dst, mark0, mark1 = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
w = wave.open(src); n = w.getnframes(); r = w.getframerate(); s = struct.unpack('<%dh' % n, w.readframes(n))
step = r // 4; bars = [math.sqrt(sum(x * x for x in s[i:i + step]) / step) for i in range(0, n - step + 1, step)]
W, H = 960, 300; img = [[(24, 28, 36)] * W for _ in range(H)]
bw = W // len(bars); top = max(bars) or 1
for i, b in enumerate(bars):
    h = int((H - 40) * b / top); t = i * 0.25
    col = (255, 170, 60) if mark0 <= t < mark1 else (90, 190, 255)
    for y in range(H - 20 - h, H - 20):
        for x in range(i * bw + 1, (i + 1) * bw):
            img[y][x] = col
for sec in range(0, int(len(bars) / 4) + 1, 5):  # ticks every 5 s
    x = min(W - 1, sec * 4 * bw)
    for y in range(H - 20, H - 10):
        img[y][x] = (200, 200, 200)
raw = b''.join(b'\x00' + bytes(v for px in row for v in px) for row in img)
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
open(dst, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
print(dst, len(bars), 'bars')
