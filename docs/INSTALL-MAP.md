# Trainz Simulator 2010 – Install and Content Map

This document maps every file type and content storage mechanism in a Trainz Simulator 2010 (TS2010) installation, based strictly on the reverse engineering, format specifications, and whole-disc data surveys conducted in this repository.

Dad's game files are only ever read. Nothing in his install is modified, patched, or run.

---

## 1. Status Classifications

Every item is marked with one of three confidence levels:

- **Known**: Traced directly in the game's disassembled code (`trainz.exe`, `KernelStandard_7c.dll`, `RenderSystem_7c.dll`, etc.) and/or verified against real data across the TS2010 disc archives.
- **Guess**: An empirical observation, mathematical model (e.g. track curvature Hermite splines), or inferred meaning that fits the observed data well but lacks definitive disassembly proof or official specification.
- **Left**: Unmapped, undecoded, format unknown, or not yet implemented (e.g. TrainzScript VM, proprietary TAD database internals, `.gse` decryption).

---

## 2. Content Storage and Indexing

TS2010 does not store loose content files in open directories for its core assets. It uses a structured archive and hashing system:

```
[TS2010 Installation Root]
├── Base.ja                   Core shared assets (1,722 files)
├── Data.ja                   Engine UI, surveyor, desktop, and sounds (973 files)
├── Builtin/
│   ├── 1.ja … 8.ja           Built-in routes, rolling stock, scenery (170,167 entries, ~7.3 GB)
├── UserData/
│   ├── assets.tdx            Asset database index (TADDaemon)
│   ├── local/                User-installed / custom content
│   ├── original/             Unmodified originals of edited assets
│   ├── editing/              Open-for-edit asset folders
│   ├── cache/                Precompiled shaders, configs, and temporary caches
│   ├── trash/                Deleted assets
│   └── settings/             Configuration files (e.g., keyboard.txt)
├── bin/                      Jet engine executables, DLL plugins, Mozilla Gecko runtime
├── scripts/                  Base TrainzScript (.gs) source files (97 files)
├── libraries/                Precompiled script libraries (.gsl) (97 files)
└── trainzoptions.txt         Engine startup flags and hardware settings
```

### 2.1 Jet Archives (`.ja`)
- **Status**: **Known**
- **Specification Doc**: [`formats/ja.md`](formats/ja.md)
- **Role**: Primary container format for built-in and core content (`Base.ja`, `Data.ja`, `Builtin\1.ja`–`8.ja`). Mounted by the engine as named resource databases (`Jet::ResourceDatabase`).
- **Container Structure**:
  - **Header**: 8-byte `"ARCHINFO"`, 32-byte info block containing magic `'ARC0'` (`0x41524330`), version `0x102` (engine accepts `0x101`–`0x102`), file count, and absolute offset to the `"IDX "` block.
  - **File Blocks**: Sequence of `"FIL "` blocks. Each has an 8-byte header (`"FIL "` + block size), a 260-byte NUL-terminated relative filename, original size, stored size, a 4-byte hash (algorithm unknown, not CRC32), 12 reserved bytes, 4-byte flags (bit 7 `0x80` = compressed), followed by `stored_size` raw payload bytes.
  - **Index Block**: Located at `idx_offset`. Starts with `"IDX "` tag. Consists of `file_count` records of exactly 296 (`0x128`) bytes each: absolute data offset (pointing directly to payload data), stored size, lowercase NUL-terminated filename (260 bytes), original size, stored size duplicate, hash, 12 reserved bytes, and flags.
  - **Lookup & Canonicalization**: File names are converted to lowercase ASCII (`_strlwr`). Slashes and backslashes are normalized to `/`, `.` segments are stripped, and `..` collapses parent segments. Engine hashes names into buckets via `h = (h << 3) ^ (signed char)c`. On duplicate entries, insertion order is preserved and the first match wins.
  - **Compression (`CFIL`)**: Detected by the `"CFIL"` magic at the start of stored data. Version `0x101`, followed by 4-byte original uncompressed size. Data is stored in blocks: a 16-bit header where bit 15 indicates raw uncompressed bytes (`header & 0x7FFF`), and clear bit 15 indicates Gage's Byte Pair Encoding (BPE) packed data expanding to at most 16 KiB (`0x4000`).
  - **BPE Variant**: Modification of Philip Gage's 1994 algorithm. A pair table of 256 codes is built: byte count > 127 skips `(count - 127)` codes directly (without reading an extra pair as Gage's original did); count ≤ 127 reads `count + 1` entries (`left` byte, plus `right` byte if `left != code`). Data expands via a 256-byte stack.
