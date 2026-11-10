# `.wav` sounds

- **Code:** `src/content/SoundFile.{h,cpp}`.
- **Tools:**
  - `sound-tool info / export`: export writes a plain 16-bit WAV under
    `~/.local/share/trainz-native/sounds/`;
  - `sound-player`: SDL2 audio. It is built in CI, but **not yet heard on a
    real machine**.

**Formats.** These are the standard RIFF/WAVE formats. The rules below come
from checking **all 5,874** `.wav` files on the disc (`Base.ja`, `Data.ja`,
`Builtin\1.ja`–`8.ja`) with a Python copy of the C++ rules:

| Format | Files | Handling |
|---|---:|---|
| PCM (tag 1): 8-bit unsigned / 16-bit signed, mono or stereo, 5,000–48,000 Hz | 5,844 | decoded to 16-bit |
| Microsoft ADPCM (tag 2, 4-bit) | 26 (9 end in a partial block) | decoded to 16-bit (standard MS ADPCM algorithm) |
| MP3 inside a WAV (tag 85) | 3 | **not decoded yet**: clear error. It would need an MP3 decoder (Debian `libmpg123-dev`) |
| An MPEG stream named `.wav` (`kuid2 12121 2121 4/idle 1.wav`) | 1 | rejected with a clear error |

**What the disc's files look like** (all tolerated, with warnings):
- **Extra chunks**, in many orders: `LIST`, `smpl`, `cue `, `bext` (5 files have
  it before `fmt `), `acid`, `PAD `, `JUNK`, `fact` and more.
- **Padding:** chunks are padded to even sizes.
- **Garbage after the sound data** in a few files. It's ignored once `fmt ` and
  `data` are found.
- **A RIFF size 1 byte past the end of the file** (2 files) and **extra bytes
  after the RIFF data** (67 files).

**Also on the disc:** three real `.mp3` files, in `Base.ja`. They aren't
decoded yet.

**Not done yet:**
- MP3 decoding.
- How sounds are used in the game: config `soundscript` blocks, 3D
  positioning, looping. That's the rest of roadmap step 9.
