# TS2010

A private project to bring a **Trainz Simulator 2010** install to Linux
natively. It targets Hearth OS (Debian 13, x86_64). **Progress and plan:**
[`ROADMAP.md`](ROADMAP.md). Planning notes are in
[`docs/ENGINE-MAP.md`](docs/ENGINE-MAP.md).

What exists so far: a **read-only reader for the game's `.ja` content archives**
and for asset configs (`config.txt` / `config.chump`, with `config-tool`; see
[`docs/formats/config.md`](docs/formats/config.md)). The archive reader is
for `Base.ja`, `Data.ja` and `Builtin/*.ja`, and comes with the `ja-tool` command.
Both have tests. The format is described in [`docs/formats/ja.md`](docs/formats/ja.md).
On top of both, `asset-tool` lists every asset in an install and where it lives
([`docs/formats/asset-index.md`](docs/formats/asset-index.md)):

```sh
asset-tool scan     /path/to/TS2010
asset-tool find     /path/to/TS2010 "<kuid:447264:1073>"
asset-tool versions /path/to/TS2010 5:6
```

`texture-tool` shows a `.texture` and saves it as a PNG under
`~/.local/share/trainz-native/textures/` ([`docs/formats/texture.md`](docs/formats/texture.md)):

```sh
texture-tool info /path/to/TS2010/Builtin/8.ja "hash-c1/kuid 149987 21565/grass_15.texture"
texture-tool png  /path/to/TS2010/Builtin/8.ja "hash-c1/kuid 149987 21565/grass_15.texture"
```

`mesh-tool` reads `.im`/`.pm` meshes ([`docs/formats/mesh.md`](docs/formats/mesh.md)): `info`, a quick
`preview` PNG, or `obj` to export OBJ + MTL + PNG textures for Blender, all under
`~/.local/share/trainz-native/meshes/`. `model-viewer` shows a mesh with its textures in a window
(OpenGL 2.1 + SDL2; mouse drag turns it, the wheel zooms, space plays a `--kin` animation, W toggles wireframe):

```sh
mesh-tool preview /path/to/TS2010/Builtin/8.ja "hash-c0/kuid2 90361 120204 2/70ton_orecar_body/70ton_orecar_body.im"
mesh-tool obj     /path/to/TS2010/Builtin/8.ja "hash-c0/kuid2 90361 120204 2/70ton_orecar_body/70ton_orecar_body.im"
model-viewer      /path/to/TS2010/Builtin/8.ja "hash-c1/kuid 58422 50128/benthd.pm" --kin "hash-c1/kuid 58422 50128/anim.kin"
```

`anim-tool info` lists a `.kin` animation's bones, frames and events
([`docs/formats/animation.md`](docs/formats/animation.md)); `mesh-tool preview --kin ANIM.kin --frame N`
shows a mesh in that pose.

`route-tool info / overview` lists a route's terrain sections and stitches their overview maps
into one PNG ([`docs/formats/route.md`](docs/formats/route.md)); the section contents are not decoded yet.

`sound-tool info / export` reads `.wav` sounds (PCM and MS ADPCM) and exports plain WAVs;
`sound-player ARCHIVE NAME` plays one through SDL2 ([`docs/formats/sound.md`](docs/formats/sound.md)).

`src/input/KeyBindings` reads the game's key bindings (`UserData/settings/keyboard.txt`;
[`docs/formats/keyboard.md`](docs/formats/keyboard.md)). `input-tool list / shared / press` shows the bindings,
and `key-tester KEYBOARD.txt` (SDL2) prints the actions each key press triggers.

## How it protects your game data

- **Read-only.** Game files are opened read-only (`O_RDONLY`, and `O_NOATIME`
  when allowed). The reader has no code that writes, moves, renames or deletes
  anything.
- **One folder for everything the port makes:** `~/.local/share/trainz-native/`
  (or `$XDG_DATA_HOME/trainz-native`). `ja-tool extract` writes only there; by
  default to `~/.local/share/trainz-native/extracted/<archive>/`. `--out` must
  point inside that folder too.
- **Guarded output.**
  - Extraction refuses any folder inside the Trainz install or `$TRAINZ_DATA_DIR`.
  - Every folder below `~/.local/share/trainz-native/` is opened without
    following symlinks, so a symlink can't redirect a write into the game.
  - Existing files are never overwritten, and archive names that could escape
    the output folder are skipped.
  - These rules are covered by the tests in `tests/test_safe_output.cpp`, which
    use a fake install; extraction hasn't been tried on Dad's real install yet.

The readers themselves run in CI on real game data (`testdata/ts2010`: `Base.ja`,
`Data.ja` and a real subset of `Builtin\8.ja`; private repo only, see
[`testdata/README.md`](testdata/README.md)).

## Build

Debian packages:

```sh
sudo apt install cmake ninja-build g++ clang libclang-rt-dev libgtest-dev libsdl2-dev libgl-dev
```

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Options:
- `-DTRAINZ_SANITIZE=ON`: AddressSanitizer + UBSan.
- `-DTRAINZ_WERROR=ON`: treat warnings as errors.
- `-DCMAKE_CXX_COMPILER=clang++`: build with clang.

On Hearth OS the packages can be installed directly with `apt` (the system is
writable), so no toolbox is needed. CI builds the same way in a plain Debian 13
container (`.github/workflows/ci.yml`).

## Use `ja-tool`

```sh
ja-tool info    /path/to/TS2010/Base.ja
ja-tool list    /path/to/TS2010/Base.ja --folder Achievements --long
ja-tool find    /path/to/TS2010/Base.ja "Achievements/Tutorial Complete Rule/config.txt"
ja-tool kuid    /path/to/TS2010/Builtin/8.ja "<kuid:447264:1073>"
ja-tool cat     /path/to/TS2010/Base.ja "Achievements/Tutorial Complete Rule/config.txt"
ja-tool verify  /path/to/TS2010/Builtin/8.ja      # decodes every entry
ja-tool extract /path/to/TS2010/Base.ja --folder Achievements
```

Lookups work like the game's: case-insensitive, `\` or `/`, and `.` and `..`
are resolved.

## Test against your real install

The normal tests build their own small archives, so they need no game files.
Tests against a real install run only when `TRAINZ_DATA_DIR` is set, and they
only read:

```sh
TRAINZ_DATA_DIR=/path/to/TS2010 ctest --test-dir build -R RealInstall --output-on-failure
```

They:
- open every `.ja` found up to two folders deep;
- check that every name can be looked up;
- check the `hash-XX` KUID folder rule on every asset;
- decode about 200 entries per archive.

To decode everything, add `TRAINZ_FULL_VERIFY=1`. That's about 7 GB, so expect
a while on a hard drive.

Game files are in the repo only because it is private (see `.gitignore` and `testdata/README.md`).
