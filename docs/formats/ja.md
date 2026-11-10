# `.ja` – Trainz "Jet Archive"

Used for `Base.ja`, `Data.ja` and `Builtin\1.ja` … `Builtin\8.ja` in Trainz Simulator 2010.

- **Reader:** `src/content/JaArchive.{h,cpp}`, with `CfilCodec`, `ArchivePath`,
  `Kuid` and `ReadOnlyFile`.
- **Command-line tool:** `tools/ja_tool.cpp` (`ja-tool`).

**Source of truth:** `KernelStandard_7c.dll` from the TS2010 SP2 build. Its build path is
`D:\BUILD\src\trainz\candidate\ts2010_sp2\Jet\Src\Jet\KernelStandard\FileSubArchive.cpp`
and its SHA-256 is
`135e13b5e1857f2cbf594ddd63f00374f58c83dc6b07812727a828b2fc20d5d1`.

**How it was read:**
- Addresses are virtual addresses in that DLL (image base `0x10000000`).
- REA was used for its import and export tables.
- The code was read from GNU `objdump` disassembly, because REA's decompilers
  were not available on the analysis PC.
- Exported names come from the DLL's export table. Functions that aren't
  exported are given by address only.

**Confidence words:**
- **Traced:** read in the disassembly.
- **Checked:** also tested on real game data (`Base.ja`, `Data.ja`, `Builtin\8.ja` from the disc).
- **Observed:** seen in data only, with no code traced.
- **Unknown:** not worked out.

All integers are little-endian (32-bit x86).

---

## 1. Header – traced, checked

The archive opener is `Jet::FileSubArchive` (not exported) and starts at about
`0x10024c40`. It opens the file with `_sopen` (import `0x1004f2f0`).

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | `"ARCH"` | Read 4 bytes and compared with the string at `0x10053448` (`0x10024ca6`–`0x10024cce`). |
| 4 | 4 | `"INFO"` | Chunk tag (string `0x10053450`). It is read by `0x10021420`, which is passed size `0x20` at `0x10024cd6`. |
| 8 | 4 | info size = 32 | The reader requires 32. |
| 12 | 4 | `'ARC0'` (bytes `30 43 52 41`) | Compared with `0x41524330` at `0x10024ceb`. |
| 16 | 4 | version | Only `0x101`–`0x102` are accepted (`0x10024cfd`–`0x10024d09`). All three disc archives are `0x102`. |
| 20 | 4 | file count | Number of index rows. |
| 24 | 4 | index offset | Absolute offset of the `"IDX "` block. |
| 28 | 4 | unknown | 42203 in `Base.ja` and `Data.ja`, 42195 in `8.ja`. **Unknown** meaning. The exports `FileSubArchive::SetDefaultBuildVersion` and `SetDefaultProductCode` suggest a build or product code. |
| 32 | 12 | – | Zero in every sample. Not read by the opener. |

Our reader rejects any mismatch, a version outside `0x101`–`0x102`, and an index
offset outside the file.

## 2. File blocks – traced (tags) / checked (layout)

These follow the header, one per file. The tag string `"FIL "` is at
`0x100534d8` and is used at `0x1002549c` and `0x1002599c`. A `"FIL!"` tag exists
right after it in `.rdata`; its meaning is **unknown**, and it wasn't seen in
the sampled archives.

```
"FIL "  u32 block_size            block_size = 288 + stored_size; it counts
                                  everything after this 8-byte tag+size
  char  name[260]                 NUL-terminated, original letter case, '/'
                                  separators. Bytes after the NUL are leftover
                                  memory from the tool that wrote the archive
                                  (they contain kernel32 addresses)
  u32   original_size
  u32   stored_size
  u32   hash                      algorithm UNKNOWN. It is not CRC32, and not
                                  the engine's name hash (both tested on
                                  samples); the
                                  reader never checks it
  u8    reserved[12]              zero in every sample; not read (0x10026e40
                                  skips it)
  u32   flags                     the reader reads only the low 16 bits; bit 7
                                  (0x80) = compressed
  u8    data[stored_size]         ← data_offset in the index points here
```

The reader never walks these blocks; it uses only the index. `ja-tool verify`
doesn't walk them either.

## 3. Index – traced, checked

