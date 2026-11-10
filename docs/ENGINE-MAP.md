# Trainz Simulator 2010 – Engine Map

A read-only map of the TS2010 engine for planning a native Linux port. Everything
here comes from the owner's own disc image (`TRS2010EE.iso`, Just Flight edition,
built 19 Apr 2010). Nothing was run, patched or changed.

## How this was done

- **Disc and installer:** I read the ISO with 7-Zip and attached it read-only to
  run `innoextract 1.9` (from the Debian package, kept in a temp folder). That
  pulled out `bin\`, `Base.ja`, `Data.ja`, `Builtin\8.ja`, `scripts\`,
  `libraries\` and `UserData\`. `Builtin\1.ja` to `7.ja` were not extracted.
- **REA:** used for PE import, export and resource tables
  (`inspect_pe_imports`, `inspect_pe_resources`, `inspect_native_exports`).
  REA's decompilers could not run on this PC: REA accepts Ghidra 12.1.x and
  12.2 is installed, and Hopper, IDA and Kuna are not set up. Code addresses
  below therefore come from GNU `objdump` disassembly plus small read-only
  Python scripts (PE parsing, `.ja` and chunk walkers, a BPE decoder).
- **Whole-disc survey (2026-11-10):** A Python prototype of every reader was run
  read-only across all 8 `Builtin` archives + `Base.ja`/`Data.ja`. Results and
  corrections are summarised in [`DISC-SURVEY.md`](DISC-SURVEY.md). The C++ readers
  themselves have run in CI on `Base.ja`, `Data.ja` and a real subset of `8.ja`;
  the full set still needs Dad's install.
- **Addresses:** these are virtual addresses inside the named module.
  - `trainz.exe` has no symbols (it exports only `JetGetPlugins`), so its
    addresses are the places that *reference* a string, not named functions.
  - Function starts written as `~0x…` were found with a padding heuristic and
    are approximate.
- **Confidence words:**
  - **Traced:** read in the game's disassembly or confirmed from its code.
  - **Checked:** traced, and every disc file fits it.
  - **Confirmed:** decoded or checked against real data.
  - **Observed:** counted or classified in the disc data (survey).
  - **Seen:** a string, import or export exists, but the behaviour wasn't traced.
  - **Unknown:** not worked out yet.

Engine identity: Auran's in-house **"Jet"** engine (C++ namespace `Jet::`).
Build path string in `KernelStandard_7c.dll`:
`D:\BUILD\src\trainz\candidate\ts2010_sp2\Jet\Src\Jet\KernelStandard\FileSubArchive.cpp`.
It was built with Microsoft Visual C++ 2005: it imports `MSVCR80`/`MSVCP80`, and
the Inno installer adds the VC80 CRT/MFC.

---

## 1. Finding and loading content

### 1.1 Folders (relative to the install folder)

| Folder / file | What it holds | Evidence |
|---|---|---|
| `Builtin\1.ja` … `Builtin\8.ja` | Built-in assets: trains, scenery, routes (about 7.3 GB) | Installer file list. `builtin\1` … `builtin\8` strings in `trainz.exe` and `TADDaemon.exe` |
| `Base.ja`, `Data.ja` | Core game assets (`Base.ja`, 1722 files) and engine UI data (`Data.ja`, 973 files: `Surveyor/`, `desktop/`, `sounds/`, `Driver/`, `ParticleFX/` …) | Decoded indexes (see 1.2) |
| `UserData\local` | User-installed content | Folder in the installer. `local` string in `trainz.exe` |
| `UserData\original`, `UserData\editing`, `UserData\cache`, `UserData\trash` | Originals of edited assets, open-for-edit copies, caches, deleted content | Installer folders. Strings `$srch_builtin`, `$srch_loc_archive`, `obsolete` |
| `UserData\settings\*.txt`, `*.chump` | Keyboard map, driver defaults, Surveyor palette | `keyboard.txt`, `driverdefaults.txt`, `surveyorpalette.chump` |
| `UserData\assets.tdx` | Asset database index | String `assets.tdx` in `trainz.exe`, `TADDaemon.exe`, `ContentManager.exe`, `TrainzUtil.exe`. In `trainz.exe` it is referenced at `0x799f11`, `0x7a7b0b`, `0x7c7b3c`, `0x7c83c8` |
| `scripts\*.gs` | 97 TrainzScript source files (`asset.gs`, `Bogey.gs`, `Cabin.gs` …) | Installer |
| `libraries\*.gsl` | 97 compiled versions of the same scripts | Installer; the files are binary |
| `bin\` | The Jet engine DLLs, `plugins.ini`, tools, `mozilla\` (Gecko) | Installer |
| `trainzoptions.txt` | Command-line style options (`-Jet=bin`, `-VBO`, `-width=1024`, `-framerate=200` …) | Shipped file |

### 1.2 The `.ja` archive (Jet Archive) – **layout checked on sampled archives; decoder partly validated**

There are no `.big` files. The packed format is `.ja`. The full specification,
including name lookup and duplicate handling, is in
[`formats/ja.md`](formats/ja.md), and the reader is `src/content/JaArchive.{h,cpp}`.

**Code** (`KernelStandard_7c.dll`, class `Jet::FileSubArchive`, source file `FileSubArchive.cpp`):

| What | Address | Notes |
|---|---|---|
| Open existing archive | `~0x10024c40`, check at `0x10024ceb` | Calls `_open` through import `0x1004f2f0`, reads 4 bytes and compares them with `"ARCH"` (string `0x10053448`). Then reads the `"INFO"` block (string `0x10053450`, length `0x20`) via `0x10021420`. Checks `dword == 0x41524330` (`'ARC0'`) and version `0x101`–`0x102` |
| Create new archive | `0x10024d88` … `0x10024db1` | Writes `'ARC0'` and version `0x102`. On failure: `"Substitute() Open new archive failed"` |
| File-entry tag `"FIL "` | string `0x100534d8`, used at `0x1002549c`, `0x1002599c` | A `"FIL!"` tag exists right after it. Its meaning is **unknown** |
| Index tag `"IDX "` | string `0x100534e0`, used at `0x100265ff`, `0x10026859` | |
| Mounting a `.ja` as a resource database | `KernelResource_7c.dll`: string `".ja"` at `0x1000a58c`, used inside `Jet::ResourceDatabase::ResourceDatabase(PString,PString)` (`0x10003b10`, reference `0x10003c4b`) and `ResourceNamelessDatabase` (`0x100065f0`, reference `0x100066bb`) | Each `.ja` is opened as a named resource database |
| `trainz.exe` | string `".ja"` at `0x9707d0`, used at `0x86e806` | |
| Other users | `JAConverter.exe`, `ContentManager.exe`, `TADDaemon.exe`, `TrainzUtil.exe`, `CCP.exe`, `Launcher.exe` contain `".ja"` / `'ARC0'` | |

**Layout** (all integers are little-endian):

```
"ARCHINFO"                      8 bytes
u32  info_size = 32
  u32 magic   = 'ARC0' (bytes "0CRA")
  u32 version = 0x102           (reader accepts 0x101–0x102)
  u32 file_count                (Base.ja 1722, Data.ja 973, 8.ja 11610)
  u32 idx_offset                (file offset of the "IDX " block)
  u32 unknown                   (42203 in Base/Data, 42195 in 8.ja; probably
                                 a build or product code: see the exports
                                 FileSubArchive::SetDefaultBuildVersion /
                                 SetDefaultProductCode. Not confirmed)
  12 bytes                      zero in every sample; not read
