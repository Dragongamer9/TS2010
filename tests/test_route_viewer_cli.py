#!/usr/bin/env python3
"""route-viewer smoke test: draw a few frames of the test route with SDL's offscreen video
driver (no window appears) and quit; refuse bad arguments.

Usage: test_route_viewer_cli.py ROUTE_VIEWER TESTDATA_DIR
Skips (exit code 77) when the test route is not there or SDL has no offscreen driver.
Reads the test archive only.
"""
import os
import subprocess
import sys
from pathlib import Path

ROUTE = "hash-fa/kuid 447264 1035"
SESSION = "hash-e1/kuid 467400 1035"


def main():
    tool = str(Path(sys.argv[1]).resolve())
    data = Path(sys.argv[2])
    archive = data / "Builtin" / "sample8.ja"
    if not archive.is_file():
        print("skipped: no test archive")
        return 77
    before = (archive.stat().st_mtime_ns, archive.stat().st_size)
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen")
    r = subprocess.run([tool, str(archive), ROUTE, "--session", str(archive), SESSION, "--install", str(data),
                        "--radius", "300", "--frames", "3"], env=env, capture_output=True, text=True, timeout=600)
    if r.returncode != 0 and ("SDL_Init failed" in r.stderr or "could not open a window" in r.stderr):
        print("skipped: SDL could not start offscreen:", r.stderr.strip())
        return 77
    assert r.returncode == 0, (r.returncode, r.stdout, r.stderr)
    assert "triangles" in r.stdout and "route-viewer  x " in r.stdout and "ms per frame" in r.stdout, r.stdout
    for bad in (["--frames", "0"], ["--frames", "2.5"], ["--radius", "abc"], ["--at", "1"], ["--nonsense"]):
        r = subprocess.run([tool, str(archive), ROUTE, *bad], env=env, capture_output=True, text=True, timeout=60)
        assert r.returncode == 2, (bad, r.returncode, r.stderr)
    r = subprocess.run([tool], env=env, capture_output=True, text=True, timeout=60)
    assert r.returncode == 2 and "usage" in r.stderr, r.stderr
    assert (archive.stat().st_mtime_ns, archive.stat().st_size) == before
    print("route-viewer checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
