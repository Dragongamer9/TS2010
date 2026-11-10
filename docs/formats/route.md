# Routes: `mapfile.*` – partly worked out

**What a route asset is.** A route is an asset folder (for example
`hash-fa/kuid 447264 1035` in `Builtin\8.ja`) holding these files:

| File | Size in the sample | Status |
|---|---:|---|
| `config.chump` | 6 KB | readable (see [`config.md`](config.md)) |
| `mapfile.gnd` | 2.1 MB | outer structure worked out (below); section contents **not decoded** |
| `mapfile.obs` | 636 KB | objects placed on the route; not decoded |
| `mapfile.trk` | 103 KB | track; not decoded |
| `mapfile.bmk` | 330 B | bookmarks; not decoded |
| `mapfile.lyr` | 40 B | layers; not decoded |
| `mapfile.trc` | 32 B | unknown |
| `thumbnail-256.jpg` | 60 KB | JPEG |

- **Code:** `src/content/GroundFile.{h,cpp}`.
- **Command-line tool:** `route-tool info / overview`.
- **Confidence:** everything here comes from **one route**, the only one in
  `Builtin\8.ja`. The other built-in routes are in `Builtin\1.ja`–`7.ja`,
  which haven't been extracted. The C++ reader has been tested on made-up
  files in CI only.

## `mapfile.gnd` – terrain sections

```
"GND\r"  u32 sectionCount  u32 (0)  u32 (4)
sectionCount × 20-byte entries:
    char tag[4]   stored byte-reversed: "5ceS" = Sec5, "sceS" = Secs
    u32 start     absolute file offset of the section's data
    u32 end       absolute offset of its overview map
    i32 x, i32 y  grid position of the section (-3..0, 0..4 in the sample)
further header data up to the first section (916 bytes in the sample; meaning unknown)
per section: [start, end) section data, then 8192 bytes = 64 × 64 u16 overview map
```

**Checked on the sample:**
- `sectionCount` = 20, matching 20 index entries.
- The sections follow each other with **no gaps**, and the last overview map
  ends exactly at the end of the file.
- Two entries share position (−1, 0), one `Sec5` and one `Secs`. Why is
  **unknown**.

**Section data**
- It starts with `u32 250`, then a mix of floats and what look like 12-byte
  records `(content, 523, float)`. 523 is Auran's KUID user number, so these
  are probably ground-texture KUIDs. The layout varies between sections.
- **Not decoded.**
- `trainz.exe` picks the section class by tag at `0x6d293e`: `Sec5` is built
  by `0x81ec70`, anything else by `0x8298c0`. Neither class has been traced.
  That's the next step, and it needs a decompiler: Ghidra 12.1.x for REA.

**Overview map**
- Drawn as 5-6-5 colour, the 20 maps show recognisable terrain: curving track
  or roads and patches of ground.
- The colours look right with **red and blue swapped**.
- Section orientation hasn't been checked: the tile edges don't line up
  perfectly yet.
- **Observed, not confirmed.**
- It is probably a small precomputed picture of each section (for maps or
  distant terrain).

## Next

1. Trace `0x81ec70` / `0x8298c0` in `trainz.exe` to decode section data:
   heights, ground textures, water.
2. Extract `Builtin\1.ja`–`7.ja` to check this against more routes.
3. `mapfile.trk` (track splines) and `mapfile.obs` (placed objects). Together
   with the meshes, they would give a route viewer.
