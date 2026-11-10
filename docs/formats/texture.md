# `.texture` – Jet texture (`JIRF` form `MMTF`)

- **Code:** `src/content/TextureFile.{h,cpp}`, `src/content/JirfFile.{h,cpp}`.
- **Command-line tool:** `texture-tool info / png`. PNGs go to
  `~/.local/share/trainz-native/textures/`.
- **Confidence words:** as in [`ja.md`](ja.md).
- **Checked:** every `.texture` in `Base.ja`, `Data.ja` and `Builtin\8.ja`
  (6,666 files), using a Python prototype of the same rules. The C++ reader has
  not yet run on real files.

## 1. Container – checked

```
"JIRF"  u32 size (bytes after this field)  "MMTF"
  "INFO" u32 len  u32 words[len/4]
  "MMAP" u32 len  pixels of the smallest mip level
  "MMAP" ...                       (one chunk per level, smallest first)
  "MMAP" ...                       last = full size
```

- Chunks follow each other with **no padding**.
- The reader is `Jet::TextureResource::Load` in `RenderSystem_7c.dll`
  (`0x10045500`). It pushes the `'MMTF'` tag at `0x10045631` and compares
  `'MMAP'` at `0x10045ab1`. The writer is `SaveMipMap` (`0x100464f0`).
- Every one of the 6,666 files has exactly `INFO` + `MMAP` chunks.

## 2. INFO – checked (fields 0, 2, 4, 5, 6) / unknown (the rest)

There are two versions: **0x107** with an 80-byte INFO (6,637 files) and
**0x103** with 60 bytes (29 files).

| Word | Meaning | Evidence |
|---:|---|---|
| 0 | version (0x107 / 0x103) | |
| 1 | 0 everywhere | unknown |
| 2 | **mip level count** | equals the number of `MMAP` chunks in all 6,666 files |
| 3 | 1 everywhere | unknown |
| 4 | **pixel format** (see 3.) | every mip size matches the format's size rule |
| 5 | **width** | as above |
| 6 | **height** | as above; non-square sizes exist (256×128 …) |
| 7 | 0 or 1 | unknown |
| 8, 9 | 2 or 1 | unknown; possibly the U/V wrap mode |
| 10 | 0 | unknown |
| 11–13 | 2 | unknown |
| 14 | 0, 1 or 2 (−1 in some 0x103 files) | unknown; 1 appears on files named "…alpha" |
| 15 | 0, or `0x41800000` (= 16.0 as a float) in 10 files | unknown |
| 16–19 | 0 (19 is 1 in 5 files) | unknown |

**Mip level `n`** (0 = full size) is `max(1, width >> n)` × `max(1, height >> n)`.
This holds for all files.

## 3. Pixel formats – traced (names) / checked (sizes, colours)

The format numbers follow the engine's own list of format names in
`RenderSystem_7c.dll`. It is stored, in this order, into the slots
`0x100b65a8`–`0x100b65d4` by the code at `0x1003c43c` onward:

| Id | Name | Bytes | Files | Status |
|---:|---|---|---:|---|
| 0 | NULL | – | 0 | – |
| 1 | **ARGB8888** | 4 per pixel | 393 | checked: bytes B, G, R, A in memory, real alpha (decoded grass shows the see-through gaps) |
| 2 | ABGR8888 | 4 per pixel | 0 | by name only |
| 3 | **RGB0888** | 4 per pixel | 1,889 | checked: bytes B, G, R, unused (that byte is 255 in almost every pixel) |
| 4 | BGR0888 | 4 | 0 | by name only |
| 5 | RGB888 | 3 | 0 | by name only |
| 6 | BGR888 | 3 | 0 | by name only |
| 7 | ARGB4444 | 2 | 0 | by name only |
| 8 | RGB565 | 2 | 0 | by name only |
| 9 | RGB555 | 2 | 0 | by name only |
| 10 | **DXT1** | 8 per 4×4 block | 3,607 | checked (grass, brick and concrete decoded and inspected) |
| 11 | **DXT3** | 16 per 4×4 block | 1 | size checked |
| 12 | **DXT5** | 16 per 4×4 block | 776 | size checked |

- **DXT block counts round up**, with at least 1 block. So a 1×1 DXT1 level
  is stored as 8 bytes, a 2×2 DXT5 level as 16.
- **The DXT decoding** is the standard S3TC algorithm. For DXT1, a block whose
  first colour ≤ the second has 3 colours plus transparent black.
- **The formats never seen** (2 and 4–9) are decoded by their names only. A
  test against real data is needed before trusting them.

## 4. Not done yet

- `.texture.txt` descriptors and loose `.tga` / `.bmp` / `.jpg` sources.
  JPEG needs a decoder: `libjpeg62-turbo-dev` in Debian would be the choice.
- The meaning of the INFO words marked unknown.
- Uploading to the GPU. That belongs to the renderer in steps 5–6; the DXT
  data can go to OpenGL as-is through S3TC.
