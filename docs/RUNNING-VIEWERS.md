# Running the Viewers on Dad's PC

Step-by-step guide to run `route-viewer`, `model-viewer`, `sound-player` and `key-tester`
on the real TS2010 install.

## Prerequisites

The viewers need SDL2 and OpenGL; the build also needs the compiler, CMake, the
test library and Python. On Hearth OS (Debian 13), this one line installs everything
(the same packages the CI builds use, see `README.md`):

```sh
sudo apt update
sudo apt install cmake ninja-build g++ clang libclang-rt-dev libgtest-dev libsdl2-dev libgl-dev python3 \
  pkg-config libmpg123-dev libjpeg62-turbo-dev
```

`libmpg123-dev` (MP3 sounds) and `libjpeg62-turbo-dev` (`.jpg` textures) are optional:
without them the build still works, but those files are refused.

## Build the Tools

From the project root:

```sh
# Configure with viewer support (it's ON by default)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build everything
cmake --build build
```

Check the viewer binaries were created:

```sh
ls -l build/route-viewer build/model-viewer build/sound-player build/key-tester
```

If any are missing, CMake will have printed why (e.g. "install libsdl2-dev" or
"install libgl-dev").

## Where the Game Files Are

For this guide we assume the TS2010 install is at:

```
/path/to/TS2010
```

Replace this with the actual path on Dad's PC. The main archives are:
- `Base.ja` – game data
- `Data.ja` – more game data
- `Builtin/8.ja` – built-in assets (largest)
- `UserData/settings/keyboard.txt` – key bindings

## Install once, then start from the menu

```sh
cmake --install build --prefix ~/.local          # tools into ~/.local/bin, a menu entry
trainz-route-viewer --set-install /path/to/TS2010 # remember where the game is (once)
```

Then **Trainz Route Viewer (TS2010 port)** is in the applications menu (it opens a terminal):
it lists the routes and their sessions, asks for a name and opens it. From a terminal,
`trainz-route-viewer "Midday Manifest"` does the same in one go (a session first; a route if
no session has that name; extra options such as `--radius 900` are passed on).

The launcher writes only `~/.local/share/trainz-native/install-dir`, and only when you run
`--set-install`; `TRAINZ_INSTALL=/path` overrides it for one run. The game folder is only
read. To remove the install, delete exactly the files it put there: CMake lists them in
`build/install_manifest.txt`, one path per line:

```sh
while IFS= read -r f; do rm -v -- "$f"; done < build/install_manifest.txt
```

## Running route-viewer

