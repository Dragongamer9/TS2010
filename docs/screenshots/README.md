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
| ![](thueringen-overview.png) | "Thüringen Romantik Strecke": every terrain section's built-in 64×64 overview map, stitched | `route-tool overview` |
| ![](thueringen-track.png) | The same route: terrain heights (black low, white high) with the track network on top | `route-tool track` |
| ![](avery-drexel-track.png) | "Avery – Drexel", the disc's largest route that fits one image (30,169 spline pieces) | `route-tool track` |

In the track maps, **red** is every `tkSt` spline piece, which seems to include roads
and other splines as well as railway (not confirmed); **yellow** is `tkBr`; **cyan**
marks vertices where 3 or more pieces meet (likely junctions). Pieces are drawn as
straight lines between vertices, because how they curve is not decoded yet. Terrain
seams that remain are real mismatches in the data (unexplained; see
[route.md](../formats/route.md)). The largest disc route, "ECML Kings Cross – York", is
too big for one image (over 8,192 pixels across).
