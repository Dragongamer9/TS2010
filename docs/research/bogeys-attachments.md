# Bogeys and mesh attachments: documentation findings

Research by Perplexity (Command Center teammate), 2026-11-11; saved and checked
against the disc by Claude. **Scope caveat:** the Trainz Wiki is maintained across
releases, so a current page is not automatically evidence of exact TS2010
behaviour. Nothing here is traced in `trainz.exe`.

## 1. Bogey subcontainer names identify attachment points (documented)

Source: Trainz Wiki, ["Bogeys" container](https://online.ts2009.com/mediaWiki/index.php/%22Bogeys%22_container)

> "The bogeys container is a list of bogey subcontainers and is used by KIND Traincar
> assets. Each bogey subcontainer has a unique tag name and these tag names are matched
> with bogie attachments in the traincar mesh. These are a.bog0, a.bog1, etc."

> "Specifies the bogie asset to use at the attachment point."

> "a.bog0 and a.bog1 are used for the two end bogies, and will locate the vehicle on the track."

The same page's GG1 example gives 0 and 1 to the main power bogies and 2 and 3 to the
unpowered ones, and a steam example uses an invisible `a.bog1`, so "0 and 1 are the
outermost visible bogies" does not hold in general.

**Port:** matches subcontainer `N` to `a.bog<N>` by name, not by list order.

## 2. `reversed` reverses the animation, not the model (documented)

Source: the same page, [`reversed`](https://online.ts2009.com/mediaWiki/index.php/%22Bogeys%22_container#reversed)

> "Reverses the animation direction. Used when the bogie attachment point in the
> traincar asset has been rotated 180 degrees around the Z axis. 1 is reversed. 0 is
> not reversed and the default value."

> "Have you by any chance rotated the attachment point by 180 degrees in your 3D editor
> to get the bogie to appear the other way round, but not set 'reversed 1' in the
> config container for that bogie?"

`rotation-permitted` is separate: it lets the bogie turn to follow the track.

**Checked on the disc (Claude):** of the bogie entries whose `a.bog<N>` point was found,
64 of 71 with `reversed 1` have that point already turned 180° about z, against 10 of 533
with `reversed 0`. This matches the wiki. **Port:** the bogie is turned only by its
point's rotation; `reversed` adds no turn (an earlier version wrongly added one).

## 3. A missing `att-parent` means the entry named `default` (documented)

Source: Trainz Wiki, ["mesh-table" container](https://online.ts2009.com/mediaWiki/index.php/%22mesh-table%22_container)

> "The att-parent defaults to 'default' which is the parent mesh."

> "The tag tells Trainz in which mesh the attachment point is located."

> "The mesh (and animation if present) is inserted at a mesh attachment point rather
> than the origin (without this line the mesh is placed relative to the origin of the
> parent model)."

Caveats: the page's overview table lists `att-parent`'s default as an empty string, and
the page is written for trainz-build 4.1, so when this default was introduced is not
established. The older [CCG/Config.txt Files](https://online.ts2009.com/mediaWiki/index.php/CCG/Config.txt_Files)
page confirms what `att-parent` is for, but not the default. No fallback is described
for a missing `default` entry. **Port:** uses `default`; 837 disc entries rely on it.

## 4. Attachment matrix layout: not documented

Sources: Trainz Wiki, [Attachment point](https://wiki.trainzsimulator.com/wiki/Attachment_point)
and [Trainz Mesh Importer](https://wiki.trainzsimulator.com/wiki/Trainz_Mesh_Importer)

> "Each mesh file may have zero or more attachment points, which can be used within the
> game to identify a specific position and orientation in 3D space relative to the mesh."

> "Attachment points may be present in the root of the mesh file, or may be a child of a
> sub-node such as an animated bone."

No retrieved source gives the binary `ATCH` layout or the rotation's index order (the
importer's "complete file specification" is for its XML input, not the binary files).
**Port:** the layout in [`../formats/mesh.md`](../formats/mesh.md) is from the data; the
rotation's index order stays an **assumption**. Identity and 180° rotations can't tell a
matrix from its transpose; a check needs an asymmetric rotation compared against a
trusted Trainz rendering. Points under animated bones are not handled.
