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
- **Addresses:** these are virtual addresses inside the named module.
  - `trainz.exe` has no symbols (it exports only `JetGetPlugins`), so its
    addresses are the places that *reference* a string, not named functions.
  - Function starts written as `~0x…` were found with a padding heuristic and
    are approximate.
- **Confidence words:**
  - **Confirmed:** decoded or checked against real data.
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

### 1.2 The `.ja` archive (Jet Archive) – **confirmed**

There are no `.big` files. The packed format is `.ja`.

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
  12 bytes zero
repeat file_count times:
  "FIL " u32 block_size
    char name[260]              NUL-terminated, original case, '/' separators;
                                the bytes after the NUL are leftover memory
    u32  original_size
    u32  stored_size
    u32  hash                   NOT CRC32 (tested). Algorithm unknown
    12 bytes zero
    u32  flags                  0x80 = stored as CFIL (compressed), 0 = plain
    u8   data[stored_size]
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
- **Verified:**
  - Two compressed files decode to exactly their recorded sizes:
    `DriverAchievementsUpdaterLibrary.gs`, 7219 bytes, and a `config.txt`,
    1426 bytes.
  - Samples of every file type in `Base.ja` and `8.ja` decode to valid
    headers (`JIRF`, `BM`, `RIFF`, `JFIF`, `ACS$`, `ID3`).
  - A full pass over every file was started but didn't finish, so whole-archive
    decoding isn't proven yet.

### 1.3 How an asset is looked up – **KUIDs confirmed, plus a correction**

Your belief is **right**: each asset has a KUID and a `config.txt`. Corrections
and additions:

- **There are two KUID forms:** `<kuid:USER:CONTENT>` and
  `<kuid2:USER:CONTENT:VERSION>`. Evidence:
  - `trainz.exe` format strings `&#60;kuid:%d:%d&#62;` and `&#60;kuid2:%d:%d:%d&#62;`;
  - `TADDaemon.exe` strings `<kuid2:%d:%d:%d>` and `kuid2 %d %d %d`.
  - Example from a decoded `config.txt`: `kuid <kuid:447264:1011>`.
- **Built-in assets usually carry `config.chump`, not `config.txt`.**
  `config.chump` is the binary form of the same tree: 1468 `.chump` files in
  `8.ja` against 139 `.txt`. Loose user content and `Base.ja` use `config.txt`.
