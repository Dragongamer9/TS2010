# Roadmap – Trainz Simulator 2010 on Linux (Hearth OS)

**Overall progress: 28%**

`[█████░░░░░░░░░░░░░░░]`

*Last updated: 2026-11-10, after the `mapfile.obs` / `.trk` container reader (step 8).*

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
| 1 | `.ja` archive reader + `ja-tool` | 3 | 90% | Claude | Done in C++, CI green, format in [`docs/formats/ja.md`](docs/formats/ja.md). **Real data:** every entry of `Base.ja` and `Data.ja` decoded in CI. **Missing:** a full pass over `Builtin\1.ja`–`8.ja` on Dad's PC. **Whole disc:** every config/texture/mesh/animation/terrain entry in all 8 `Builtin` archives decoded (Python survey, [`docs/DISC-SURVEY.md`](docs/DISC-SURVEY.md)). |
| 2 | Asset configs: `config.txt` + `config.chump` + KUIDs | 3 | 85% | Claude | Done in C++, CI green, format in [`docs/formats/config.md`](docs/formats/config.md). **Real data:** 1,639 configs parse in CI, all 1,468 asset KUIDs match their folders. **Missing:** tracing the text grammar in `trainz.exe`; `Builtin\1.ja`–`7.ja`. **Whole disc:** all 19,112 `.chump` + 70 `config.txt` parse (one grammar fix: value on the next line). |
| 3 | Asset index (KUID → where the asset lives) + `asset-tool` | 4 | 60% | Claude | Written in C++ ([`docs/formats/asset-index.md`](docs/formats/asset-index.md)). **Real data:** 1,626 assets indexed in CI, no KUID warnings. **Missing:** `.cdp` packages, which copy/version the game picks (not traced), an on-disk cache, a run on Dad's full install. |
| 4 | Textures: `.texture` (`MMTF`), `.tga`, `.bmp`, `.jpg` + `texture-tool` | 4 | 85% | Claude | `.texture` done in C++ ([`docs/formats/texture.md`](docs/formats/texture.md)): ARGB8888, RGB0888, DXT1/3/5, mip chain, PNG export. **`.tga`/`.bmp`/`.jpg`** done in C++ ([`docs/formats/image.md`](docs/formats/image.md)): TGA types 1/2/3/10 (+9/11), BMP 4/8/24/32 bpp + RLE8, JPEG baseline/progressive via libjpeg (optional); `texture-tool png` takes them. **Real data:** every texture in the test data parses in CI; all 1,276 test-data images decode (C++, ASan, run by hand). `.texture.txt` descriptors: keys/values/`Tile` traced in `RenderSystem_7c.dll`, all 360 disc descriptors resolve and combine ([`docs/formats/texture-descriptor.md`](docs/formats/texture-descriptor.md)). **Missing:** how the game itself loads images and uses alpha (not traced). **Whole disc:** all 89,524 textures fit (INFO version 262 found); of 20,544 images, 20,540 decode in C++ and 4 damaged ones are refused. |
| 5 | 3D models: `.im` / `.pm` (`JIRF`/`IDXM`) + a model viewer | 8 | 60% | Claude | `GEOM` (v101/102/200/201) and `MATL` (v102/103) worked out from data; C++ reader, `mesh-tool` ([`docs/formats/mesh.md`](docs/formats/mesh.md)) and a **GPU `model-viewer`** (SDL2 + OpenGL 2.1, textures, `.kin` playback). **Real data:** 291 meshes / 1,438 parts parse in CI. **Missing:** the viewer has **not been run on a screen yet**; `INFO`/`INFL`/`ATCH`/`SKEL`/`PMIN`/`NINF`; tracing the reader code. **Whole disc:** all materials fit once UTF-16 text is read; 65 meshes look damaged by a text-mode transfer before the disc was made; `repairTextModeDamage` turns them into consistent files. |
| 6 | Materials and shaders (`m.onetex` …, `vtx.txt`/`pxl.txt`) | 6 | 0% | Claude | Map to modern shaders; Cg is discontinued. |
| 7 | Animation: `.kin` (`ANIM`) + `anim-tool` | 4 | 60% | Claude | `INFO`/`EVNT`/`SKEL`/`FRAM` worked out from data; C++ reader, posing in `mesh-tool preview --kin` ([`docs/formats/animation.md`](docs/formats/animation.md)). **Real data:** parses in CI. Playback is **implemented in `model-viewer`, awaiting a run on screen**. **Missing:** whether bone positions are absolute or parent-relative, what events do. **Whole disc:** all 2,629 animations fit. |
| 8 | Routes: `mapfile.gnd` / `.trk` / `.obs` / `.bmk` + a route viewer | 10 | 40% | Claude (viewer) | `mapfile.gnd` header, index, **world block** (environment 0x463dd0, version extras 0x6d2d6c, object container 0x886be0) and **section data traced** in `trainz.exe` (loader 0x6d2aa0, section readers 0x82be00/0x821200, vertex 0x6f0e50): 250 texture slots with KUIDs, up to 36×36 cells, 76×76 (Secs) / 148×148 (Sec5) vertices with up to 3 texture layers and a height; C++ `parseGroundSection`, `route-tool info` ([`docs/formats/route.md`](docs/formats/route.md)). **Whole disc:** all 15,243 sections of the 50 routes decode to exactly their end offset, and every world block ends exactly where the first section starts (Python checker and C++ reader). `mapfile.obs` / `.trk`: the object container is traced and every disc file but one old route reads to its end (170 of 172 files, 2.5 M objects; [`docs/formats/route-objects.md`](docs/formats/route-objects.md)). **Missing:** each object class's payload, what the world values mean (lighting? `Rule` payloads), container version 2 (one old route), what the vertex `extra` byte and cell value mean, grid spacing/orientation, `.bmk`, the viewer. |
| 9 | Sound: WAV/MP3 playback, 3D positioning | 4 | 50% | Claude (decoding/playback layer) | WAV reader (PCM 8/16-bit, MS ADPCM, MP3 inside WAV) and MP3 via libmpg123 (optional at build time): every sound on the disc decodes except one `.wav` that is really DVD-style MPEG-2 video with AC-3 audio (refused). All 6 MP3s (3 `.mp3` in `Base.ja`, 3 MP3-in-WAV in `Builtin\4.ja`/`7.ja`) match `ffmpeg` within 1 LSB. `sound-tool` export and an SDL2 `sound-player` ([`docs/formats/sound.md`](docs/formats/sound.md)). **Missing:** playback heard on a real machine, `soundscript`/3D positioning/looping. |
| 10 | Input: keyboard, mouse, joystick (SDL) + `keyboard.txt` bindings | 2 | 60% | Claude (device layer) | `keyboard.txt` reader in C++ **matching the traced parser** (`trainz.exe` 0x4d5bb0/0x4d4b60: 141 key names, word-by-word rules), key-press → action lookup, SDL scancode mapping, `input-tool` and an SDL `key-tester` ([`docs/formats/keyboard.md`](docs/formats/keyboard.md)); **Real data:** the shipped file reads in full (6 groups, 383 actions, every key name known). **Missing:** `key-tester` run on a screen, picking between actions that share a key (by mode), mouse, joystick, the live DIK → engine-code conversion (codes are traced and position-ordered; the conversion is not found). |
| 11 | Sessions: `profile.dat` / `.obs` / `.trk` / `.lyr` | 4 | 0% | Claude (reader) | |
| 12 | TrainzScript: compiler, VM, native bindings (`.gs`/`.gsl`/`.gse`) | 15 | 0% | owner / other | The biggest, riskiest piece. `.gse` may be encrypted. |
| 13 | Train simulation: physics, brakes, coupling, signals, AI drivers | 15 | 0% | owner / other | **Not mapped at all yet**; it's in `trainz.exe` with no symbols. |
| 14 | Game UI (menus, Surveyor, driver HUD) and the in-game HTML pages | 8 | 0% | owner / other | The game uses an embedded Gecko browser for its pages. |
| 15 | Networking / Download Station / iTrainz chat | 3 | 0% | owner / other | Optional; 2010 servers probably gone (not checked). |
| 16 | Packaging and launcher for Hearth OS (menu entry, data folder, settings) | 3 | 0% | Claude | Must keep any previous version startable. |
| 17 | Validation on Dad's PC (real-install tests, full archive verify, speed on the hard drive) | 4 | 40% | Claude + owner | **Started:** real game data in the private repo (`testdata/ts2010`), all `RealInstall.*` tests run in CI with a full decode. **Missing:** the same tests on Dad's full install, speed on his hard drive. **Whole disc surveyed** (170,167 Builtin entries plus Base/Data scans; [`docs/DISC-SURVEY.md`](docs/DISC-SURVEY.md)). |
| | **Total** | **102** | | | |

