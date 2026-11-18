# Texture descriptors: `.texture.txt`

Reader: `src/content/TextureDescriptor.{h,cpp}` (`parseTextureDescriptor`,
`resolveDescriptorPath`, `composeDescriptorImage`). Tests:
`tests/test_texture_descriptor.cpp` and `RealInstall.TextureDescriptorsResolve`.
`texture-tool info / png` accept them.

Evidence classes: **traced** (read in `RenderSystem_7c.dll` disassembly), **observed**
(seen in the disc files), **wiki** (the official Trainz wiki, not checked in code).

## On the disc (observed)

360 descriptors, all in `Base.ja` (none in `Data.ja` or `Builtin\1.ja`–`8.ja`, whose
assets ship compiled `.texture` files). Every one is plain ASCII `Key=value` lines,
CR LF or LF, often without a final newline, with keys in either case
(`Primary=` / `primary=`). Keys used: `Primary` (all 360), `Alpha` (105), `Tile` (359),
plus a few `AlphaHint`, `Anisotropy`, `Mag/Min/MipFilter`, `Compression`, `Hint`.

Every descriptor's images exist next to it, decode, and combine
(`RealInstall.TextureDescriptorsResolve`: 360 of 360, no warnings).

## Keys and values (traced)

The reader is around VA `0x10005aa0`–`0x10006500` in `RenderSystem_7c.dll`. It reads
the file through `Jet::ConfigData` (`Find` / `Count` / `operator>>`) using key tokens
made at start-up (`PStringCache::Create`, globals `0x100b6154`–`0x100b6190`).
The value words are lowercased (`AnsiString::ToLower`) and looked up in
(name, number) tables at `0x1006ca78`–`0x1006cb34` by the helper at `0x10006510`.
An unknown word leaves the field unchanged.

| Key | Read as | Values (engine number) |
|---|---|---|
| `Primary`, `Alpha` | file name; the descriptor's folder is used (`File::Path::DelPath`), then `Resource::ConvertResourceName` (not traced) | — |
| `Dimension` | word | `two` 0, `cube` 1, `volume` 2, `one` 3 |
| `Compression` | word | `none` 0, `dxt1` 1, `dxt3` 2, `dxt5` 3 |
| `Hint` | word | `static` 0, `dynamic` 1 |
| `NormalMapHint` | word | `none` 0, `normalmap` 1 |
| `ModifyMap` | word | `none` 0, `flipgreen` 1 |
| `AlphaHint` | word | `opaque` 0, `semitransparent` 1, `masked` 2 |
| `Anisotropy` | whole number | e.g. 16 |
| `AutoMip` | word | `none` 0, `default` 1, `fastest` 2, `nicest` 3 |
| `MagFilter`, `MinFilter`, `MipFilter` | word | `nearest` 0, `linear` 1, `default` 2 |
| `Tile` | letters | see below |

**`Tile` (traced, `0x1000645b`–`0x100064c3`):** if the key exists, both directions are
set to clamp (1); the value is lowercased; if it contains an `s` anywhere, horizontal
becomes wrap (2); if it contains a `t`, vertical becomes wrap (2). So `st` wraps both,
`none` clamps both, and (oddly) any word with those letters counts.

A `Texture` key is also looked up first; no disc file has one, and what it does is not
traced. Defaults for absent keys are not traced; the reader leaves those fields empty.

## How Primary and Alpha combine (wiki, not traced)

From the official wiki page "Texture file" (online.ts2009.com):
- `Primary` alone: the image's colour; any alpha in the file is ignored (opaque).
- `Alpha` naming the same file: that image's own RGBA.
- A different `Alpha` image: colour from `Primary`, opacity from the `Alpha` image's grey
  level; the sizes must match.

`composeDescriptorImage` does exactly that (grey level = red channel, which is exact for
grey images). This fits the disc: 165 TGAs carry an alpha channel that is 0 everywhere
(see [image.md](image.md)); used through a descriptor without `Alpha`, they show opaque.

## Not known

- `Jet::ConfigData`'s tokenizer (is `=` required? quotes? comments?); the reader
  accepts `Key=value`, `Key value`, quoted values and `//` comment lines.
- What `Resource::ConvertResourceName` does to the name (case, extension changes).
- The defaults for absent keys, and what `Texture` does.
- The game's own Primary/Alpha combining code (two loaders follow, at `0x100075f0`
  and `0x100077d0`, not traced).
