# Screenshots

Made on 2026-11-11 by the port's own command-line tools from the TS2010 disc content
(read-only), then shrunk and recompressed. These images come from N3V's copyrighted
content in Dad's own copy, so they belong in this **private** repository only.

None of these is the game running: there is no playable build yet. They show what the
port can already read and draw.

| Image | What it is | Made with |
|---|---|---|
| ![](orecar-preview.png) | 70-ton ore car body, a `.im` mesh, flat-shaded (no textures in this preview) | `mesh-tool preview … --view 3q` |
| ![](benthd-preview.png) | `benthd.pm`, an older `.pm` mesh (a bogie) | `mesh-tool preview … --view 3q` |
| ![](grass-texture.png) | `grass_15.texture` (`MMTF` → RGBA) | `texture-tool png` |
| ![](crate-descriptor.png) | A crate built from a `.texture.txt` descriptor: colour TGA + separate grey alpha mask (see-through slats) | `texture-tool png` |
| ![](sample-view.png) | **3D view** of the test route: terrain coloured from its overview maps (the grey strip is the airport runway), track, signals (magenta) and trees (green). Rendered by the port's own software renderer from decoded data; no models or textures yet | `route-tool view` |
| ![](midday-view.png) | **3D view** of "Midday Manifest": one of its trains (orange) on its track through a town. Vehicles are drawn at their track piece's height, a preview approximation, not a decoded vehicle elevation | `route-tool view … --session … --radius 500` |
| ![](sample-view-models.png) | **3D view with models** (`--install`): the test route's houses, yard turntable and buildings drawn as their real 3D models from the disc (856 objects, trees included, after the bare-number KUID fix). Preview limits: one flat colour per part (the average of its texture); full-detail daytime parts only; parts on attachment points (e.g. doors) are left out | `route-tool view … --install …` |
| ![](midday-view-models.png) | "Midday Manifest" with models: bushes, poles and an industrial structure along the line, the train in orange | `route-tool view … --session … --install …` |
| ![](thueringen-overview.png) | "Thüringen Romantik Strecke": every terrain section's built-in 64×64 overview map, stitched | `route-tool overview` |
| ![](thueringen-track.png) | The same route: terrain heights (black low, white high), placed objects (green) and the track network on top | `route-tool track` |
| ![](sample-route-objects.png) | The test route with its 10,201 placed objects (dark green dots; signals magenta) under the track | `route-tool track` |
| ![](midday-manifest-trains.png) | The "Midday Manifest" session's 25 trains (231 vehicles, orange) on its route `<kuid:107727:1019>`, with track, junctions and objects | `route-tool track … --session …` |
| ![](midday-manifest-yard.png) | Close-up (3×) of the busiest yard: a long train sitting on its track line | `route-tool track … --session …`, cropped |
| ![](old-session-trains.png) | An older (version-1) session, "Frühdienst Teil 2", on its route "Winter in Deutschland": 12 trains, 33 vehicles (orange, small at this scale) | `route-tool track … --session …` |
| ![](old-session-station.png) | Close-up (3×) of its busiest area: vehicles on their track lines around junctions. Old-layout trains are found by a pattern (see session.md) | `route-tool track … --session …`, cropped |
| ![](avery-drexel-track.png) | "Avery – Drexel", the disc's largest route that fits one image (30,169 spline pieces) | `route-tool track` |

In the track maps, **red** is every `tkSt` spline piece, which seems to include roads
and other splines as well as railway (not confirmed); **yellow** is `tkBr`; **cyan**
marks vertices where 3 or more pieces meet (likely junctions). Pieces are drawn as
straight lines between vertices, because how they curve is not decoded yet. Terrain
seams that remain are real mismatches in the data (unexplained; see
[route.md](../formats/route.md)). The largest disc route, "ECML Kings Cross – York", is
too big for one image (over 8,192 pixels across).