**Overall** = Σ(weight × done) ÷ Σ(weight) = (2×0.65 + 3×0.90 + 3×0.85 + 4×0.60 + 4×0.85 + 8×0.60 + 4×0.60 + 10×0.40 + 4×0.50 + 2×0.60 + 4×0.40) ÷ 102 =
28.35 ÷ 102 = 27.79% → **28%**. The parts Claude doesn't write (steps 12–15) carry 41 of the
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
| 2026-11-10 | 28% | Step 8 (35% → 40%): `mapfile.obs` / `mapfile.trk` read as the game's object container (traced, `trainz.exe` 0x886be0; now shared with the `.gnd` world reader). 39 class registrations traced (factory 0x807eb0) for tag names. Whole disc: 170 of 172 files read to their exact end (2,539,231 objects; Python and C++ agree); the 2 others are the old robescenario route (container v2). Object payloads not decoded yet. Overall 27.85 → 28.35 ÷ 102 = 27.79% → 28%. |
| 2026-11-10 | 27% | Step 4 (75% → 85%): `.texture.txt` reader. Keys, value words with their engine numbers, and the `Tile` letter rule traced in `RenderSystem_7c.dll` (~0x10005aa0; tables 0x1006ca78); Primary/Alpha combining from the official wiki (not traced). All 360 disc descriptors (all in `Base.ja`) resolve, decode and combine; `texture-tool info/png` read them. Overall 27.45 → 27.85 ÷ 102 = 27.30% → 27%. |
| 2026-11-10 | 27% | Step 8 (30% → 35%): the `mapfile.gnd` world block (environment keys, version extras, object container) is traced and decoded; it ends exactly at the first section on all 50 disc routes (route helper, merged earlier but not counted). README and the step 8 row no longer call it undecoded; MP3/JPEG are built only when their CMake option is ON **and** the library is found (ChatGPT review). Decoder review (the part Gemini didn't finish): MP3 and image decoders hold up; MS ADPCM had two signed-overflow bugs a crafted WAV could hit (predictor sum, uncapped step size), now fixed (64-bit predictor, ffmpeg-style cap) with regression tests. Overall 26.95 → 27.45 ÷ 102 = 26.91% → 27%. |
| 2026-11-10 | 26% | **Helpers merged** into this branch: images (step 4, 60% → 75%), route sections + world block (step 8, 15% → 30%), MP3 (step 9, 40% → 50%). The helpers' own rows below each used the old total; combined: 24.45 + 4×0.15 + 10×0.15 + 4×0.10 = 26.95 ÷ 102 = 26.42% → 26%. Temporary helper worktrees are no longer tracked (ChatGPT review). |
| 2026-11-10 | 25% | Step 4 (60% → 75%): `.tga` / `.bmp` / `.jpg` loaders (`src/content/ImageFile`, [`docs/formats/image.md`](docs/formats/image.md)), `texture-tool png` for them, JPEG through Debian's `libjpeg62-turbo-dev` (optional in CMake, added to CI). Survey of all 8 `Builtin` archives + Base/Data: 20,544 images (11,640 TGA, 2,687 BMP, 6,217 JPEG, no `.jpeg`); TGA types 2/10/1/3 at 16/24/32 bpp, both origins; BMP 4/8/24/32 bpp and 33 RLE8; JPEG 5,518 baseline + 699 progressive. The C++ decoders, run by hand with ASan/UBSan (not yet in CI), decode 20,540; 4 damaged files are refused (a TGA with a 32 bpp header and 24 bpp data, 3 JPEGs cut off mid-scan). Found 165 TGAs whose alpha is 0 everywhere, so alpha use must come from the material. Tests: 37 new unit tests + `RealInstall.LooseImagesDecode`. Overall 24.45 → 25.05 ÷ 102 = 24.56% → 25%. |
| 2026-11-10 | 25% | Step 8 (15% → 30%): traced the `mapfile.gnd` loaders in `trainz.exe` with objdump (file 0x6d2aa0, Secs/Sec5 section data 0x82be00/0x821200, vertex 0x6f0e50, overview 0x6e4070). Byte 3 is a version (13); index entries are `{x, y, tag, start, end}`, so the earlier "two header words" were section 0's position and the "duplicate positions" were a grouping mistake. Section data = 250 texture slots (weight, KUID when > 0), cells, then 76×76 or 148×148 vertices (`0xFE`/`0xFD` short forms or a texture list, height float). **All 15,243 sections of the 50 disc routes decode to exactly their end** (plus 623 sections in two older `.gnd` files, Python only); C++ `parseGroundSection`, tests, `tools/dev/gnd_sections.py`. Height/weight meanings are labelled guesses. Overall 24.45 → 25.95 ÷ 102 = 25.44% → 25%. |
| 2026-11-10 | 24% | Step 9 (40% → 50%): MP3 decoding with libmpg123 (fed from memory, 16-bit output, 256 MiB output cap, optional in CMake via pkg-config; `pkg-config libmpg123-dev` added to CI and the README). `decodeWav` handles MP3 inside a WAV (tag 85) and a bare MP3 stream named `.wav`; new `decodeMp3`/`decodeSoundFile`; `sound-tool` and `sound-player` take `.mp3`. Disc inventory: 3 `.mp3` (`Base.ja` squelch sounds, 22,050 Hz stereo) + 3 MP3-in-WAV (`bear.wav` 11,025 Hz mono, two identical `foghorn.wav` 24,000 Hz mono); all 6 decode and match `ffmpeg` within 1 LSB. The 4th refused WAV (`idle 1.wav`) turned out to be MPEG-2 video + AC-3 audio, not MPEG audio, so it stays refused. Unit tests use hand-built silent MPEG-1 Layer III frames (`tests/test_mp3.cpp`); `RealInstall.SoundsDecode` now expects MP3 to decode when built with mpg123 and passes on the full `Builtin\1.ja`–`8.ja`. Built and run by hand with g++ + ASan/UBSan (no cmake on this PC); CI not yet run on it. Overall 24.45 → 24.85 ÷ 102 = 24.36% → 24%. |
| 2026-11-10 | 24% | Step 10 (50% → 60%): traced the game's key-binding parser (`trainz.exe` VA 0x4d5bb0 splitter, 0x4d4b60 word parser): 141 key names with their engine codes, `tolower`, `,`/space splitting, words applied in order, unknown words clear the combo. The reader now matches it exactly (dropped 27 names it never accepted: 24 display-table names such as `page up`, `kp/`, `.`, `~`, plus `f13`–`f15`). `camera_car` = ctrl+alt+shift+Z is now **traced**, no longer a guess. Engine codes are `DIK_`+1 for 80 of 98 keys, the rest in `DIK_` order: position-based numbering (the live conversion is not found). Shared-key mode choice and physical-key mapping labelled as inferences (ChatGPT review). Overall 24.25 → 24.45 ÷ 102 = 23.97% → 24%. |
| 2026-11-10 | 24% | Step 10 (30% → 50%): key-press → action lookup (`ActionMap`, exact modifiers; the port's rule, not traced), SDL scancode mapping (every name round-trips), `input-tool list/shared/press`, SDL `key-tester` (built, not yet run on a screen). Found 52 shared combos in the shipped file (e.g. `W` = throttle, DCC and regulator up); the names suggest the game picks by mode, which is an inference, not traced. Scancode (physical-key) binding is the port's choice; the game's own name-to-key mapping is not traced (correction from ChatGPT review). Overall 23.85 → 24.25 ÷ 102 = 23.77% → 24%. |
| 2026-11-10 | 23% | Step 10 (0% → 30%): `keyboard.txt` reader (`src/input/KeyBindings`), key names from the game's table in `trainz.exe`; the shipped file reads in full (383 actions); for the odd `camera_car  alt ctrl shift c a r z` line it binds `ctrl+alt+shift+Z` and ignores `c a r` with a warning; that reading is the port's **unconfirmed guess**, not the game's traced behaviour. `sound-player` now builds whenever SDL2 is present (ChatGPT review). Overall 23.25 → 23.85 ÷ 102 = 23.38%, still 23%. |
| 2026-11-10 | 23% | Step 9 (0% → 40%): WAV reader (PCM + MS ADPCM), `sound-tool`, SDL2 `sound-player`; under the same rules in Python, 5,870 of the 5,874 disc WAVs decode and 4 are refused as unsupported (3 MP3-in-WAV, 1 MPEG stream). Survey docs corrected (15,243 sections, 170,167 Builtin entries; repair described as likely, not proven; direct apt on Hearth OS). |
| 2026-11-10 | 21% | **Whole-disc survey** of all 8 `Builtin` archives + Base/Data (170,167 entries in `Builtin\1.ja`–`8.ja`, plus Base/Data scans; [`docs/DISC-SURVEY.md`](docs/DISC-SURVEY.md)): found UTF-16 material text, texture INFO 0x106, a next-line config string, and 65 meshes that look damaged by a text-mode transfer (repair to a consistent file added). Steps 1→90%, 2→85%, 4→60%, 5→60%, 7→60%, 8→15%, 17→40%. Viewer honours `TRAINZ_WERROR` (ChatGPT review). |
| 2026-11-10 | 19% | Step 5 (50% → 55%): `model-viewer` (SDL2 + OpenGL 2.1): textured meshes, mouse orbit/zoom, `.kin` playback; built in CI but not yet run on a screen. CI report step now keeps the test exit status; ENGINE-MAP updated for the full Base/Data decode and GEOM v101 (ChatGPT review). |
| 2026-11-10 | 19% | First C++ run on **real game data** (repo now private): `Base.ja`, `Data.ja` and a real subset of `8.ja` in `testdata/ts2010`; all 10 `RealInstall.*` tests pass in CI with a full decode. It found GEOM v101 (fixed). Steps 1→85%, 2→80%, 3→60%, 4→55%, 5→50%, 7→55%, 8→12%, 17→25%. |
| 2026-11-10 | 16% | Step 8 (0% → 10%): `mapfile.gnd` outer structure (20 sections, offsets, overview maps) checked on the disc route; `route-tool info/overview`. Section contents need the `trainz.exe` loaders traced. Step 7 review fix: `poseMesh` validates pose counts before changing anything and documents in-place behaviour. |
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

- **C++ not yet run on `Builtin\1.ja`–`8.ja` in full.** The Python survey
  covered the whole disc; the C++ readers run in CI on `Base.ja`, `Data.ja`
  and a subset of `8.ja`. Run the `RealInstall` tests on Dad's install (or here,
  if builds are allowed on this PC).
- **No decompiler for REA on this PC.** It needs Ghidra 12.1.x. Without it,
  code inside `trainz.exe` can only be read as disassembly.
- **Keep the repo private.** It now holds N3V's game files (`testdata/`).
