# `.im` / `.pm` – Jet indexed meshes (`JIRF` form `IDXM`)

- **Code:** `src/content/MeshFile.{h,cpp}`. Viewer helper:
  `src/view/MeshPreview.{h,cpp}`.
- **Command-line tool:** `mesh-tool info / preview / obj`. Output goes to
  `~/.local/share/trainz-native/meshes/`.
- **Confidence words:** as in [`ja.md`](ja.md).

**How this was worked out.** The layout comes **from the data**: the reader
code in `Render3DFoundation_7c.dll` (`IndexedMeshDefinition`,
`IndexedMeshDefinitionOld`) has not been traced. It was checked with a Python
prototype on every `.im` and `.pm` in `Base.ja` and `Builtin\8.ja`
(`Data.ja` was missed at first; see below):
- 7,982 mesh parts in total;
- **every `GEOM` and `MATL` chunk is accounted for byte by byte**;
- every index is below its vertex count;
- the normals are unit length (> 99%; the rest are zero-length normals on
  degenerate faces).

A real mesh, the 70-ton ore car body (`kuid2 90361 120204 2`), was rendered
from end and side and looks right.

**C++ on real data:** CI runs the reader on `Base.ja`, `Data.ja` and a real
subset of `8.ja` (see `testdata/README.md`). The first run found one mesh in
`Data.ja` with GEOM **version 101**, which the survey had missed. Its layout
fits exactly with one header word fewer than version 102.

**Whole disc:** See [`../DISC-SURVEY.md`](../DISC-SURVEY.md) for the check across the whole disc (all 8 `Builtin` archives). It found UTF-16 text in `MATL`, 65 meshes damaged by a text-mode transfer before the disc was made, and 476 files with harmless extra bytes at the end.

## 1. File

```
"JIRF" u32 size "IDXM"
  "INFO"  44 bytes (version 102) or 68 bytes (version 104); meaning not worked out
  "CHNK"  one per material part:
     u32 100   u32 part index
     "MATL" ...   material, see 3.
     "GEOM" ...   geometry, see 2.
     "NINF" ...   only in .pm (progressive meshes); not parsed
  "ATCH"  attachment points, see 4. (read)
  "INFL" / "SKEL" / "PMIN"   optional top-level chunks; not parsed
```

Counts seen in `8.ja` + `Base.ja`:

| Top-level chunk | Count |
|---|---:|
| `INFL` | 2,023 |
| `ATCH` (attachment points, see 4.) | 299 |
| `SKEL` (skeleton) | 145 |
| `PMIN` (`.pm` only) | 26 |
| `NINF` (inside `.pm` parts) | 106 |

## 2. `GEOM` – geometry (checked)

| Version | Header | Count in data |
|---|---|---:|
| 101 | `101, 4, f, nv, nt, ni, nf` (28 bytes) | 1 (`Data.ja` `surveyor/mark/mark.pm`) |
| 102 | `102, 4, f, nv, nt, ni, nf, 0` (32 bytes) | 112 |
| 200 | `200, 4, f, nv, nt, ni, nf, 0, nameLen` (36 bytes) | 5,790 |
| 201 | `201, 4, flags, f, nv, nt, ni, nf, 0, nameLen` (40 bytes) | 2,080 |

- `nv` vertices, `nt` triangles, `ni` = 3 × `nt` indices, `nf` = `nt` face normals.
- `f` is one float whose meaning is unknown.
- The second word is always 4.

**Then, in this order:**

```
name[nameLen]          v200/201 only: a NUL-padded name, e.g. "b.r.door1", "b.r.load".
                       Present (nameLen 8..28) in 1,118 parts (14%). Probably the
                       bone/attachment the part follows when animated (not confirmed).
vertex[nv]             x, y, z, u, v   (5 floats, 20 bytes); Z is up
u16 index[ni]          triangle list
float vertexNormal[nv][3]
float faceNormal[nf][3]
float extra[nv][3]     v201 with flags & 1 only (40 parts); meaning unknown
```

- **The size rule:** `header + nameLen + 20·nv + 2·ni + 12·nv + 12·nf (+ 12·nv)`
  equals the chunk size for **all** parts.
- **Indices are 16-bit**, so a part has at most 65,536 vertices.

## 3. `MATL` – material (checked)

```
u32 version            102 (5,904) or 103 (2,078)
u32 nameLen            NUL-padded name follows; if bit 30 (0x40000000) is set the text is UTF-16LE
                       and the low 30 bits are its byte count (56 materials on the disc). Same for texture paths.
char name[nameLen]     e.g. "*shed*6A392CC2*floor*m.onetex" or "wall.m.tbumptex"
float values[16]       colours and shininess (e.g. 1.0s, 0.1255s, 10.0); individual meaning not confirmed
u32 textureCount
  per texture: u32 type, u32 pathLen, char path[pathLen], float amount
```

