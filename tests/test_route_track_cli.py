"""route-tool track on the real test route: draws the track network over the terrain.

Run: python3 tests/test_route_track_cli.py BUILT_ROUTE_TOOL TESTDATA_DIR
Skips (exit code 77, which CTest reports as "Skipped") when the test route is not there. Never writes into the
test data: output goes to a temporary XDG data folder.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROUTE = "hash-fa/kuid 447264 1035"
SESSION = "hash-e1/kuid 467400 1035"


def rgba_rows(png):
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(png):
        size = struct.unpack_from(">I", png, pos)[0]
        tag = png[pos + 4:pos + 8]
        body = png[pos + 8:pos + 8 + size]
        if tag == b"IHDR":
            w, h = struct.unpack_from(">II", body)
        elif tag == b"IDAT":
            idat += body
        pos += 12 + size
    raw = zlib.decompress(idat)
    stride = 1 + 4 * w
    assert len(raw) == stride * h
    for y in range(h):
        assert raw[y * stride] == 0  # the writer uses filter 0 only
        yield raw[y * stride + 1:(y + 1) * stride]


def main():
    tool = str(Path(sys.argv[1]).resolve())
    archive = Path(sys.argv[2]) / "Builtin" / "sample8.ja"
    if not archive.is_file():
        print("test route not found; skipped")
        sys.exit(77)
    before = archive.stat().st_mtime_ns, archive.stat().st_size
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, HOME=tmp, XDG_DATA_HOME=str(Path(tmp) / "data"))
        r = subprocess.run([tool, "track", str(archive), ROUTE], env=env, capture_output=True, text=True, timeout=120)
        assert r.returncode == 0, r.stderr
        assert "794 pieces drawn" in r.stdout, r.stdout
        out = Path(tmp) / "data/trainz-native/routes" / (ROUTE.replace("/", "_") + ".track.png")
        assert "placed objects" in r.stdout, r.stdout
        red = yellow = cyan = green = 0
        for row in rgba_rows(out.read_bytes()):
            for i in range(0, len(row), 4):
                px = tuple(row[i:i + 4])
                red += px == (230, 30, 30, 255)
                yellow += px == (230, 210, 30, 255)
                cyan += px == (0, 220, 240, 255)
                green += px == (20, 120, 40, 255)
        # The route's 790 stretches, 4 tkBr pieces and 38 junction-like vertices all show up.
        assert red > 1000 and yellow > 0 and cyan > 0 and green > 1000, (red, yellow, cyan, green)
        # --session: the test session's trains are read and counted. Its own route is not
        # in the test data, so they are drawn on this route only where they happen to fall.
        r = subprocess.run([tool, "track", str(archive), ROUTE, "--session", str(archive), SESSION, "--out",
                            str(Path(tmp) / "data/trainz-native/routes/session")],
                           env=env, capture_output=True, text=True, timeout=120)
        assert r.returncode == 0, r.stderr
        assert "session: 25 trains, 231 vehicles" in r.stdout, r.stdout
        r = subprocess.run([tool, "track", str(archive), ROUTE, "--session", str(archive), "no/such/session"],
                           env=env, capture_output=True, text=True, timeout=120)
        assert r.returncode != 0 and "profile.dat" in r.stderr, (r.returncode, r.stderr)
        # route-tool view: a 3D picture of part of the route.
        r = subprocess.run([tool, "view", str(archive), ROUTE], env=env, capture_output=True, text=True, timeout=300)
        assert r.returncode == 0, r.stderr
        assert " triangles," in r.stdout and "objects as models 0" in r.stdout, r.stdout  # no --install: dots only
        view = Path(tmp) / "data/trainz-native/routes" / (ROUTE.replace("/", "_") + ".view.png")
        rows = list(rgba_rows(view.read_bytes()))
        assert len(rows) == 720 and len(rows[0]) == 1280 * 4
        sky = tuple(rows[0][0:4])
        assert sky[2] > 200 and sky[3] == 255, sky  # sky at the top
        track = sum(1 for row in rows[300:] for i in range(0, len(row), 4) if tuple(row[i:i + 3]) == (200, 30, 30))
        assert track > 50, track  # track drawn on the terrain
        # --install: placed objects drawn as their 3D models (the test data has only a few assets).
        r = subprocess.run([tool, "view", str(archive), ROUTE, "--install", str(Path(sys.argv[2])), "--radius", "500",
                            "--out", str(Path(tmp) / "data/trainz-native/routes/models")],
                           env=env, capture_output=True, text=True, timeout=300)
        assert r.returncode == 0, r.stderr
        m = re.search(r"objects as models (\d+)", r.stdout)
        assert m and int(m.group(1)) > 0, r.stdout
        r = subprocess.run([tool, "view", str(archive), ROUTE, "--radius", "5"], env=env, capture_output=True,
                           text=True, timeout=60)
        assert r.returncode != 0 and "radius" in r.stderr, r.stderr
        # --eye: a camera placed near the centre still renders; bad values are refused.
        r = subprocess.run([tool, "view", str(archive), ROUTE, "--eye", "-30", "-20", "10", "--radius", "200",
                            "--out", str(Path(tmp) / "data/trainz-native/routes/eye")],
                           env=env, capture_output=True, text=True, timeout=300)
        assert r.returncode == 0, r.stderr
        assert (Path(tmp) / "data/trainz-native/routes/eye" / (ROUTE.replace("/", "_") + ".view.png")).is_file()
        for bad in (["0", "0", "0"], ["nan", "1", "1"], ["1", "1e9", "1"]):
            r = subprocess.run([tool, "view", str(archive), ROUTE, "--eye", *bad], env=env, capture_output=True,
                               text=True, timeout=60)
            assert r.returncode != 0 and "--eye" in r.stderr, (bad, r.returncode, r.stderr)
    assert (archive.stat().st_mtime_ns, archive.stat().st_size) == before
    print(f"route track CLI checks passed (red {red}, yellow {yellow}, cyan {cyan}, green {green})")


if __name__ == "__main__":
    main()
