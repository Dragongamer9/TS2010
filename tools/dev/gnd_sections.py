#!/usr/bin/env python3
"""Check the mapfile.gnd section layout (docs/formats/route.md) on real files.

The world data between the index and the first section is parsed with the
rules of the file loader 0x6d2aa0 (environment 0x463dd0, object container
0x886be0) and must end exactly at the first section. Every section's bytes
[start, end) are parsed with the layout traced in
trainz.exe (Secs reader 0x82be00, Sec5 reader 0x821200, vertex reader
0x6f0e50) and the parse must end exactly at `end`. Then the 8192-byte
overview map must follow. Prints one line per file and a total.

Inputs are read-only. Accepts .gnd files and/or .ja archives (every *.gnd
entry inside is checked; CFIL/BPE entries are decoded in memory).

Usage:
  python3 -I tools/dev/gnd_sections.py FILE.gnd|ARCHIVE.ja ...
  python3 -I tools/dev/gnd_sections.py --stats FILE...   (also print field statistics)
  python3 -I tools/dev/gnd_sections.py --world FILE...   (check only the world data block; fast)
"""
import collections
import os
import struct
import sys

VERTS = {"Secs": 76 * 76, "Sec5": 148 * 148}  # 0x1690 at 0x82c267, 0x5590 at 0x82166c
TEX_SLOTS = 250  # 0xfa, 0x82bfdb
HEIGHT_CELLS = 36 * 36  # 0x510 limit at 0x82c02f, 0x23 bounds at 0x82c205
OVERVIEW = 64 * 64 * 2


# ---- .ja reading (docs/formats/ja.md), same rules as tools/dev/make_sample_ja.py ----
def _bpe(src):
    out = bytearray()
    i, n = 0, len(src)
    while i < n:
        left = list(range(256))
        right = [0] * 256
        c = 0
        cnt = src[i]
        i += 1
        while True:
            if cnt > 127:
                c += cnt - 127
            else:
                for _ in range(cnt + 1):
                    left[c] = src[i]
                    i += 1
                    if left[c] != c:
                        right[c] = src[i]
                        i += 1
                    c += 1
            if c >= 256:
                break
            cnt = src[i]
            i += 1
        size = (src[i] << 8) | src[i + 1]
        i += 2
        end = i + size
        stack = []
        while True:
            if stack:
                b = stack.pop()
            else:
                if i == end:
                    break
                b = src[i]
                i += 1
            if b == left[b]:
                out.append(b)
            else:
                stack.append(right[b])
                stack.append(left[b])
    return bytes(out)


def _cfil(data):
    if data[:4] != b"CFIL":
        return data
    usize = struct.unpack_from("<I", data, 8)[0]
    out = bytearray()
    i = 12
    while len(out) < usize:
        h = struct.unpack_from("<H", data, i)[0]
        i += 2
        ln = h & 0x7FFF if h & 0x8000 else h
        out += data[i:i + ln] if h & 0x8000 else _bpe(data[i:i + ln])
        i += ln
    return bytes(out)


def gnd_entries(path):
    if os.path.isdir(path):
        for fn in sorted(os.listdir(path)):
            if fn.lower().endswith((".gnd", ".ja")):
                yield from gnd_entries(os.path.join(path, fn))
        return
    if not path.lower().endswith(".ja"):
        with open(path, "rb") as f:
            yield path, f.read()
        return
    with open(path, "rb") as f:
        head = f.read(44)
        count, idx = struct.unpack_from("<II", head, 20)
        f.seek(idx + 8)
        rows = f.read(count * 296)
        for k in range(count):
            r = rows[k * 296:(k + 1) * 296]
            off, stored = struct.unpack_from("<II", r)
            name = r[8:268].split(b"\0")[0].decode("latin1")
            if name.endswith(".gnd"):
                f.seek(off)
                yield path + ":" + name, _cfil(f.read(stored))


# ---- mapfile.gnd ----
class Reader:
    def __init__(self, b, pos, end):
        self.b, self.p, self.end = b, pos, end

    def take(self, fmt):
        n = struct.calcsize(fmt)
        if self.p + n > self.end:
            raise ValueError("runs past the section end at %d" % self.p)
        v = struct.unpack_from(fmt, self.b, self.p)
        self.p += n
        return v if len(v) > 1 else v[0]


