# Routes: `mapfile.*` – terrain sections decoded

**What a route asset is.** A route is an asset folder (for example
`hash-fa/kuid 447264 1035` in `Builtin\8.ja`) holding these files:

| File | Size in the sample | Status |
|---|---:|---|
| `config.chump` | 6 KB | readable (see [`config.md`](config.md)) |
| `mapfile.gnd` | 2.1 MB | header, index, world data and section data **traced and checked** (below); the meaning of several world fields is not known |
| `mapfile.obs` | 636 KB | objects placed on the route; not decoded |
| `mapfile.trk` | 103 KB | track; not decoded |
| `mapfile.bmk` | 330 B | bookmarks; not decoded |
| `mapfile.lyr` | 40 B | layers; not decoded |
| `mapfile.trc` | 32 B | unknown |
| `thumbnail-256.jpg` | 60 KB | JPEG |

- **Code:** `src/content/GroundFile.{h,cpp}` (`parseGround`, `parseGroundWorld`, `parseGroundSection`).
- **Command-line tool:** `route-tool info / overview` (`info` now decodes every section).
- **Checker:** `tools/dev/gnd_sections.py` (Python, reads `.gnd` files, folders or `.ja` archives read-only).

**How it was read.** The code is `bin\trainz.exe` from the TS2010 install,
read as GNU `objdump -d -M intel` disassembly (no decompiler). Addresses are
virtual addresses (VA = file offset + 0x400000 for `.text` and `.rdata`).

**Confidence words** (same as [`ja.md`](ja.md)):
- **Traced:** read in the disassembly.
- **Checked:** traced, and every disc file fits it.
- **Observed:** seen in data only, no code traced.
- **Guess:** a name or meaning that neither the code nor the data proves.

## `mapfile.gnd` – traced, checked

### Correction to the earlier reading (2026-11-10)

The first version of this page grouped the index wrongly. It read
`"GND\r" u32 count i32 a i32 b` followed by 20-byte entries
`{tag, start, end, x, y}`. In fact the "two header words" are the `x, y` of
section 0, and every entry is `{x, y, tag, start, end}`. Under the old grouping
each entry carried the next section's position, and the last entry took its
position from the first 8 bytes of the world data. That made up the
"two sections at (−1, 0)" in the sample and the "5 routes with two sections at
one position" in the disc survey. **With the traced layout, no route on the
disc has two sections at the same position.** The section start/end offsets were
right before and are unchanged.

### File layout

The file loader is `0x6d2aa0`. It reads through a buffered stream: the 4 KiB
buffer is at stream+0xc, with byte/u32 helpers `0x41ed60`, `0x41ed10`,
`0x413a10` and `0x413980`, and the slow path is `0x80a7a0`.

```
u8  magic[3]          "GND"; read (0x6d2bfa, 0x6d2c03, 0x6d2c0c) but NOT compared by the game
u8  version           0x6d2c1e; the game accepts 1..13 (sub al,1 / cmp al,0xc / ja, 0x6d2c2a).
                      Kept at world+0x8550 and passed to every section reader.
u32 sectionCount      0x6d2c42
sectionCount × {      loop 0x6d2c4f..0x6d2cc5
    i32 x             0x6d2c57 (stored as i16, 0x6d2c85)
    i32 y             0x6d2c64 (stored as i16, 0x6d2c8e)
    u32 tag           only if version >= 12 (cmp [esp+0x13],0xc / jl, 0x6d2c69); else 'Secs'.
                      Stored reversed: bytes "5ceS" = 'Sec5', "sceS" = 'Secs'.
                      'Sec5' builds a Sec5 (factory 0x81ec70); anything else a Secs (0x8298c0), 0x6dd62c
    u32 start         section vtable+0x5c: 0x827230 (Secs) / 0x81acf0 (Sec5) -> object+0x560
    u32 end           same function -> object+0x540
}
world data            read straight after the loop, up to the first section (below); 700–3,784 bytes
per section, at its start offset: section data (below)
per section, at its end offset:   overview map (below)
```

So an index entry is 20 bytes for version ≥ 12 and 16 bytes for older files.
The sections are **loaded lazily**. The loader keeps only `start` and `end`.
When a section is needed, `0x82c810` (Secs) seeks to `start`
(`0x82c88e`..`0x82c8bb`) and calls the data reader with the file's version
(`0x82c8c5`..`0x82c8d5`).

