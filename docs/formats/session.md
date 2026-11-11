# Sessions: `profile.dat`, `profile.obs`, `profile.trk`, `profile.lyr`

Reader: `src/content/SessionFile.{h,cpp}` (`parseSessionProfile`, `decodeBehaviour`).
Tests: `tests/test_session.cpp`, `RealInstall.SessionProfilesRead` (on the "Midday
Manifest" session added to `testdata/ts2010/Builtin/sample8.ja`).

A session is a route plus its own changes and rules. On the disc there are 138 session
assets (`kind "profile"`); 34 of them also carry `profile.obs`, `profile.trk` and
`profile.lyr`.

Everything here is **observed in the data, not traced** in the game's code.

## `profile.obs`, `profile.trk`

The same layout as `mapfile.obs` / `mapfile.trk` ([route-objects.md](route-objects.md)):
all 68 on the disc read to their exact end. They are small (the session's additions to
the route): mostly `tsO2` trackside objects, a few track vertices and stretches.

## `profile.dat`

```
u32 version                 0 (1 file), 1 (103), 2 (30), 3 (4)
{ u32 size, u8 data[size] } × 3 (version 0), 4 (version 1), 5 (versions 2 and 3)
version 3: 44 more bytes    not decoded (floats 0.5 and 2000 among them)
```

All 138 files read to their exact end this way. Blocks that read as one **object
container** ([route-objects.md](route-objects.md)) hold:

| Block (version 1) | Block (versions 2, 3) | Items | What they are |
|---|---|---|---|
| 0 | 0 | `BHVR` (4,254 on the disc) | **behaviours**: the session's rules, each with its settings |
| 1 | 2 | `trCt` (1,481) | probably trains (tag `tCrt`); 3 KB payloads, not decoded |
| 2 | 1, 3 | (not a container) | not decoded |
| 3 | 4 | `gslb` (252) | library state, e.g. `helpmode`, `hidesigns`, `template` |

Almost every item ends with an embedded `.chump` ([config.md](config.md)).

### Behaviours (`BHVR`)

```
version 3:  u32 3   u32 size   u32 1   u8 0   u32 nameLength   name   u32 0   chump
version 2:  u32 2   u32 size   u32 nameLength   name   u32 0   chump
```

`size` is the payload length minus 8 and the chump ends exactly at the payload's end.
**4,251 of 4,254** disc behaviours fit (3,994 version 3, 257 version 2); the other 3 are
unexplained. Names are UTF-8 ("Prüfe Trackside"). The most common rules: "Set HTML Pages",
"Trigger Check", "Set Junctions", "Ordered List", "Wait", "Driver Setup". The "Startup
Options" rule's settings include `startupoptions.hour`, `.mins`, `.rate`, `.weathertype`.

## Not known

- `trCt` (trains?) and the non-container blocks; the version-3 trailer.
- The 3 behaviours that fit neither layout.
- `profile.lyr` (25 bytes; contains `session-layer`).
- How the game applies a session's `.obs`/`.trk` over the route's.