def read_header(b):
    """GND loader 0x6d2aa0: 3 magic bytes (not checked), u8 version, u32 count,
    then per section i32 x, i32 y, [u32 tag if version >= 12], u32 start, u32 end."""
    version = b[3]
    count = struct.unpack_from("<I", b, 4)[0]
    p = 8
    secs = []
    for _ in range(count):
        x, y = struct.unpack_from("<ii", b, p)
        p += 8
        tag = "Secs"
        if version >= 12:
            tag = b[p:p + 4][::-1].decode("latin1")
            p += 4
        start, end = struct.unpack_from("<II", b, p)
        p += 8
        secs.append((x, y, tag, start, end))
    return version, secs, p


def read_world(b, version, pos, end, st):
    """World data after the index (file loader 0x6d2aa0 from 0x6d2cc7 on).
    Returns the position where it ends; the caller checks it equals `end`."""
    r = Reader(b, pos, end)
    if version >= 7:  # environment, 0x463dd0
        count = r.take("<i")
        sub = -1
        if count == -1:
            sub = r.take("<i")
            if sub != -1:
                count = r.take("<i")
        count = max(0, min(count, 32))
        st["env_sub"][sub] += 1
        st["env_count"][count] += 1
        r.take("<Ii")  # KUID -> env+0xee4
        r.take("<2f")
        for _ in range(count):
            r.take("<f12ff12f")  # 104 bytes: f32, 3 x vec4, f32, 3 x vec4 (0x80b170 reads 4 floats)
        if sub != -1:
            r.take("<q")  # 64-bit time, given to _localtime64 by 0x463c10
            if sub >= 1:
                r.take("<2f")
    else:
        raise ValueError("version %d world data (0x463cb0) not traced" % version)
    if version >= 11:
        r.take("<4f")  # 0x6d2d73 -> world+0x858c
    if version >= 9:
        r.take("<Ii")  # KUID -> world+0x41a0 (0x6d2d82)
    if version >= 8:  # object container, vtable+0x28 = 0x886be0
        cver = r.take("<I")
        st["container_ver"][cver] += 1
        if 1 <= cver <= 5:
            if cver >= 5:
                r.take("<I")
            if cver == 2:
                raise ValueError("container version 2 items are not self-sized (not handled)")
            if cver >= 3:
                while True:  # 0x886800
                    index = r.take("<i")
                    if index == -1:
                        break
                    tag = r.take("<4s")[::-1].decode("latin1")
                    r.take("<Ii")  # KUID
                    size = r.take("<I")
                    if size > 0xFFFFFF:
                        raise ValueError("container item size %d" % size)
                    r.take("<%ds" % size)
                    st["container_items"][tag] += 1
            if cver >= 4:  # 0x8858d0: u32 size, then size bytes (skipped by this loader)
                size = r.take("<I")
                r.take("<%ds" % size)
                st["tail_sizes"][size] += 1
    if version < 13:  # ground-texture table, 0x6d2db8..0x6d2ef8
        n = r.take("<i")
        for _ in range(max(0, min(n, 250))):
            r.take("<f")
            r.take("<Ii")
    return r.p


def read_vertex(r, version, st):
    """0x6f0e50."""
    first = r.take("<B")
    layers = 0
    if first == 0xFE:  # one texture, weight 255, height 0
        r.take("<B")
        if version > 2:
            r.take("<B")
        st["v_fe"] += 1
        return
    if first == 0xFD:  # one texture, weight 255, explicit height
        r.take("<B")
        if version > 2:
            r.take("<B")
        h = r.take("<f")
        st["v_fd"] += 1
        st["h"].append(h)
    else:
        b = first
        n = 0
        while b != 0xFF:
            r.take("<B")  # weight
            if version > 2:
                r.take("<B")  # third per-layer byte
            b = r.take("<B")
            n += 1
            if n > 0x20:
                break
        layers = n
        h = r.take("<f")
        st["v_list"] += 1
        st["layers"][layers] += 1
        st["h"].append(h)
    if version < 12:
        r.take("<3B")


