# Asset configuration: `config.txt` and `config.chump`

Every Trainz asset has a configuration: its KUID, its kind, its files and its
settings. It comes in two forms that hold the same kind of tree:

- **`config.txt`**: text. Used by `Base.ja` and loose user content.
- **`config.chump`**: binary, with an `ACS$` header. Used by most built-in
  assets: there are 1,468 `.chump` against 139 `.txt` files in
  `Builtin\8.ja`. Those are counts of files, not of assets.

**Code:**
- **Reader:** `src/content/AssetConfig.{h,cpp}`, with KUID helpers in
  `src/content/Kuid.{h,cpp}`.
- **Command-line tool:** `tools/config_tool.cpp` (`config-tool show` / `config-tool kuid`).

**Confidence words** (as in [`ja.md`](ja.md)):
- **Traced:** read in the game's code.
- **Checked:** tested on the disc's data.
- **Observed:** seen in data only.
- **Unknown:** not worked out.

Data checks used `Base.ja`, `Data.ja` and `Builtin\8.ja` from the disc. They
were done with a Python prototype of the same rules; the C++ reader has not yet
run on real files.

---

## 1. `config.chump`

### 1.1 Header – traced

The reader is in `trainz.exe` (no symbols) and starts at `0x7ff710`. Its marker
check is at `0x7ff7d1`.

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | `"ACS$"` | Compared with `0x24534341`. |
| 4 | 4 | version | 1 in every sample. Read but not compared by this function. |
| 8 | 4 | reserved | 0 in every sample. |
| 12 | 4 | body size | Size 0 is accepted as empty. Otherwise the body must fit in the file (`0x7ff856`). |
| 16 | … | body | Loaded into a buffer, then checked by `0x7ff1f0`. |

Bytes after the body are not read. Our reader ignores them with a warning.

### 1.2 Items – traced (walk) / checked (layout)

The checker is `0x7ff1f0(list, length)`. It runs while **more than 5 bytes**
remain in the list. Each item is:

```
u32  size          bytes that follow this field: 1 + keyLen + 1 + value
u8   keyLen        length of the key INCLUDING its terminating NUL
char key[keyLen]   e.g. "kuid\0"
u8   type
u8   value[size - keyLen - 2]
```

What the game checks:
- the type byte lies inside the list (`0x7ff21b`);
- `size` ≤ the bytes left (`0x7ff21f`);
- for **type 0** it checks the value as a nested list of `size − keyLen − 2`
  bytes (`0x7ff223`–`0x7ff238`);
- the walk moves forward and stays inside the list (`0x7ff252`–`0x7ff258`).

**Up to 5 left-over bytes at the end of any list are ignored.**

**What our reader adds:**
- **No underflow:** it also refuses `size < keyLen + 2`, where the game's
  arithmetic would wrap around.
- **No reading on the boundary:** it refuses a type byte exactly at the end of
  the list. The game's `>` check allows that and then reads one byte past the end.
- **Nesting limit:** groups may nest at most 64 deep.

### 1.3 Value types – observed

The checker only knows type 0. The meanings below come from the data (counts
from 40 sampled files):

| Type | Meaning | Value bytes | Seen |
|---|---|---|---|
| 0 | group | nested item list | 202 |
| 1 | integers | `n × int32` | 277 |
| 2 | floats | `n × float32` | 79 |
| 3 | string | text + NUL, **sometimes padded with extra NULs** | 3145 |
| 4 | binary | raw bytes (e.g. an embedded JPEG `thumbnail`) | 3 |
| 5 | null | none (e.g. an empty `contact-website`) | 69 |
| 13 | KUID | 8 bytes, see 1.4 | 166 |

Other type numbers (6–12 and 14+) were never seen. Our reader keeps them as raw
bytes with a warning instead of failing.

### 1.4 KUID values (type 13) – observed, checked

```
u32 packed     bits 0–24: USER, signed 25-bit
               bits 25–31: VERSION (0 = plain kuid)
i32 CONTENT
```

- **A negative USER** (bit 24 set) has all-ones top bits, which means no
  version. 548 values decode this way, to users −1, −2, −3, −10, −12 and −19.
- **Checked:** the top-level `kuid` of **all 1,468** `config.chump` files in
  `Builtin\8.ja` matches the folder it sits in. That includes
  `kuid2 113998 9002 127`, where version 127 sets the top bit.
- **Not traced:** the decoding code in the game.

**KUIDs stored as strings.** A KUID can also appear as a string (type 3), in
looser forms:
- `<KUID2:33404:22112:02>`: capitals and leading zeros;
- `37028:27013`: no brackets;
- `<KUID2:2512:3:0>`: version 0, which is the same asset as the plain
  `<kuid:2512:3>` (its folder is `kuid 2512 3`).

`parseKuidLoose` accepts all three.

---

## 2. `config.txt` – observed

The parser in `trainz.exe` has no symbols and **has not been traced**. The
grammar below parses every `config.txt` in the three archives, 171 files, and
gives the folder's KUID for every asset whose config sits directly in its
folder.

```
file      := [UTF-8 BOM] statement*
statement := key ( '"' text '"'        string; may run over several lines
                 | '{' statement* '}'  group; the '{' may also be on the next line
                 | rest-of-line        bare value
                 | nothing )           key with no value
key       := run of characters up to space, tab, line end, '{', '}' or '"'
```

**Bare values** run to the end of the line, with trailing spaces removed:
- `<kuid:…>` / `<kuid2:…>` → KUID;
- `12` or `1,2,3` → integers;
- `3.1` or `0, 4.5, -1` → floats;
- anything else (`scenery`, `Bridge`, paths) → string.

**Comments:** `//` up to the line end is skipped.
- In the samples, `//` appears only in the engine's own `key = value;` files
  (such as `water.config.txt`), which are a different syntax and not asset
  configs.
- Whether the game accepts `//` in asset configs is **unknown**.

**Samples:**
- They use CRLF line ends.
- 176 of 184 text files start with a UTF-8 BOM.
- No backslash escapes appear inside strings.

What the parser accepts beyond these samples is **unknown**: other escapes,
`;` comments, a `//` inside a bare value.

**Repeated keys:** `ConfigNode::child()` returns the first match, compared
ASCII case-insensitively. The game's rule for repeated keys is **unknown**.

---

## 3. What is still open

- The meaning of the chump header's version and reserved fields beyond "1"
  and "0".
- Chump types 6–12 and 14+, if they exist.
- How the game resolves repeated keys, and whether key case matters.
- The exact text grammar the game accepts: needs the parser in `trainz.exe`
  traced, which needs a working decompiler.
- `ConfigData::ReadBinFile` (`KernelStandard_7c.dll` `0x10012a50`) reads a
  different binary format, with type bytes 0–7. That's the engine's own config
  cache, not `.chump`. It isn't ported.