- **Evidence / Checked**:
  > "Every compressed file sampled from Base.ja and 8.ja (about 30) decodes to a valid file header."
  > "Two files decode to exactly their recorded sizes."
  > "Full pass in C++ (2026-11-10): CI decodes every entry of Base.ja (1,722) and Data.ja (973) to its recorded size, under AddressSanitizer, with no problems. These are the two archives that use CFIL/BPE in the repo's test data; the 8.ja sample is stored uncompressed."
  > "The flag (bit 7, File::Entry::IsCompressed at 0x10015470) matched the magic in all 14,305 index rows of Base.ja, Data.ja and Builtin\8.ja."
  > "Whole disc: every config/texture/mesh/animation/terrain entry in all 8 Builtin archives decoded (Python survey, docs/DISC-SURVEY.md)."

### 2.2 KUID Folder Organization and Hashing
- **Status**: **Known** (folder structure and hash rule verified); **Guess** (exact engine hash implementation untraced in binary).
- **Specification Doc**: [`formats/ja.md`](formats/ja.md) §6, [`formats/asset-index.md`](formats/asset-index.md)
- **Folder Naming Rules**:
  Inside `.ja` archives, assets are organized into hashed subfolders:
  - Plain: `hash-XX/kuid USER CONTENT/...`
  - Versioned: `hash-XX/kuid2 USER CONTENT VERSION/...`
- **Hash Rule (`XX`)**:
  `XX` is the 2-digit hexadecimal representation of the **XOR of all 8 little-endian bytes of `(int32 USER, int32 CONTENT)`**.
- **KUID Identification**:
  - `kuid <kuid:USER:CONTENT>` represents plain assets (version 0).
  - `kuid2 <kuid2:USER:CONTENT:VERSION>` represents versioned assets.
  - For negative user IDs (`USER < 0`), version 127 represents "no version" (plain KUID), matching folder names `kuid _1 CONTENT`.
  - **Bare-Number KUIDs**: 1,018 configs on the disc specify their KUID as a bare number (e.g. `kuid 101135`). 1,006 sit in folders named `kuid _1 <number>`, establishing that bare numbers map to `<kuid:-1:number>`.
- **Evidence / Checked**:
  > "the hash rule matched all 499 asset folders tested in 8.ja, but the code that computes XX has not been traced."
  > "The RealInstall.KuidFolderHashRule test checks it on every folder of every archive in an install."
  > "1,006 of them sit in a KUID-named folder (1,005 bare, 1 quoted), and in every one that folder is kuid _1 <that number>. So the reader takes them as <kuid:-1:number> (user −1, written _1 in folder names)."
  > "With the rule no config is dropped for its KUID: the index goes from 18,264 to 19,282 assets (+1,018, none removed), and every one whose folder is named after a KUID matches it."

### 2.3 Asset Configuration: `config.txt` and `config.chump`
- **Status**: **Known** (`config.chump` header and walk traced, types observed; `config.txt` grammar observed and validated across entire disc).
- **Specification Doc**: [`formats/config.md`](formats/config.md)
- **Role**: Every Trainz asset must contain a configuration defining its KUID, asset kind (`scenery`, `traincar`, `bogey`, `map`, `profile`, `groundtexture`, etc.), mesh references, sound scripts, and operational parameters.
- **Binary Form (`config.chump`)**:
  - Used by most built-in content (19,112 files on the disc).
  - Header: 4-byte `"ACS$"` (`0x24534341`), `u32` version (1), `u32` reserved (0), `u32` body size.
  - Item loop: `u32 size`, `u8 keyLen` (including NUL), `char key[keyLen]`, `u8 type`, `u8 value[...]`.
  - Value types:
    - `0`: Group (nested item list)
    - `1`: Integer array (`int32[]`)
    - `2`: Float array (`float32[]`)
    - `3`: String (NUL-terminated ASCII/UTF-8)
    - `4`: Binary blob (e.g. embedded JPEG thumbnails)
    - `5`: Null (empty value)
    - `13`: Packed KUID: 4-byte packed user/version (`bits 0–24`: signed 25-bit user, `bits 25–31`: 7-bit version), followed by `i32` content ID.
