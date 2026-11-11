# Terrain height diagnostic

```
./build/route-tool heights ARCHIVE.ja ROUTE_FOLDER [--out DIR]
```

For example:

```
./build/route-tool heights testdata/ts2010/Builtin/sample8.ja "hash-fa/kuid 447264 1035"
```

Writes `<route-folder>.heights.png` (path separators replaced by underscores)
through `SafeOutputDir`, by default under `~/.local/share/trainz-native/routes/`
(or `$XDG_DATA_HOME/trainz-native/routes/`). Existing files are refused. The
archive is only read; `--out` remains restricted to the port's data folder and
protected game folders remain excluded.

Each section becomes a 64×64 tile, sampled nearest-neighbour from the section's
`side − 4` cells (72 for `Secs`, 144 for `Sec5`). Sections use their index x/y
positions: increasing x goes right and increasing y goes up. Inside a section,
columns run toward +x and rows toward +y, so the tile's top is the section's last
row. This layout is observed, not traced: a section's column/row `side − 4` equals the
neighbour's column/row 0 on 81% of the disc's section edges (see
[route.md](../formats/route.md)). The other 19% of edges don't match and are
**unexplained**, so seams still show there; this is not a seamless reconstruction. The 3 rows and columns past the shared edge are not drawn. The 10 m spacing
is observed (track vertices sit on this terrain), not traced; this is not a terrain
interchange format.

All finite vertex heights across the route determine one shared scale: the
minimum is black and the maximum white. A flat route is mid-grey (128). The
command prints that range; height units are not independently confirmed.
Missing sections and non-finite sampled heights are transparent. A route with
no finite heights is rejected. Height arithmetic uses double precision so
extreme finite float heights cannot overflow during normalization.

The existing 8192×8192 image limit applies before image allocation. Sections
are decoded twice from the already loaded archive entry: once to find the
range, once to draw, avoiding another full-route decoded vertex cache.

Regression check (requires a built route-tool; creates its own synthetic data):

```
python3 tests/test_route_heights_cli.py ./build/route-tool
```

The check uses a temporary HOME and XDG data directory, never game data.

## Track map: `route-tool track`

```
./build/route-tool track ARCHIVE.ja ROUTE_FOLDER [--out DIR]
```

Draws the height map as above, then the route's track network from `mapfile.trk` on
top, and writes `<route-folder>.track.png` the same protected way:

- red: track stretches (`tkSt`); yellow: wrapped stretches (`tkBr`);
- cyan dots: vertices with 3 or more track pieces (junctions, probably);
- under the track, placed objects from `mapfile.obs` as dark green dots (signals magenta);
- with `--session ARCHIVE.ja SESSION_FOLDER`, the session's trains on top: every vehicle at
  its own stored position as an orange dot (version-5 trains only; see
  [session.md](../formats/session.md));
- each piece is a **straight line** between its two vertices: how track curves is not
  decoded, so tight curves look faceted where vertices are far apart.

The positions use the observed track layout (baseboard + metres; see
[route-objects.md](../formats/route-objects.md)). Pieces with a missing end are left out
and reported as warnings. On the test route it draws 794 pieces and 38 junction-like
vertices; some straight grids are probably roads (Trainz also stores those as splines;
not confirmed).

Regression check (uses the real test route; skips if it is missing):

```
python3 tests/test_route_track_cli.py ./build/route-tool testdata/ts2010
```

## 3D view: `route-tool view`

```
./build/route-tool view ARCHIVE.ja ROUTE_FOLDER [--session ARCHIVE.ja SESSION_FOLDER] [--at X Y] [--radius M] [--out DIR]
```

Renders a 1280×720 perspective picture of the area within `--radius` metres (default 900,
allowed 50…20,000) of a point, seen from the south-west and above, with the small software
renderer in `src/view/SceneRender` (no GPU). It shows only what the port has decoded:

- terrain from the height grid (10 m cells, observed layout), each vertex coloured from the
  section's own 64×64 overview map, shaded by a fixed sun;
- track as straight red lines between vertices, 0.5 m above them (yellow: `tkBr`); only
  pieces with both ends inside the area;
- placed objects as dark green dots (signals magenta) at their stored height;
- with `--session`, every vehicle as an orange dot, drawn at the height of its train's track
  piece. That height is a **preview approximation, not a decoded vehicle elevation**: a
  vehicle's own stored `z` is often 0.3, and what it means is not known.

No meshes, textures, curves or rotation yet. `--at X Y` takes world metres (+x east, +y
north); without it the view centres on the first train, or else the area with the most
track junctions. Writes `<route-folder>.view.png` through `SafeOutputDir`.