**Checked on the disc:** all 50 `mapfile.gnd` files are version 13 (`'\r'`).
Two other `.gnd` files are older: `1.ja` `razorback_railway_v6.gnd` is
version 11 (457 sections) and `4.ja` `robescenario.gnd` is version 10
(166 sections). Both fit 16-byte entries. Sections follow each other with no
gaps, and the last overview map ends at the end of the file.

### Section data – traced, checked

The readers are `0x82be00` (Secs) and `0x821200` (Sec5). Their code is
identical except for the vertex count. `v` is the file's version byte.

```
if v >= 13:                                  (0x82be6a)
    u32 slotCount                            0x82bea5; 250 in every disc section
    min(slotCount, 250) × {                  loop bound 0xfa at 0x82bfdb
        f32 weight                           0x82bef0
        if weight > 0.0:                     comiss/jbe 0x82bf68 (so NaN and <= 0: nothing follows)
            u32 content                      KUID reader 0x80c8d0, first word -> kuid+4
            u32 packedUser                   second word: user = low 25 bits sign-extended
                                             (bit 24, 0x80c91d), version = bits 25-31 (0x80c931)
    }
if v >= 4:                                   (0x82bfeb)
    u32 cellCount                            0x82c005; if > 0x510 (1296 = 36 × 36) it becomes 0 (0x82c02f)
    cellCount × {
        if v >= 5:  u16 col, u16 row         0x82c0aa, 0x82c0de
        else:       u32 index                col = (index % side) / 2, row = (index / side) / 2 (0x82c198)
        f32 value                            0x82c1d7
    }                                        stored only when col <= 35 and row <= 35 (0x82c205)
                                             into a 36 × 36 table of 12-byte cells (+4 value, +9 = 1)
side × side vertices                         0x82c267: 0x1690 = 76 × 76 (Secs); 0x82166c: 0x5590 = 148 × 148 (Sec5)
```

**Vertex** (`0x6f0e50`, called with the file version; 36-byte objects in
memory):

```
u8 first                                     0x6f0f3a
first == 0xFE:  u8 texture, [u8 extra if v > 2], height = 0          0x6f0f63..0x6f0fbb
first == 0xFD:  u8 texture, [u8 extra if v > 2], f32 height          0x6f0fd7..0x6f101e
otherwise:      list, starting with texture = first:                 0x6f1023..0x6f1106
                    { u8 weight, [u8 extra if v > 2], u8 nextTexture }
                until nextTexture == 0xFF (or after 33 entries, 0x6f10fc); only the first 3 are kept;
                first == 0xFF means an empty list
                then f32 height                                      0x6f1164
if v < 12:      3 more bytes (vertex+0x1d..0x1f)                     0x6f11a9..0x6f11ce
```

In memory: texture indexes at +0x14..0x16 (0xFF = none), weights at
+0x17..0x19 (0xFF for the short forms), the third per-layer byte at
+0x1a..0x1c, height at +0xc. Version < 13 files then remap texture indexes
through a world-level table (`0x6f11d3`..`0x6f1229`); no bytes are read there.

The writer for the same layout is `0x827300` (Secs section) and `0x6f0cb0`
(vertex). It picks `0xFE` when a vertex has one layer and a height of exactly
0.0, `0xFD` for one layer with any other height, and the list otherwise
(`0x6f0cb5`..`0x6f0d28`). It writes the cells it finds flagged in the 36 × 36
table, as `(u16 col, u16 row, f32)`.

**Checked: every section fits exactly.** Parsing with these rules ends exactly
at the section's `end` offset for every section. The Python checker and the
C++ reader agree:

| Files | Sections | Section data bytes | Result |
|---|---:|---:|---|
| 50 `mapfile.gnd` (all of `Builtin\1.ja`–`8.ja`), version 13 | 15,243 (15,237 Secs, 6 Sec5) | 653,817,764 | all end exactly at `end`; the C++ `parseGroundSection` gives the same result |
| 2 older `.gnd` (versions 10, 11) | 623 | 51,097,846 | all end exactly at `end` (Python checker only; the C++ reader refuses versions < 12) |

**So 100% of the section-data bytes are accounted for by traced fields.**

