# Route objects and track: `mapfile.obs`, `mapfile.trk`

Reader: `src/content/ObjectContainer.{h,cpp}` (`parseRouteObjectFile`,
`readObjectContainer`, `objectClassName`). Checker: `tools/dev/route_objects.py`.
Tests: `tests/test_route_objects.cpp`, `RealInstall.RouteObjectFilesRead`.

Evidence classes: **traced** (read in `trainz.exe` disassembly), **checked** (traced and
every disc file fits), **observed** (seen in the data only), **guess**.

## File layout (container traced; file framing observed)

```
u32 fileWord                    .trk: 2 (all 85); .obs: 9 (48), 8 (36), 7 (1). Meaning not traced.
container × N, until the end    .trk: 2 containers; .obs: 1
```

Each **container** is the game's object container, the same one the `mapfile.gnd` world
block uses (reader `0x886be0`, item loop `0x886800`):

```
u32 version                     1..5; any other value ends the container (no items)
if version >= 5: u32 word       meaning not traced (equals fileWord in all 255 disc containers)
if version == 2: items without sizes (not handled)
if version >= 3: items until index == -1:
    i32  index
    u32  tag                    class tag; the bytes are reversed (file "JBOm" = "mOBJ")
    u32  content, u32 packed user/version   (KUID, read by 0x80c8d0)
    u32  size                   at most 0xFFFFFF (0x8869b8)
    u8   payload[size]
if version >= 4: u32 n, u8 bytes[n]   skipped by the game; 16 bytes on the disc:
                                      FEEDCEEB, version, word, -1
```

**Checked on the whole disc** (all 8 `Builtin` archives; Python checker and C++ reader agree):
170 of 172 files read to their exact last byte, holding 2,539,231 objects. The 2 that don't
are the version-10 `robescenario` route, whose containers are version 2.

## Object classes (traced registrations)

Each class registers its tag with the factory at `0x807eb0`, mostly with its asset-kind
name pushed next to it (39 registrations in all; the others are UI elements):

| Tag (readable) | In the file | Class name | Objects on the disc |
|---|---|---|---:|
| `mobs` | `sbom` | `mapobjectstub` | 1,657,171 |
| `tkVx` | `xVkt` | *(no name; track vertex, guess)* | 424,591 |
| `tkSt` | `tSkt` | *(no name; track stretch, guess)* | 307,270 |
| `mOBJ` | `JBOm` | `mapobject` | 78,677 |
| `tsO2` | `2Ost` | *(unconfirmed: one `trackside` registration has no tag nearby)* | 23,619 |
| `tkBr` | `rBkt` | *(no name; a wrapped stretch, see below)* | 14,173 |
| `sgOb` | `bOgs` | `mosignal` | 13,806 |
| `jnOb` | `bOnj` | `mojunction` | 10,138 |
| `cmOb` | `bOmc` | *(no name)* | 3,734 |
| `obCr` | `rCbo` | `mocrossing` | 2,318 |
| `mIND` | `DNIm` | `industry` | 1,490 |
| `mFXT` | `TXFm` | `fixedtrack` | 1,331 |
| `mBUI` | `IUBm` | `buildable` | 846 |
| `ttOb` | `bOtt` | `turntable` | 68 |

Registered but not seen in route files: `scOb` scenery, `tsOb` trackside, `flOb`
floatscenery, `mkOb` motrackmark, `tgOb` motrigger, `spOb` mospeedboard, `mPAS`
passenger-station, `DvrS` driver-character, `gslb` library, `BHVR` behavior (the `RVHB`
bytes in `profile.dat`), `Rule` (the `.gnd` world items), `trCt`.

## Track vertices and stretches (observed; reader `src/content/TrackLayout`)

The track classes' own readers are not traced; these layouts come from the data and
are checked against all 50 disc routes.

**`tkVx` (track vertex), 41 bytes, version 3 on all 406,207 disc vertices:**

```
u32 version (3)   i16 boardX   i16 boardY   f32 x   f32 y   f32 z
4 × { i32 piece (-1 = empty), u8 flag }   u8 last
```

The 4 slots list every track piece (`tkSt` or `tkBr`) that ends at this vertex: on the
disc, 614,430 of 614,432 links point at a piece that really ends there. The other 2 are
**unexplained** and left as they are (the reader reports them as warnings and repairs
nothing). Both are in "ECML Kings Cross - York" (`<kuid:447264:1019>`, `Builtin\5.ja`):

| Vertex | Its slots (piece, flag) | Problem |
|---:|---|---|
| 33398 | (33, 0), (41, 1) | no track piece 33 in the file |
| 33490 | (129, 0), (135, 1) | no track piece 129 in the file |

33 and 129 exist only as vertex numbers in the second container; whether that is a
coincidence or a different kind of link is not known. Slots used per vertex: 1 (211,040, line ends), 2 (182,491,
along a line), 3 (12,242; junctions, guess: there are 10,138 `mojunction` objects), 4
(421), 0 (13). The `flag` is not simply "start" or "end" of the piece; its meaning and
`last` (0, 1 or 2) are not known.

`boardX/boardY` name the terrain section (baseboard) the vertex lies on; `x`/`y` are
metres inside it (0..720, toward +x and +y); `z` is the height in metres. Evidence: 99.5%
of disc vertices fall inside a terrain section, and there `z` matches the terrain height
(sampled with the grid layout in [route.md](route.md), 10 m cells) within 1 m for 66%
(mean difference 2.07 m; track also runs on embankments and bridges). With the rows
flipped the same test gives 39% (mean 11.84 m), and with x/y swapped similarly poor, so
the layout and the terrain grid orientation confirm each other.

**`tkSt` (track stretch), versions 6 (30 or 29 bytes) and 4 (24 bytes):**

```
u32 version   i32 from   i32 to   u32 content   u32 packed user/version   u8 rest[] (not decoded)
```

`from`/`to` are vertex indices: both exist for all 293,750 disc stretches; the KUID
always equals the item's own KUID (the track asset).

**`tkBr`, 88 or 94 bytes:** `u32 2`, then a whole stretch as above (version, from, to,
KUID, …), then more fields not decoded (on the test route: three `i32` values each
followed by sixteen `ff` bytes). Its ends are the two vertices that link to it. With
`tkSt` that makes 307,226 track pieces on the disc, every one with both ends present.
What `tkBr` adds over a stretch (bridge? attached objects?) is not known. Median length between the two
ends: 52 m (99th percentile 661 m). How a stretch curves between its ends (the `rest`
bytes, or the vertices' own undecoded bytes) is not known.

In `.trk` the first container holds the stretches (`tkSt`, plus `tkBr` in most) and the
second the vertices (`tkVx`) in every non-empty disc file; 33 `.trk` files hold two empty
containers (observed). The first `tkSt` payloads begin with
pairs of small increasing numbers (0, 1), (1, 2), … that look like vertex indices (guess).

## Not known yet

- Every class's payload layout: each class has its own serializer, none traced yet.
  `tkVx` and `tkSt` are partly decoded from the data (above); the rest is not.
- `fileWord` and the container `word`; the meaning of `index` for `.obs` (not 0..n−1).
- Container version 2 (items without sizes), used by one old route.
- The names behind `tkSt`, `tkVx`, `tkBr`, `tsO2`, `cmOb`, `trCt`.
