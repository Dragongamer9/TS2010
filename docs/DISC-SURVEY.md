# Whole-disc survey (2026-11-10)

Every content file type the port reads was checked across the **whole TS2010
disc**: `Builtin\1.ja`–`8.ja`, and `Base.ja` / `Data.ja` in separate scans.

- **Files covered:**
  - **The main survey** went through `Builtin\1.ja`–`8.ja`: 170,167 archive
    entries, in which every `.texture`, `.im`/`.pm`, `.kin`, `config.chump`,
    `config.txt` and `mapfile.gnd` was decoded and checked.
  - **`Base.ja` and `Data.ja`** were covered by the separate mesh / damage
    scans. Their configs were checked earlier with the prototype, and they run
    in full in CI.
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
| **Damaged meshes** | **65 files** in `1.ja`–`4.ja` | **most likely** a text-mode transfer turned every `0x0A` into `0x0D 0x0A` before the disc was made. Every one can be repaired into a consistent file (see below) |
| Animations (`.kin`) | 2,629 | **all fit** |
| `config.chump` | 19,112 | **all parse**; all 17,388 top-level asset KUIDs match their `hash-XX/kuid…` folder |
| `config.txt` | 70 | **all parse** after one grammar fix: a string value may start on the next line (`3.ja` `kuid -13:151000/train`) |
| Route terrain (`mapfile.gnd`) | 50 routes, 15,243 sections | **all** laid out back to back to the end of the file; `Secs` 15,237 tags, `Sec5` only 6. **Section data (re-checked 2026-11-10 with the traced layout):** every section decodes to exactly its end offset. No two sections share a position; the "5 routes with two sections at one position" came from the old, wrongly grouped index (see `formats/route.md`) |

## The damaged meshes

All 65 damaged files are in `Builtin\1.ja`–`4.ja`, and none are in `5.ja`–`8.ja`,
`Base.ja` or `Data.ja`. Examples:
- `kuid 62456 55189/br189.pm`;
- `ind_brake_lever.pm`;
- the `br189_bogey_shadow.pm`;
- `kuid2 316 10010 4/cpr_budd_chateau_sleeper_shadow.pm`;
- `kuid 46162 1040210/sd40_2_bn_shadow.pm`.

**Why text-mode damage is the likely explanation** (strong evidence, not
proof):
- Each file is exactly *declared size + number of `0d 0a` pairs* bytes long.
- After turning every `0d 0a` back into `0a`, all of them parse cleanly:
  sizes, `GEOM`, `MATL` and indices.
- That doesn't prove the result is byte-for-byte the original file, or how
  the damage happened.

**What the code does:**
- **The strict readers reject these files**, which is probably what the game
  does too (likely showing nothing for that part).
- **`repairTextModeDamage()`** (`src/content/JirfFile.h`) applies the
  reversal only when the result is a consistent file: its declared size
  matches its length. A consistent file is not guaranteed to be the original
  bytes.
- **`parseMeshOrRepair()`** uses it. `mesh-tool` and `model-viewer` call that,
  and print a warning when they repair.

## What changed in the code because of this survey

- `MATL` text fields: UTF-16 when bit 30 of the length is set.
- `config.txt`: a string value may start on the line after its key.
- Textures: INFO version 262 is known; a text descriptor gets a clear error.
- `repairTextModeDamage()` / `parseMeshOrRepair()` for the 65 damaged meshes.
- `mapfile.gnd`: byte 3 is a version (13 on every disc route). The "two header words" after the
  section count were the x, y of section 0: an index entry is `{x, y, tag, start, end}`
  (traced in `trainz.exe` 0x6d2aa0; corrected 2026-11-10).
