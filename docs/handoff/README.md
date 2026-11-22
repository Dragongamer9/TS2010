# Handoff: picking the work up after the PC reset

Written by Claude on 2026-11-18, before the owner moved this PC from Debian to Fedora.
If you are Claude in a new chat, read this first, then `memory/MEMORY.md`.

## What is in this folder

| File | What it is |
|---|---|
| `memory/` | My memory notes from the old PC (rules the owner gave and how I work). Copy them into the new chat's memory folder, or just read them. |
| `chat-history.md` | The whole chat from 2026-11-10 to 2026-11-18 as readable text: every message to me (owner, teammates, the app) and every reply. Tool output is left out; each tool call is one line. The raw log was 79 MB, too big for GitHub. Made with `tools/dev/export_chat.py`. |

## The project in one paragraph

A native Linux remake of Trainz Simulator 2010 for the owner and his dad, private and never shared.
It reads the original game's own files from the install, the way OpenMW uses Morrowind's files.
The original files are only ever read: never changed, run or patched.
Everything the port writes goes under `~/.local/share/trainz-native/`. There is no Wine.
Progress, with the math, is in `ROADMAP.md`. The plan is in `docs/PLAN.md`, and how to run things is in `docs/RUNNING-VIEWERS.md`.

## Rules the owner set (still in force)

- His dad's game data is read-only.
- Version numbers are the owner's. Never pick or bump one; ask him.
- Commits only as the owner, with no AI attribution lines.
- Push only to the chat's own `hearth/` branch. Never push to main or a tag, and never merge, release or tag.
- Never claim something was pushed, tested or done unless it really was.
- Test runs are silent: `SDL_AUDIODRIVER=dummy`, and never pass `--sound`.
- Send fresh screenshots with every progress report (`docs/screenshots/`, plus a README row).
- Skip any teammate that has run out of usage.

## Where the data was on the old PC (none of it is in git)

| Old path | What it was | How to get it back |
|---|---|---|
| `/var/tmp/trs_install` | Symlinks to `testdata/ts2010/Base.ja` and `Data.ja` (both in git), plus `Builtin`. | Recreate the symlinks. |
| `/var/tmp/trs_builtin/app/Builtin/1.ja`–`8.ja` | 7.3 GB unpacked from the owner's TRS2010EE disc (`Data/setup-1a.bin` … `setup-2b.bin`) with `innoextract`. Too big for GitHub; only `testdata/ts2010/Builtin/sample8.ja` is in git. | Unpack again from the disc with `innoextract` (Fedora: `dnf install innoextract`). Put the output somewhere outside the disc and outside dad's install. |
| `/var/tmp/gtestdeb`, the SDL headers | Debian packages unpacked by hand, because there was no cmake or root. | On Fedora, install `cmake gcc-c++ gtest-devel SDL2-devel` and build normally. |
| `/var/tmp/oldtr/*.cpp`, `shrink.py`, `soundbars.py` | Survey helpers and screenshot scripts. | Already copied into `tools/dev/surveys/` and `tools/dev/`. |

The old memory note `local-build-setup.md` describes the hand g++ build on Debian.
**Update 2026-11-21:** done. The new system is a Fedora-based atomic Hearth OS with no compiler on the host, so builds run in a `toolbox` container named `trainz-build`; all 403 tests pass there. `/var` survived the move, so the game data copies in `/var/tmp` are still there; inside the container they are under `/run/host/var/tmp`. CI still builds on Debian.

## Where the work stopped

- Commit `7082f7e` holds the uncouple-engine order, routes that keep clear of other trains, and hold-and-retry when there's no clear way.
  These were **not fully tested on the old PC**: the last local test build ran out of memory.
  Check its GitHub Actions result first.
- Coal session "Wharton Coal Passive": the AI engine shunts, couples to the brake van and the cattle wagons, and uncouples.
  It reached order 12 of 88 before the last change.
  The `UnCouplez` meaning is a guess.
- Maglev session "Dejan to Clare Bay" works: train 4 reaches "D Train Stop", and train 3 waits for the player's trigger.
- ChatGPT has not yet reviewed the uncoupling and train-clearance code in `src/game/RouteDrive.cpp`, because the diff it was shown was cut off.

## Next steps, in order

1. Check CI for `7082f7e` and fix anything red.
2. Build on Fedora once and note what differs.
3. Get ChatGPT's review of the uncoupling and clearance code.
4. Continue the coal session from order 12: Navigate To industry, Load and Unload, Decouple.
5. Decide when the session's "Set Junctions" rules run.
6. Later: a GPU (OpenGL) renderer. This is the big graphics improvement the owner asked about.