repeat file_count times:
  "FIL " u32 block_size         (= 288 + stored_size: everything after the
                                 8-byte tag and size)
    char name[260]              NUL-terminated, original case, '/' separators;
                                the bytes after the NUL are leftover memory
    u32  original_size
    u32  stored_size
    u32  hash                   NOT CRC32 (tested). Algorithm unknown
    12 bytes                    zero in every sample; the reader skips them
    u32  flags                  low 16 bits read; 0x80 = compressed (the reader
                                actually detects compression by the "CFIL" magic)
    u8   data[stored_size]      <- the index's data_offset points here
"IDX " u32 block_size           = file_count × 296
  repeat: u32 data_offset (absolute), u32 stored_size,
          char lowercase_name[260], then a copy of
          original_size, stored_size, hash, 12 zero bytes, flags
          (the index is not sorted)
```

**`CFIL` compressed file** (writer at `0x10020237`–`0x10020330`, reader check at
`0x10020053`, block walk at `0x10020dfd`–`0x10020e2f`):

```
"CFIL"  u32 version = 0x101  u32 original_size
repeat until original_size bytes are produced:
  u16 header
    if header & 0x8000: (header & 0x7fff) raw bytes follow
    else:               header bytes of BPE data follow, which expand to
                        at most 16 KiB (0x4000)
