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
./build/route-tool view ARCHIVE.ja ROUTE_FOLDER [--session ARCHIVE.ja SESSION_FOLDER] [--at X Y] [--radius M]
                        [--install TS2010_FOLDER] [--out DIR]
```

Renders a 1280×720 perspective picture of the area within `--radius` metres (default 900,
allowed 50…20,000) of a point, seen from the south-west and above, with the small software
renderer in `src/view/SceneRender` (no GPU). It shows only what the port has decoded:

- terrain from the height grid (10 m cells, observed layout), each vertex coloured from the
  section's own 64×64 overview map, shaded by a fixed sun;
- track as straight red lines between vertices, 0.5 m above them (yellow: `tkBr`); only
  pieces with both ends inside the area;
- placed objects as dark green dots (signals magenta) at their stored height, or, with
  `--install`, as **their 3D models**: the install is indexed (`AssetIndex`) and each
  object's asset config chooses the meshes (`selectAssetMeshes`):
  - a top-level `mesh`; or every `mesh-table` entry that is the full-detail daytime model at
    the asset's origin; or, with neither, `asset-filename` + `.im`;
  - **skipped and counted**: entries with `lod-level` above 0 (simpler copies for distance),
    `night-mesh-base` (night variants) and shadow meshes (by name). Entries with `att` (doors,
    fans, horns …) are hung on that attachment point (the mesh's `ATCH` chunk) of the entry
    named by `att-parent`, or of `default` when none is given (837 disc entries; the port's
    reading, not traced). Chains of attached parts work; a missing parent or point is counted.

  These rules are the port's reading of the key names, not traced game behaviour. Each mesh
  is turned by the object's yaw (radians, anticlockwise, inferred; which side faces +x at 0
  is not known) and placed at its position. **Textures:** each part shows its colour map
  (the material's type-1 texture), a copy no bigger than 128×128 plus the file's smaller
  mips; each triangle takes the mip closest to its size on screen, sampled nearest-texel and
  perspective-correct. Texture `v` is taken as running down the image (top row = 0), the
  Direct3D convention; this is assumed from the pictures looking right, not traced. No
  blending, lighting maps or the game's material flags: a texture is drawn as a **cut-out**
  (texels with alpha under 128 left out) only when its alpha looks like a mask, a
  **heuristic**: at least 5% clear and 1% solid texels, and under half in between (what
  the rule changes was listed on two views: grass, flowers, trees, lichen, a chain and a
  window alpha, no vehicle bodies). Other textures are drawn solid, since their alpha may
  mean something else (gloss), so a few cut-outs may still show as squares, and some bushes
  are dark because their texture is (their solid texels average RGB 43,41,53; how the game
  lights them is not known). A part with no readable texture keeps a flat grey. `.lm`
  level-of-detail lists are not read (an `.im` of the same name is used if there is one). Two more fallbacks, both observed and not traced:
  - `.lm`: no `.lm` file exists on the disc; the archives ship `<name>.lm.txt` text lists
    (`mesh("1.0") { name="body.im"; }`), and the entry with the largest level is used;
  - reskins: a Paint Shed reskin (`skin.dat`) without a mesh of its own uses the base asset
    named by the second KUID in its `skin.dat` (25 of the 61 disc reskins with two KUIDs get
    a model this way). Its own textures are not applied yet.

  SpeedTree trees (`.srt` files, a separate format) are not read: they are drawn as a
  **placeholder** green cone, 4 m wide and 12 m tall, an invented stand-in, not the tree.
  Assets without a readable mesh stay dots; the reasons and the skipped parts are printed;
- with `--session`, every vehicle as its 3D model when `--install` is given and the model
  can be read (else an orange dot), drawn at the height of its train's track piece and turned
  to follow its neighbours in the train (a vehicle's own stored yaw is 0 on the disc; front and
  back are not known; train models are long along their own +y). **Bogies** are drawn on the
  body's `a.bog<N>` points: a traincar names them as `bogey <kuid>` (827 disc traincars, the
  same model on every point) or as a `bogeys` group (328 traincars) whose entry N names the
  model for `a.bog<N>`, with `reversed` read as a 180° turn about z (both matches inferred,
  not traced). Bare numbers are user −1 (the disc's bogie folders, e.g. `kuid _1 100008`,
  confirm it). A bogie's `asset-filename` often exists only as a `.pm` (286 disc assets),
  which is used when there is no `.im`. Bogies don't turn on curves (track curves are not
  decoded). That height is a **preview approximation, not a decoded vehicle elevation**: a
  vehicle's own stored `z` is often 0.3, and what it means is not known.

`--eye DX DY DZ` puts the camera that many metres from the centre's ground point (for
close-ups). No terrain textures (the ground is coloured from the overview maps), track
curves or animation yet. `--at X Y` takes world metres (+x east, +y
north); without it the view centres on the first train, or else the area with the most
track junctions. Writes `<route-folder>.view.png` through `SafeOutputDir`.
