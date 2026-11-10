# Loose images – `.tga`, `.bmp`, `.jpg`

Assets keep source images next to their compiled `.texture` files, and mesh
materials and configs refer to them by name (icons, thumbnails, `$screenshot$`
pictures, masks, ground textures, displacement maps).

- **Code:** `src/content/ImageFile.{h,cpp}` (`decodeTga`, `decodeBmp`,
  `decodeJpeg`, `decodeImage`). All return `RgbaImage`: 8-bit R, G, B, A, top row
  first, the same layout `decodeToRgba8()` gives for `.texture`.
- **Command-line tool:** `texture-tool info / png` takes these names too. PNGs go to
  `~/.local/share/trainz-native/textures/` (through `SafeOutputDir`, as for `.texture`).
- **JPEG** goes through libjpeg (`libjpeg62-turbo-dev` from Debian). It is
  optional: CMake builds it in when `find_package(JPEG)` finds it; without it,
  `decodeJpeg` throws `ContentError` and the JPEG tests skip (CI sets
  `TRAINZ_REQUIRE_JPEG=1` so they fail there instead).

**Confidence words** (as in [`ja.md`](ja.md), adapted):
- **Observed:** counted in the disc data (survey below).
- **Spec:** from the public format specifications (Truevision TGA 2.0, Microsoft
  BMP / `BITMAPINFOHEADER`, JPEG ITU T.81 via libjpeg). **Nothing here is traced in
  the game's own code:** how `trainz.exe` / `RenderSystem_7c.dll` load these
  files has not been looked at.
- **Port's choice:** a decision the spec leaves open; the game may differ.

## Survey (observed, 2026-11-10)

Every `.tga`, `.bmp`, `.jpg` and `.jpeg` entry in all 8 `Builtin` archives plus
`Base.ja` and `Data.ja` (`sample8.ja` in `testdata` is a subset of `8.ja`, so it is
not counted again). The headers were classified by a throwaway C++ survey built on
`JaArchive`, then every file was decoded with `ImageFile` (also the
`RealInstall.LooseImagesDecode` test, run under ASan/UBSan on `Builtin\1.ja`–`8.ja`).

| Archives | `.tga` | `.bmp` | `.jpg` | `.jpeg` | Total |
|---|---:|---:|---:|---:|---:|
| `Builtin\1.ja`–`8.ja` | 10,650 | 2,476 | 6,145 | 0 | 19,271 |
| `Base.ja` | 521 | 182 | 70 | 0 | 773 |
| `Data.ja` | 469 | 29 | 2 | 0 | 500 |
| **All** | **11,640** | **2,687** | **6,217** | **0** | **20,544** |

No duplicate names were hidden, every file read back from its archive, and every
file's magic matches its extension (all BMPs start `BM`, all JPEGs `FF D8`).

**Result:** 20,540 decode; **4 are refused as damaged** (below).

### TGA (11,640)

| Image type | bpp | Alpha bits | Origin | Files |
|---|---:|---:|---|---:|
| 2 true-colour | 24 | 0 | bottom-left | 5,154 |
| 2 true-colour | 24 | 0 | top-left | 107 |
| 2 true-colour | 32 | 8 | bottom-left | 5,855 |
| 2 true-colour | 32 | 8 | top-left | 25 |
| 2 true-colour | 32 | **0** | bottom-left | 436 |
| 2 true-colour | 32 | **0** | top-left | 16 |
| 2 true-colour | 16 | 1 | bottom-left | 16 |
| 2 true-colour | 16 | 0 | bottom-left | 3 |
| 10 RLE true-colour | 24 | 0 | bottom-left | 8 |
| 10 RLE true-colour | 24 | 0 | top-left | 12 |
| 10 RLE true-colour | 32 | 8 | bottom-left | 1 |
| 1 colour-mapped (8-bit index, 256 × 24-bit map) | 8 | 0 or 8 | bottom 5 / top 1 | 6 |
| 3 grey | 8 | 8 | bottom-left | 1 |