```

- **Compressor:** `0x1000ccc0` (it allocates a 0x3a04-byte work area).
- **Decompressor:** `0x1000d130`. It is **Byte Pair Encoding** (Philip Gage,
  1994) with one difference: a "skip" count byte (> 127) only skips table
  entries and does *not* also read a pair, as Gage's original does.
- **BPE sub-block format:**
  1. A 256-entry pair table, made of runs:
     - a count byte > 127 means skip (count − 127) entries;
     - otherwise (count + 1) entries follow, each one `left` byte, plus a
       `right` byte when `left ≠ index`.
  2. A big-endian `u16` byte count.
  3. That many bytes of data, expanded through a stack.
- **Verified (partly):**
  - Two compressed files decode to exactly their recorded sizes:
    `DriverAchievementsUpdaterLibrary.gs`, 7219 bytes, and a `config.txt`,
    1426 bytes.
  - Samples of every file type in `Base.ja` and `8.ja` decode to valid
    headers (`JIRF`, `BM`, `RIFF`, `JFIF`, `ACS$`, `ID3`).
  - **Full pass (2026-11-10):** the C++ reader decodes every entry of
    `Base.ja` (1,722) and `Data.ja` (973) to its recorded size in CI. A full pass
    over `Builtin\1.ja`–`8.ja` still needs Dad's install.

### 1.3 How an asset is looked up – **KUIDs seen in every sample; folder rule observed**

Your belief is **mostly right**: every sampled asset has a KUID and a
configuration, stored as text (`config.txt`) or binary (`config.chump`).
Corrections and additions:

- **There are two KUID forms:** `<kuid:USER:CONTENT>` and
  `<kuid2:USER:CONTENT:VERSION>`. Evidence:
  - `trainz.exe` format strings `&#60;kuid:%d:%d&#62;` and `&#60;kuid2:%d:%d:%d&#62;`;
  - `TADDaemon.exe` strings `<kuid2:%d:%d:%d>` and `kuid2 %d %d %d`.
  - Example from a decoded `config.txt`: `kuid <kuid:447264:1011>`.
- **Built-in assets usually carry `config.chump`, not `config.txt`.**
  `config.chump` is the binary form of the same tree. File counts in `8.ja`:
  1468 `.chump` against 139 `.txt`. Those are counts of files, not of asset
  configurations; the `Base.ja` samples use `config.txt`.
- **Folder rule inside the archives:** `hash-XX/kuid USER CONTENT/…` or
  `hash-XX/kuid2 USER CONTENT VERSION/…`, where **XX = the XOR of the 8 bytes of
  `(int32 USER, int32 CONTENT)` in little-endian**. This is **observed only**:
  it matched all 499 distinct asset folders tested in `8.ja`, but the code
  that builds the names hasn't been traced. The test
  `RealInstall.KuidFolderHashRule` checks it on a whole install. Example:
  `hash-C0/kuid 447264 1073/profile.obs`.
- **Lookup service:** the asset database is `assets.tdx`, kept by
  `TADDaemon.exe`, the Trainz Asset Database daemon. It holds the classes
  `TADArchiveTable` and `TADArchiveTableBase` and strings such as `map-kuid`,
  `kuid-table`, `I@kuid-generator` and `$tad_archive_unreadable`. Scripts look
  assets up through `TrainzScriptBase::FindAsset(KUID)`,
  `GetLibrary(KUID)` and `SearchAssets(string,bool)` (binding strings in
  `trainz.exe`).
- **In `config.txt`, dependencies are named by KUID** in `kuid-table { … }` and
  `script-include-table { … }`. Files are named relative to the asset folder
  (`script "tutorialcomplete.gs"`, `icon-texture "tutorialcomplete.texture"`).

**Unknown:** the exact search order between `local` / `editing` / built-in
packs and asset versions (kuid2 VERSION). It needs a trace through
`TADDaemon.exe`.

---

## 2. File formats

