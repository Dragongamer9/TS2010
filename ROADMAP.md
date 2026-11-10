# Roadmap – Trainz Simulator 2010 on Linux (Hearth OS)

**Overall progress: 6%**

`[█░░░░░░░░░░░░░░░░░░░]`

*Last updated: 2026-11-10, after step 2 (asset configs) and the KUID folder-rule trace.*

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
| 3 | Asset index (KUID → where the asset lives) | 4 | 0% | Claude | Next. Covers built-in `.ja` folders, `UserData/local`, `.cdp` packages. Version and priority rules are unknown. |
| 4 | Textures: `.texture` (`MMTF`), `.tga`, `.bmp`, `.jpg` | 4 | 0% | Claude | Container known; pixel-format field unknown. |
| 5 | 3D models: `.im` / `.pm` (`JIRF`/`IDXM`) + a model viewer | 8 | 0% | Claude | Container known; `GEOM` vertex layout unknown. |
| 6 | Materials and shaders (`m.onetex` …, `vtx.txt`/`pxl.txt`) | 6 | 0% | Claude | Map to modern shaders; Cg is discontinued. |
| 7 | Animation: `.kin` (`ANIM`) | 4 | 0% | Claude | Container known. |
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

**Overall** = Σ(weight × done) ÷ Σ(weight) = (2×0.65 + 3×0.80 + 3×0.75) ÷ 102 =
5.95 ÷ 102 = 5.8% → **6%**. The parts Claude doesn't write (steps 12–15) carry 41 of the
102 weight points.

---

## What a "milestone done" means

A milestone reaches **100%** only when **all** of these are true:
1. The code is merged (by you).
2. CI is green on gcc and clang, with sanitizers.
3. The format doc says what is traced, checked, observed and unknown.
4. It has been run read-only against Dad's real install.

Until then it stays below 100%, so steps 1 and 2 are at 80% and 75% and not 100%.

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
| 2026-11-10 | 6% | Step 0 (60% → 65%): traced the plain/kuid2 folder rule in `TADDaemon.exe` (`0x51919a`). This fixed one edge case: a negative user with version 0. Engine-map citation for the `.chump` reader corrected (ChatGPT review). Roadmap created. |
| 2026-11-10 | 6% | Step 2 (0% → 75%): `config.txt` / `config.chump` reader, `config-tool`, 74 passing tests + 5 real-install tests (skipped in CI). |
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