def read_section(b, tag, start, end, version, st):
    """Secs 0x82be00 / Sec5 0x821200 (identical apart from the vertex count)."""
    r = Reader(b, start, end)
    if version >= 13:
        ntex = r.take("<I")
        st["ntex"][ntex] += 1
        for i in range(min(ntex, TEX_SLOTS)):
            w = r.take("<f")
            if w > 0.0:  # comiss / jbe at 0x82bf68: NaN and <= 0 have no KUID
                content, packed = r.take("<Ii")
                st["tex_used"] += 1
                user = packed & 0x1FFFFFF
                if user & 0x1000000:
                    user -= 0x2000000
                st["kuid_users"][user] += 1
    if version >= 4:
        n = r.take("<I")
        if n > HEIGHT_CELLS:
            n = 0
        st["n_cells"][min(n, 9999)] += 1
        for _ in range(n):
            if version >= 5:
                col, row = r.take("<HH")
            else:
                r.take("<I")
                col = row = 0
            r.take("<f")
            if col > 35 or row > 35:
                st["cell_out_of_range"] += 1
    for _ in range(VERTS[tag]):
        read_vertex(r, version, st)
    return r.p


def check(name, b, st, world_only=False):
    version, secs, index_end = read_header(b)
    bad = []
    first = min(s[3] for s in secs)
    try:
        wend = read_world(b, version, index_end, first, st)
        if wend != first:
            bad.append("world data: parse ends at %d, first section at %d (%+d)" % (wend, first, wend - first))
        else:
            st["world_ok"] += 1
    except ValueError as e:
        bad.append("world data: %s" % e)
    if world_only:
        print("%s: version %d, world data %d bytes: %s" % (name, version, first - index_end,
                                                           "OK" if not bad else bad[0]))
        return len(secs), len(bad)
    for i, (x, y, tag, start, end) in enumerate(secs):
        if tag not in VERTS:
            bad.append("section %d: tag %r" % (i, tag))
            continue
        try:
            p = read_section(b, tag, start, end, version, st)
        except ValueError as e:
            bad.append("section %d (%s): %s" % (i, tag, e))
            continue
        if p != end:
            bad.append("section %d (%s): parse ends at %d, end is %d (%+d)" % (i, tag, p, end, p - end))
        if end + OVERVIEW > len(b):
            bad.append("section %d: overview map past the end of the file" % i)
        st["sections"] += 1
        st["tags"][tag] += 1
        st["data_bytes"] += end - start
    pos = collections.Counter((x, y) for x, y, *_ in secs)
    dups = sum(1 for v in pos.values() if v > 1)
    print("%s: version %d, %d sections, index ends %d, extra header %d bytes, duplicates %d: %s" % (
        name, version, len(secs), index_end, min(s[3] for s in secs) - index_end, dups,
        "OK" if not bad else "%d BAD" % len(bad)))
    for m in bad[:5]:
        print("   ", m)
    return len(secs), len(bad)


def main(argv):
    stats = "--stats" in argv
    world_only = "--world" in argv
    paths = [a for a in argv if a not in ("--stats", "--world")]
    if not paths:
        print(__doc__)
        return 2
    st = collections.defaultdict(int)
    for k in ("ntex", "n_cells", "layers", "kuid_users", "tags", "env_sub", "env_count",
              "container_ver", "container_items", "tail_sizes"):
        st[k] = collections.Counter()
    st["h"] = []
    total = bad = files = 0
    for p in paths:
        for name, b in gnd_entries(p):
            n, nb = check(name, b, st, world_only)
            files += 1
            total += n
            bad += nb
    print("files %d (world data fits in %d), sections %d, problems: %d; section data bytes %d" % (
        files, st["world_ok"], total, bad, st["data_bytes"]))
    if stats:
        h = st["h"]
        print("world: env sub-version", dict(st["env_sub"]), "env key count", dict(st["env_count"]))
        print("world: container version", dict(st["container_ver"]), "items", dict(st["container_items"]),
              "tail sizes", dict(st["tail_sizes"]))
        print("tags", dict(st["tags"]))
        print("texture slot counts", dict(st["ntex"]), "slots with a KUID", st["tex_used"])
        print("KUID users", st["kuid_users"].most_common(8))
        print("height-cell counts (top)", st["n_cells"].most_common(8), "out of range", st["cell_out_of_range"])
        print("vertices: 0xfe %d, 0xfd %d, list %d; layers %s" % (
            st["v_fe"], st["v_fd"], st["v_list"], dict(st["layers"])))
        if h:
            hs = sorted(h)
            print("explicit heights: n %d, min %.2f, median %.2f, max %.2f" % (
                len(hs), hs[0], hs[len(hs) // 2], hs[-1]))
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