| Format | Extension(s) | Status | Layout / evidence |
|---|---|---|---|
| Archive | `.ja` | Layout checked on sampled archives; decoder partly validated | See 1.2 |
| Content package | `.cdp` (`.cdp2`) | Seen | `"Unable to write archive file. Needs to be .cdp file extension"` (`trainz.exe`, `TADDaemon.exe`); `TrainzUtil createCDP <file.cdp> <kuid…>`; `ContentManager.exe` file filter `CDP file (*.cdp;*.cdp2)`. Not sampled |
| Asset config (text) | `config.txt` | **Confirmed**; **whole disc: 70 files, all parse** | In the samples: UTF-8 with BOM and CRLF line ends (what the parser accepts beyond that is not traced); `key  value` lines, `name { … }` sub-blocks, quoted strings, `<kuid…>` values, `-fr` style language suffixes (`username-fr`). **Whole disc survey [`DISC-SURVEY.md`](DISC-SURVEY.md):** one grammar fix (value may start on the next line). Parser: `Jet::ConfigData` in `KernelStandard_7c.dll` (88 exports; writer `ConfigData::Write` `0x10011350`). `config.txt` referenced in `trainz.exe` at `0x61fea8`, `0x6212c7`, `0x64804f`, `0x648e51`, `0x66c8fe`, `0x7ccec3` |
| Asset config (binary) | `.chump` | Header and item walk traced; value types observed; **whole disc: 19,112 files, all parse** | Magic `ACS$`, `u32` version 1, `u32` reserved, `u32` body size, then size/key/type/value items. Reader in **`trainz.exe`**: header `0x7ff710`, item walk `0x7ff1f0` (no symbols). **Whole disc survey [`DISC-SURVEY.md`](DISC-SURVEY.md):** all 17,388 top-level asset KUIDs match their folder names. Full layout in [`formats/config.md`](formats/config.md). Also used for settings and UI strings (`interfaceFrench.chump` …). *Correction:* `ConfigData::ReadBin` / `ReadBinFile` (`KernelStandard_7c.dll` `0x10013220`, `0x10012a50`) read a **different** binary format with type bytes 0–7, not `.chump` |
| Mesh | `.im` | Layout checked (Python, all 7,982 parts in `Base.ja` + `8.ja`; GEOM v101 found later in `Data.ja` by the C++ run); C++ reader also run in CI on real data (Base/Data/sample of 8.ja); **whole disc: 105,178 parts fit, except those in 65 damaged files** | `JIRF` form `IDXM`: `INFO`, one `CHNK` per material (`u32 100, u32 index`) holding `MATL` (name, 16 floats, textures with type/path/amount) and `GEOM` (v101/102/200/201: header, optional bone name, vertices x,y,z,u,v, u16 indices, vertex normals, face normals, optional extra per-vertex floats); optional `INFL`/`ATCH`/`SKEL`. Z is up. Reader code not traced. **Whole disc survey [`DISC-SURVEY.md`](DISC-SURVEY.md):** versions 200 (80,535), 201 (21,753), 102 (2,837), 101 (53); 65 files look damaged by a text-mode transfer; `repairTextModeDamage` turns them into consistent files (not proven to be the original bytes). Full layout in [`formats/mesh.md`](formats/mesh.md) |
| Older mesh | `.pm` | Container confirmed; **whole disc survey [`DISC-SURVEY.md`](DISC-SURVEY.md):** 476 files have harmless extra bytes at the end | Same `JIRF`/`IDXM` container; each `CHNK` also carries `NINF`. Class `IndexedMeshDefinitionOld` (93 exports) |
| LOD set | `.lm`, `.lm.txt` | Seen | `trainz.lm`, `trainz.lm.txt` (`trainz.exe`, `TrainzUtil.exe`) |
| Animation | `.kin` | Layout checked (Python, all 135 disc files); C++ reader also run in CI on real data (Base/Data/sample of 8.ja); **whole disc: 2,629 animations, all fit** | `JIRF` form `ANIM`: `INFO` (source file, frame count, fps), `EVNT` (frame, flags, name), `SKEL` (nested `BONE` tree), one `FRAM` per frame with position + quaternion per bone. Bone-position space not confirmed. Reader (`AnimationModifier`) not traced. See [`formats/animation.md`](formats/animation.md) |
| Texture | `.texture` | Layout and formats checked (Python, all 6,666 disc textures); C++ reader also run in CI on real data (Base/Data/sample of 8.ja); **whole disc: 89,524 textures, all fit** | `JIRF` form `MMTF`: `INFO` (80 bytes, version `0x107`; 60 bytes in `0x103`), then one `MMAP` per mip level, **smallest first**. INFO word 2 = mip count, 4 = pixel format, 5/6 = width/height. Formats are Jet's `DisplayFormat` ids (name list in `RenderSystem_7c.dll`): **ARGB8888 (1), RGB0888 (3), DXT1 (10), DXT3 (11), DXT5 (12)** are in use; the other ids (2, 4–9) were never seen. **Whole disc survey [`DISC-SURVEY.md`](DISC-SURVEY.md):** INFO versions 263 (89,189), 262 (195), 259 (140); 1 file is actually a `.texture.txt` descriptor. Reader `TextureResource::Load` `0x10045500`. Full layout in [`formats/texture.md`](formats/texture.md) |
| Texture descriptor | `.texture.txt` | **Keys, values and `Tile` traced** (`RenderSystem_7c.dll` ~`0x10005aa0`); C++ reader; all 360 disc descriptors resolve and combine. Combining rules from the wiki only. See [`formats/texture-descriptor.md`](formats/texture-descriptor.md) | Text file pointing at a `.tga`/`.bmp`/`.jpg`. Plugins `RenderSystem:texture.txt`, `RenderSystem:tga-tex/bmp-tex/jpg-tex` in `plugins.ini`. **Official wiki (not yet checked against the disc files):** `.texture.txt` files are ASCII text descriptors for a single texture. Primary= alone supplies RGB; any embedded alpha is ignored. Primary= and Alpha= referencing the same image use that image's RGBA channels. Different Primary and Alpha images use primary for RGB and a grayscale image for opacity; their dimensions must match. Tile supports s (horizontal wrapping), t (vertical wrapping), st (both), and none. According to the wiki, **transparency comes from the descriptor, not the image file**, which fits the 165 TGAs whose alpha is 0 everywhere. URL: https://online.ts2009.com/mediaWiki/index.php/Texture_file |
| Image sources | `.tga`, `.bmp`, `.jpg` | **Observed (standard formats)**; C++ decoders in `src/content/ImageFile.{h,cpp}`; **whole disc: 20,544 images, 20,540 decode, 4 damaged** | Standard headers. `KernelResources:tga/bmp/jpg` plugins; JPEG through `ijl10.dll` (Intel JPEG Library), imported by `KernelStandard_7c.dll`. **Whole disc survey [`DISC-SURVEY.md`](DISC-SURVEY.md):** 11,640 TGA (165 of them carry an alpha channel that is 0 everywhere, so alpha use must come from the material), 2,687 BMP, 6,217 JPEG; the port's decoders handle TGA types 1/2/3/10 (9/11 handled too, but none exist on the disc), BMP 4/8/24/32 bpp + RLE8, JPEG baseline/progressive. See [`formats/image.md`](formats/image.md). |
| Shader / material text | `vtx.txt`, `pxl.txt`, `material.txt` | Seen | `plugins.ini` entries. ARB assembly programs embedded in the DLLs (`PARAM texMat[4] = { state.matrix.texture[0] }`) |
| Trees | `.srt` | Header confirmed | Magic `SRT 05.0.1` (SpeedTree). Read by `bin\plugins\TNISpeedTree.dll` |
| Route terrain | `mapfile.gnd` | Header, index, world block and section data **traced and checked** on all 50 disc routes (15,243 sections); the meaning of several world values is a guess | `GND` + u8 version (13), section count, entries `{x, y, reversed tag, start, end}` (loader `trainz.exe` `0x6d2aa0`). Section data (`0x82be00` Secs / `0x821200` Sec5): 250 texture slots (weight + KUID), up to 36×36 cells, then 76×76 (Secs) or 148×148 (Sec5) vertices (`0x6f0e50`: textures, weights, height). Then a 64×64 u16 overview map (`0x6e4070`). **Whole disc survey [`DISC-SURVEY.md`](DISC-SURVEY.md):** all sections in all 50 routes decode to exactly their end offset; of 2 older `.gnd` files, version 11 fits completely and version 10's sections fit, but its world block (object container v2) does not yet (Python checker only). See [`formats/route.md`](formats/route.md) |
| Route objects | `mapfile.obs` | Seen | Strings `mapfile.obs`, `.obs`. **Official wiki:** mapfile.obs is the objects file, storing information about objects placed in the map. **Community:** contains placed assets' 3D coordinates, rotations and elevation changes. URLs: https://online.ts2009.com/mediaWiki/index.php/CCG/Example:Map (official), https://en.wikibooks.org/wiki/Trainz/file_types_by_extensions (community) |
| Route track | `mapfile.trk` | Seen | Strings `mapfile.trk`, `.trk`. **Official wiki:** mapfile.trk is "the track layout file". **Community:** holds track and spline-object location/vertex information. URLs: https://online.ts2009.com/mediaWiki/index.php/CCG/Example:Map (official), https://en.wikibooks.org/wiki/Trainz/file_types_by_extensions (community) |
| Route bookmarks | `mapfile.bmk` | Seen | 330-byte sample: `u32 7`, then float positions. **Official wiki:** mapfile.bmk stores the bookmarks saved in Surveyor. URL: https://online.ts2009.com/mediaWiki/index.php/CCG/Example:Map |
| Route extra | `mapfile.trc` | Seen | 32-byte sample. Purpose **unknown** |
| Session | `profile.dat`, `profile.obs`, `profile.trk`, `profile.lyr` | Headers seen | `.obs`/`.trk` start with small `u32` counts, then reversed-tag chunks (bytes `2Ost`, `tSkt`). `.lyr` contains `session-layer`. `.dat` (1.4 MB) contains the bytes `RVHB` |
| Sound | `.wav`, `.mp3` | WAV checked on all 5,874 disc files; **MP3 decoding added** via libmpg123 (optional); C++ reader + tools; **whole disc: every sound decodes except one** (`idle 1.wav`, really an MPEG-2 video stream with AC-3 audio); the 3 MP3-in-WAV files that the first survey refused now decode (C++ run by the MP3 helper on all 8 `Builtin` archives) | RIFF/WAVE: PCM 8/16-bit (5,844), MS ADPCM (26), MP3-in-WAV (3, now decoded), one MPEG-2 program stream misnamed `.wav`. Three real `.mp3` files in `Base.ja` + 3 MP3-in-WAV = **6 MP3 streams, all decode** (checked against ffmpeg to within 1 sample value). See [`formats/sound.md`](formats/sound.md) |
| Script source | `.gs` | Confirmed | TrainzScript (GameScript) text |
| Compiled script | `.gsl` | Seen | Binary (contains names such as `asset.gs`). Format **unknown** |
| Protected script | `.gse` | Seen | Protected script: encrypted, obfuscated or compressed, **not determined** (8 KB sample: `u32 1`, `u32 0`, `u32 size`, then high-entropy bytes). `"CRC Mismatch on '%s' after recompile. Do you have both .gs and .gse present?"`. `trainz.exe` references `.gse` at `0x8adc64` |
| Video | (AVI / DirectShow) | Seen | `VFWMoviePlayer_7c.dll` (imports `AVIFIL32`), `DSMoviePlayer_7c.dll` (ole32/DirectShow) |