**Observed on the disc** (15,243 version-13 sections):
- `slotCount` is always 250. 125,784 slots carry a KUID.
- The most common KUID users are −1 (64,317), 68787, 46819, 523, 9 and 122285.
  That fits ground-texture assets: −1/−3 for Auran built-ins, the rest for
  content creators.
- 13,554 sections have no cells and 178 have all 1,296. The rest have a few.
- Vertices: 38.6 M `0xFE`, 22.2 M `0xFD` and 31.0 M lists (2 layers: 10.3 M,
  3 layers: 20.4 M, 1 layer: 0.3 M).
- Explicit heights range from −3000 to +3000 (median 36.6). Height 0 is common
  (the `0xFE` form).
- **The vertices form a row-by-row grid of `side` columns.** In the sample
  route, the mean |height difference| between neighbours at stride 1 and at
  stride `side` is about the same, and 5–50 times smaller than at stride
  `side / 2`. Which axis is x and which is y has **not** been checked.

**Guesses (not proven):**
- The vertex float is the terrain **height in metres**. The range and
  smoothness fit, but the code only shows it compared against 0.0 and a few
  constants (`0x82c323`..`0x82c353`).
- `weight` is the blend weight of a texture layer. The writer leaves out a
  layer whose weight is 0 (`0x6f0bd0`), and the short forms imply 255.
  `extra` is unknown (perhaps rotation or scale).
- 76 × 76 vertices for a Secs section would mean a 10 m grid on a 720 m
  baseboard with a 2-vertex border on each side, and Sec5's 148 × 148 a 5 m grid.
  Neither the spacing nor the border has been traced.
- `cells` (a 36 × 36 grid, one cell per 2 × 2 vertices in the old index form)
  holds one float per cell, possibly a water level. Not traced.
- The texture-slot weight might be a usage count or scale. The writer leaves
  out the KUID when it is ≤ 0. Not traced.

### World data – traced, checked

The world data is read by the file loader right after the section loop
(`0x6d2cc7` on). The buffered stream is the same one, and nothing is skipped.
`v` is the file version.

```
if v >= 7: environment                       0x463dd0 (v < 7: 0x463cb0, not traced)
    i32 count                                0x463df2
    if count == -1:
        i32 sub                              0x463e06 (sub-version; -1 when absent)
        if sub != -1: i32 count              0x463e17
    count = clamp(count, 0, 32)              0x463e1c..0x463e42
    KUID envKuid                             0x463e4a -> env+0xee4
    f32 a, f32 b                             0x463e8a, 0x463ecd -> env object +0xf5c, +0xf84
    count × 104-byte key {                   0x463f1a..0x463fce, kept at env+0x1e4 + 0x68 i
        f32 time; vec4 c0, c1, c2;           0x80b170 reads a vec4 as 4 × f32
        f32 time2; vec4 c3, c4, c5
    }
    if sub != -1: i64 date                   0x463ff5 (8 bytes) -> env+0xf88; 0x463c10 passes it to
                                             _localtime64 and uses tm_mday/tm_mon (+0xc, +0x10)
        if sub >= 1: f32, f32                0x46404d, 0x46409a -> env+0xf98, +0xf94
jump table on v at 0x6d2d6c (index v - 6; 0x6d3290):
    v 11..13: vec4 colour                    0x6d2d73 -> world+0x858c
    v 9..13:  KUID                           0x6d2d82 -> world+0x41a0
    v 8..13:  object container               0x6da060 -> vtable+0x28 of world+0x803c = 0x886be0
    (v 1..5 jump to 0x6d30cc instead; not traced)
object container (0x886be0):
    u32 cver                                 0x886c0a; anything outside 1..5 ends the container
    if cver >= 5: u32 word                   0x886c82
    cver 1: no items; cver 2: items not self-sized (not handled);
    cver 3..5: items until index == -1       0x886800
        { i32 index, u32 tag (reversed, e.g. 'Rule'), KUID, u32 size (<= 0xFFFFFF), size bytes }
                                             the game checks it ends at start + size (0x886b8f)
    if cver >= 4: u32 size, size bytes       0x8858d0; with this loader's flags (0x6da07f: 1, 0) the
                                             game seeks past them (0x885949..0x885972). Inside: 0xFEEDCEEB,
                                             u32 version, [u32], records ending in -1 (0x8859b2..0x885c1d)
if v < 13: ground-texture table              0x6d2db8..0x6d2ef8
    i32 n; min(n, 250) × { f32, KUID }       loop bound 0x1f40 / 0x20 = 250
```

