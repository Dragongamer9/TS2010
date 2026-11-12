# Packaging (Debian .deb for Hearth OS)

How to build a Debian package of the tools and route viewer. This is **not** the
playable game — only the read-only tools that look at a TS2010 install.

## Version (owner only)

**You** choose the version string. Nobody else may invent, bump or apply one.

Debian requires the version to **start with a digit**. When you configure the
build, set it:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTRAINZ_PACKAGE_VERSION=YOUR_VERSION_HERE
```

Until you set it, CMake keeps the placeholder `OWNER_SET_VERSION`. Do not ship a
package that still uses that placeholder.

CI only uses `0~ci`: it starts with a digit (so dpkg accepts it) and is clearly
**not** a real release. That string is for the packaging check in GitHub Actions
alone; it is not a version for Dad to install as a release.

The Debian *package name* may not contain `~`, so CMake turns `~` into `-` in
the name only (version and install folder keep your string as given). Example:
version `0~ci` → package `trainz-native-0-ci`, files under `/opt/trainz-native/0~ci/`.

## Every file under `/opt` (nothing in `/usr`)

The package installs **only** under `/opt/trainz-native/<version>/`. Every
binary, the launcher, and the `.desktop` file live in that tree. There are **no**
shared paths in `/usr` (no `/usr/bin`, no `/usr/share/applications`), so two
versions never fight over the same file.

## How the viewer finds its files (not baked at build time)

Paths are **not** fixed from `CMAKE_INSTALL_PREFIX` when the binaries are
compiled. Nothing in the C++ tools or the launcher embeds the install prefix.
After install they work like this:

| What | How it is found |
|---|---|
| `route-viewer` (from the launcher) | Same folder as `trainz-route-viewer` (`dirname` of the script), else `PATH`, else `$TRAINZ_ROUTE_VIEWER` |
| Port settings / PNGs / exports | `~/.local/share/trainz-native/` (or `$XDG_DATA_HOME/trainz-native`) at **run** time |
| The TS2010 game folder | `$TRAINZ_INSTALL`, or the path stored by `--set-install` in that data folder |
| The `.desktop` file | Only a menu shortcut; the programs do not load assets from `share/` |

So a build configured with the default prefix (for example `/usr/local`) can still
be packed with CPack into `/opt/trainz-native/<version>/`. CPack’s
`CPACK_PACKAGING_INSTALL_PREFIX` only decides where files land **inside the
`.deb`**; it does not need to match the prefix used at compile time, because the
programs never look that prefix up. The launcher under
`/opt/trainz-native/<version>/bin/` finds `route-viewer` next to itself in that
same `bin/` folder.

## Side-by-side installs (previous version stays startable)

Each package is named `trainz-native-<version-with-tildes-as-dashes>` and owns
only its own `/opt/trainz-native/<version>/` folder. A newer package therefore
does **not** overwrite an older one: both can be installed at once, and the older
binaries keep working.

Examples after two versions are installed:

```text
/opt/trainz-native/<older-version>/bin/trainz-route-viewer
/opt/trainz-native/<newer-version>/bin/trainz-route-viewer
```

There is no `Conflicts` / `Replaces` between these versioned packages. Settings
under `~/.local/share/trainz-native/` are shared and are only written when you
run `--set-install` on a launcher.

## How Dad starts a particular version

Because nothing is on the default `PATH` and nothing is in `/usr/share/applications`,
pick the version by its folder:

**Full path (simplest):**

```sh
/opt/trainz-native/<version>/bin/trainz-route-viewer --set-install /path/to/TS2010
/opt/trainz-native/<version>/bin/trainz-route-viewer
/opt/trainz-native/<version>/bin/trainz-route-viewer "Midday Manifest"
```

Use the `<version>` folder you want (the older one still works if that package is
still installed).

**Menu entry:** the `.desktop` file is shipped at

```text
/opt/trainz-native/<version>/share/applications/trainz-route-viewer.desktop
```

Desktop menus do not scan `/opt` by default. To show one version in the menu,
copy that file into `~/.local/share/applications/` (a name that includes the
version helps when several are installed) and set `Exec=` to the **full path** of
that version’s launcher, for example:

```text
Exec=/opt/trainz-native/<version>/bin/trainz-route-viewer
```

Each menu entry then starts exactly one version; they do not share a single
`/usr` path.

## Build the .deb

Build as usual (with SDL2 / OpenGL so the viewers are included), with your
version set, then run CPack:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTRAINZ_PACKAGE_VERSION=YOUR_VERSION_HERE
cmake --build build --parallel
cpack --config build/CPackConfig.cmake -G DEB
```

The `.deb` appears in `build/`. Check what it contains:

```sh
dpkg-deb -c build/trainz-native-*_amd64.deb
```

Every path should start with `./opt/trainz-native/<version>/`. There should be no
`./usr/` entries.

## Install and remove

```sh
sudo apt install ./build/trainz-native-<name-version>_<version>_amd64.deb
```

Runtime libraries the package declares: `libsdl2-2.0-0`, `libgl1`, `libmpg123-0`,
`libjpeg62-turbo`.

Remove one version without touching the others:

```sh
sudo apt remove trainz-native-<name-version>
```

(`<name-version>` is your version with `~` turned into `-`.)

## Relation to `cmake --install`

`cmake --install build --prefix ~/.local` is unchanged: it still puts unversioned
tools in `~/.local/bin` and a menu entry in `~/.local/share/applications`. Use
that for a personal install from source. Use the `.deb` when you want a packaged
copy under `/opt/trainz-native/<version>/` that can sit beside older packaged
copies.

More on running the viewers: [`RUNNING-VIEWERS.md`](RUNNING-VIEWERS.md).
