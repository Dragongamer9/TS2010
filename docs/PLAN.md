# Plan: Trainz Simulator 2010 on Linux, OpenMW-style

**Goal (the owner's):** a native Linux remake of TS2010 for the owner and his dad (private,
never shared) that loads and plays the original game's own files from their install, the way
OpenMW plays Morrowind's: **our engine, their data.** The original files are the source of
routes, trains, scenery, textures, sounds and sessions; they are only read, never changed,
run or patched. The original program (`trainz.exe` and its DLLs) is studied, never run.

## Where things stand (2026-11-12)

| Area | State | Evidence |
|---|---|---|
| Archives (`.ja`) | done | every entry of every disc archive reads; traced in `KernelStandard_7c.dll` ([formats/ja.md](formats/ja.md)) |
| Asset configs, KUIDs, the asset index | done | all 19,282 assets index, KUIDs match folders ([asset-index.md](formats/asset-index.md)) |
| Textures, images | done | all `.texture` files decode; descriptors traced |
| Meshes (`.im`/`.pm`), attachment points, LOD lists | done | all parts and all 5,868 `ATCH` chunks decode |
| Animation (`.kin`) | read, not yet played in the view | |
| Terrain (`mapfile.gnd`) | done (heights, textures); texture scale/rotation byte not decoded | all 15,243 sections end exactly; section data traced |
| Track (`.trk`), its curves | read; curves are **a model** checked against session trains (97% within 0.5 m), not traced | [route-objects.md](formats/route-objects.md) |
| Placed objects (`.obs`) | positions and yaw read; class payloads mostly not | 1.76 M objects |
| Sessions (`profile.dat`) | trains and behaviours read (823 of 826 old trains, all 655 new) | [session.md](formats/session.md) |
| Sounds (WAV, MP3) | decode; not yet played in the world | |
| Key bindings (`keyboard.txt`) | traced parser | |
| Materials | kinds surveyed; alpha meaning documented | [mesh.md](formats/mesh.md) |
| TrainzScript (`.gs`/`.gsl`/`.gse`) | **not started** (the biggest unknown) | |
| Simulation (physics, signals, AI) | the game's own **not mapped**; ours started (`src/sim`) | [ENGINE-MAP.md](ENGINE-MAP.md) |

**First goal reached (2026-11-12): load one route and drive one train.**
`route-viewer --install DIR --session "Midday Manifest" --drive` drives that session's first
train (31 vehicles, each with its own model and bogies) on its own route, with throttle, brake
and reverser. Since then each vehicle's mass comes from its config and the locomotive's pulling
force from its own `enginespec` curve; resistance, brakes and adhesion are still presets.
Other limits: junctions always take the straightest way, no signals, no sound, software
rendering at about 6 frames a second.

## Engine: stay with C++ (decided), move drawing to the GPU next

The engine is already C++20 with its own readers, renderer, track model, physics, input,
launcher and Debian package, all tested in CI on the real data. Recommendation:

- **Keep C++** rather than moving to Godot: everything that reads the original files is C++
  and tested; Godot would need it all wrapped as GDExtension and would add a large engine we
  don't control, for features (an editor, a scene system) the remake doesn't need first.
- **Next infrastructure step: a GPU renderer** (OpenGL 3.3 core first, as the R9 390 and Mesa
  handle it well and `model-viewer` already uses OpenGL; Vulkan later only if needed). The
  software renderer stays for tests and screenshots.

## What to decode next, in order

1. **The train's own physics data**: `enginespec` assets and traincar config keys (mass,
   traction, brakes), so trains drive like themselves. **Started:** `mass` and the
   `throttle-power` curves are read and used (`src/content/EngineSpec`). Left: the throttle's
   own notches, `dynamic-brake`, the air-brake keys (`pressure/*`, `brakeratio`), what the
   `motor` numbers mean, and why the curves look ~2.5× too strong if they are kN.
2. **Junctions and signals in the route** (`mojunction` and signal objects in `.obs`):
   switch settings and signal states, then a simple signal system. **Junctions started:**
   settings read from the track vertices and switchable while driving (J). **Signals
   started:** placed from the route (piece, distance, direction) with simple stop/caution/clear
   blocks. Left: what the `jnOb` fields mean, a session's own junction settings, the signals'
   own aspects and lights.
3. **Sounds in the world**: engine and wheel sounds from the traincar assets (`soundscript`
   configs), positioned in 3D.
4. **Cab views**: interior meshes and their controls (attachment points are already read).
5. **The rest of `profile.dat`** (driver commands, rules' settings) and the session's goals.
6. **TrainzScript**: the scripts behind rules, industries and some trains: the hardest part
   (a compiler/VM; `.gse` may be encrypted). Started only once the above work.

## Team split for the first pass (2026-11-12)

- Install map (every file type, role, format, evidence, how content is stored and indexed):
  consolidated from the format docs into [INSTALL-MAP.md](INSTALL-MAP.md).
- The main program's engine parts and how it loads content: [ENGINE-MAP.md](ENGINE-MAP.md)
  (loading, archives, configs, textures, meshes, terrain are traced; simulation, signals,
  sound mixing, saves and TrainzScript are not; function-level work needs a decompiler that
  runs here: REA needs Ghidra 12.1.x, this PC has 12.2).
- Public knowledge of the formats and tools: [research/public-formats.md](research/public-formats.md).
- This plan.

## Rules

Dad's game data is never written; everything the port makes goes under
`~/.local/share/trainz-native/`. The repo is private; game files in it are for testing the
readers only. Version numbers are the owner's.