- **Folder rule inside the archives:** `hash-XX/kuid USER CONTENT/…` or
  `hash-XX/kuid2 USER CONTENT VERSION/…`, where **XX = the XOR of the 8 bytes of
  `(int32 USER, int32 CONTENT)` in little-endian**. This matched all 499
  distinct asset folders tested in `8.ja`. Example:
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
| Archive | `.ja` | **Confirmed** | See 1.2 |
| Content package | `.cdp` (`.cdp2`) | Seen | `"Unable to write archive file. Needs to be .cdp file extension"` (`trainz.exe`, `TADDaemon.exe`); `TrainzUtil createCDP <file.cdp> <kuid…>`; `ContentManager.exe` file filter `CDP file (*.cdp;*.cdp2)`. Not sampled |
| Asset config (text) | `config.txt` | **Confirmed** | UTF-8 with BOM, CRLF line ends, `key  value` lines, `name { … }` sub-blocks, quoted strings, `<kuid…>` values, `-fr` style language suffixes (`username-fr`). Parser: `Jet::ConfigData` in `KernelStandard_7c.dll` (88 exports; writer `ConfigData::Write` `0x10011350`). `config.txt` referenced in `trainz.exe` at `0x61fea8`, `0x6212c7`, `0x64804f`, `0x648e51`, `0x66c8fe`, `0x7ccec3` |
| Asset config (binary) | `.chump` | Header confirmed | Magic `ACS$`, `u32` version 1, then a tagged tree (strings such as `\x05kuid`). Readers `ConfigData::ReadBin` `0x10013220` / `0x10013290` / `0x100133c0`, `ReadBinFile` `0x10012a50`; writer `WriteBinFile` `0x10012880`. Also used for settings and UI strings (`interfaceFrench.chump` …) |
| Mesh | `.im` | Container confirmed | `JIRF` chunk container (`"JIRF"`, `u32` size, form type `IDXM`). Chunks: `INFO` (68 bytes), then one `CHNK` per material (header `u32 0x64, u32 index`) holding `MATL` (material name such as `*Alastair*11FA873*alistair*m.onetex`) and `GEOM` (vertex/index data, inner layout **unknown**); ends with `INFL`. Classes `IndexedMeshDefinition`, `IndexedMeshInstance`, `IndexedMeshLODInstance` (`Render3DFoundation_7c.dll`). Loaded through `%s/%s.im`, `default_shadow.im` (`trainz.exe`) |
| Older mesh | `.pm` | Container confirmed | Same `JIRF`/`IDXM` container; each `CHNK` also carries `NINF`. Class `IndexedMeshDefinitionOld` (93 exports) |
| LOD set | `.lm`, `.lm.txt` | Seen | `trainz.lm`, `trainz.lm.txt` (`trainz.exe`, `TrainzUtil.exe`) |
| Animation | `.kin` | Container confirmed | `JIRF` form `ANIM` with an `INFO` chunk. Class `AnimationModifier` (`Render3DFoundation_7c.dll`). Strings `anim.kin` (references in `trainz.exe` at `0x554c17`, `0x5a9882`, `0x67a218`, `0x87ac1f`, `0x907778`) |
| Texture | `.texture` | Container confirmed | `JIRF` form `MMTF`: `INFO` (80 bytes, starts `0x107`), then one `MMAP` chunk per mip level, **smallest first**. The 32×32 sample holds 4-byte pixels; whether other pixel formats (e.g. DXT) occur is **unknown** |
| Texture descriptor | `.texture.txt` | Seen | Text file pointing at a `.tga`/`.bmp`/`.jpg`. Plugins `RenderSystem:texture.txt`, `RenderSystem:tga-tex/bmp-tex/jpg-tex` in `plugins.ini` |
| Image sources | `.tga`, `.bmp`, `.jpg` | Confirmed (standard) | Standard headers. `KernelResources:tga/bmp/jpg` plugins; JPEG through `ijl10.dll` (Intel JPEG Library), imported by `KernelStandard_7c.dll` |
| Shader / material text | `vtx.txt`, `pxl.txt`, `material.txt` | Seen | `plugins.ini` entries. ARB assembly programs embedded in the DLLs (`PARAM texMat[4] = { state.matrix.texture[0] }`) |
| Trees | `.srt` | Header confirmed | Magic `SRT 05.0.1` (SpeedTree). Read by `bin\plugins\TNISpeedTree.dll` |
| Route terrain | `mapfile.gnd` | Header confirmed | Magic `GND\r`, then tagged chunks whose 4-byte tags are stored reversed (e.g. bytes `5ceS` = `Sec5`). Inner layout **unknown**. `trainz.exe` references `mapfile.gnd` at `0x87ca9c` |
| Route objects | `mapfile.obs` | Seen | Strings `mapfile.obs`, `.obs` |
| Route track | `mapfile.trk` | Seen | Strings `mapfile.trk`, `.trk` |
| Route bookmarks | `mapfile.bmk` | Seen | 330-byte sample: `u32 7`, then float positions |
| Route extra | `mapfile.trc` | Seen | 32-byte sample. Purpose **unknown** |
| Session | `profile.dat`, `profile.obs`, `profile.trk`, `profile.lyr` | Headers seen | `.obs`/`.trk` start with small `u32` counts, then reversed-tag chunks (bytes `2Ost`, `tSkt`). `.lyr` contains `session-layer`. `.dat` (1.4 MB) contains the bytes `RVHB` |
| Sound | `.wav`, `.mp3` | Confirmed (standard) | RIFF/WAVE and ID3 MP3 in the archives. String `mp3` in `SoundCore_7c.dll`. The MP3 decoder isn't identified (**unknown**) |
| Script source | `.gs` | Confirmed | TrainzScript (GameScript) text |
| Compiled script | `.gsl` | Seen | Binary (contains names such as `asset.gs`). Format **unknown** |
| Protected script | `.gse` | Seen | Encrypted or obfuscated script (8 KB sample: `u32 1`, `u32 0`, `u32 size`, then high-entropy bytes). `"CRC Mismatch on '%s' after recompile. Do you have both .gs and .gse present?"`. `trainz.exe` references `.gse` at `0x8adc64` |
| Video | (AVI / DirectShow) | Seen | `VFWMoviePlayer_7c.dll` (imports `AVIFIL32`), `DSMoviePlayer_7c.dll` (ole32/DirectShow) |

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
- The rest of the renderer is API-neutral and lives in separate DLLs:
  `RenderSystem_7c` (Display, Material, TextureManager, VertexProgram),
  `Render3DCore_7c` (Camera, ViewPort, Particle, materials such as
  `OneTextureMaterial`), `Render3DFoundation_7c` (meshes, animation,
  `GeometryConfigData`), `Render3DExtension_7c` (ParticleFX, Haze, Decal) and
  `Render2DCore_7c` (sprites, canvas).
- `WaterSystem_7c.dll` (water materials, ocean simulation) and
  `TNISpeedTree.dll` (trees) are separate plugins.