- **Every `MATL` ends exactly** after its last texture.
- **The material kind** is the `m.xxx` ending of the name. Seen: `m.onetex`,
  `m.notex`, `m.reflect`, `m.gloss`, `m.tbumptex`.
- **Texture paths are the artist's original Windows paths**, for example
  `C:\Documents and Settings\…\wall.texture`. The file name at the end is the
  `.texture` to use.
- **Texture types seen:** 1 (colour map, 7,912), 9 (reflection map, 274),
  8 (normal map, 40), 5 (31; meaning unknown).
- **Where the texture lives:**
  - 6,885 of 8,257 references are in the **same folder as the mesh**; 1 is
    one folder up.
  - **1,371 are nowhere in the asset.** They probably come from another asset
    or are missing. How the game resolves those is **unknown**.

### Material kinds on the disc

All 108,674 mesh parts in `Base.ja`, `Data.ja` and `Builtin\1.ja`–`8.ja`, by the material
kind (the `m.…` at the end of the material name; the `*…*` pieces before it are the mesh and
part names, not flags). Texture types are the `MATL` texture slots (1 = colour map). The
alpha columns sample up to 401 colour maps per kind at a 64 px mip: all solid, a cut-out mix
(as `wantsAlphaTest` reads it), or smooth.

| Kind | Parts | Texture types (count) | Colour alpha: solid / cut-out / smooth |
|---|---:|---|---|
| `m.onetex` | 95,441 | 1 (95,431), few others | 323 / 65 / 13 |
| `m.reflect` | 8,162 | 1 (8,073), 9 (8,306) | 401 / 0 / 0 |
| `m.tbumptex` | 1,343 | 1 (1,343), 8 (1,320) | 342 / 34 / 25 |
| `m.notex` | 822 | none (2 with 11) | – |
| `m.gloss` | 422 | 1 (422), 9 (422) | 11 / 113 / 277 |
| (no kind) | 312 | 1 (304), 8 (111), 9 (20) | 186 / 20 / 87 |
| `m.tbumpgloss` | 172 | 1 (172), 8 (161), 9 (171) | 163 / 2 / 7 |

**Inferred, not traced or documented here:** type 9 goes with reflection and gloss (an
environment map?), type 8 with the bump kinds (a normal or bump map?); in `m.gloss` the colour
map's alpha is mostly smooth, so it is taken as a gloss amount, not transparency. The 3D
view therefore never draws `m.gloss`, `m.tbumpgloss` or `m.reflect` alpha as a cut-out
(`materialMayCutOut`); without that, 113 sampled `m.gloss` textures would have had holes.

## 4. `ATCH` – attachment points (checked on the whole disc)

Named points that other parts hang on: bogies (`a.bog0`, `a.bog1`), couplers
(`a.limfront`, `a.limback`), loads, lights, effects, passenger and track ends
(`a.track0a`/`a.track0b`), and so on.

```
u32 100                version (the only value seen)
u32 count
then count times:
  u32 nameBytes        bit 30 set: the name is UTF-16LE (32 names); the low bits are the byte count
  name[nameBytes]      NUL-padded (8-bit names: lengths are multiples of 4)
  float rotation[9]    a 3x3 rotation
  float position[3]    metres, in the mesh's own space (Z up)
```

**Whole disc (observed):** all 5,868 `ATCH` chunks in `Base.ja`, `Data.ja` and
`Builtin\1.ja`–`8.ja` (`.im` and `.pm`) decode exactly to their end: 113,760 points, and
every rotation has unit-length rows. The 70 t ore car's `a.bog0`/`a.bog1` sit at
y = ∓2.311 m, z = 0.419 m; a 1 km straight track piece has `a.track0a`/`a.track0b` at
y = ∓500 m, the `0a` end turned 180° about z. This also shows the position is the last 3 floats.

**Not traced:** the index order of the rotation. 99% of bogie points are the identity or
180° about z, where the order makes no difference; for the rest the port assumes Direct3D's
(`rotation[0..2]` is where the point's x axis points, `[3..5]` its y, `[6..8]` its z).
A damaged `ATCH` is reported as a warning and the mesh still loads.

## 5. Not done yet

- `INFO`, `INFL`, `SKEL`, `PMIN`, `NINF`; the index order of the `ATCH` rotation.
- What the header float and the v201 extra values mean.
- Tracing the reader in `Render3DFoundation_7c.dll` to confirm the field names.
- A real GPU model viewer. `mesh-tool preview` is a small software renderer
  for checking by eye, and `mesh-tool obj` exports to Blender and other viewers.
- Exported texture coordinates flip `v` (`1 − v`); that's a guess and needs a
  visual check in a viewer.