- No right-to-left images, no types 9/11, no 15-bit, no colour map on a true-colour image.
- 670 files have an image ID field (skipped). 11,013 have the TGA 2.0 footer,
  627 don't; the footer and extension area are not needed and are ignored.
- Largest side: 17 files have a side of 2048 or more; none is over 8192.

### BMP (2,687)

All have the 40-byte `BITMAPINFOHEADER`, 1 plane, and are bottom-up.

| bpp | Compression | Files |
|---:|---|---:|
| 24 | none | 1,322 |
| 8 | none (palette; 812 give `biClrUsed`, 471 use 0 = 256) | 1,283 |
| 8 | **RLE8** | 33 |
| 4 | none (palette; 44 give `biClrUsed`) | 48 |
| 32 | none | 1 |

Examples of the 8-bit and 4-bit files by name: masks (`consist_mask.bmp`,
`pozink_alpha.bmp`) and displacement maps (`displacements/hill/hill.bmp`); their
palettes were not analysed.

### JPEG (6,217)

All 8-bit, 3 components (YCbCr). None is grey, CMYK or arithmetic-coded.

| Kind | 4:4:4 | 4:2:2 | 4:2:0 | Files |
|---|---:|---:|---:|---:|
| SOF0 baseline | 2,580 | 107 | 2,831 | 5,518 |
| SOF2 progressive | 391 | 0 | 308 | 699 |

