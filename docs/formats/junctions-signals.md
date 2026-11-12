# Junctions and Signals: `mapfile.obs`, `mapfile.trk`

This document defines the skeleton for decoding junctions (`mojunction`) and signals (`mosignal`) in Trainz Simulator 2010 route and session files.

It consolidates what is already established across [`INSTALL-MAP.md`](../INSTALL-MAP.md), [`route-objects.md`](route-objects.md), [`route.md`](route.md), [`session.md`](session.md), and [`ROADMAP.md`](../../ROADMAP.md), and provides the reference framework for decoding the full object payloads and track-linking logic.

---

## 1. What is Already Established

### 1.1 Object Classes and Tags
Route objects are stored in the game's standard object container format inside `mapfile.obs` (and session `profile.obs`), parsed by `src/content/ObjectContainer.{h,cpp}`:

| Class Tag (file) | Tag (readable) | Factory Class Name | Disc Count | Role |
|---|---|---|---:|---|
| `bOnj` | `jnOb` | `mojunction` | 10,138 | Junction / turnout switch mechanism |
| `bOgs` | `sgOb` | `mosignal` | 13,806 | Trackside railway signals |
| `2Ost` | `tsO2` | `trackside` (unconfirmed) | 23,619 | Placed trackside objects |
| *(unseen in `.obs`)* | `tsOb` | `trackside` | 0 | Registered class in `trainz.exe` (factory `0x807eb0`) |
| *(unseen in `.obs`)* | `spOb` | `mospeedboard` | 0 | Registered class in `trainz.exe` (speed limit signs) |
| *(unseen in `.obs`)* | `mkOb` | `motrackmark` | 0 | Registered class in `trainz.exe` (track marks / nav points) |
| *(unseen in `.obs`)* | `tgOb` | `motrigger` | 0 | Registered class in `trainz.exe` (track triggers) |

### 1.2 Placement and Base Block (`PlacedObject`)
Both `jnOb` and `sgOb` share the common 8-byte class header followed by the standard `PlacedObject` base block (`src/content/PlacedObject.{h,cpp}`):

```
u32 3                   Class header word 1
u32 0xff000000          Class header word 2
u8  flag                0 or 1 (equal distribution; meaning unknown)
i16 boardX              Baseboard X (terrain section index)
i16 boardY              Baseboard Y (terrain section index)
f32 x                   Metres inside the 720 m baseboard (0..720) toward +x
f32 y                   Metres inside the 720 m baseboard (0..720) toward +y
f32 z                   Height in metres
f32 yaw                 Rotation (stored as 0.0 for signals and junctions)
u8  rest[]              Class-specific payload (partly observed)
```

**Observed Geometric Properties:**
- **Terrain Alignment**: 75–82% of `sgOb` signals sit within 1.0 m of the ground; for five classes counted together (`jnOb`, `obCr`, `mIND`, `mFXT`, `ttOb`), 79–97% sit within 1.0 m of the nearest track vertex (the source does not measure `jnOb` alone).
- **Yaw = 0.0**: Unlike buildings and scenery objects whose `yaw` stores rotation in radians, signals and junctions store `0.0` (they presumably take their direction from the track, a guess).
- **Names in Payload**: Inspection of `jnOb` payloads shows ASCII names embedded later in the record (e.g. `"Single Track"`, `"Airport Junction 2"`).

---

## 2. Track Network Geometry (`mapfile.trk`)

### 2.1 Junction Vertices (`tkVx`)
Track geometry in `mapfile.trk` consists of stretches (`tkSt`, `tkBr`) connecting vertices (`tkVx`):
- Each vertex record is 41 bytes (version 3 on all 406,207 disc vertices) with 4 link slots:
  ```
  4 × { i32 piece_index (-1 = empty), u8 flag }
  ```
- **Slot Distribution on Disc**:
  - `1 link`: 211,040 vertices (track line ends / buffer stops).
  - `2 links`: 182,491 vertices (continuous track along a line).
  - `3 links`: 12,242 vertices (probably turnouts; a guess, see the junction correlation below).
  - `4 links`: 421 vertices (crossings / double slips).
  - `0 links`: 13 vertices.
- **Junction Correlation**: 12,242 three-link vertices against 10,138 `jnOb`. Why the numbers differ is not yet known (guesses: routes without lever objects, junctions built from several pieces).
- **Test Route Verification**: On the real test route (`ROADMAP.md` step 8, `tools/route_tool.cpp`), `route-tool track` identifies **38 junction-like vertices** (vertices with $\ge 3$ links, plotted as cyan dots) connecting 794 track pieces.