- **Text Form (`config.txt`)**:
  - Used in `Base.ja` and user content.
  - Syntax: Optional UTF-8 BOM, CRLF line endings, key-value pairs (`key value`), block groups (`container { ... }`), quoted multiline strings, and bare scalar or list values. Strings may start on the line following the key.
- **Evidence / Checked**:
  > "Data checks used Base.ja, Data.ja and Builtin\8.ja from the disc. They were done with a Python prototype of the same rules. Since 2026-11-10 the C++ reader also runs in CI on real game data (Base.ja, Data.ja and a real subset of 8.ja; see testdata/README.md): 1,639 configs parse, and all 1,468 built-in assets' KUIDs match their folders."
  > "Whole disc: all 19,112 config.chump and 70 config.txt files parse; all 17,388 top-level asset KUIDs match their folders. See ../DISC-SURVEY.md for the check across the whole disc (all 8 Builtin archives)."
  > "Checked: the top-level kuid of all 1,468 config.chump files in Builtin\8.ja matches the folder it sits in. That includes kuid2 113998 9002 127, where version 127 sets the top bit."

### 2.4 The Asset Index (`AssetIndex`)
- **Status**: **Known** (indexing mechanism, scanning order, KUID resolution); **Left** (runtime collision arbitration, TAD cache format, `.cdp` archive extraction).
- **Specification Doc**: [`formats/asset-index.md`](formats/asset-index.md)
- **Asset Definition**:
  - An asset is any directory containing a `config.txt` or `config.chump`.
  - The KUID declared inside the config takes absolute precedence over folder names.
  - Sub-configs located in subdirectories of an asset (e.g. cabin controls) are sub-parts, not separate assets.
  - If a directory contains both forms, `config.chump` takes precedence.
- **Scan Order**:
  1. `Base.ja`
  2. `Data.ja`
  3. `Builtin/*.ja` (sorted alphanumerically: `1.ja` through `8.ja`)
  4. `UserData/local` (unpacked directory tree up to 6 levels deep)
- **Database Daemon (`TADDaemon.exe`)**:
  - Manages the `assets.tdx` master index.
  - Resolves dependencies (`kuid-table`), search queries, and obsolete replacements (`obsolete-table`).
- **Evidence / Checked**:
  > "Checked on the disc (Python prototype of the same rule):
  > Builtin\8.ja: 1468 assets, 1 sub-part
  > Base.ja: 169 assets, 0 sub-parts
  > Data.ja: 1 asset, 0 sub-parts"
  > "Real data: 1,626 assets indexed in CI, no KUID warnings. Bare-number KUIDs ... now read: 1,018 configs recovered, none dropped for its KUID, 19,282 assets indexed."

---

## 3. Comprehensive File Type Catalog