**Route-world lighting and "Rule" values:** **Unknown / guess** – no public documentation found linking these specific fields in the parsed world block to lighting or session rules.

---

### Public documentation and code leads for route files

- **TransDEM note (community):** Most Trainz route files have **internal version numbers**, distinct from both the Trainz product version and the executable build number. TransDEM reads these and acts accordingly. TS12 SP1 introduced a changed `.gnd` format, moving ruler objects into a separate file. TransDEM 2.3 had issues with TS12 SP1 exports; TransDEM 2.4 was being tested to restore compatibility. **Keep these three identifiers separate: Trainz product version, executable build number, and internal route-file version.** URL: https://forum.transdem.de/viewtopic.php?t=243 (link from the research hand-off; not opened by us)
- **TrainzMapFiles (community):** claims support for Trainz 1.1–2019 (TS2010 falls in that range; not checked), `.gnd` reading only, described as experimental. Repository: https://github.com/rileyzzz/TrainzMapFiles (not opened by us). Does not establish readers for `.trk`, `.obs` or `.bmk`. **Status: lead found, not verified.**

---

## 3. Subsystems

### Plugin system (the backbone)

- `Kernel_7c.dll` reads `bin\plugins.ini`, loads `<Name>_7c.dll` (string `_7c.dll`)
  and calls each module's exported `JetGetPlugins<Name>`. Examples:
  `JetGetPluginsKernel` at `0x100059b0`, `JetGetPluginsSoundDS8`,
  `JetGetPluginsInputDirectX`, `JetGetPluginsRenderDirectX9`.
