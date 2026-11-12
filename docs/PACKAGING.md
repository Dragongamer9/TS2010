# Packaging (Debian .deb for Hearth OS)

How to build a Debian package of the tools and route viewer. This is **not** the
playable game — only the read-only tools that look at a TS2010 install.

## Version (owner only)

**You** choose the version string. Nobody else may invent, bump or apply one.

When you configure the build, set it:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTRAINZ_PACKAGE_VERSION=YOUR_VERSION_HERE
```

Until you set it, CMake keeps the placeholder `OWNER_SET_VERSION`. Do not ship a
package that still uses that placeholder.

## Side-by-side installs (previous version stays startable)

Each package is named `trainz-native-<version>` and installs only under
`/opt/trainz-native/<version>/`. A newer package therefore does **not** overwrite
an older one: both can be installed at once, and the older binaries keep working.

Examples after two versions are installed:

```text
/opt/trainz-native/<older-version>/bin/trainz-route-viewer
/opt/trainz-native/<newer-version>/bin/trainz-route-viewer
```

There is no `Conflicts` / `Replaces` between these versioned packages. Settings
under `~/.local/share/trainz-native/` are shared and are only written when you
run `trainz-route-viewer --set-install`.

## Build the .deb

Build as usual (with SDL2 / OpenGL so the viewers are included), with your
version set, then run CPack:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTRAINZ_PACKAGE_VERSION=YOUR_VERSION_HERE
cmake --build build --parallel
cpack --config build/CPackConfig.cmake -G DEB
```

The `.deb` appears in `build/`, named like
`trainz-native-YOUR_VERSION_HERE_YOUR_VERSION_HERE_amd64.deb`.

Check what it contains:

```sh
dpkg-deb -c build/trainz-native-*_amd64.deb
```

You should see the tools under `./opt/trainz-native/<version>/bin/` and the menu
file under `./opt/trainz-native/<version>/share/applications/`.

## Install and remove

```sh
sudo apt install ./build/trainz-native-<version>_<version>_amd64.deb
```

Runtime libraries the package declares: `libsdl2-2.0-0`, `libgl1`, `libmpg123-0`,
`libjpeg62-turbo`.

Start the launcher by full path (the versioned `/opt/.../bin` is not on the
default `PATH`):

```sh
/opt/trainz-native/<version>/bin/trainz-route-viewer --set-install /path/to/TS2010
/opt/trainz-native/<version>/bin/trainz-route-viewer
```

The `.desktop` file ships inside that same versioned tree. Desktop menus do not
scan `/opt/.../share/applications` by default; copy or symlink it into
`~/.local/share/applications/` if you want the menu entry, and point `Exec=` at
the versioned launcher if needed.

Remove one version without touching the others:

```sh
sudo apt remove trainz-native-<version>
```

## Relation to `cmake --install`

`cmake --install build --prefix ~/.local` is unchanged: it still puts unversioned
tools in `~/.local/bin` and a menu entry in `~/.local/share/applications`. Use
that for a personal install from source. Use the `.deb` when you want a packaged
copy under `/opt/trainz-native/<version>/` that can sit beside older packaged
copies.

More on running the viewers: [`RUNNING-VIEWERS.md`](RUNNING-VIEWERS.md).
