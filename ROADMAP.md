# Roadmap – Trainz Simulator 2010 on Linux (Hearth OS)

**Overall progress: 15%**

`[███░░░░░░░░░░░░░░░░░]`

*Last updated: 2026-11-10, after step 7 (animation format).*

This file is updated with every change that moves the project forward. The
percentage is a **weighted estimate**, not a measurement: see
[How the percentage works](#how-the-percentage-works).

**Target:** your dad's PC.
- Hearth OS (Debian 13, x86_64), AMD A10 Richland (4 cores), 32 GB RAM, a
  hard drive.
- Radeon R9 390 now, maybe an RTX 3060/3050 later.
- Keyboard and mouse.

**Ground rules:**
- His game files are only ever read.
- Everything the port makes goes in `~/.local/share/trainz-native/`.

**Who writes what:**
- **Claude:** file-format readers, viewers, converters, tests, CI, tools and
  packaging, all marked **Claude** below.
- **Not Claude:** the parts that rebuild the game itself: the TrainzScript
  engine, the train simulation and the game's UI. They're on the list so the
  plan is complete, marked **owner / other**.

---

## Milestones

| # | Milestone | Weight | Done | Who | Status |
|---:|---|---:|---:|---|---|
| 0 | Analysis: engine map, disc contents, tooling | 2 | 65% | Claude | Engine map written ([`docs/ENGINE-MAP.md`](docs/ENGINE-MAP.md)). Function-level names inside `trainz.exe` still need a working decompiler (REA needs Ghidra 12.1.x; this PC has 12.2). |
| 1 | `.ja` archive reader + `ja-tool` | 3 | 80% | Claude | Done in C++, CI green, format in [`docs/formats/ja.md`](docs/formats/ja.md). **Missing:** a run on real game files and a full decode of every entry. |
| 2 | Asset configs: `config.txt` + `config.chump` + KUIDs | 3 | 75% | Claude | Done in C++, CI green, format in [`docs/formats/config.md`](docs/formats/config.md). The plain/kuid2 folder rule is traced. **Missing:** a C++ run on real files; tracing the text grammar in `trainz.exe`. |
| 3 | Asset index (KUID → where the asset lives) + `asset-tool` | 4 | 55% | Claude | Written in C++ ([`docs/formats/asset-index.md`](docs/formats/asset-index.md)): Base/Data/Builtin `.ja` + `UserData/local`, sub-parts skipped, all copies and versions reported. **Missing:** `.cdp` packages, which copy/version the game picks (not traced), an on-disk cache, a real-install run. |
| 4 | Textures: `.texture` (`MMTF`), `.tga`, `.bmp`, `.jpg` + `texture-tool` | 4 | 50% | Claude | `.texture` done in C++ ([`docs/formats/texture.md`](docs/formats/texture.md)): all 5 formats in use (ARGB8888, RGB0888, DXT1/3/5), mip chain, PNG export; checked on all 6,666 disc textures. **Missing:** `.texture.txt`, `.tga`/`.bmp`/`.jpg`, a real-install C++ run. |
| 5 | 3D models: `.im` / `.pm` (`JIRF`/`IDXM`) + a model viewer | 8 | 45% | Claude | `GEOM` (v102/200/201) and `MATL` (v102/103) layouts worked out from data and checked on all 7,982 parts; C++ reader, `mesh-tool` (info, software preview PNG, OBJ+MTL+PNG export) ([`docs/formats/mesh.md`](docs/formats/mesh.md)). **Missing:** a GPU viewer, `INFO`/`INFL`/`ATCH`/`SKEL`/`PMIN`/`NINF`, tracing the reader code, a real-install C++ run. |
| 6 | Materials and shaders (`m.onetex` …, `vtx.txt`/`pxl.txt`) | 6 | 0% | Claude | Map to modern shaders; Cg is discontinued. |
| 7 | Animation: `.kin` (`ANIM`) + `anim-tool` | 4 | 50% | Claude | `INFO`/`EVNT`/`SKEL`/`FRAM` worked out from data, all 135 disc files fit; C++ reader, posing in `mesh-tool preview --kin` ([`docs/formats/animation.md`](docs/formats/animation.md)). **Missing:** whether bone positions are absolute or parent-relative (absolute looks right, not confirmed), playback, what events do, a real-install C++ run. |
| 8 | Routes: `mapfile.gnd` / `.trk` / `.obs` / `.bmk` + a route viewer | 10 | 0% | Claude (viewer) | Headers known; inner layouts unknown. |
| 9 | Sound: WAV/MP3 playback, 3D positioning | 4 | 0% | Claude (decoding/playback layer) | |
| 10 | Input: keyboard, mouse, joystick (SDL) + `keyboard.txt` bindings | 2 | 0% | Claude (device layer) | |
| 11 | Sessions: `profile.dat` / `.obs` / `.trk` / `.lyr` | 4 | 0% | Claude (reader) | |
| 12 | TrainzScript: compiler, VM, native bindings (`.gs`/`.gsl`/`.gse`) | 15 | 0% | owner / other | The biggest, riskiest piece. `.gse` may be encrypted. |
| 13 | Train simulation: physics, brakes, coupling, signals, AI drivers | 15 | 0% | owner / other | **Not mapped at all yet**; it's in `trainz.exe` with no symbols. |
| 14 | Game UI (menus, Surveyor, driver HUD) and the in-game HTML pages | 8 | 0% | owner / other | The game uses an embedded Gecko browser for its pages. |
| 15 | Networking / Download Station / iTrainz chat | 3 | 0% | owner / other | Optional; 2010 servers probably gone (not checked). |
| 16 | Packaging and launcher for Hearth OS (menu entry, data folder, settings) | 3 | 0% | Claude | Must keep any previous version startable. |
| 17 | Validation on Dad's PC (real-install tests, full archive verify, speed on the hard drive) | 4 | 0% | Claude + owner | Needs the tests run with `TRAINZ_DATA_DIR` on his PC. |
| | **Total** | **102** | | | |

**Overall** = Σ(weight × done) ÷ Σ(weight) = (2×0.65 + 3×0.80 + 3×0.75 + 4×0.55 + 4×0.50 + 8×0.45 + 4×0.50) ÷ 102 =
15.75 ÷ 102 = 15.44% → **15%**. The parts Claude doesn't write (steps 12–15) carry 41 of the
102 weight points.

---

## What a "milestone done" means

A milestone reaches **100%** only when **all** of these are true:
1. The code is merged (by you).
2. CI is green on gcc and clang, with sanitizers.
3. The format doc says what is traced, checked, observed and unknown.
4. It has been run read-only against Dad's real install.

Until then it stays below 100%, so none of steps 1–7 is at 100% yet.

## How the percentage works

- **Weight:** a rough size estimate per milestone (bigger = more work). The
  weights add up to about 100, so 1 point ≈ 1% of the whole port.
- **Done:** an honest estimate per milestone of how much of it is finished.
- **Overall:** weight × done, added up and divided by the total weight.
- Weights may be adjusted as we learn more; every change is noted in the log below.

---

## Progress log

Newest first. Each entry says what moved and why. The entries before the
roadmap existed were filled in afterwards from the chat history.

| Date | Overall | Change |
|---|---:|---|
| 2026-11-10 | 15% | Step 7 (0% → 50%): `.kin` reader (`anim-tool`, mesh posing); all 135 disc animations fit; bone-position space rendered both ways (absolute looks right, not confirmed). Preview hardened against overflowing intermediate maths (ChatGPT review). |
| 2026-11-10 | 13% | Step 5 (0% → 45%): mesh reader (`GEOM`/`MATL` worked out from data, all 7,982 parts fit), software preview and OBJ export (`mesh-tool`); ore-car mesh rendered correctly. Step 4 review fix: texture sizes bounded before any arithmetic (no overflow). |
| 2026-11-10 | 10% | Step 4 (0% → 50%): `.texture` reader, DXT1/3/5 + 32-bit decoding, PNG export, `texture-tool`; format ids traced to the engine's name list; sizes checked on 6,666 textures, colours checked by eye. Step 3 review fixes: only valid parent assets hide sub-parts; config reads bounded to 16 MiB. |
| 2026-11-10 | 8% | Step 3 (0% → 55%): asset index + `asset-tool`; selection rule checked on the disc (8.ja: 1,468 assets, 1 sub-part skipped). |
| 2026-11-10 | 6% | Step 0 (60% → 65%): traced the plain/kuid2 folder rule in `TADDaemon.exe` (`0x51919a`). This fixed one edge case: a negative user with version 0. Engine-map citation for the `.chump` reader corrected (ChatGPT review). Roadmap created. |
| 2026-11-10 | 6% | Step 2 (0% → 75%): `config.txt` / `config.chump` reader, `config-tool`, 74 passing tests + 5 real-install tests (skipped in CI). |
| 2026-11-10 | 4% | Step 0 (50% → 60%): traced the archive name lookup, duplicate handling and `CFIL` detection in `KernelStandard_7c.dll`, and the `.chump` reader in `trainz.exe`. Overall (2×0.60 + 3×0.80) ÷ 102 = 3.5% → 4%. |
| 2026-11-10 | 3% | Step 1 (70% → 80%): extraction made symlink-safe and confined to the port folder; protected folders checked before anything is created (ChatGPT reviews). |
| 2026-11-10 | 3% | Step 1 (0% → 70%): `.ja` reader, `ja-tool`, CI on Debian 13 (gcc/clang, ASan/UBSan). |
| 2026-11-10 | 1% | Step 0 (0% → 50%): disc analysed, engine map written. |

---

## Open blockers

- **No real-data C++ run yet.** Either run the real-install tests on Dad's PC,
  or make the repo private so `Base.ja` and `Data.ja` can go in for CI.
- **No decompiler for REA on this PC.** It needs Ghidra 12.1.x. Without it,
  code inside `trainz.exe` can only be read as disassembly.
- **The repo is public.** No game files go in until it's private.