| Extension / Name | Role in TS2010 | Format Summary | Status | Evidence & Format Reference |
|---|---|---|---|---|
| **`.ja`** | Archive package | `"ARCHINFO"`, `'ARC0'` header, `"FIL "` data blocks, 296-byte `"IDX "` rows, BPE/CFIL compression | **Known** | [`formats/ja.md`](formats/ja.md)<br>• CI decodes every entry of `Base.ja` (1,722) and `Data.ja` (973)<br>• All 170,167 entries across `Builtin\1.ja`–`8.ja` decoded in disc survey |
| **`config.chump`** | Binary asset configuration | Binary tree (`ACS$` magic, `u32` body size, typed items, type 13 packed KUIDs) | **Known** | [`formats/config.md`](formats/config.md)<br>• All 19,112 `.chump` files on the disc parse cleanly<br>• All 17,388 top-level asset KUIDs match their folders |
| **`config.txt`** | Text asset configuration | UTF-8 text, CRLF, `key value` and `container { ... }` blocks | **Known** | [`formats/config.md`](formats/config.md)<br>• All 70 `config.txt` files on disc parse cleanly<br>• Traced `Jet::ConfigData` parser in `KernelStandard_7c.dll` |
| **`.im`** | 3D indexed mesh | `JIRF`/`IDXM` container with `INFO`, `CHNK`, `MATL`, `GEOM` (v101/102/200/201), `ATCH` attachment points | **Known** | [`formats/mesh.md`](formats/mesh.md)<br>• 105,178 parts across disc fit geometry layout<br>• All 105,182 `MATL` chunks fit byte-by-byte<br>• 65 damaged meshes identified and repaired via `repairTextModeDamage` |
| **`.pm`** | Progressive mesh (legacy) | `JIRF`/`IDXM` container with `CHNK`, `GEOM`, `MATL`, plus `NINF` and `PMIN` progressive LOD chunks | **Known** (base); **Left** (`NINF`/`PMIN`) | [`formats/mesh.md`](formats/mesh.md)<br>• Checked with Python prototype across all disc `.pm` files<br>• 476 files have harmless trailing bytes from export tools |
| **`.kin`** | Skeletal animation | `JIRF`/`ANIM` container with `INFO` (fps, frame count), `EVNT` events, `SKEL` (`BONE` hierarchy), `FRAM` transform arrays | **Known** (skeleton/frames); **Guess** (absolute vs relative bone pos); **Left** (events) | [`formats/animation.md`](formats/animation.md)<br>• All 2,629 `.kin` files across disc match layout byte-by-byte<br>• All 135 files in `Base.ja`, `Data.ja`, `8.ja` verified in Python and CI |
| **`.texture`** | Compiled GPU texture | `JIRF`/`MMTF` container with `INFO` header and sequential `MMAP` mip chunks (smallest to largest) | **Known** | [`formats/texture.md`](formats/texture.md)<br>• All 89,524 `.texture` files on disc fit size rules<br>• Formats: ARGB8888 (5,228), RGB0888 (27,832), DXT1 (44,311), DXT3 (293), DXT5 (11,860)<br>• INFO versions 263, 262, 259 verified |
| **`.texture.txt`** | Texture descriptor | ASCII key-value text defining `Primary`, `Alpha`, `Tile` mode, filters, hints | **Known** | [`formats/texture-descriptor.md`](formats/texture-descriptor.md)<br>• Traced parser in `RenderSystem_7c.dll` (`~0x10005aa0`)<br>• All 360 descriptors on disc (all in `Base.ja`) resolve and combine cleanly |
| **`.tga`** | Truevision TARGA image | Standard image header: 16/24/32 bpp, uncompressed or RLE; alpha mask data | **Known** | [`formats/image.md`](formats/image.md)<br>• 11,640 files surveyed on disc; 11,639 decode cleanly<br>• 1 damaged file refused (`Builtin\4.ja nightwindows.tga`)<br>• 165 files carry 0 alpha everywhere (opacity controlled by material) |
| **`.bmp`** | Windows Bitmap image | Standard `BITMAPINFOHEADER`: 4/8/24/32 bpp, uncompressed or RLE8 | **Known** | [`formats/image.md`](formats/image.md)<br>• 2,687 files surveyed on disc, all decode cleanly (including 33 RLE8 bitmaps)<br>• Used for masks, bump maps, and displacement maps |
| **`.jpg`** | JPEG image | JFIF/Exif baseline and progressive scans (YCbCr 4:4:4, 4:2:2, 4:2:0) | **Known** | [`formats/image.md`](formats/image.md)<br>• 6,217 files surveyed on disc (5,518 baseline, 699 progressive); 6,214 decode<br>• 3 truncated files refused as damaged |
| **`mapfile.gnd`** | Route terrain & environment | Binary `"GND"` header, version 13 (`0x0D`), section index, world data block, lazy-loaded `Secs`/`Sec5` terrain sections, 64×64 overview map | **Known** (grid, heights, layers); **Guess** (texture scale/rot byte); **Left** (lighting/env meaning) | [`formats/route.md`](formats/route.md)<br>• All 15,243 sections across 50 disc routes end exactly at expected offsets<br>• Section data loaders traced in `trainz.exe` (`0x82be00`/`0x821200`)<br>• 81% of 27,006 neighbouring section edges match terrain elevations exactly |
| **`mapfile.trk`** | Route track network | Object containers holding track stretches (`tkSt`), bridge/wrapped stretches (`tkBr`), and track vertices (`tkVx`) | **Known** (stretches, vertices, links); **Guess** (Hermite curve model); **Left** (raw payloads) | [`formats/route-objects.md`](formats/route-objects.md)<br>• 170 of 172 disc `.trk` files read to end (2.5M objects)<br>• 307,226 track pieces with both ends present; 614,430 of 614,432 links consistent<br>• 97.0% of session vehicles sit within 0.5 m of curve model |
| **`mapfile.obs`** | Route placed scenery & objects | Object containers storing base block: board coordinates, local x/y/z metres, yaw rotation in radians | **Known** (positions, yaw, classes); **Guess** (pitch/roll); **Left** (specialized class payloads) | [`formats/route-objects.md`](formats/route-objects.md)<br>• Positions decoded for 1,760,978 of 1,761,124 objects across all 50 routes<br>• 75–97% of signals, junctions, crossings, and industries sit directly on terrain |
| **`profile.dat`** | Session rules, trains & state | Binary file with size-prefixed blocks holding object containers: `BHVR` (rules/behaviours), `trCt` (trains), `gslb` (library state) | **Known** (v5 trains, rules, container walk); **Guess** (older train heuristic); **Left** (tail chump, 3 odd rules) | [`formats/session.md`](formats/session.md)<br>• All 138 disc `profile.dat` files read to exact end<br>• 4,251 of 4,254 rules/behaviours decode<br>• All 655 version-5 trains (3,363 vehicles) and 823 of 826 older trains decode |
| **`profile.obs` / `profile.trk`** | Session object & track deltas | Object containers containing session-specific trackside objects (`tsO2`), tracks, and modifications | **Known** (container); **Left** (patching logic onto route) | [`formats/session.md`](formats/session.md)<br>• All 68 files on the disc read to their exact end using route container parser |
| **`.wav`** | Sound audio | Standard RIFF/WAVE: PCM 8/16-bit, MS ADPCM (tag 2), MP3-in-WAV (tag 85) | **Known** (audio decoders); **Left** (soundscripts, 3D world positioning) | [`formats/sound.md`](formats/sound.md)<br>• 5,874 files checked on disc: 5,844 PCM, 26 MS ADPCM, 3 MP3-in-WAV<br>• 1 rejected file (`idle 1.wav`, MPEG-2 program stream with AC-3 audio) |
| **`.mp3`** | Compressed audio | MPEG-1/2/2.5 Layer III streams (standalone in `Base.ja` or embedded in WAV) | **Known** (decoding); **Left** (playback triggers) | [`formats/sound.md`](formats/sound.md)<br>• All 6 MP3 streams (3 `.mp3` + 3 MP3-in-WAV) decode in C++ via libmpg123 and match ffmpeg within 1 LSB |
| **`keyboard.txt`** | Input key bindings | Formatted using `config.txt` syntax, defining action-to-combo mappings | **Known** (parser, key codes); **Left** (mode dispatch, live DIK conversion) | [`formats/keyboard.md`](formats/keyboard.md)<br>• Word parser traced in `trainz.exe` (`0x4d5bb0`/`0x4d4b60`)<br>• All 141 key names and 383 shipped action bindings parse and map to SDL scancodes |
| **`.srt`** | SpeedTree foliage models | Proprietary SpeedTree 5.0 binary format (`SRT 05.0.1`), read by `TNISpeedTree.dll` | **Guess** (billboard bounding box); **Left** (3D mesh geometry) | [`formats/route-objects.md`](formats/route-objects.md), [`ENGINE-MAP.md`](ENGINE-MAP.md)<br>• All 379 disc files match `SRT 05.0.1` header<br>• Bounding box extracted to size billboard quads with atlas textures |
| **`.lm` / `.lm.txt`** | Level-of-detail mesh lists | `.lm.txt` are plain text lists of `.im` meshes by LOD distance. `.lm` binary files are absent from disc | **Known** (`.lm.txt` loading); **Left** (`.lm` binary if encountered) | [`formats/mesh.md`](formats/mesh.md), [`ROADMAP.md`](../ROADMAP.md)<br>• 796 `.lm.txt` files on disc; viewer selects highest-detail entry |
| **`skin.dat`** | Paint Shed reskin definition | Binary/text configuration mapping reskin textures to base asset meshes | **Known** (texture fallback); **Left** (Paint Shed tool internals) | [`ROADMAP.md`](../ROADMAP.md)<br>• Observed across 25 disc reskins; base mesh is taken from the 2nd KUID |
| **`mapfile.bmk`** | Route bookmarks | Surveyor saved view bookmarks (330 bytes in sample: `u32 7` + camera coordinates) | **Left** | [`formats/route.md`](formats/route.md), [`ENGINE-MAP.md`](ENGINE-MAP.md)<br>• Official wiki confirms role; binary payload unparsed |
| **`mapfile.lyr` / `profile.lyr`** | Route & session layers | Small layer definitions (25–40 bytes, contains `"session-layer"`) | **Left** | [`formats/session.md`](formats/session.md), [`ENGINE-MAP.md`](ENGINE-MAP.md)<br>• Unparsed |
| **`mapfile.trc`** | Route extra data | 32-byte binary block present in route assets | **Left** | [`formats/route.md`](formats/route.md), [`ENGINE-MAP.md`](ENGINE-MAP.md)<br>• Purpose unknown |
| **`assets.tdx`** | Master asset database index | Proprietary database maintained by `TADDaemon.exe` | **Left** | [`ENGINE-MAP.md`](ENGINE-MAP.md)<br>• Referenced throughout `trainz.exe` and `TADDaemon.exe`; port rebuilds index dynamically in memory |
| **`.cdp` / `.cdp2`** | Content Dispatcher Package | User content packaging archive created by `TrainzUtil` / `ContentManager` | **Left** | [`formats/asset-index.md`](formats/asset-index.md), [`ENGINE-MAP.md`](ENGINE-MAP.md)<br>• No `.cdp` files shipped on original disc media |
| **`.gs`** | TrainzScript source code | High-level object-oriented scripting language (GameScript) source text | **Known** (presence); **Left** (compiler & VM) | [`ENGINE-MAP.md`](ENGINE-MAP.md)<br>• 97 script files present in `scripts\` folder |
| **`.gsl`** | Compiled script library | Precompiled bytecode library | **Left** | [`ENGINE-MAP.md`](ENGINE-MAP.md)<br>• 97 files present in `libraries\`; binary format undecoded |
| **`.gse`** | Protected script binary | Encrypted/obfuscated compiled script | **Left** | [`ENGINE-MAP.md`](ENGINE-MAP.md)<br>• High-entropy binary with CRC checks; undecrypted |
| **`vtx.txt` / `pxl.txt`** | Shader definitions | Low-level ARB assembly vertex/pixel shader programs | **Left** | [`formats/mesh.md`](formats/mesh.md), [`ENGINE-MAP.md`](ENGINE-MAP.md)<br>• NVIDIA Cg and ARB assembly strings located in renderer DLLs |
| **`trainzoptions.txt`** | Engine runtime configuration | Text file containing CLI-style configuration flags (`-VBO`, `-width=`, etc.) | **Known** | [`ENGINE-MAP.md`](ENGINE-MAP.md)<br>• Read by `trainz.exe` on startup |

---

## 4. Subsystem File Detail and Evidence

### 4.1 Meshes, Materials, and Attachments
- **Formats**: `.im`, `.pm`, `.kin`
- **Specification Docs**: [`formats/mesh.md`](formats/mesh.md), [`formats/animation.md`](formats/animation.md), [`research/materials.md`](research/materials.md), [`research/bogeys-attachments.md`](research/bogeys-attachments.md)
- **Geometry (`GEOM`)**:
  - `GEOM` headers come in four versions: 101 (28 bytes), 102 (32 bytes), 200 (36 bytes), and 201 (40 bytes).
  - Geometry layout: vertex array (`x, y, z, u, v` floats, 20 bytes/vertex; Z is up), 16-bit triangle indices (`ni`), vertex normals (`3 × float32`), face normals (`3 × float32`), and optional extra vertex floats for v201.
- **Materials (`MATL`)**:
  - Material version 102 or 103. Text string lengths with bit 30 set indicate UTF-16LE encoding (56 materials on disc).
  - Materials specify kind via name suffix: `m.onetex`, `m.reflect`, `m.gloss`, `m.tbumptex`, `m.notex`, `m.tbumpgloss`.
  - Texture slots: 1 (diffuse/colour map), 8 (normal map), 9 (reflection/specular map).
  - Alpha behaviour: Diffuse alpha represents opacity in `m.onetex`, `m.tbumptex`, and `m.reflect`. In `m.gloss` and `m.tbumpgloss`, alpha represents reflection strength/mask, so cut-out testing is suppressed (`materialMayCutOut` returns false).
- **Attachments (`ATCH`)**:
  - Top-level chunk in `.im`/`.pm` containing attachment points for bogies (`a.bog0`, `a.bog1`), couplers (`a.limfront`, `a.limback`), passenger points, and track connections (`a.track0a`, `a.track0b`).
  - Each point specifies: name (UTF-16LE if bit 30 set), 3×3 rotation matrix (row unit vectors), and 3D offset position in metres.
- **Evidence / Checked**:
  > "every GEOM and MATL chunk is accounted for byte by byte; every index is below its vertex count; the normals are unit length (> 99%; the rest are zero-length normals on degenerate faces)."
  > "all 5,868 ATCH chunks in Base.ja, Data.ja and Builtin\1.ja–8.ja (.im and .pm) decode exactly to their end: 113,760 points, and every rotation has unit-length rows."
  > "Whole disc: all 2,629 .kin files fit."

### 4.2 Route Geometry and Placed Objects
- **Formats**: `mapfile.gnd`, `mapfile.trk`, `mapfile.obs`
- **Specification Docs**: [`formats/route.md`](formats/route.md), [`formats/route-objects.md`](formats/route-objects.md)
- **Terrain (`mapfile.gnd`)**:
  - Section count followed by lazy-loaded `Secs` (76×76 vertex grid, 10 m spacing, 720 m baseboard) or `Sec5` (148×148 vertex grid, 5 m spacing).
  - Vertices encode height in metres and up to 3 blended ground texture layers.
  - World data block contains time-of-day lighting, environment KUIDs, and `Rule` container items.
  - End of each section stores a 64×64 16-bit overview minimap texture.
- **Track Network (`mapfile.trk`)**:
  - First container stores track stretches (`tkSt`) and bridges (`tkBr`); second container stores track vertices (`tkVx`).
  - Track vertices store baseboard coordinate, internal metre position, height, and up to 4 link slots to track pieces.
  - Track curvature is modelled as a cubic Hermite spline across vertex tangents, where bit `0x20` of the stretch flags indicates straight chord alignment.
- **Placed Objects (`mapfile.obs`)**:
  - Containers store scenery (`mOBJ`), stub scenery (`mobs`), signals (`sgOb`), junctions (`jnOb`), crossings (`obCr`), industries (`mIND`), fixed track (`mFXT`), and turntables (`ttOb`).
  - Every object payload begins with: flag byte, baseboard coordinates (`boardX`, `boardY`), internal metre offsets (`x`, `y`, `z`), and un-wrapped yaw rotation in radians.
- **Evidence / Checked**:
  > "all 50 mapfile.gnd files are version 13 ('\r')... 15,243 sections (15,237 Secs, 6 Sec5) ... all end exactly at end; the C++ parseGroundSection gives the same result."
  > "170 of 172 files read to their exact last byte, holding 2,539,231 objects."
  > "Positions decode for 1,760,978 of 1,761,124 objects; the other 146 remain unsupported or unexplained."
  > "307,226 pieces, every end present; 614,430 of 614,432 links consistent."
  > "Over the 6,071 vehicles of 125 disc sessions: 97.0% lie within 0.5 m of the modelled track (87.0% of the straight chords) and 99.5% within 3 m (95.6%)."

### 4.3 Sessions and Behaviours
- **Formats**: `profile.dat`, `profile.obs`, `profile.trk`
- **Specification Doc**: [`formats/session.md`](formats/session.md)
- **Behaviours (`BHVR`)**:
  - Encodes session rules, driver commands, startup options, triggers, and camera settings.
  - Each behaviour wraps a rule name and an embedded `config.chump` data payload.
- **Trains (`trCt`)**:
  - Version 5 trains store track stretch ID, distance offset along the stretch, direction flag, and vehicle list.
  - Each vehicle stores its KUID, position coordinates, and display name.
  - Older train formats (version 1 sessions) lack record lengths and are located via pattern matching heuristics.
- **Evidence / Checked**:
  > "All 138 files read to their exact end this way."
  > "4,251 of 4,254 disc behaviours fit (3,994 version 3, 257 version 2); the other 3 are unexplained."
  > "all 655 version-5 trains on the disc chain to their tail; all 3,363 vehicles have a position block and a name; on the test session 'Midday Manifest' (25 trains, 231 vehicles) against its route <kuid:107727:1019>: every trackPiece is one of the route's track stretches, the first vehicle stands on that piece's line (0.0 m away), and distance is the metres from one end of the piece (all 25 within 1 m)."
  > "823 of the 826 qualify (4,500 vehicles); the other 3 are refused."

### 4.4 Audio and Sound Playback
- **Formats**: `.wav`, `.mp3`
- **Specification Doc**: [`formats/sound.md`](formats/sound.md)
- **Audio Formats**:
  - PCM audio (8-bit unsigned / 16-bit signed, mono/stereo, 5,000–48,000 Hz).
  - Microsoft ADPCM (4-bit, format tag 2).
  - MPEG audio (format tag 85 inside WAV containers, or standalone `.mp3`).
- **Evidence / Checked**:
  > "checking all 5,874 .wav files on the disc (Base.ja, Data.ja, Builtin\1.ja–8.ja): PCM (tag 1) 5,844; Microsoft ADPCM (tag 2) 26; MP3 inside a WAV (tag 85) 3; MPEG-2 program stream named .wav (idle 1.wav) 1 (rejected)."
  > "all 6 decode (RealInstall.SoundsDecode on the full Builtin archives and on testdata/ts2010) (C++ with libmpg123 1.32.10). Decoded samples were compared with ffmpeg's decoder: same alignment, same length (except the partial last frame above), every compared sample within 1 LSB."

### 4.5 Controls and Input
- **Formats**: `keyboard.txt`
- **Specification Doc**: [`formats/keyboard.md`](formats/keyboard.md)
- **Input System**:
  - Text configuration located in `UserData/settings/keyboard.txt`.
  - Parsed by `trainz.exe` word splitter (`0x4d5bb0`) and parser (`0x4d4b60`).
  - DirectInput `DIK_` scancodes mapped to position-ordered internal engine codes.
- **Evidence / Checked**:
  > "the shipped file reads in full (6 groups, 383 actions, every key name known)."
  > "RealInstall.KeyboardBindingsRead on the shipped file (testdata/ts2010/UserData/settings/keyboard.txt, a byte-exact copy from the installer)."
  > "Every key round-trips key -> scancode -> key with no two keys sharing a scancode (tests/test_sdl_keys.cpp)."

---

## 5. Summary: What is Known, What is a Guess, and What is Left

### What is Known
1. **Archive and Asset Indexing**: `.ja` header, indexing, BPE/CFIL decompression, name canonicalization, KUID XOR hashing, bare-number KUID recovery (`<kuid:-1:number>`), and reading all 19,282 assets.
2. **Configurations**: Complete binary layout of `config.chump` (`ACS$`) and parsing of `config.txt`.
3. **Textures and Images**: Decoding of all 89,524 `.texture` files (ARGB, RGB0, DXT1/3/5), all 360 `.texture.txt` descriptors, and 20,540 loose `.tga`/`.bmp`/`.jpg` images.
4. **Meshes**: Geometry (`GEOM` v101–201), materials (`MATL` v102–103), and attachment points (`ATCH`) across all 105,178 parts and 5,868 attachment blocks.
5. **Terrain and Routes**: Complete parsing of 15,243 `mapfile.gnd` sections, height extraction, ground texture slots, overview maps, track stretches and vertices, and placed object coordinates.
6. **Audio**: Decoding of all PCM, MS ADPCM, and MP3 streams.
7. **Key Bindings**: Complete token parser and scancode mapping for `keyboard.txt`.

### What is a Guess (Empirical Models & Inferences)
1. **Track Curvature**: Hermite cubic spline model through vertex tangents (matches 97% of vehicle positions within 0.5 m, but internal tangent computation is not traced in `trainz.exe`).
2. **Terrain Vertex Extra Byte**: Believed to encode paint direction (16 steps) and scale, based on bit patterns and TRS19 documentation, but not proven.
3. **Placed Object Pitch and Roll**: Believed to be encoded in trailing floats of `mobs` records.
4. **Old Train Record Parsing**: Vehicle records in version-1 sessions lack stored lengths; heuristic pattern matching recovers 823 of 826 trains, but false match immunity relies on disc-tested heuristics.
5. **Ground Texture Blend Scaling**: 10-metre repeat assumed for texture blending.

### What is Left
1. **TrainzScript Engine**: `.gs` compiler, virtual machine runtime, native C++ bindings, and handling of protected `.gse` scripts (the largest unmapped area, milestone 12).
2. **Train Physics & Simulation**: Reading locomotive `enginespec` performance curves, dynamic braking, DCC vs cab mode physics, and traincar mass integration.
3. **Signalling & Junction Mechanics**: Junction switching logic (`mojunction`), signal state propagation (`mosignal`), and block control.
4. **World Sound & Script Integration**: `soundscript` trigger blocks from configs, distance attenuation, and Doppler/3D positioning.
5. **SpeedTree Geometry**: Full 3D parsing of `.srt` trees (currently rendered as billboard quads).
6. **Proprietary Databases**: Internal structure of `UserData/assets.tdx` and unpacked `.cdp` archive extraction.
