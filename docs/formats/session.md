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
| 1 | 2 | `trCt` (1,481) | **trains** (tag `tCrt`): version 5 decoded, see below |
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

### Trains (`trCt`)

```
u32 0xcaffa1da   u32 5   u32 vehicleCount   u32 0   i32 trackPiece   f32 distance   u32 direction
vehicles: { u32 content, u32 packed user/version (the vehicle's KUID), u32 0, u8 0, u32 size, record[size] }
tail: FEEDCEEB … (with an embedded .chump; not decoded)
```

A vehicle record starts `u32 8 or 9, u32 3, u32 …ff` (perhaps a colour), then the
placed-object position block (flag, board, x, y, z, yaw; see
[route-objects.md](route-objects.md)); at offset 41 its name: `u8 0x80, u32 length, text`
("CN SD40-2 1", "GATX Oil Tanker 4"). The rest is not decoded.

Checked:
- all **655 version-5 trains** on the disc chain to their tail; all **3,363 vehicles** have a
  position block and a name;
- on the test session "Midday Manifest" (25 trains, 231 vehicles) against its route
  `<kuid:107727:1019>`: every `trackPiece` is one of the route's track stretches, the first
  vehicle stands on that piece's line (0.0 m away), and `distance` is the metres from one
  end of the piece (all 25 within 1 m). This is **checked on that one 25-train test
  session only**; it does not establish which end `distance` counts from, what
  `direction` means, or how placement works on curved track (pieces are compared as
  straight lines).

**Older trains** (826, in version-1 sessions) have no magic and no record sizes:

```
u32 vehicleCount   u32 0   i32 trackPiece   f32 distance   u32 direction
vehicles: { u32 content, u32 packed user, u32 word (0 or 1), u8 0, record }
record: u32 4 (or 1 or 5)   u32 3   u32 colour?   i16 boardX   i16 boardY   f32 x   f32 y   f32 z   f32 yaw   …
        u32 nameLength (record offset 40)   name   … (more, not decoded)
```

Nothing gives a record's length, so the reader **finds vehicles by a pattern** (`u32 0`
or `1`, a zero byte, `u32 4`, `1` or `5`, `u32 3` after each KUID). **The word after the
KUID** is 0 or 1 in both layouts (old: 3,979 vehicles 0, 521 vehicles 1; version 5: 2,897 and
466); its meaning is not known (a vehicle's facing in the train is a guess; 1 occurs at the
front, in the middle and at the end of trains). Until 2026-11-11 the pattern required 0
there, which refused 157 trains. It accepts a train only when the
number found equals `vehicleCount`, the first one starts at byte 20, every match has a
valid name and a position inside its baseboard (x and y in 0…720), and neighbouring
vehicles stand under 50 m apart. Both position rules are **disc-tested heuristics, not
established format constraints**. The baseboard rule rests on the size-checked data: all
3,363 version-5 vehicles (read by their stored sizes, not by the pattern) lie inside
0…720, as do 99.99% of placed objects and 99.5% of track vertices. That last rule is a
**disc-tested heuristic, not an established format constraint**: no accepted train in the
surveyed disc data exceeds it (all 655 size-checked version-5 trains and all accepted old
trains have their largest neighbour gap under 50 m). How many false matches it catches
is not measured. **823 of the 826** qualify (4,500 vehicles); the other 3 are refused (2 with more matches
than `vehicleCount`, 1 failing a vehicle check). **Check of the 157 added trains:** on the
125 disc sessions whose route the index finds, 98.2% of the vehicles of trains holding a
word-1 vehicle lie within 1 m of the route's modelled track (99.4% within 3 m, worst 3.6 m),
as do 97.6% of the trains accepted before (99.6%, worst 6.7 m). (An earlier note here said version-5 records had a different
position layout; that was a misreading: their board numbers looked implausible only
because big routes use boards beyond ±50. They use the version-4 layout.) This is a heuristic, not a
traced layout. False matches are the main risk (the pattern could appear inside a
vehicle's undecoded data), so `tests/test_session.cpp` checks that such a train is
refused, both when the extra match breaks the count and when the count is altered to
agree (the fake vehicle has no valid name), and that over-long or unterminated names and
every truncation are refused, as is a fake vehicle with a plausible name but a distant
position. Removing the name check makes those tests fail. **Not ruled out:** a stray match
with both a plausible name and a plausible position would still be accepted. Checked on one version-1 session ("hash-0b/kuid2 239249 100534 2" on its
route `<kuid2:143919:27565:1>`): all 12 trains name a track piece of the route and the
first vehicle stands within 0.1 m of that piece's line. Record version 1 was added after
the same check on two more sessions whose accepted trains contain version-1 vehicles
(`<kuid:164042:100181>` and `<kuid:63015:100022>`): 5 of 5 trains each, within 0.1 m.
Record version 5: one session (`<kuid:-12:111224>` on `<kuid:-12:1002>`): 15 of 15 trains,
0.0 m.

## Not known

- The 3 old trains the checks refuse; what the word after the KUID means; the real length of an old vehicle
  record; most of each vehicle record, the train tail; the non-container blocks; the
  version-3 trailer.
- The 3 behaviours that fit neither layout.
- `profile.lyr` (25 bytes; contains `session-layer`).
- How the game applies a session's `.obs`/`.trk` over the route's.
