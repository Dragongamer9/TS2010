# TS2010

A private project to bring a **Trainz Simulator 2010** install to Linux
natively. It targets Hearth OS (Debian 13, x86_64). Planning notes are in
[`docs/ENGINE-MAP.md`](docs/ENGINE-MAP.md).

What exists so far: a **read-only reader for the game's `.ja` content archives**
(`Base.ja`, `Data.ja`, `Builtin/*.ja`). It comes with the `ja-tool` command and
tests. The format is described in [`docs/formats/ja.md`](docs/formats/ja.md).

## Your game data is safe

- **Read-only.** Game files are opened read-only (`O_RDONLY`, and `O_NOATIME`
  when allowed). Nothing in the install or your own content is ever written,
  moved, renamed or deleted.
- **One folder for everything the port makes:** `~/.local/share/trainz-native/`
  (or `$XDG_DATA_HOME/trainz-native`). By default `ja-tool extract` writes to
  `~/.local/share/trainz-native/extracted/<archive>/`.
- **Guarded output.** `ja-tool extract` refuses an output folder inside the
  Trainz install or inside `$TRAINZ_DATA_DIR`. It never overwrites an existing
  file, and it skips archive names that could escape the output folder.

## Build

Debian packages:

```sh
sudo apt install cmake ninja-build g++ clang libgtest-dev
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

Don't commit game files to this repo.
