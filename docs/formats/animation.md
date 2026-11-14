# `.kin` – Jet animation (`JIRF` form `ANIM`)

- **Code:** `src/content/AnimationFile.{h,cpp}`.
- **Command-line tools:** `anim-tool info`, and `mesh-tool preview --kin … --frame N`
  to pose a mesh.
- **Confidence words:** as in [`ja.md`](ja.md).

**How this was worked out.** The layout comes **from the data**: the reader in
`Render3DFoundation_7c.dll` (`AnimationModifier`) has not been traced. It was
checked with a Python prototype on **all 135** `.kin` files in `Base.ja`,
`Data.ja` and `Builtin\8.ja`:
- every chunk is used up exactly;
- the frame count matches `INFO`;
- every frame holds 7 floats per bone;
- every bone's child count matches its nested bones.

Since 2026-11-10 the C++ reader also runs in CI on real game data (`Base.ja`, `Data.ja` and a real subset of `8.ja`; see `testdata/README.md`).

**Whole disc:** all 2,629 `.kin` files fit. See [`../DISC-SURVEY.md`](../DISC-SURVEY.md) for the check across the whole disc (all 8 `Builtin` archives).

## 1. File

Every file has its chunks in this order:

```
"JIRF" u32 size "ANIM"
  "INFO"  u32 100, u32 nameLen, char source[nameLen], u32 frameCount, u32 fps, float value
  "EVNT"  u32 100, u32 count, then count × { u32 frame, u32 flags, u32 nameLen, char name[nameLen] }
  "SKEL"  u32 100, then BONE chunks (roots)
  "FRAM"  × frameCount
```

- **`source`** is the artist's file, e.g. `AAR0bogy205.gmax`. All text fields
  are NUL-padded.
- **`fps`:** 30 in 121 files; also 1, 2, 10, 15 and 16.
- **`value`:** 1.0 in every file; meaning unknown.
- **Event flags:** 4 or 0. Example names from a bell sounder: `warble` /
  `/warble`, which look like start and stop of a sound, and `warble_active` /
  `warble_finished`. What the flags mean, and what the game does with events,
  is **unknown**.

## 2. Skeleton

```
"BONE" u32 size
  u32 100, u32 nameLen, char name[nameLen], u32 childCount
  childCount × nested "BONE" chunks
```

- **Bones are stored as a tree.** Flattened depth-first, their order is the
  order the frames use.
- **Seen:** 2–33 bones per file.
- **Names** look like `b.r.ground`, `b.r.wheel01` and `b.r.door1`.
- **Mesh parts point at bones:** a part names the bone it follows in its
  `GEOM` header ([`mesh.md`](mesh.md) §2). The wheel parts of the bogie mesh
  sit around (0,0,0), so mesh vertices are local to their bone.

## 3. Frames

```
"FRAM" u32 100, u32 frameIndex (0, 1, 2 … in order)
  per bone: float position[3], float rotation[4]   (quaternion x, y, z, w)
```

**Whether `position` is absolute or relative to the parent bone is not
confirmed.**
- I rendered the bogie (`kuid 58422 50128`) both ways.
- Reading positions as **absolute** (model space) puts the side frame's
  journal boxes level with the axle centres, like a real Bettendorf truck.
  Reading them as relative lifts the frame about 10 cm.
- `poseMesh` uses the absolute reading and says so.
- Tracing `AnimationModifier` would settle it.

## 4. Not done yet

- Playback is implemented in `model-viewer` (frame by frame, timed by `fps`,
  no interpolation between frames) but has not been run on a screen yet.
- What events do (sounds? script messages?).
- The mesh-side `SKEL` and `INFL` chunks (bone weights for skinned meshes, if
  that's what `INFL` is).
