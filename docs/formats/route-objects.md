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
| `tkBr` | `rBkt` | *(no name)* | 14,173 |
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

In `.trk` the first container holds the stretches (`tkSt`, plus `tkBr` in most) and the
second the vertices (`tkVx`) in every non-empty disc file; 33 `.trk` files hold two empty
containers (observed). The first `tkSt` payloads begin with
pairs of small increasing numbers (0, 1), (1, 2), … that look like vertex indices (guess).

## Not known yet

- Every class's payload layout: each class has its own serializer, none traced yet.
- `fileWord` and the container `word`; the meaning of `index` for `.obs` (not 0..n−1).
- Container version 2 (items without sizes), used by one old route.
- The names behind `tkSt`, `tkVx`, `tkBr`, `tsO2`, `cmOb`, `trCt`.
