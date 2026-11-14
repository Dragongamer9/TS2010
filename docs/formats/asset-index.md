# Asset index

`src/content/AssetIndex.{h,cpp}` and the `asset-tool` command build a list of
every asset in an install and where it lives. Everything is read-only.

## What counts as an asset

**An asset is a folder holding `config.txt` or `config.chump`.**
- A config inside another asset's folder is a **sub-part** of that asset. An
  example is `hash-c6/kuid 61119 1171/train_brake_lever/config.txt`, whose KUID
  differs from its parent's. Sub-parts are not listed as assets of their own.
- **The KUID comes from the config**, not from the folder name.
- **The folder name is only a cross-check.** If `hash-XX/kuid U C` disagrees
  with the config, the config wins and a warning is recorded.
- **If a folder has both files, `config.chump` is used.** This is an
  **assumption**: no such folder exists in `Base.ja`, `Data.ja` or `8.ja`, so
  it couldn't be checked.

**Checked on the disc** (Python prototype of the same rule):

| Archive | Folders with a config | Assets | Sub-parts |
|---|---:|---:|---:|
| `Builtin\8.ja` | 1469 | 1468 | 1 |
| `Base.ja` | 169 | 169 | 0 |
| `Data.ja` | 1 | 1 | 0 |

## Where it looks

`AssetIndex::scanInstall(INSTALL)`, with names matched ignoring case:
1. `Base.ja`, then `Data.ja`;
2. `Builtin/*.ja`, in name order;
3. `UserData/local`, walked as loose folders (at most 6 levels deep).

**Not covered yet:**
- **`.cdp` packages:** there's no sample on the disc to check the format against.
- **`UserData/editing` and `UserData/original`:** how the game uses them hasn't
  been traced.

**Assumption about `UserData/local`:** the disc ships it empty, so its real
layout wasn't seen. The index doesn't depend on the folder layout: any folder
with a config counts.

## What is not decided (unknown rules)

- **The same KUID in several places** (built-in and local, say).
  `AssetIndex::find` returns **every** copy, in the order the sources were
  added. The game's choice isn't traced.
- **Versions.** `allVersions(user, content)` lists the plain KUID and every
  kuid2 version, lowest first. Which one the game uses when another asset
  asks for an older KUID is **not traced**.
- **`obsolete-table`.** It's read from each config, and `obsoletedBy(kuid)`
  lists the assets that declare a KUID obsolete. What the game does with that
  is **not traced**.

## Traced pieces it relies on

- KUID packing and the plain/kuid2 rule: [`config.md`](config.md) §1.4.
- Archive lookup, first-row-wins for duplicate names: [`ja.md`](ja.md) §3–4.

## Hard drive behaviour

Per archive, the index reads only the config entries, sorted by file offset,
so the drive moves forward only. For `8.ja` that's 1,468 small reads spread
over 556 MB. The index lives in memory and is not yet cached to disk. A
cache would go in `~/.local/share/trainz-native/`.

## Bare-number KUIDs (observed, not traced)

**1,018** disc configs give their own KUID as a bare number (`kuid 101135`; one as a number
in quotes, `kuid "101060"`) instead of `<kuid:…>`. Until 2026-11-11 the index dropped every
one as "no readable top-level kuid". (The first count of this said 500: the index keeps
only its first 500 warnings, so the other 518 were not visible.)

- **1,006** of them sit in a KUID-named folder (1,005 bare, 1 quoted), and in **every** one
  that folder is `kuid _1 <that number>`. So the reader takes them as `<kuid:-1:number>`
  (user −1, written `_1` in folder names).
- The other **12** are in `Base.ja`/`Data.ja` folders not named after a KUID
  (`environment/calm_water`, `…/mediumclouds`, …). They are read by the same rule, but
  nothing on the disc confirms it for them.

With the rule no config is dropped for its KUID: the index goes from 18,264 to **19,282**
assets (+1,018, none removed), and every one whose folder is named after a KUID matches it.
