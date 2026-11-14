#!/usr/bin/env python3
"""drive-demo smoke test: the own game's generated world runs with no game files at all,
offscreen, and the train moves. Usage: test_drive_demo_cli.py DRIVE_DEMO
Skips (77) only when SDL has no offscreen video driver."""
import os
import re
import subprocess
import sys
import tempfile


def main():
    tool = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, SDL_VIDEODRIVER="offscreen", HOME=tmp, XDG_DATA_HOME=os.path.join(tmp, "data"))
        r = subprocess.run([tool, "--frames", "20", "--png"], env=env, capture_output=True, text=True, timeout=600,
                           cwd=tmp)  # run from an empty folder: nothing to read
        if r.returncode != 0 and "SDL_Init failed" in r.stderr and "not available" in r.stderr:
            print("skipped: SDL has no offscreen video driver")
            return 77
        assert r.returncode == 0, (r.returncode, r.stdout, r.stderr)
        m = re.search(r"drive-demo\s+(\d+) km/h", r.stdout)
        assert m and int(m.group(1)) > 10, r.stdout  # it got going
        pngs = [f for f in os.listdir(os.path.join(tmp, "data", "trainz-native", "game"))]
        assert len(pngs) == 1 and pngs[0].endswith(".png"), pngs
        r = subprocess.run([tool, "--frames", "0"], env=env, capture_output=True, text=True, timeout=60)
        assert r.returncode == 2, (r.returncode, r.stderr)
        r = subprocess.run([tool, "--nonsense"], env=env, capture_output=True, text=True, timeout=60)
        assert r.returncode == 2 and "usage" in r.stderr, r.stderr
    print("drive-demo checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