### 2.2 Vertex Link Flags and Curvature
- The `flag` byte in each link slot identifies the **side of the vertex** from which the piece departs:
  - Pairs of pieces with **different flags** point in opposite directions (92% within 30°, 69% within 10°).
  - Pairs of pieces with the **same flag** point in the same direction: on the disc, 83% of same-flag pairs point within 30° of each other (68% within 10°). These represent the diverging legs of a turnout.

### 2.3 Current Simulation Behavior (`sim::TrackNetwork`)
In `src/sim/TrackNetwork.{h,cpp}`:
- Trains advance across vertices by transitioning from a piece on one side of a vertex to an exiting piece on the opposite side.
- When multiple exits exist (a junction):
  - **Default**: The train takes the exit that turns least (the straightest chord).
  - **Switch Setting**: The map `net.switches[vertex] = piece` overrides the selection. In `drive-demo` (milestone G2), pressing `J` toggles `net.switches` between the mainline and siding pieces.

---

## 3. Session Integration (`profile.dat`)

### 3.1 Junction Rules (`BHVR`)
- Session profiles (`profile.dat`, [`session.md`](session.md)) contain `BHVR` blocks holding rule definitions and embedded `config.chump` data.
- Among the most common rules on the disc is `"Set Junctions"`; by its name it probably sets turnout directions for a session (a guess: its settings are not decoded).
- Sub-rules and trigger checks (`"Trigger Check"`) monitor train movements over junctions.

### 3.2 Session Objects
- Session delta files `profile.obs` (68 files on disc) contain session-specific trackside objects (`tsO2`), speed restrictions, and temporary signals.

---

## 4. Known / Guess / Left Reference Table

| Feature / Detail | Status | Evidence / Source | Description |
|---|---|---|---|
| **`jnOb` / `mojunction` registration** | **Known** | `trainz.exe` factory registration at `0x807eb0` | Factory binds tag `jnOb` to class `mojunction`. 10,138 objects on disc. |
| **`sgOb` / `mosignal` registration** | **Known** | `trainz.exe` factory registration at `0x807eb0` | Factory binds tag `sgOb` to class `mosignal`. 13,806 objects on disc. |
| **Class base block & position** | **Known** | `src/content/PlacedObject.{h,cpp}`, verified on 50 disc routes | 8-byte header (`0x03`, `0xff000000`), board coordinates, x/y/z offsets (75–82% for signals near ground; 79–97% for five classes together near nearest vertex). |
| **Signal/junction yaw convention** | **Observed** | `route-objects.md` ("signals and junctions store 0 here"; no per-object count given) | Stored as `0.0`. Whether rotation is stored elsewhere in the record is not known. |
| **Junction vertex topology** | **Guess** | `src/content/TrackLayout.{h,cpp}`, verified on all 50 routes | Vertices with $\ge 3$ links represent junctions (12,242 3-way, 421 4-way on disc; 38 on test route; "3 links = junction" is a guess). |
| **Turnout diverging legs** | **Observed** (not traced) | Disc survey of 13,387 same-flag vertex link pairs | Same link flag denotes pieces branching from the same side of the vertex (83% within 30°). |
| **Simulation switch branching** | **Known** (the port's own rule, not the game's) | `src/sim/TrackNetwork.{h,cpp}`, unit tests, `drive-demo` | `net.switches[vertex] = piece` routes trains through turnouts; default selects straightest exit. |
| **Session junction initialization** | **Guess** | `docs/formats/session.md` (rule names on the disc) | `BHVR` rules named `"Set Junctions"` exist, with embedded `.chump` settings; that they set initial turnout directions is a guess from the name (settings not decoded). |
| **Track-relative orientation** | **Guess** | Inferred from `yaw = 0.0` and proximity to track vertices | Signals and junction switch stands orient themselves along the vector of the attached track piece. |
| **`jnOb` vertex tie mechanism** | **Left** | To be decoded from `jnOb` payload | How `jnOb` links to its `tkVx` vertex (e.g. by vertex index, coordinates, or distance). |
| **`jnOb` switch direction state** | **Left** | To be decoded from `jnOb` payload | How initial switch alignment (left vs right vs straight) is encoded. |
| **Switch stand mesh & animation** | **Left** | To be mapped from asset configs (`kind "junction"`) | Lever meshes (`a.lever`), switch lanterns, and target animations. |
| **`sgOb` track attachment tie** | **Left** | To be decoded from `sgOb` payload | How `sgOb` identifies the track stretch and distance offset it guards. |
| **Signal aspects and logic** | **Left** | Asset configs (`kind "signal"`) and engine scripts | Aspect states (stop, caution, proceed), light attachments, and block occupancy propagation. |
| **AI junction path reservation** | **Left** | Untraced in `trainz.exe` | How AI drivers request and lock junction switches along a path. |
