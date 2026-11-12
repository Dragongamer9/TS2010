# The own game: a train simulator that runs without the original files

The owner's end goal is a game of its own: it must start and be playable with **no
Trainz Simulator 2010 files at all**. This page is the plan for that. The TS2010 readers
(`src/content`) stay, as an optional way to look at Dad's own installed copy, but the game
never depends on them.

## Rules

- **Original content only.** The game ships nothing from TS2010 or any other game: no
  models, textures, sounds, routes, names, logos or look copied, converted or traced from
  them. Its world, trains and sounds are made for it (by hand, procedurally, or from freely
  licensed sources whose licence is recorded next to the file).
- **Its own name.** Not "Trainz"; the name is the owner's choice (still to be picked).
- **Genre, not a copy.** It is a train simulator; its mechanics (driving, signals, timetables)
  are common to the genre, but its design is its own.
- The repo's `testdata/ts2010` (Dad's files, private repo) is for testing the readers only;
  nothing in the game may load it.

## What carries over

Everything below is the port's own code, written for this project, and works without game files:

| Part | Where | State |
|---|---|---|
| Software 3D renderer (textures, cut-outs, mips) | `src/view/SceneRender` | working |
| Track geometry: curves through vertices, straight pieces, junction sides | `src/content/TrackLayout` (`trackCurve`, `TrackLocator`) | working (the data structures are general) |
| Vehicles on track: bogie placement, tilt | `src/view/RouteScene` (`placeOnBogies`) | working |
| Fly camera, keyboard / mouse / gamepad | `src/view/FlyCamera`, `src/input` | working |
| Window, launcher, packaging | `tools/viewer`, `tools/launcher`, CPack | working |
| **Simulation core**: driving along track, train physics | `src/sim` | working, tested |
| **First generated world and driving** | `src/game`, `tools/game/drive_demo.cpp` | **started**: runs with no game files |

## Milestones

1. **Simulation core** (started): a position on the track that drives forward and back across
   vertices and junctions (`sim::TrackNetwork`), and train physics (`sim::stepTrain`: traction
   limited by force and power, brakes, Davis resistance, gradients). Tested without any files.
2. **A first drivable world, generated** (started): `drive-demo` makes gentle hills, a 2.1 km
   oval with a turnout into a buffered siding, and a box locomotive with three wagons on
   bogies, all by code; drive it with throttle, brake, reverser and the turnout switch, behind
   a chase camera. **It runs with no TS2010 files at all** (CTest `DriveDemo.Cli` runs it from
   an empty folder). Still to come here: better models, a cab view, speed limits.

   ![The first generated world](screenshots/own-game-first-world.png)

   `drive-demo` keys: W/S or Up/Down throttle, Space (held) brake, R reverser (when stopped),
   J turnout (main line / siding), C chase or free camera (free: the route-viewer keys), Esc quit.
   ("drive-demo" is a working name; the game's name is the owner's to pick.)
3. **Our own content formats**: a plain, documented file format for routes and trains, and
   a way to save the generated world.
4. **Driver view and controls**: cab or chase camera, speedometer, simple HUD.
5. **Signals and rules**: blocks, signals, speed limits.
6. **Scenarios**: a start position, goals (deliver, reach a station on time), save/load.
7. **Sound**: our own or freely licensed engine and wheel sounds.
8. **A track editor**: lay track on the terrain.

Packaging is shared with the port: [`PACKAGING.md`](PACKAGING.md).
