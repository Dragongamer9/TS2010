# `.im` / `.pm` – Jet indexed meshes (`JIRF` form `IDXM`)

- **Code:** `src/content/MeshFile.{h,cpp}`. Viewer helper:
  `src/view/MeshPreview.{h,cpp}`.
- **Command-line tool:** `mesh-tool info / preview / obj`. Output goes to
  `~/.local/share/trainz-native/meshes/`.
- **Confidence words:** as in [`ja.md`](ja.md).

**How this was worked out.** The layout comes **from the data**: the reader
code in `Render3DFoundation_7c.dll` (`IndexedMeshDefinition`,
`IndexedMeshDefinitionOld`) has not been traced. It was checked with a Python
prototype on every `.im` and `.pm` in `Base.ja` and `Builtin\8.ja`:
- 7,982 mesh parts in total;
- **every `GEOM` and `MATL` chunk is accounted for byte by byte**;
- every index is below its vertex count;
- the normals are unit length (> 99%; the rest are zero-length normals on
  degenerate faces).

A real mesh, the 70-ton ore car body (`kuid2 90361 120204 2`), was rendered
from end and side and looks right. The C++ reader has been tested on made-up
files in CI only.

## 1. File

```
"JIRF" u32 size "IDXM"
  "INFO"  44 bytes (version 102) or 68 bytes (version 104); meaning not worked out
  "CHNK"  one per material part:
     u32 100   u32 part index
     "MATL" ...   material, see 3.
     "GEOM" ...   geometry, see 2.
     "NINF" ...   only in .pm (progressive meshes); not parsed
  "INFL" / "ATCH" / "SKEL" / "PMIN"   optional top-level chunks; not parsed
```

Counts seen in `8.ja` + `Base.ja`:

| Top-level chunk | Count |
|---|---:|
| `INFL` | 2,023 |
| `ATCH` (probably attachment points) | 299 |
| `SKEL` (skeleton) | 145 |
| `PMIN` (`.pm` only) | 26 |
| `NINF` (inside `.pm` parts) | 106 |

## 2. `GEOM` – geometry (checked)

| Version | Header | Count in data |
|---|---|---:|
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
u32 nameLen            NUL-padded name follows
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

## 4. Not done yet

- `INFO`, `INFL`, `ATCH`, `SKEL`, `PMIN`, `NINF`.
- What the header float and the v201 extra values mean.
- Tracing the reader in `Render3DFoundation_7c.dll` to confirm the field names.
- A real GPU model viewer. `mesh-tool preview` is a small software renderer
  for checking by eye, and `mesh-tool obj` exports to Blender and other viewers.
- Exported texture coordinates flip `v` (`1 − v`); that's a guess and needs a
  visual check in a viewer.
