---
name: silent-test-runs
description: "Never let route-viewer test runs play sound through the owner's speakers"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 1b777294-c3e9-42f7-bedb-daf517e8de46
  modified: 2026-11-14T06:50:09.807Z
---

Test runs of route-viewer must be silent: always run them with `SDL_AUDIODRIVER=dummy` (as well as `SDL_VIDEODRIVER=offscreen`), and use `--record-sound` to check audio as a file. Since 2026-11-14 the viewer itself keeps `--frames` runs silent unless `--sound` is passed, and has `--mute`.

**Why:** the owner heard the train through his speakers during my offscreen runs and asked me to mute it (2026-11-14).

**How to apply:** every route-viewer command I run gets `SDL_AUDIODRIVER=dummy`; never pass `--sound` in my own runs. Related: [[local-build-setup]].
