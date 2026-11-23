---
name: local-build-setup
description: "How to build/test the TS2010 port on this PC without cmake, and which flags CI uses"
metadata:
  node_type: memory
  type: project
  originSessionId: 1b777294-c3e9-42f7-bedb-daf517e8de46
  modified: 2026-11-11T08:18:56.141Z
---

This PC has g++ but no cmake. Local builds are done by hand: compile the `add_library(trainz_content ...)` sources from CMakeLists.txt plus tests/tools with g++, gtest unpacked at /var/tmp/gtestdeb/root/usr (libgtest.a, libgtest_main.a). Real game data: install at /var/tmp/trs_install, built-in archives at /var/tmp/trs_builtin/app/Builtin; render outputs go via XDG_DATA_HOME=/var/tmp/shots/xdgN. PNG compression: /var/tmp/obsscripts/shrink.py IN OUT 1.

**Why:** a local build without CI's warning set passed, then CI failed on -Wshadow (2026-11-11).

**How to apply:** compile locally with CI's flags: `-std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual -Werror` (test_real_install.cpp gives a false libstdc++ regex warning at -O1 with sanitizers; build that one without -Werror locally).

Source list for the hand build: `sed -n 31,75p CMakeLists.txt | grep -o "src/[A-Za-z0-9_/]*\.cpp"` (include digits, or `Mp3Decoder.cpp` is dropped and linking fails). `/tmp` is cleared between days, so object folders `/tmp/tzt` and `/tmp/tzb` may need a full rebuild.

Also run the CLI tests before pushing (plain `python3 -I tests/test_<name>_cli.py BUILT_TOOL "$PWD/testdata/ts2010"`, with `SDL_VIDEODRIVER=offscreen` for route-viewer): CI failed on 2026-11-13 because only the gtest suite had been run locally.