- `trainz.exe` imports **`nvtt.dll`** (NVIDIA Texture Tools `Compressor`, with
  `enableCudaAcceleration` and `cudart.dll`) to compress textures at run time.

### Sound

- **`SoundCore_7c.dll`** holds the engine-side sound system: `Sound`, `SndBase`,
  `Snd3D`, `SndPolyline`, `SndAreaPolygon`, `SoundResource`, `SoundDefResource`
  and `Microphone`.
- Two back ends sit on top of it:
  - **`SoundDS8_7c.dll`**: DirectSound 8, created through COM (`ole32`).
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
- The compiler and the virtual machine that runs scripts seem to be **inside
  `trainz.exe`**. It holds the native binding table, e.g.
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
| `bin\trainz.exe` | 6.55 MB | **The game itself**: trains and driving, routes, the Surveyor editor, the TrainzScript compiler/VM and bindings, the asset-DB client, networking | Imports every Jet core DLL plus `WSOCK32`, OpenSSL, `nvtt`, `trainznativeinterface`. Only export `JetGetPlugins` |
| `KernelStandard_7c.dll` | 488 KB | Basics: files, `.ja` archives, `CFIL`/BPE, `ConfigData` (txt/chump), strings, bitmaps, paths, options | 1701 exports (`File`, `ConfigData`, `Bitmap`, `Path` …) |
| `Kernel_7c.dll` | 116 KB | Plugin loader, tasks, app object, compression helpers | `PluginConfig`, `TaskManager`, `Compress`, `JetGetPluginsKernel` |
| `KernelResource_7c.dll`, `KernelResources_7c.dll` | | Resource databases (mounting `.ja` files) and bitmap resources | `ResourceDatabase`, `ResourceSystem`, `BitmapResource` |
| `RenderSystem_7c`, `Render2DCore_7c`, `Render3DCore_7c`, `Render3DFoundation_7c`, `Render3DExtension_7c` | | API-neutral renderer, meshes, animation, particles | See Graphics above |
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
   the `hash-XX/kuid …` folder rule. *Done as a prototype in this analysis;
   the next step is a full-archive pass over `Builtin\1.ja`–`8.ja`.*
2. **`config.txt` parser and the `.chump` (`ACS$`) binary reader**, plus a KUID
   type (`kuid` / `kuid2`).
3. **Own asset index**: scan `local`, the `.ja` files and `.cdp` packages into a
   KUID → location table. This avoids depending on `assets.tdx`/TADDaemon early.
4. **Images and textures**: `.tga`/`.bmp`/`.jpg`, then `.texture` (`MMTF`
   mips). Work out the `INFO` pixel-format field.
5. **Meshes**: `JIRF` reader → `.im`/`.pm` `CHNK`/`MATL`/`GEOM` → draw one
   static object with a simple OpenGL or Vulkan renderer. The game already
   abstracts its graphics API (it has an OpenGL back end), so this matches its
   own split.
6. **Materials and shaders**: map the `m.onetex`, `m.notex` … material names
   and the `vtx.txt`/`pxl.txt` programs to modern shaders. Cg itself is
   discontinued.
7. **Animation** `.kin` (`ANIM`).
8. **Routes**: `mapfile.gnd` (terrain), then `mapfile.trk` (track),
   `mapfile.obs` (objects), `mapfile.bmk` → load and fly around a built-in route.
9. **Sound**: WAV and MP3 through OpenAL or SDL audio, positional like `Snd3D`.
10. **Input**: keyboard, mouse and joystick through SDL (replaces DirectInput 8).
    Read `UserData\settings\keyboard.txt` for the bindings.
11. **Sessions**: `profile.dat`/`.obs`/`.trk`/`.lyr`.
12. **TrainzScript**: lexer, compiler and VM, then the native bindings
    (`TrainzScriptBase`, `GSObject`, `Soup` …). This is the biggest and riskiest
    piece. `.gsl` and `.gse` would need their own analysis; `.gse` looks
    encrypted.
13. **UI** (`InterfaceSystem`) and the in-game HTML pages (replacing Gecko).
14. **Networking and Download Station**: last, and possibly never; the 2010
    services' status is unknown.

## 6. Open questions

- `FIL ` hash algorithm; the meaning of the `"FIL!"` tag; the last `ARCHINFO` field.
- Inner layouts of `GEOM`, `NINF`, `INFL`, `MMTF` `INFO`, `.kin`, `.gnd`,
  `.trk`, `.obs`, `profile.dat`, `.gsl`, `.gse`.
- Search order and version resolution between `local`, `editing`, the built-in
  `.ja` files and kuid2 versions.
- The MP3 decoder path.
- Function-level names inside `trainz.exe` (needs a working decompiler: install
  Ghidra 12.1.x for REA, or configure IDA).
