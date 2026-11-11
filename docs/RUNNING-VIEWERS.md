# Running the Viewers on Dad's PC

Step-by-step guide to run `model-viewer`, `sound-player` and `key-tester` on the
real TS2010 install.

## Prerequisites

The viewers need SDL2 and OpenGL. On Hearth OS (Debian 13):

```sh
sudo apt update
sudo apt install libsdl2-dev libgl-dev
```

If you want MP3 playback with `sound-player`, also install:

```sh
sudo apt install libmpg123-dev
```

These are the same packages that the CI builds use.

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
ls -l build/model-viewer build/sound-player build/key-tester
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

Use `asset-tool` to list what's available:

```sh
# Find all .im and .pm meshes
build/asset-tool find /path/to/TS2010 --type im --type pm | head -20
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
build/sound-player /path/to/TS2010/Base.ja "UI\main menu.wav"
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
build/sound-player /path/to/TS2010/Data.ja "Sound/ambient.mp3"
```

It can also play MP3 data wrapped in a WAV container (common in TS2010).

### Finding sounds to play

List sounds with `asset-tool`:

```sh
build/asset-tool find /path/to/TS2010 --type wav --type mp3 | head -20
```

Or search for a specific sound by name:

```sh
build/ja-tool list /path/to/TS2010/Base.ja | grep -i "main menu"
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

### "Cannot open a window" or "OpenGL 2.1 context"

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

### "Cannot open audio device"

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
# 1. Play the main menu sound
build/sound-player /path/to/TS2010/Base.ja "UI\main menu.wav"

# 2. View a simple mesh
build/model-viewer /path/to/TS2010/Builtin/8.ja "hash-c1/kuid 58422 50128/benthd.pm"
  # (drag with mouse, zoom with wheel, press Q to quit)

# 3. Test a key binding
build/key-tester /path/to/TS2010/UserData/settings/keyboard.txt
  # (press a few keys, then Esc twice to quit)
```

If all three work, the viewers are ready to use on Dad's PC.
