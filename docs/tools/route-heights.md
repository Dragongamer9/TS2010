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
- each piece follows the port's **curve model** (straight pieces flagged in the data,
  others a smooth curve through the vertices; checked against 6,071 session vehicles: 97%
  within 0.5 m; see [route-objects.md](../formats/route-objects.md)). A model, not traced.

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
    named by `att-parent`, or of `default` when none is given (837 disc entries; documented
    in the Trainz wiki, see [`../research/bogeys-attachments.md`](../research/bogeys-attachments.md);
    not traced). Chains of attached parts work; a missing parent or point is counted.

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
    a model this way). Its **own textures are used**: a texture of the base model whose file
    name the reskin also ships (e.g. `main.texture`, the Paint Shed's finished paint) is read
    from the reskin's folder instead (observed, not traced; 28 textures in the 25 reskins
    drawn, all `main.texture` at the reskin folder's root; names match without case, and a
    file in a subfolder would match too, which no disc reskin has). `skin.dat`'s decals and `colormap.dat` are not read (the finished texture already
    holds them).

  SpeedTree trees (`.srt`, "SRT 05.0.1", a separate format whose geometry is not read) are
  drawn as a **billboard**: two crossed upright cut-out quads showing the first tree image in
  the asset's own `billboardatlas_diffuse.texture`, sized by the bounding box in the `.srt`
  header (observed: 6 floats at byte 36, all 379 disc files) times the mesh-table entry's
  `mesh-scale` (every disc tree entry has one, usually two numbers, read as a range whose mean
  is used: inferred). That gives disc tree heights of 1.5–122 m, median 13.7 m. A flat
  picture of the tree, not its 3D shape; only the first tree entry is drawn (seasons are
  separate entries). When no billboard can be made (e.g. `<kuid:523:1150>` ships only its
  config), the tree is still a **placeholder** green cone, 4 m wide and 12 m tall.
  Assets without a readable mesh stay dots; the reasons and the skipped parts are printed;
- with `--session`, every vehicle as its 3D model when `--install` is given and the model
  can be read (else an orange dot). Where the modelled track lies within 3 m (99.5% of disc
  session vehicles), the vehicle takes the track's direction and height there, the way round
  given by its neighbours in the train; otherwise it is drawn at its train's track piece's
  height and turned to follow its neighbours (a vehicle's own stored yaw is 0 on the disc;
  front and back are not known; train models are long along their own +y). **Bogies** are drawn on the
  body's `a.bog<N>` points: a traincar names them as `bogey <kuid>` (827 disc traincars, the
  same model on every point) or as a `bogeys` group (328 traincars) whose entry named N is
  for `a.bog<N>` (documented in the Trainz wiki). `reversed` only reverses the wheel
  animation (documented; on the disc 64 of 71 reversed points are already turned 180°), so
  the bogie is turned by its point's rotation alone. Not traced in the exe. Bare numbers are user −1 (the disc's bogie folders, e.g. `kuid _1 100008`,
  confirm it). A bogie's `asset-filename` often exists only as a `.pm` (286 disc assets),
  which is used when there is no `.im`. Each bogie also turns to the modelled track under its own point (within ±90° of
  the body). A body with both `a.bog0` and `a.bog1` is **placed from them** (as the game's wiki
  says those two points locate the vehicle on the track): both points go onto the modelled
  track, the body lies along the line between them and tilts by their height difference.
  Checked on 6,531 disc session vehicles: the stored positions are a median 0.001 m from this
  placement (90% within 0.08 m); on curves (1,105 vehicles) a median 0.10 m against 0.14 m for
  putting the centre itself on the track. That is closer, but within the curve model's own
  error, so it does not show that the game places vehicles this way. That height is a **preview approximation, not a decoded vehicle elevation**: a
  vehicle's own stored `z` is often 0.3, and what it means is not known.

`--eye DX DY DZ` puts the camera that many metres from the centre's ground point (for
close-ups). Track follows the curve model above (drawn in pieces of about 5 m). With `--install` the
**ground is textured**: each section's vertices name up to 3 texture layers (slot number +
weight byte, decoded and traced, see [route.md](../formats/route.md)); a slot names a
`groundtexture` asset, whose image is its `asset-filename` + `.texture` (or `.texture.txt`,
a descriptor naming a `.bmp`/`.tga`; observed). For each section one image (1024 px for a
radius up to 300 m, else 512) is baked: every pixel blends the textures of the four nearest
vertices' layers by distance and weight byte (bilinear, in the game's reading of the weight
as a blend weight, not traced). **Assumed:** one texture repeat per 10 m cell. A Trainz forum
tutorial for TS2009 and later says a ground texture "will cover a 10m x 10m square in
Trainz" (see [route.md](../formats/route.md) for the sources and the leads on scale and
direction); it is not traced, and painted layers can be scaled and turned in the game,
which the view ignores (the per-layer `extra` byte is not decoded). Version 12 files (which remap texture numbers through a world table) keep the
overview colours. Textures are sampled bilinearly (cut-outs keep the nearest texel). No
animation yet. `--at X Y` takes world metres (+x east, +y
north); without it the view centres on the first train, or else the area with the most
track junctions. Writes `<route-folder>.view.png` through `SafeOutputDir`.
