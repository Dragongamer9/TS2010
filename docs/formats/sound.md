# `.wav` and `.mp3` sounds

- **Code:** `src/content/SoundFile.{h,cpp}`; MP3 in `src/content/Mp3Decoder.cpp`.
- **Tools:**
  - `sound-tool info / export`: export writes a plain 16-bit WAV under
    `~/.local/share/trainz-native/sounds/` (a `.mp3` export gets `.wav` added
    to its name);
  - `sound-player`: SDL2 audio. It is built in CI, but **not yet heard on a
    real machine**.

**Formats.** These are the standard RIFF/WAVE formats. The rules below come
from checking **all 5,874** `.wav` files on the disc (`Base.ja`, `Data.ja`,
`Builtin\1.ja`–`8.ja`) with a Python copy of the C++ rules:

| Format | Files | Handling |
|---|---:|---|
| PCM (tag 1): 8-bit unsigned / 16-bit signed, mono or stereo, 5,000–48,000 Hz | 5,844 | decoded to 16-bit |
| Microsoft ADPCM (tag 2, 4-bit) | 26 (9 end in a partial block) | decoded to 16-bit (standard MS ADPCM algorithm) |
| MP3 inside a WAV (tag 85) | 3 | decoded with libmpg123 (see [MP3](#mp3)) |
| An MPEG-2 program stream named `.wav` (`kuid2 12121 2121 4/idle 1.wav`) | 1 | rejected with a clear error: it is DVD-style video (MPEG-2 video 352×288, AC-3 audio 48 kHz stereo, 10.6 s, per `ffprobe`), with no MPEG audio in it |

**What the disc's files look like** (all tolerated, with warnings):
- **Extra chunks**, in many orders: `LIST`, `smpl`, `cue `, `bext` (5 files have
  it before `fmt `), `acid`, `PAD `, `JUNK`, `fact` and more.
- **Padding:** chunks are padded to even sizes.
- **Garbage after the sound data** in a few files. It's ignored once `fmt ` and
  `data` are found.
- **A RIFF size 1 byte past the end of the file** (2 files) and **extra bytes
  after the RIFF data** (67 files).

## MP3

**Every MPEG audio file on the disc** (found by listing every `.mp3` and every
`.wav` in `Base.ja`, `Data.ja`, `Builtin\1.ja`–`8.ja` and checking the `.wav`
headers; there are no `.mp3` files in `Data.ja` or `Builtin`):

| Archive | Name | Bytes | Stream | Decoded |
|---|---|---:|---|---|
| `Base.ja` | `libraries/commandsounds/squelch_longer.mp3` | 5,541 | ID3v2 tag, MPEG-2 Layer III, 64 kbit/s, 22,050 Hz stereo | 12,096 frames (0.549 s) |
| `Base.ja` | `libraries/commandsounds/squelch_short.mp3` | 4,287 | same | 8,640 frames (0.392 s) |
| `Base.ja` | `libraries/commandsounds/squelch_shorter.mp3` | 3,242 | same | 5,760 frames (0.261 s) |
| `Builtin\4.ja` | `hash-c3/kuid 68787 35061/bear.wav` | 10,248 | tag 85, MPEG-2.5 Layer III, 20 kbit/s, 11,025 Hz mono (10,190 data bytes) | 44,928 frames (4.075 s); last 50 bytes are a partial frame |
| `Builtin\7.ja` | `hash-ae/kuid 4468 39505/foghorn.wav` | 63,104 | tag 85, MPEG-2 Layer III, 32 kbit/s, 24,000 Hz mono (63,033 data bytes) | 377,856 frames (15.744 s); last 57 bytes are a partial frame |
| `Builtin\7.ja` | `hash-b2/kuid 4468 39501/foghorn.wav` | 63,104 | byte-identical to the one above | same |

So the "6 MP3 files" of the earlier roadmap are these 3 `.mp3` + 3 MP3-in-WAV.

**How it is decoded:** libmpg123 (Debian `libmpg123-dev`, found with
pkg-config; optional at build time), fed from memory with
`mpg123_open_feed`/`mpg123_feed`, output forced to signed 16-bit at the
stream's own rate and channel count. ID3 tags are skipped by mpg123.
- **MP3 inside a WAV:** the `data` chunk is decoded; the `fmt ` extension
  (MPEGLAYER3WAVEFORMAT) is not needed. If the WAV header's rate or channels
  disagreed with the stream, the stream would win with a warning (they agree
  on all 3 disc files).
- **A bare MP3 stream named `.wav`** (an ID3 tag or a valid MPEG audio frame
  header instead of `RIFF`) is decoded as MP3 with a warning. None is on the
  disc; the one non-RIFF `.wav` is the program stream above.
- **Errors** (all `ContentError`, never a crash): no frame found (garbage, less
  than one frame), a stream that changes rate or channels part-way, decoded
  output above 256 MiB (checked before each append), any mpg123 error.
- **A partial frame at the end** is dropped with a warning. `ffmpeg` instead
  zero-pads it and decodes one more frame (576 samples for these MPEG-2/2.5
  files), so its output for the two WAVs is 576 samples longer.
- **Observed quirk:** a stream of exactly one frame is refused ("no MPEG audio
  frames found"); mpg123 wants a second frame header to confirm the first.
  Not seen on the disc.

**Checked:** all 6 decode (`RealInstall.SoundsDecode` on the full `Builtin`
archives and on `testdata/ts2010`) (C++ with libmpg123 1.32.10). Decoded samples were
compared with `ffmpeg`'s decoder: same alignment, same length (except the
partial last frame above), every compared sample within 1 LSB (every 7th
sample compared). Not yet heard on a real machine.

**Without libmpg123** the build still works; MP3 files and MP3-in-WAV give a
clear "MP3 decoding is not built in" / "MP3 inside a WAV file: ..." error.

**Not done yet:**
- How sounds are used in the game: config `soundscript` blocks, 3D
  positioning, looping. That's the rest of roadmap step 9.

## A train's own engine and horn sounds (2026-11-13, observed, not traced)

A traincar config names its sounds with `enginesound <kuid>` and `hornsound <kuid>` (the
SD40-2 `<kuid:-12:3534>`: `<kuid:-1:42003000>` and `<kuid:-1:42003101>`).

- **`enginesound`** (kind `enginesound`): one loop per notch, `idle 1.wav` … `idle 8.wav`; a
  clip for every notch change, `up 1 - 2.wav` … `up 7 - 8.wav` and `down 2 - 1.wav` …
  `down 8 - 7.wav`; and `stop.wav`.
- **`hornsound`** (kind `hornsound`, `two-part 1`): `horn_start.wav`, `horn_loop.wav`,
  `horn_end.wav` (this one also ships `w_bell_1.wav` and `idle 1.wav`).

**The port's use** (`game::TrainSound`; its own rules, not the game's, whose soundscript,
volumes and pitch with speed are not traced):

- notch 0 plays `idle 1`;
- a notch change plays the clips step by step, then the new loop;
- the horn starts, loops while H is held (1.5 s at least) and ends;
- everything is mixed to mono at 22,050 Hz.

`route-viewer --drive` plays it through SDL when an audio device opens. `--record-sound`
writes what was played to a `.wav` under `~/.local/share/trainz-native/routes/`; that is how
it was checked here, since this PC runs offscreen with no speakers. Not done yet: wheel and
brake sounds, sounds placed in 3D (other trains, scenery), `stop.wav`, the bell.

## The game's general running sounds (Data.ja, observed)

`Data.ja` ships the sounds common to all trains:

- `sounds/track/wheel_clang1..6.wav`, `wheel_clang_slow1/2.wav` (and `_med`, `_slow`,
  `_slow_med`, `in_between_wheel.wav`, `flange_squeal.wav`);
- `sounds/junction/wheels_over_points_1..6.wav`;
- `sounds/airbrakes/start.wav`, `idle 1.wav`, `stop.wav`;
- `sounds/wheelslip/start.wav`, `idle 1.wav`, `stop.wav`.

**The port's use** (`game::RunningSounds`, its own rules):

- a pair of clacks at every rail joint, every 12 m (a GUESS: US 39 ft rails), the axles
  2.4 m apart; louder with speed; the slow clips below 20 km/h;
- a wheels-over-points clip when the train runs through a junction;
- the brake hiss (start, loop, stop) while the brake pipe pressure is falling.

Flange squeal (`flange_squeal.wav`) loops on curves tighter than 250 m above 10 km/h, louder
when tighter (the radius from the heading change over the next 10 m); the bell
(`w_bell_1.wav` of the hornsound asset) loops while B has it on. Wheel slip and the `_med`
clips are not used yet. Checked by recording two
runs, with and without `Data.ja`, and comparing them: the running sounds start when the train
moves, at about 2–3 clacks a second at speed.
