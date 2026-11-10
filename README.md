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
  - These rules are covered by the tests in `tests/test_safe_output.cpp`. They
    haven't yet been run against a real install.

## Build

Debian packages:

```sh
sudo apt install cmake ninja-build g++ clang libclang-rt-dev libgtest-dev
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

Hearth OS is an image-based system, so `apt install` may not apply directly on
the host. Build inside a Debian 13 toolbox or container if needed. CI does
exactly that (`.github/workflows/ci.yml`).

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

Game files are kept out of the repo while it is public (see `.gitignore`).
