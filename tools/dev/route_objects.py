#!/usr/bin/env python3
"""Read-only checker for mapfile.obs / mapfile.trk (and profile.obs / profile.trk).

Both are a u32 file word followed by one or more object containers, the same
container the .gnd world block uses (traced: trainz.exe 0x886be0; see
docs/formats/route-objects.md):

    u32 version (1..5); if version >= 5: u32 extra
    items: { i32 index (-1 ends), char tag[4] (reversed), i32 content, i32 user, u32 size, payload[size] }
    if version >= 4: u32 n, then n bytes (skipped by the game)

Usage: route_objects.py PATH...   (.ja archives, folders of them, or loose files)
Never writes anything.
"""
import collections
import importlib.util
import os
import struct
import sys

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("gnd_sections", os.path.join(_here, "gnd_sections.py"))
_gnd = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_gnd)

SUFFIXES = (".obs", ".trk")


def entries(path):
    if os.path.isdir(path):
        for fn in sorted(os.listdir(path)):
            if fn.lower().endswith(SUFFIXES + (".ja",)):
                yield from entries(os.path.join(path, fn))
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
            if name.lower().endswith(SUFFIXES):
                f.seek(off)
                yield path + ":" + name, _gnd._cfil(f.read(stored))


def container(d, pos, tags, versions):
    ver, = struct.unpack_from("<I", d, pos)
    pos += 4
    if not 1 <= ver <= 5:
        raise ValueError(f"container version {ver} at {pos - 4}")
    versions[ver] += 1
    if ver >= 5:
        pos += 4
    items = 0
    while True:
        i, = struct.unpack_from("<i", d, pos)
        pos += 4
        if i == -1:
            break
        tag = d[pos:pos + 4].decode("latin1")
        size, = struct.unpack_from("<I", d, pos + 12)
        pos += 16 + size
        if pos > len(d):
            raise ValueError(f"item {i} ({tag}) runs past the end")
        tags[tag] += 1
        items += 1
    if ver >= 4:
        n, = struct.unpack_from("<I", d, pos)
        pos += 4 + n
    return pos, items


def check(name, d, st):
    if len(d) < 8:
        raise ValueError("too short")
    first, = struct.unpack_from("<I", d, 0)
    pos = 4
    count = 0
    while pos < len(d):
        pos, _ = container(d, pos, st["tags"], st["versions"])
        count += 1
    if pos != len(d):
        raise ValueError(f"ends at {pos}, file is {len(d)}")
    st["files"] += 1
    st["shape"][(name.rsplit(".", 1)[-1].lower(), first, count)] += 1


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    st = {"files": 0, "tags": collections.Counter(), "versions": collections.Counter(), "shape": collections.Counter()}
    bad = 0
    for p in argv[1:]:
        for name, d in entries(p):
            try:
                check(name, d, st)
            except (ValueError, struct.error) as e:
                bad += 1
                print("FAIL", name, e)
    print(f"files read to their exact end: {st['files']}, failed: {bad}")
    print("(suffix, first word, containers):", dict(st["shape"]))
    print("container versions:", dict(st["versions"]))
    print("tags:", dict(st["tags"].most_common()))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
