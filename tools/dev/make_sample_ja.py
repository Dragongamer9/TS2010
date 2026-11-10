#!/usr/bin/env python3
"""Build testdata/ts2010/Builtin/sample8.ja: a real subset of Builtin/8.ja.

Builtin/8.ja (556 MB) is too large for a normal git repository, so CI tests
real data through this subset instead. Entries are copied byte for byte from
8.ja after decompression and stored uncompressed (no CFIL), with their
original names, sizes and hash fields. The archive layout follows
docs/formats/ja.md.

Picked:
  - every file of a few assets that cover the formats we read:
      the route        hash-fa/kuid 447264 1035     (mapfile.*, config.chump)
      the ore car      hash-c0/kuid2 90361 120204 2 (.im with bone names, .kin)
      a bogie          hash-c1/kuid 58422 50128     (.pm, .kin with 3 bones)
      a steel bridge   hash-c0/kuid 101839 22229    (GEOM v201 flag 1, MATL v103)
      a bell sounder   hash-d4/kuid2 60850 60001 2  (.kin with events)
  - every config.chump and config.txt in the archive
  - a few .texture files of every pixel format in use

Usage (reads 8.ja only; writes only the output file, which must not exist):
  python3 -I tools/dev/make_sample_ja.py /path/to/Builtin/8.ja testdata/ts2010/Builtin/sample8.ja
"""
import os
import struct
import sys

ASSET_FOLDERS = (
    "hash-fa/kuid 447264 1035/",
    "hash-c0/kuid2 90361 120204 2/",
    "hash-c1/kuid 58422 50128/",
    "hash-c0/kuid 101839 22229/",
    "hash-d4/kuid2 60850 60001 2/",
)
TEXTURES_PER_FORMAT = 3


def bpe_expand(src):
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
                if c >= 256:
                    break
                cnt = src[i]
                i += 1
                continue
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


def decode(data):
    if data[:4] != b"CFIL":
        return data
    usize = struct.unpack("<I", data[8:12])[0]
    out = bytearray()
    i = 12
    while len(out) < usize:
        h = struct.unpack("<H", data[i:i + 2])[0]
        i += 2
        if h & 0x8000:
            ln = h & 0x7FFF
            out += data[i:i + ln]
        else:
            ln = h
            out += bpe_expand(data[i:i + ln])
        i += ln
    return bytes(out)


def texture_format(data):
    if data[:4] != b"JIRF" or data[8:12] != b"MMTF":
        return None
    o = 12
    while o + 8 <= len(data):
        tag = data[o:o + 4]
        size = struct.unpack("<I", data[o + 4:o + 8])[0]
        if tag == b"INFO" and size >= 20:
            return struct.unpack("<I", data[o + 8 + 16:o + 8 + 20])[0]
        o += 8 + size
    return None


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src_path, out_path = sys.argv[1], sys.argv[2]
    if os.path.exists(out_path):
        print("refusing to overwrite", out_path)
        return 1
    with open(src_path, "rb") as f:
        head = f.read(44)
        if head[:8] != b"ARCHINFO" or head[12:16] != b"0CRA":
            print("not a .ja archive")
            return 1
        count, idx_off = struct.unpack("<II", head[20:28])
        f.seek(idx_off + 8)
        rows = f.read(count * 296)
        picked = []
        seen = set()
        per_format = {}
        for i in range(count):
            r = rows[i * 296:(i + 1) * 296]
            off, stored = struct.unpack("<II", r[:8])
            name = r[8:268].split(b"\0")[0].decode("latin1")
            if name in seen:
                continue
            base = name.rsplit("/", 1)[-1]
            want = name.startswith(ASSET_FOLDERS) or base in ("config.chump", "config.txt")
            if not want and not name.endswith(".texture"):
                continue
            f.seek(off - 288)
            fil_header = f.read(288)
            f.seek(off)
            data = decode(f.read(stored))
            if not want:
                fmt = texture_format(data)
                if fmt is None or per_format.get(fmt, 0) >= TEXTURES_PER_FORMAT:
                    continue
                per_format[fmt] = per_format.get(fmt, 0) + 1
            original_name = fil_header[:260].split(b"\0")[0]
            hash_field = fil_header[268:272]
            seen.add(name)
            picked.append((original_name, data, hash_field))

    out = bytearray(b"ARCHINFO")
    out += struct.pack("<I", 32)
    out += struct.pack("<IIII", 0x41524330, 0x102, len(picked), 0)  # index offset patched below
    out += struct.pack("<I", 42195)  # same value as 8.ja
    out += bytes(12)
    rows_out = []
    for original_name, data, hash_field in picked:
        name_field = original_name.ljust(260, b"\0")
        header = name_field + struct.pack("<II", len(data), len(data)) + hash_field + bytes(12) + struct.pack("<I", 0)
        out += b"FIL " + struct.pack("<I", 288 + len(data))
        offset = len(out) + 288
        out += header + data
        rows_out.append(struct.pack("<II", offset, len(data)) + original_name.lower().ljust(260, b"\0") +
                        struct.pack("<II", len(data), len(data)) + hash_field + bytes(12) + struct.pack("<I", 0))
    struct.pack_into("<I", out, 24, len(out))
    out += b"IDX " + struct.pack("<I", 296 * len(rows_out)) + b"".join(rows_out)
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "xb") as f:
        f.write(out)
    print("wrote", out_path, len(picked), "entries,", len(out), "bytes; texture formats", sorted(per_format.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
