# Test data: real Trainz Simulator 2010 files

**Private repository only.** These are N3V Games' copyrighted files from Dad's
own TS2010 disc (Just Flight edition, `TRS2010EE.iso`). If this repository is
ever made public, delete them from git history first.

| File | Size | Source |
|---|---:|---|
| `ts2010/Base.ja` | 44 MB | copied unchanged from the disc installer (SHA-256 starts `ec1b3716`) |
| `ts2010/Data.ja` | 65 MB | copied unchanged from the disc installer (SHA-256 starts `9f33b848`) |
| `ts2010/Builtin/sample8.ja` | 12 MB | a **subset** of `Builtin\8.ja`, rebuilt by `tools/dev/make_sample_ja.py` (now including the "Midday Manifest" session) |
| `ts2010/UserData/settings/keyboard.txt` | 15 KB | copied unchanged from the disc installer (SHA-256 starts `94b0521d`) |

**About `sample8.ja`:**
- The full `Builtin\8.ja` is 556 MB, over GitHub's 100 MB per-file limit, so
  the script repacks a real subset instead.
- Entries are byte-for-byte copies of the decoded files, with their original
  names, sizes and hash fields.
- They are stored **uncompressed**, so this archive does not exercise
  `CFIL`/BPE. `Base.ja` and `Data.ja` do.
- It holds 1,550 entries:
  - every `config.chump` / `config.txt` in `8.ja`;
  - the route `kuid 447264 1035`;
  - the ore car `kuid2 90361 120204 2`;
  - the bogie `kuid 58422 50128`;
  - the steel bridge `kuid 101839 22229`;
  - the bell sounder `kuid2 60850 60001 2`;
  - three textures of each pixel format in use (one for DXT3, the only one on the disc).

CI runs the `RealInstall.*` tests against this folder, with a full decode
(`TRAINZ_DATA_DIR=testdata/ts2010`, `TRAINZ_FULL_VERIFY=1`). The archives are
only read.