- Error strings: `"Plugin library not found"`, `"Plugin library has no entry point"`.
- Each `plugins.ini` line has the form
  `+Plugin=<Module>:<name>:<GUID>:<group GUID>`.

### Graphics

- **Two back ends ship.**
  - **DirectX 9:** `RenderDirectX9_7c.dll`, which imports **`d3dx9_42.dll`**;
    class `DisplayTextureDirectX9`; plugin `RenderDirectX9:DirectX9`.
  - **OpenGL:** `RenderOpenGL_7c.dll`, which imports **NVIDIA Cg** (`cg.dll`,
    `cgGL.dll`); classes `ContextDescriptorOpenGLWGL`, `DisplayTextureOpenGL`;
    plugins `RenderOpenGL:StdOpenGL`, `ATIOpenGL`, `XTermOpenGL`, `ARB`.
- `trainz.exe` holds the option strings `DirectX`, `DirectX9`, `OpenGL`.
- The rest of the renderer **appears** API-neutral: these DLLs import no
  Direct3D or OpenGL library (only `USER32`/`GDI32` and the Jet DLLs). Their
  code was not read. They are:
  `RenderSystem_7c` (Display, Material, TextureManager, VertexProgram),
  `Render3DCore_7c` (Camera, ViewPort, Particle, materials such as
  `OneTextureMaterial`), `Render3DFoundation_7c` (meshes, animation,
  `GeometryConfigData`), `Render3DExtension_7c` (ParticleFX, Haze, Decal) and
  `Render2DCore_7c` (sprites, canvas).
- `WaterSystem_7c.dll` (water materials, ocean simulation) and
  `TNISpeedTree.dll` (trees) are separate plugins.
- `trainz.exe` imports **`nvtt.dll`** compression functions (NVIDIA Texture
  Tools `Compressor::process`, `enableCudaAcceleration`), and `cudart.dll`
  ships. When they run, and whether CUDA is actually used, is **not traced**.

### Sound

- **`SoundCore_7c.dll`** holds the engine-side sound system: `Sound`, `SndBase`,
  `Snd3D`, `SndPolyline`, `SndAreaPolygon`, `SoundResource`, `SoundDefResource`
  and `Microphone`.
- Two back ends sit on top of it:
  - **`SoundDS8_7c.dll`**: DirectSound 8. It is probably created through COM
    (it imports `ole32` and not `dsound.dll`); the call site is not traced.
  - **`SoundDS3D_7c.dll`**: DirectSound 3D, "DirectX7".
- Neither back end imports `dsound.dll` statically.

### Input

- **`InputSystem_7c.dll`** holds the device-neutral input system:
  `InputDevice`, `JoystickDevice`, `MouseDevice`, `InputAction`,
  `InputActionMapper`, `ForceEffect` (force feedback) and `EmuDevice`.
- Two back ends sit on top of it:
  - **`InputDirectX_7c.dll`**: imports `DINPUT8.dll` (**DirectInput 8**).
  - **`InputWin32_7c.dll`**: uses USER32 and WINMM.
- `trainz.exe` names `TrainzHID_7c.dll`, but that file **is not on the disc**.

### Scripting