After that, `0x6d1c90` and `0x6d3350` run. They don't read this stream
(`0x6d3350` builds file paths, probably for the other `mapfile.*` files).

**Checked:** with these rules, the world data of **all 50 `mapfile.gnd`**
ends exactly at the first section, in both the Python checker and the C++
`parseGroundWorld`. The version-11 `razorback_railway_v6.gnd` fits as well
(Python). The version-10 `robescenario.gnd` uses container version 2, whose
items aren't self-sized, so it isn't decoded.

**Observed on the 50 routes:**
- Sub-version: −1 on 42 routes, 0 on 7 and 1 on 1.
- Key count: 7 on 40 routes (6–12 overall).
- Key times lie in [0, 1). They are not always ascending; many routes use
  0.72, 0.76, 0.78, 0.0, …, which fits a time of day that wraps round.
- Every vec4 component is in [0, 1], and the 4th component of each key colour
  is 1.0, so these look like RGBA colours.
- The `date` values are 0 or Unix seconds (the largest is 1,247,698,800 =
  July 2009).
- The v ≥ 11 colour always has 0.8 as its 4th component.
- The v ≥ 9 KUID always has user −1: content 6342 on 22 routes, 6351 on 12,
  6353 on 8, 6352 on 3, 6354 and 101293 on 2 each, 8009 on 1.
- Container version: 5 on 49 routes and 4 on 1. 68 items, all tagged `Rule`
  (32-byte payloads on the route checked).
- The tail is 16 bytes on 49 routes (`FEEDCEEB`, 5, 13, −1) and 12 bytes on
  the version-4 one.

**Guesses (not proven):**
- The environment block is the time-of-day lighting: key time and colours,
  for example sun/ambient/fog. `envKuid` might be the sky or environment
  asset.
- The v ≥ 11 colour might be the water colour, and the v ≥ 9 KUID the
  default ground texture or water asset.
- `Rule` items are route rules (session/scenario rules); the payload layout is
  each rule class's own.
- The container `word` (13 on most routes) might be a build or version number.

### Overview map – traced, observed colours

`0x6e4260`: for version ≥ 12 the map at `end` is **64 × 64 u16** (sizes chosen
at `0x6e42fa`..`0x6e431e`: side 0x40, buffer 0x4000). `0x6e4070` expands
each value into 4 bytes:
- byte 0 = bits 11–15 (5 → 8 bits);
- byte 1 = bits 5–10 (6 → 8);
- byte 2 = bits 0–4;
- byte 3 = 0xFF.

The bytes go into a Jet `Bitmap` of format 2 (`Bitmap(Size, Format, data)`
at `0x6e4499`), which is uploaded as a texture.

**Partly traced in `KernelStandard_7c.dll`:**
- `Bitmap::GetInfoTGA` (`0x10008260`) gives **format 2 to 32-bit
  true-colour TGA files** and format 3 to 24-bit ones (`0x10008350`..`0x1000835d`).
- TGA stores those pixels as B, G, R, A. So if the TGA loader copies pixels
  without reordering them, format 2 is B, G, R, A in memory. Then byte 0
  (bits 11–15) is **blue** and bits 0–4 are **red**.
- That "no reordering" step is **not traced**.

**Observed:** the maps look right with bits 0–4 as red, which is what
`overviewToRgb` does. The two agree, but the colour order isn't settled. Versions < 12 skip 18 bytes and read a
128 × 128 map through `0x460800` (`0x6e43af`..`0x6e43ed`); that isn't worked
out, and the C++ reader refuses versions < 12.

## Still unknown

1. What the world fields mean: the environment colours and the two floats,
   the colour and KUID, the container `word`, `Rule` payloads and the tail
   records. Also the version < 7 environment (`0x463cb0`) and container
   version 2.
2. The meaning of the vertex `extra` byte and the cell value, and the
   real-world spacing and orientation of the grid.
3. How the 250 texture slots map to ground-texture assets at draw time.
4. The overview colour order (whether the TGA loader reorders bytes for
   format 2).
5. `mapfile.trk` (track splines) and `mapfile.obs` (placed objects). Together
   with the meshes, they would give a route viewer.