(Sampling is the first component's factors: `0x11`, `0x21`, `0x22`.) Markers seen:
JFIF, Exif, Adobe `APP14` in many combinations; libjpeg handles them all.

### Refused files (observed)

| File | Why |
|---|---|
| `Builtin\4.ja` `hash-da/kuid 2000 28000/nightwindows.tga` | header says 256×256 at 32 bpp, but holds exactly 196,608 bytes = 256×256 at **24** bpp. Whether the asset uses it is not checked (its folder also has `.texture` files). |
| `Builtin\3.ja` `hash-92/kuid2 86661 9210691 1/log_equip_tn.jpg` | baseline JPEG cut off inside the scan data (no end marker; libjpeg runs out of data in the middle of a scan even after an end marker is added). |
| `Builtin\4.ja` `hash-c8/kuid2 243555 10040 2/screenshot.jpg` | as above |
| `Builtin\4.ja` `hash-ea/kuid2 243555 10010 2/screenshot.jpg` | as above |

What the game shows for these is unknown. libjpeg on its own would pad the
missing part with grey; the port refuses instead (port's choice).

One BMP (`Builtin` `hash-93/kuid _13 161001/front_mask.bmp`) has a 14-entry
palette but uses higher indices in 1,100 pixels. Those are drawn black and a
warning is returned (port's choice).

## Alpha (observed / port's choice)

Measured on the decoded pixels:

| Files | Alpha 255 everywhere | 0 everywhere | Mixed |
|---|---:|---:|---:|
| TGA 32 bpp, 8 alpha bits (5,880 decoded) | 368 | 79 | 5,433 |
| TGA 32 bpp, **0** alpha bits (452) | 78 | 70 | 304 |
| TGA 16 bpp, 1 alpha bit (16) | 0 | 16 | 0 |

- **32 bpp TGA:** the 4th byte is always returned as alpha, even when the
  descriptor says 0 alpha bits, because 304 of those 452 files clearly hold a real
  alpha channel (the spec says such bytes are not alpha). Port's choice.
- **16 bpp TGA:** bit 15 is alpha only when the descriptor says 1 alpha bit
  (spec). All 16 such disc files have it 0 everywhere.
- So **165 TGAs that carry alpha have 0 everywhere.** A renderer that blended
  them would draw them invisible, so whether alpha is used must come from the
  material type (step 6), not from the file. `RgbaImage::alphaFromFile` says
  whether A came from the file (true) or is just 255 (false).
- **BMP:** always opaque. The 4th byte of 32 bpp `BI_RGB` is "reserved" in the
  spec, so the one such disc file is read as opaque (its 4th bytes were not inspected).
- **JPEG:** always opaque.

## What the decoders accept (spec)

### TGA

```
u8  id_length      u8  colour_map_type (0/1)   u8  image_type
u16 map_first      u16 map_length              u8  map_entry_bits (15/16/24/32)
u16 x_origin       u16 y_origin                u16 width   u16 height
u8  bits_per_pixel u8  descriptor: bits 0-3 alpha bits, bit 4 right-to-left, bit 5 top origin
id[id_length]  colour_map[map_length * entry bytes]  pixel data ...
```

- Types 1/9 (8-bit index into the map), 2/10 (15/16/24/32 bpp, stored B, G, R
  [, A]; 16 bpp as A1R5G5B5), 3/11 (8-bit grey). Types 9, 10, 11 are RLE.
- **RLE:** packet byte `n`; top bit set = one pixel repeated `(n & 0x7f) + 1`
  times, else that many raw pixels. Packets may run on into the next row
  (TGA 1.0 allowed it); a packet past the image end is an error.
- Rows are reordered to top-first, left-to-right.
- A colour index below `map_first` or past the map is an error.

### BMP

- `"BM"`, `u32 pixel_offset` at 10; info header of 40 bytes (or 52/56/108/124,
  whose first 40 bytes are the same); the 12-byte OS/2 header is refused.
- 1/4/8 bpp with a B, G, R, x palette (`biClrUsed` entries, 0 = 2^bpp), 24 and
  32 bpp B, G, R [, x]. Rows padded to 4 bytes. Negative height = top-down.
- **RLE8** (compression 1, 8 bpp only): `n v` = `n` copies of index `v`;
  `0 0` end of line, `0 1` end of bitmap, `0 2 dx dy` delta, `0 n` = `n` raw
  indices padded to 2 bytes. Pixels the stream skips get palette index 0
  (port's choice). Running past the last row, or data ending before the
  end-of-bitmap code, is an error. Writing past a row's end is dropped with a warning.
- Refused: 16 bpp, `BI_BITFIELDS`, RLE4, JPEG/PNG-in-BMP (none on the disc).

### JPEG

- libjpeg (libjpeg-turbo 2.1.5 in Debian 13) decodes straight to RGBA
  (`JCS_EXT_RGBA`); grey, YCbCr and RGB JPEGs are accepted, CMYK/YCCK refused.
- libjpeg's fatal errors `longjmp` back into a small C-style function; only C
  structs live across it, so no C++ destructor is skipped.
- **Truncation is an error,** not grey padding. A file that ends without its
  `FF D9` marker is decoded from a copy with the marker added: if the picture is
  complete, that's only a warning; if libjpeg still runs out of scan data, or a
  progressive file is missing later scans (some coefficient never reaches full
  precision in libjpeg's `coef_bits`), it is refused.
- At most 500 scans (libjpeg-turbo's advice against "scan bombs"), at most
  1 GiB of libjpeg working memory.

### Limits (all three)

Width and height must be 1..8192 (`kMaxTextureSize`), checked before any size
arithmetic or allocation. TGA and uncompressed BMP also check that the file
holds enough bytes for the picture before allocating it.

## Tests

- `tests/test_image.cpp`: every sub-type above built in code (TGA 1/2/3/9/10/11,
  15/16/24/32 bpp, both origins, right-to-left, image ID, colour map; BMP 1/4/8/24/32 bpp,
  RLE8 with runs, raw runs, end of line, delta and end of bitmap, top-down, larger headers;
  JPEG baseline, progressive, grey, missing end marker). Every shorter prefix
  of each file throws `ContentError`; garbage, bad headers and oversized
  dimensions throw; thousands of byte-flipped files never crash (ASan/UBSan).
- `RealInstall.LooseImagesDecode`: decodes every image in the install and prints
  counts by sub-type; the 4 damaged files above are listed as known.

## Not done / unknown

- How the game itself decodes these (its own loader or a library), and what it
  does with the 4 damaged files and with alpha that is 0 everywhere.
- `.texture.txt` descriptors, which say how an image's colour and alpha are
  combined (e.g. a `*_alpha.bmp` mask), are still missing (step 4).
- `.png`, `.dds` or other image kinds: none of those names were surveyed.