The loader is `0x10026520`, called from the opener at `0x10024d0f`.
- It seeks to the index offset (`_lseek`), reads 8 bytes and compares the tag
  with `"IDX "` (string `0x100534e0`, `0x100265f8`).
- **The block size field is ignored.** It reads `file count` rows of `0x128`
  (296) bytes, in batches of up to `0x1000` rows (`0x10026630`–`0x1002669c`).
  We do the same, but first check that the rows fit in the file, and we warn
  when the size field disagrees.

| Row offset | Size | Field |
|---|---|---|
| 0 | 4 | data offset (absolute; points at the file's data, i.e. just past its 288-byte header) |
| 4 | 4 | stored size |
| 8 | 260 | name, **lowercase**, NUL-terminated |
| 268 | 4 | original size |
| 272 | 4 | stored size (a second copy) |
| 276 | 4 | hash (same as in the file block) |
| 280 | 12 | reserved |
| 292 | 4 | flags (the reader takes the `u16` at 292, `0x10026e94`) |

Per row (`0x100266d0`–`0x10026740`, `0x100260b0`, `0x10026e40`):
- **The name must have a NUL within 260 bytes.** If it doesn't, the game stops
  loading the index; our reader stops with an error that names the row.
- **The name is lowercased** with MSVC `_strlwr` (import `0x1004f278`, call at
  `0x100260e9`). In the "C" locale that changes only ASCII `A`–`Z`.
- **Names of 259+ characters** are emptied by `0x10023790`, which also sets an
  error flag, so the row is skipped. **Empty names** are skipped
  (`0x1002610c`). We skip both and add a warning.
- **Folders:** each parent folder of the name is recorded for directory
  listings (`0x10026170`, `strrchr('/')`).
- **Name table:** the entry goes into a hash table (`0x10027a30`). The bucket
  is `jetNameHash(name) % bucket_count`, using the engine string hash
  `0x100150f0`: `h = (h << 3) ^ (signed char)c`. Rows are **appended**, and
  duplicates are kept.

The game does not check that a row's data lies inside the file. We warn at open
time and refuse to read such an entry.

## 4. Looking up a name – traced, checked on real names

Callers at `0x100253b3` and `0x10026c60`:
1. Copy the name, rejecting 259+ characters (`0x100237e0`).
2. `_strlwr`.
3. **Canonicalize** with `0x10023a20`:
   - split on `\` and `/` (separator string `0x100533c0`);
   - drop empty parts and `.` (string `0x10053218`);
   - `..` removes the previous part (string `0x1005321c`; nothing happens at the top);
   - join the parts with `/`;
   - a leading separator becomes a leading `/`.

   So `/a/b` does **not** find the stored name `a/b`.
4. Look up with `0x10027b70`: hash, pick the bucket, walk it **in insertion
   order** with an exact byte compare. **The first match wins**, so with
   duplicate names the earlier index row is used.

`JaArchive::find` does exactly these steps, using `std::unordered_map::emplace`,
which keeps the first entry. **Not seen in real data:** `Base.ja`, `Data.ja`
and `8.ja` contain no duplicate names (nor backslashes or leading `/`; the
longest name is 123 characters), so first-wins is traced but untested on the
game's files. **Partly traced:** the drive-letter (`X:`) branch
of the canonicalizer for `..`, which doesn't apply to archive names.

## 5. Compression: `CFIL` – traced, checked

- **Detection:** by the **`"CFIL"` magic at the start of the stored data**
  (`strncmp` at `0x1002005b`, string `0x10053220`), not by the flag. The flag
  (bit 7, `File::Entry::IsCompressed` at `0x10015470`) matched the magic in
  all 14,305 index rows of `Base.ja`, `Data.ja` and `Builtin\8.ja`. `ja-tool verify` reports any entry where they disagree.

**Header** (`0x1001ffd0`–`0x10020097`):
- In version `0x102` archives, and for standalone files, the reader reads 12
  bytes: `"CFIL"`, `u32 version`, `u32 original_size`. The size is taken only
  when `version >= 0x101` (`0x10020084`). All samples have version `0x101`.
- In version `0x101` archives it reads only the 4-byte magic (`0x10020038`),
  and the size comes from the entry. **Not checked on real data:** no `0x101`
  archive was available. The unit tests cover it.

**Blocks** (reader `0x10020dfd`–`0x10020e2f`, writer `0x10020237`–`0x10020330`):

```
u16 header
  bit 15 set:   (header & 0x7FFF) raw bytes follow
  bit 15 clear: header bytes of packed data follow; they expand to at most
                0x4000 (16 KiB). Every block except the last is a full 16 KiB.
```

The compressor is `0x1000ccc0` (not ported).

**Packed data = Byte Pair Encoding** (decoder `0x1000d130`). This is Philip
Gage's BPE (1994) with **one difference**. One packed block may hold several
sub-blocks back to back. Each sub-block works like this:

1. **Start from the identity table:** `left[c] = c` for all 256 codes.
2. **Read the pair table** as a series of runs, starting at code 0.
   - A count byte **> 127** skips `count − 127` codes. In the game's version
     the next count byte follows directly; Gage's original reads one pair
     after a skip.
   - A count byte ≤ 127 is followed by `count + 1` entries, each made of one
     `left` byte, plus a `right` byte when `left ≠ code`.
   - The table is done when the code reaches 256 or more (`cmp ecx,0x100; jge`).
3. **Read the length:** a big-endian `u16` count of packed bytes.
4. **Expand those bytes with a stack.**
   - A code with `left[b] == b` is a literal and is written out.
   - Any other code pushes `right[b]`, then `left[b]`, so `left` comes out first.
   - The stack holds 256 bytes.

**Checked:**
- Every compressed file sampled from `Base.ja` and `8.ja` (about 30) decodes
  to a valid file header.
- Two files decode to exactly their recorded sizes.
- A full pass over every entry has **not** been run yet. Run
  `ja-tool verify` or `TRAINZ_FULL_VERIFY=1` (see the README).

**Hardening that the game does not have** (all errors, never crashes):
- truncated input;
- a pair table running past 256 codes;
- stack overflow from cyclic tables;
- output larger than the block or file;
- zero-length blocks;
- a size field that would make us allocate huge buffers.

## 6. Assets and KUIDs – observed

The archive itself only knows file names. Built-in assets live in folders named
after their KUID:

```
hash-XX/kuid USER CONTENT/...
hash-XX/kuid2 USER CONTENT VERSION/...
```

`XX` is the **XOR of the 8 little-endian bytes of `(int32 USER, int32 CONTENT)`**
(the version is not included).
- In the index it appears lowercased (`hash-c0`).
- **Observed only:** the hash rule matched all 499 asset folders tested in
  `8.ja`, but the code that computes `XX` **has not been traced**.
  `TADDaemon.exe` formats `"hash-%02X"` at `0x44984a`; that spot loops over
  all 256 folders and is not the hash itself. The `kuid` / `kuid2` part of
  the name **is** traced: see `docs/formats/config.md` §1.4.
- The `RealInstall.KuidFolderHashRule` test checks it on every folder of
  every archive in an install.

`JaArchive::findKuid` lists an asset's files.
- A plain `<kuid:U:C>` matches only its `kuid U C` folder.
- With `anyVersion` it also matches `kuid2 U C *` folders.

**Unknown:** how the game picks between several versions, and between
built-in, `local` and `editing` copies of an asset. That's the asset database's
job, not the archive's.

## 7. What our reader does differently on purpose

| Situation | Game | Ours |
|---|---|---|
| Name not terminated in 260 bytes | stops loading the index | error naming the row |
| Index rows don't fit in the file | reads garbage / fails late | error at open |
| `IDX ` size field disagrees | ignored | ignored, plus a warning |
| Data range outside the file | fails at read time (unchecked) | warning at open, error on read |
| Damaged BPE data | can overflow its stack buffer | error |
| `CFIL` size field huge | allocates it | allocates in small steps, errors when data runs out |

## 8. Hard drive behaviour

- **Opening** reads the 44-byte header, then the whole index (3.4 MB for
  `8.ja`) in 1.2 MB batches with a single seek.
- **Reading an entry** reads its stored bytes front to back: in one piece for
  `read()`, in 1 MiB chunks for streams.
- **`ja-tool extract` and `verify`** sort entries by offset so the drive moves
  forward only.
