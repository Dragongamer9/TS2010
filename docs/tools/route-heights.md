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

Each section becomes a 64×64 tile. The command samples the full stored vertex
grid with nearest-neighbour sampling, including its borders, for either `Secs`
(76×76) or `Sec5` (148×148). Sections use their index x/y positions: increasing
x goes right and increasing y goes up; stored vertex rows go down the image.
This orientation is a diagnostic convention, not confirmed game behavior.
Grid spacing, overlapping borders and the mapping to world coordinates still
need research. Seams may therefore be visible; this is not a terrain interchange
format or a claim of exact reconstruction.

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
