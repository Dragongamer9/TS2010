#!/usr/bin/env python3
"""trainz-route-viewer launcher checks: remembering the install (only with --set-install,
only under the data folder), refusing a wrong folder, showing a route by name, listing.

Usage: test_launcher_cli.py LAUNCHER ROUTE_VIEWER TESTDATA_DIR
Skips (77) without the test data or when SDL has no offscreen driver.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def run(args, env, stdin=""):
    return subprocess.run(["sh", *args], env=env, input=stdin, capture_output=True, text=True, timeout=600)


def main():
    launcher, viewer, data = str(Path(sys.argv[1]).resolve()), str(Path(sys.argv[2]).resolve()), Path(sys.argv[3])
    if not (data / "Builtin" / "sample8.ja").is_file():
        print("skipped: no test data")
        return 77
    archive = data / "Builtin" / "sample8.ja"
    before = (archive.stat().st_mtime_ns, archive.stat().st_size)
    with tempfile.TemporaryDirectory() as tmp:
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": tmp, "XDG_DATA_HOME": str(Path(tmp) / "data"),
               "TRAINZ_ROUTE_VIEWER": viewer, "SDL_VIDEODRIVER": "offscreen"}
        for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS", "LD_LIBRARY_PATH"):
            if k in os.environ:
                env[k] = os.environ[k]
        setting = Path(tmp) / "data" / "trainz-native" / "install-dir"

        r = run([launcher, "springfield"], env)  # nothing remembered yet: a hint, nothing written
        assert r.returncode == 1 and "--set-install" in r.stderr, (r.returncode, r.stderr)
        assert not setting.exists()

        r = run([launcher, "--set-install", str(Path(tmp) / "nope")], env)
        assert r.returncode == 2 and not setting.exists(), (r.returncode, r.stderr)
        r = run([launcher, "--set-install", tmp], env)  # a folder without Builtin/
        assert r.returncode == 1 and "Builtin" in r.stderr and not setting.exists(), (r.returncode, r.stderr)
        r = run([launcher, "--set-install", str(data)], env)
        assert r.returncode == 0 and setting.read_text().strip() == str(data.resolve()), (r.returncode, r.stderr)
        made = sorted(str(p.relative_to(tmp)) for p in Path(tmp).rglob("*"))
        assert made == ["data", "data/trainz-native", "data/trainz-native/install-dir"], made  # nothing else

        # A name no session has: falls back to the routes, then draws (offscreen) and quits.
        r = run([launcher, "springfield", "--radius", "200", "--frames", "2"], env)
        if r.returncode != 0 and "SDL_Init failed" in r.stderr and "not available" in r.stderr:
            print("skipped: SDL has no offscreen video driver")
            return 77
        assert r.returncode == 0 and "route: Springfield Industrial District" in r.stdout, (r.returncode, r.stdout, r.stderr)
        assert "trying the routes" in r.stderr, r.stderr
        r = run([launcher, "no such thing"], env)  # nothing anywhere: the route lookup's "no match"
        assert r.returncode == 3, (r.returncode, r.stderr)
        # No name: the list, a question, and an empty answer quits.
        r = run([launcher], env, stdin="\n")
        assert r.returncode == 0 and "Springfield Industrial District" in r.stdout and "Type a session" in r.stdout, r.stdout
        # TRAINZ_INSTALL wins over the remembered folder.
        env2 = dict(env, TRAINZ_INSTALL=str(Path(tmp) / "missing"))
        r = run([launcher, "springfield"], env2)
        assert r.returncode == 1, (r.returncode, r.stderr)
    assert (archive.stat().st_mtime_ns, archive.stat().st_size) == before
    print("launcher checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
