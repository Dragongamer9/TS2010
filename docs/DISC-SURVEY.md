# Whole-disc survey (2026-11-10)

Every content file type the port reads was checked across the **whole TS2010
disc**: `Base.ja`, `Data.ja` and `Builtin\1.ja`–`8.ja`.

- **Files covered:** 170,159 archive entries in total. Every `.texture`,
  `.im`/`.pm`, `.kin`, `config.chump`, `config.txt` and `mapfile.gnd` was
  decoded and checked.
- **How it was run:** the archives were unpacked read-only from the ISO into a
  temp folder on this PC (not into the repo). The checks used Python
  prototypes of the same rules the C++ readers use.
- **Not covered by this survey:** the C++ readers themselves on `1.ja`–`8.ja`.
  CI runs them on `Base.ja`, `Data.ja` and a subset of `8.ja`; the full set
  needs Dad's install.

## Results

| What | Files / parts | Result |
|---|---:|---|
| Textures (`.texture`) | 89,524 | **all fit** their size rule. INFO versions 263 (89,189), 262 (195), 259 (140). Formats: DXT1 44,311 · RGB0888 27,832 · DXT5 11,860 · ARGB8888 5,228 · DXT3 293 |
| Not really a texture | 1 | `6.ja` `parkeerplaats.texture.texture` is a text descriptor (`Primary=parkeerplaats.bmp`); the reader now says so |
| Mesh geometry (`GEOM`) | 105,178 parts | fit the layout, except the parts of the 65 damaged files below. Versions 200 (80,535), 201 (21,753), 102 (2,837), 101 (53) |
| Mesh materials (`MATL`) | 105,182 | **all fit** once **UTF-16 text** is understood (56 materials: bit 30 of a text length) |
| Meshes with extra bytes after the data | 476 files | the content is fine; many end with the same 120-byte `TZL…` trailer (probably a tool's stamp), some with `0d 0a`. Readers ignore the extra bytes |
| **Damaged meshes** | **65 files** in `1.ja`–`4.ja` | a text-mode transfer turned every `0x0A` into `0x0D 0x0A` **before** the disc was made. Every one is exactly repairable (see below) |
| Animations (`.kin`) | 2,629 | **all fit** |
| `config.chump` | 19,112 | **all parse**; all 17,388 top-level asset KUIDs match their `hash-XX/kuid…` folder |
| `config.txt` | 70 | **all parse** after one grammar fix: a string value may start on the next line (`3.ja` `kuid -13:151000/train`) |
| Route terrain (`mapfile.gnd`) | 50 routes, 15,193 sections | **all** laid out back to back to the end of the file; `Secs` 15,237 tags, `Sec5` only 6; 5 routes have two sections at one position |

## The damaged meshes

All 65 damaged files are in `Builtin\1.ja`–`4.ja`, and none are in `5.ja`–`8.ja`,
`Base.ja` or `Data.ja`. Examples:
- `kuid 62456 55189/br189.pm`;
- `ind_brake_lever.pm`;
- the `br189_bogey_shadow.pm`;
- `kuid2 316 10010 4/cpr_budd_chateau_sleeper_shadow.pm`;
- `kuid 46162 1040210/sd40_2_bn_shadow.pm`.

**Proof it's text-mode damage:** each file is exactly *declared size + number of
`0d 0a` pairs* bytes long. After turning every `0d 0a` back into `0a`, all of
them parse perfectly: sizes, `GEOM`, `MATL` and indices.

**What the code does:**
- **The strict readers reject these files**, which is probably what the game
  does too (likely showing nothing for that part).
- **`repairTextModeDamage()`** (`src/content/JirfFile.h`) undoes the damage
  only when the size rule proves the repair is exact.
- **`parseMeshOrRepair()`** uses it. `mesh-tool` and `model-viewer` call that,
  and print a warning when they repair.

## What changed in the code because of this survey

- `MATL` text fields: UTF-16 when bit 30 of the length is set.
- `config.txt`: a string value may start on the line after its key.
- Textures: INFO version 262 is known; a text descriptor gets a clear error.
- `repairTextModeDamage()` / `parseMeshOrRepair()` for the 65 damaged meshes.
- `mapfile.gnd`: the two header words after the section count are **not**
  constants. They vary per route (e.g. −2/5, 0/1, 42/−10) and look like
  signed numbers, perhaps an origin. Their meaning is unknown.
