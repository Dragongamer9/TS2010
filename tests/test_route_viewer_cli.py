#!/usr/bin/env python3
"""route-viewer smoke test: draw a few frames of the test route with SDL's offscreen video
driver (no window appears) and quit; refuse bad arguments.

Usage: test_route_viewer_cli.py ROUTE_VIEWER TESTDATA_DIR
Skips (exit code 77) when the test route is not there or SDL has no offscreen video driver
(only then: any other startup failure fails the test).
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
    # Skip only when SDL itself has no offscreen video driver (SDL_Init reports "offscreen not
    # available"); a window, renderer or texture that fails after that is a real failure.
    if r.returncode != 0 and "SDL_Init failed" in r.stderr and "not available" in r.stderr:
        print("skipped: SDL has no offscreen video driver:", r.stderr.strip())
        return 77
    assert r.returncode == 0, (r.returncode, r.stdout, r.stderr)
    assert "triangles" in r.stdout and "route-viewer  x " in r.stdout and "ms per frame" in r.stdout, r.stdout
    for bad in (["--frames", "0"], ["--frames", "2.5"], ["--radius", "abc"], ["--at", "1"], ["--nonsense"],
                ["--keys", "x:w"], ["--keys", "3:F1"], ["--keys", "0:w"], ["--keys", "1.5:a"], ["--camera", "1", "2", "3"],
                ["--watch", "-1"], ["--watch", "x"]):
        r = subprocess.run([tool, str(archive), ROUTE, *bad], env=env, capture_output=True, text=True, timeout=60)
        assert r.returncode == 2, (bad, r.returncode, r.stderr)
    r = subprocess.run([tool], env=env, capture_output=True, text=True, timeout=60)
    assert r.returncode == 2 and "usage" in r.stderr, r.stderr
    # By name: list, pick a route by part of its name, refuse unknown or missing --install.
    r = subprocess.run([tool, "--install", str(data), "--list"], env=env, capture_output=True, text=True, timeout=300)
    assert r.returncode == 0 and "Springfield Industrial District  <kuid:447264:1035>" in r.stdout, (r.returncode, r.stdout)
    assert "1 routes" in r.stdout, r.stdout
    r = subprocess.run([tool, "--install", str(data), "--route", "springfield", "--radius", "200", "--frames", "2"],
                       env=env, capture_output=True, text=True, timeout=600)
    assert r.returncode == 0 and "route: Springfield Industrial District" in r.stdout and "ms per frame" in r.stdout, \
        (r.returncode, r.stdout, r.stderr)
    r = subprocess.run([tool, "--install", str(data), "--route", "<kuid:447264:1035>", "--frames", "1", "--radius", "100"],
                       env=env, capture_output=True, text=True, timeout=600)
    assert r.returncode == 0, (r.returncode, r.stderr)
    r = subprocess.run([tool, "--install", str(data), "--route", "no such route"], env=env, capture_output=True, text=True,
                       timeout=300)
    assert r.returncode == 3 and "no single route" in r.stderr, (r.returncode, r.stderr)
    # Drive mode needs a session with track and --install; refused clearly otherwise.
    r = subprocess.run([tool, str(archive), ROUTE, "--drive"], env=env, capture_output=True, text=True, timeout=300)
    assert r.returncode == 2 and "--drive needs" in r.stderr, (r.returncode, r.stderr)
    r = subprocess.run([tool, "--install", str(data), "--route", "springfield", "--drive"], env=env, capture_output=True,
                       text=True, timeout=300)
    assert r.returncode == 2 and "--drive needs" in r.stderr, (r.returncode, r.stderr)
    r = subprocess.run([tool, str(archive), ROUTE, "--train", "0"], env=env, capture_output=True, text=True, timeout=60)
    assert r.returncode == 2, (r.returncode, r.stderr)
    for bad in (["--route", "x"], ["--install", str(data)]):
        r = subprocess.run([tool, *bad], env=env, capture_output=True, text=True, timeout=300)
        assert r.returncode == 2, (bad, r.returncode, r.stderr)
    assert (archive.stat().st_mtime_ns, archive.stat().st_size) == before
    print("route-viewer checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