- **TrainzScript / GameScript**: `.gs` source, `.gsl` compiled, `.gse` protected.
- `Lexer_7c.dll` provides the lexer: `Jet::Lexer`, `Tokenizer`, `CompilerError`,
  `PAVCompilerError`, `ErrorList`. Examples: `Lexer::SkipSpaces` `0x10002f20`,
  `Lexer::SetLineNo` `0x10002e20`.
- The compiler and the virtual machine that runs scripts **may** be inside
  `trainz.exe`; this is an inference. What `trainz.exe` is known to hold is the
  native binding descriptions, e.g.
  `$Asset@TrainzScriptBase::FindAsset(KUID)`,
  `$Soup@TrainzScriptBase::TrainzSystemCall(string,Soup)` and
  `$string@GSObject::GetGSClassName()`.
- **TNI (Trainz Native Interface):** `trainznativeinterface.dll` (71 exports such
  as `TNIStreamReadInt8`, `TNIGraphicsSetTimeOfDay`) is the plugin API for native
  plugins like `TNISpeedTree.dll` (export `TNIEntryPoint`).

### Networking

- `trainz.exe` imports **`WSOCK32.dll`** (14 functions, by ordinal) and
  **OpenSSL** (`SSLEAY32`, `LIBEAY32`).
- URLs: `http://game.ts2009.com/…`, `https://www.auran.com/trainz/DLST_Search_Text.php…`
  (Download Station search), `https://www.auran.com/games/gamefiles/trainz/update.htm`,
  and `itrainz.auran.com` (the in-game chat, "TS2009 iTrainz Chat Interface"
  in `Base.ja`).
