#!/usr/bin/env python3
"""Build testdata/ts2010/Builtin/sample-reskin.ja: whole asset folders copied from
several real archives, for tests that need assets from more than one of them.

Entries are copied byte for byte after decompression and stored uncompressed (no CFIL),
with their original names and hash fields, in the layout make_sample_ja.py writes.

Picked (TS2010 Builtin):
  6.ja  hash-41/kuid 107727 100012/   a Paint Shed reskin (skin.dat, its own main.texture)
  4.ja  hash-ce/kuid _10 199/         its base asset, the 54 ft covered hopper (.pm mesh)

Usage (reads the archives only; writes only the output file, which must not exist):
  python3 -I tools/dev/make_reskin_ja.py OUT.ja ARCHIVE FOLDER/ [ARCHIVE FOLDER/ ...]
"""
import importlib.util
import os
import struct
import sys

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("make_sample_ja", os.path.join(_here, "make_sample_ja.py"))
_sample = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_sample)


def entries(src_path, folder):
    """(original name, decoded data, hash field) for every entry under folder."""
    out = []
    with open(src_path, "rb") as f:
        head = f.read(44)
        if head[:8] != b"ARCHINFO" or head[12:16] != b"0CRA":
            raise SystemExit("not a .ja archive: " + src_path)
        count, idx_off = struct.unpack("<II", head[20:28])
        f.seek(idx_off + 8)
        rows = f.read(count * 296)
        seen = set()
        for i in range(count):
            r = rows[i * 296:(i + 1) * 296]
            off, stored = struct.unpack("<II", r[:8])
            name = r[8:268].split(b"\0")[0].decode("latin1")
            if name in seen or not name.startswith(folder):
                continue
            f.seek(off - 288)
            fil_header = f.read(288)
            f.seek(off)
            data = _sample.decode(f.read(stored))
            seen.add(name)
            out.append((fil_header[:260].split(b"\0")[0], data, fil_header[268:272]))
    if not out:
        raise SystemExit("nothing under " + folder + " in " + src_path)
    return out


def main():
    if len(sys.argv) < 4 or len(sys.argv) % 2 != 0:
        print(__doc__)
        return 2
    out_path = sys.argv[1]
    if os.path.exists(out_path):
        print("refusing to overwrite", out_path)
        return 1
    picked = []
    for i in range(2, len(sys.argv), 2):
        picked += entries(sys.argv[i], sys.argv[i + 1].lower())
    out = bytearray(b"ARCHINFO")
    out += struct.pack("<I", 32)
    out += struct.pack("<IIII", 0x41524330, 0x102, len(picked), 0)  # index offset patched below
    out += struct.pack("<I", 42195)  # the value make_sample_ja.py writes
    out += bytes(12)
    rows_out = []
    for original_name, data, hash_field in picked:
        header = original_name.ljust(260, b"\0") + struct.pack("<II", len(data), len(data)) + hash_field + bytes(12) + struct.pack("<I", 0)
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
    print("wrote", out_path, len(picked), "entries,", len(out), "bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