Fly around a route in a window: terrain with its ground textures, track (curves by the
port's model), buildings, trees and a session's trains, all from the game files. It is
drawn by the port's own software renderer, so expect roughly 5–15 frames a second; it is a
viewer for checking what the port reads, not the game.

### Basic usage

```sh
# Every route of the install and its sessions, by name
build/route-viewer --install /path/to/TS2010 --list

# A session by (part of) its name: its route is found for you
build/route-viewer --install /path/to/TS2010 --session "Midday Manifest"

# Or a route by name, with no trains
build/route-viewer --install /path/to/TS2010 --route "Holly"
```

A name can be part of a name if only one matches (otherwise it lists the matches), or a
KUID such as `"<kuid:107727:1019>"`. The older form with archive and folder still works:
`build/route-viewer /path/to/TS2010/Builtin/7.ja "hash-92/kuid 107727 1019" --session
/path/to/TS2010/Builtin/8.ja "hash-e1/kuid 467400 1035" --install /path/to/TS2010`; without
`--install` there, objects are dots and the ground is coloured from the overview maps. `--at X Y` starts somewhere else (world metres), `--radius M` sets how much is loaded
(50–5000 m, default 600; bigger is slower). It prints what it is reading first; indexing a
whole install takes a few seconds.

### Controls

| Key | Does |
|---|---|
| W / S or Up / Down | forward / back |
| A / D | sideways |
| Left / Right | turn |
| Page Up / Page Down | look up / down |
| Q / E | down / up |
| Shift (held) | 5× faster |
| R | back to the start view |
| Esc | quit |
| Right mouse button + drag | look around |
| Mouse wheel | forward / back (10 m a step) |
| Gamepad left stick | move |
| Gamepad right stick | look |
| Gamepad triggers | down (left) / up (right) |
| Gamepad A (bottom button), held | 5× faster |
| Gamepad Start | back to the start view |

A gamepad SDL knows (Xbox- or PlayStation-style) can be plugged in while the viewer runs.
None of this is the game's own controls; these are the viewer's.

The window title shows the camera position and how long the last frame took.

### Finding a route and a session

`--list` prints every route (assets of kind `map`) with its sessions (kind `profile`,
which name their route with `map-kuid`). On Dad's disc that is 52 routes.

## Running model-viewer

Shows a Trainz mesh with its textures in an OpenGL window.

### Basic usage

```sh
build/model-viewer /path/to/TS2010/Builtin/8.ja "hash-c1/kuid 58422 50128/benthd.pm"
```

This opens a 1024×768 window with the model. It:
- Reads the mesh from the archive
- Loads any textures referenced by the mesh's materials
- Displays it with lighting

### With animation

To show an animated model, add `--kin` with the animation file:

```sh
build/model-viewer /path/to/TS2010/Builtin/8.ja "hash-c1/kuid 58422 50128/benthd.pm" \
  --kin "hash-c1/kuid 58422 50128/anim.kin"
```

### Controls

| Key / Action | What it does |
|---|---|
| **Mouse drag (left button)** | Rotate the model |
| **Mouse wheel** | Zoom in / out |
| **Space** | Play / pause the animation (if `--kin` was given) |
| **W** | Toggle wireframe on / off |
| **Esc or Q** | Quit |

### Finding a mesh to view

List the meshes in an archive with `ja-tool`:

```sh
# The first 20 .im and .pm meshes in Builtin/8.ja
build/ja-tool list /path/to/TS2010/Builtin/8.ja | grep -iE '\.(im|pm)$' | head -20
```

Or search for a specific asset:

```sh
build/asset-tool find /path/to/TS2010 "<kuid:58422:50128>"
```

Then use the path it reports with `model-viewer`.

### Tips

- The window is resizable. Resizing updates the view immediately.
- If the model looks tiny or huge, the camera auto-scales to fit it. This
  works best when the mesh has sensible coordinates.
- Textures are loaded from the same folder as the mesh in the archive. If
  a texture is missing, a warning is printed on the console.
- The viewer refuses to load files over 256 MB (safety limit).

## Running sound-player

Plays a sound from a Trainz archive through the default audio device.

### Basic usage

```sh
build/sound-player /path/to/TS2010/Base.ja "trackside/switchlever/points.wav"
```

This:
- Reads the sound file from the archive
- Opens the default SDL2 audio device
- Plays it once
- Exits when finished

### MP3 sounds

If the build has `libmpg123` support (you installed `libmpg123-dev` and CMake
found it), it can play MP3 files directly:

```sh
build/sound-player /path/to/TS2010/Base.ja "libraries/commandsounds/squelch_short.mp3"
```

It can also play the few WAV files that hold MP3 data inside (3 on the whole disc).

### Finding sounds to play

List sounds with `ja-tool`:

```sh
build/ja-tool list /path/to/TS2010/Base.ja | grep -iE '\.(wav|mp3)$' | head -20
```

Or search for a specific sound by name:

```sh
build/ja-tool list /path/to/TS2010/Base.ja | grep -i "points"
```

### Tips

- The console prints the number of frames and sample rate before playing.
- If SDL2 can't open an audio device, it prints the error and exits.
- Use headphones for quiet sounds—they may be hard to hear through laptop speakers.

## Running key-tester

Opens a small window and shows which game actions each key press triggers,
based on a `keyboard.txt` bindings file.

### Basic usage

```sh
build/key-tester /path/to/TS2010/UserData/settings/keyboard.txt
```

This:
- Reads the key bindings from `keyboard.txt`
- Opens a 480×120 window
- Prints to the console as you press keys

### Filter by action groups

By default it only reports actions in the `driver` and `gameplay` groups.
To see other groups, specify them:

```sh
build/key-tester /path/to/TS2010/UserData/settings/keyboard.txt "driver,gameplay,camera"
```

### What you'll see

Each key press prints a line like:

```
Ctrl+Shift+D -> driver/next-view
```

This means pressing Ctrl+Shift+D triggers the "next-view" action in the
"driver" group. If a key has no action, it prints "(no action)".

### Controls

| Key | What it does |
|---|---|
| **Any key** | Prints the action(s) it triggers |
| **Esc (twice)** | Quits – press Esc, then Esc again |

### Tips

- The window must have focus for key presses to register.
- The key names are the game's internal names (e.g. "Ctrl+Shift+D", not the
  physical key codes).
- If a key isn't bound to anything, you'll see "(key X has no game name)"
  or "(no action)".
- The tester never modifies the bindings file—it's read-only.

## Common Issues

### "cannot open a window" or "OpenGL 2.1 context"

Make sure SDL2 and OpenGL drivers are installed:

```sh
sudo apt install libsdl2-dev libgl-dev mesa-utils
```

Then rebuild:

```sh
cmake --build build
```

### "SDL_Init failed"

Check that you have permission to access the display and audio. On a
standard desktop install this should work automatically.

### "texture ... is not in this folder of the archive"

The mesh references textures that aren't in the same folder. This is
normal for some meshes. The model will still display, but without those
textures (they'll appear grey).

### "the sound is empty"

The file exists but contains no audio data. This can happen with damaged
files. Try a different sound.

### "cannot open an audio device"

SDL2 couldn't open the default audio device. Check your sound system:

```sh
aplay -l  # List ALSA devices
speaker-test -c 2  # Test stereo playback
```

If using PipeWire/PulseAudio, make sure the appropriate packages are
installed.

## Quick Verification

To confirm everything works, try these three commands in order:

```sh
# 1. Play a short sound (switch points)
build/sound-player /path/to/TS2010/Base.ja "trackside/switchlever/points.wav"

# 2. View a simple mesh
build/model-viewer /path/to/TS2010/Builtin/8.ja "hash-c1/kuid 58422 50128/benthd.pm"
  # (drag with mouse, zoom with wheel, press Q to quit)

# 3. Test a key binding
build/key-tester /path/to/TS2010/UserData/settings/keyboard.txt
  # (press a few keys, then Esc twice to quit)
```

If all three work, the viewers are ready to use on Dad's PC.

## What to send back

For each tool Dad tries, copy back:
- The **exact command** he ran
- Any **error text** printed in the terminal
- For `model-viewer`: whether a window opened, and if it showed the model
- For `sound-player`: whether the sound played
- For `key-tester`: whether a window opened and if key presses showed up
- A **screenshot** if something looks wrong (window blank, model missing, etc.)

This lets us diagnose any issues quickly.