- `bin\mozilla\` (161 files: `xul.dll`, `nss3.dll`, `js3250.dll`,
  `necko_http.xpt`, `network.proxy.*` preferences) is an embedded Gecko browser
  for the in-game web pages.
- I didn't check whether these 2010 servers still work.

### Database and content manager

- `TADDaemon.exe` runs the asset database (`assets.tdx`, archive tables).
- `ContentManager.exe` (MFC) is the content manager; `CMPData\tools\*.bat` are
  its hooks.
- `TrainzUtil.exe` is a command-line tool (e.g. `createCDP`).
- `JAConverter.exe` converts `.ja` archives.
- `CCP.exe` is the content creation tool.

### UI and debug

- `InterfaceSystem_7c.dll` (2095 exports: `UI`, `Window`, `Dialog`, `Menu`,
  `TreeView`, `EditBox` …) and `InterfaceResources_7c.dll` (fonts, cursors,
  icons) provide the user interface.
- `DebugSupport_7c` and `DebugInterface_7c` hold debug consoles.

---

## 4. What lives in the `.exe` and what lives in DLLs

| File | Size | Role | Evidence |
|---|---|---|---|
| `Trainz.exe` (install root) | 64 KB | Small launcher | Imports only `SHELL32`, `KERNEL32` |
| `bin\trainz.exe` | 6.55 MB | **The game itself**: trains and driving, routes, the Surveyor editor, TrainzScript binding descriptions (the compiler/VM probably too, not traced), the asset-DB client, networking | Imports every Jet core DLL plus `WSOCK32`, OpenSSL, `nvtt`, `trainznativeinterface`. Only export `JetGetPlugins` |
| `KernelStandard_7c.dll` | 488 KB | Basics: files, `.ja` archives, `CFIL`/BPE, `ConfigData` (the engine's own config files; asset `.chump` is read in `trainz.exe`), strings, bitmaps, paths, options | 1701 exports (`File`, `ConfigData`, `Bitmap`, `Path` …) |
| `Kernel_7c.dll` | 116 KB | Plugin loader, tasks, app object, compression helpers | `PluginConfig`, `TaskManager`, `Compress`, `JetGetPluginsKernel` |
| `KernelResource_7c.dll`, `KernelResources_7c.dll` | | Resource databases (mounting `.ja` files) and bitmap resources | `ResourceDatabase`, `ResourceSystem`, `BitmapResource` |
| `RenderSystem_7c`, `Render2DCore_7c`, `Render3DCore_7c`, `Render3DFoundation_7c`, `Render3DExtension_7c` | | Renderer core, meshes, animation, particles (appear API-neutral: no D3D/GL imports) | See Graphics above |
| `RenderDirectX9_7c`, `RenderOpenGL_7c` | | Graphics API back ends | `d3dx9_42`; `cg`/`cgGL` |
| `SoundCore_7c`, `SoundDS8_7c`, `SoundDS3D_7c` | | Sound | See Sound above |
| `InputSystem_7c`, `InputDirectX_7c`, `InputWin32_7c` | | Input | `DINPUT8` |
| `InterfaceSystem_7c`, `InterfaceResources_7c` | | User interface | |
| `WaterSystem_7c`, `plugins\TNISpeedTree.dll`, `trainznativeinterface.dll` | | Water, trees, native plugin API | |
| `Lexer_7c.dll` | 56 KB | Script and config lexer | |
| `DSMoviePlayer_7c`, `VFWMoviePlayer_7c` | | Video | |
| `TADDaemon.exe`, `ContentManager.exe`, `TrainzUtil.exe`, `JAConverter.exe`, `CCP.exe`, `Launcher.exe`, `TrainzDiag.exe`, `XCrashReport.exe` | | Separate tools and services | |
| Third party | | `cg`, `cgGL`, `cudart`, `nvtt`, `ijl10`, OpenSSL, Mozilla Gecko, VC80 CRT/MFC, `dx7vb`, `unicows`, `scrrun` | |

---

## 5. Suggested porting order (simplest foundations first)

Each step can be tested against the real data before the next one starts.

1. **`.ja` reader** (archive header, `FIL `/`IDX ` entries, `CFIL` + BPE) and
   the `hash-XX/kuid …` folder rule. *Written in C++ (`src/content/`, `ja-tool`).
   The next step is a full pass over `Builtin\1.ja`–`8.ja`
   (`TRAINZ_FULL_VERIFY=1`).*
2. **`config.txt` parser and the `.chump` (`ACS$`) binary reader**, plus a KUID
   type (`kuid` / `kuid2`). *Written in C++ (`src/content/AssetConfig`,
   `config-tool`); see [`formats/config.md`](formats/config.md).*
3. **Own asset index**: scan `local`, the `.ja` files and `.cdp` packages into a
   KUID → location table. This avoids depending on `assets.tdx`/TADDaemon early.
   *Written in C++ (`src/content/AssetIndex`, `asset-tool`) for `.ja` and
   loose folders; `.cdp` not yet. See [`formats/asset-index.md`](formats/asset-index.md).*
4. **Images and textures**: `.tga`/`.bmp`/`.jpg`, then `.texture` (`MMTF`
   mips). Work out the `INFO` pixel-format field. *`.texture` written in C++
   (`src/content/TextureFile`, `texture-tool`); see
   [`formats/texture.md`](formats/texture.md).*
5. **Meshes**: `JIRF` reader → `.im`/`.pm` `CHNK`/`MATL`/`GEOM` → draw one
   static object with a simple OpenGL or Vulkan renderer. *Reader, software
   preview and OBJ export written (`src/content/MeshFile`, `mesh-tool`); see
   [`formats/mesh.md`](formats/mesh.md). GPU `model-viewer` written (SDL2 + OpenGL 2.1), not yet run on a screen.* The game already
   abstracts its graphics API (it has an OpenGL back end), so this matches its
   own split.
6. **Materials and shaders**: map the `m.onetex`, `m.notex` … material names
   and the `vtx.txt`/`pxl.txt` programs to modern shaders. Cg itself is
   discontinued.
7. **Animation** `.kin` (`ANIM`). *Reader written (`src/content/AnimationFile`, `anim-tool`); see [`formats/animation.md`](formats/animation.md).*
8. **Routes**: `mapfile.gnd` (terrain), then `mapfile.trk` (track),
   `mapfile.obs` (objects), `mapfile.bmk` → load and fly around a built-in route.
9. **Sound**: WAV and MP3 through OpenAL or SDL audio, positional like `Snd3D`.
10. **Input**: keyboard, mouse and joystick through SDL (replaces DirectInput 8).
    Read `UserData\settings\keyboard.txt` for the bindings.
11. **Sessions**: `profile.dat`/`.obs`/`.trk`/`.lyr`.
12. **TrainzScript**: lexer, compiler and VM, then the native bindings
    (`TrainzScriptBase`, `GSObject`, `Soup` …). This is the biggest and riskiest
    piece. `.gsl` and `.gse` would need their own analysis.
13. **Train simulation – not mapped yet.** Driving physics, traction and
    braking, coupling, signalling, junctions and AI drivers. None of this has
    been analysed; it lives in `trainz.exe`, which has no symbols. Finishing
    steps 1–12 gives a content viewer, **not a working game**.
14. **UI** (`InterfaceSystem`) and the in-game HTML pages (replacing Gecko).
15. **Networking and Download Station**: last, and possibly never; the 2010
    services' status is unknown.

## 6. Open questions

- `FIL ` hash algorithm; the meaning of the `"FIL!"` tag; the last `ARCHINFO` field.
- Inner layouts of `NINF`, `INFL`, `.trk`, `.obs`, `.bmk`, `profile.dat`, `.gsl`, `.gse`.
  (`GEOM`, `MMTF` `INFO` and `.kin` are worked out from data; the `.gnd` world block is
  traced, but what several of its values mean is still a guess.)
- Search order and version resolution between `local`, `editing`, the built-in
  `.ja` files and kuid2 versions.
- The MP3 decoder path.
- Function-level names inside `trainz.exe` (needs a working decompiler: install
  Ghidra 12.1.x for REA, or configure IDA).
