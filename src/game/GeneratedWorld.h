// The own game's first world, made entirely by code (no game files): gentle hills, an oval
// track with a siding, and simple trains built from boxes. Everything here is original.
#pragma once

#include "content/TrackLayout.h"
#include "view/AssetMeshes.h"
#include "view/SceneRender.h"

#include <cstdint>
#include <vector>

namespace trainz::game {

struct GeneratedWorld {
    trainz::content::TrackLayout track;
    // Terrain heights on a grid: x from x0 in `cells` steps of `cellSize`, likewise y.
    double x0 = 0, y0 = 0, cellSize = 10;
    int cellsX = 0, cellsY = 0;
    std::vector<float> heights;  // (cellsX + 1) * (cellsY + 1), row by row (y), then x
    std::int32_t junctionVertex = -1;  // where the siding leaves the main line
    std::int32_t sidingPiece = -1;     // the first piece of the siding
    std::int32_t startPiece = -1;      // where the train starts (main line, south straight)

    double heightAt(double x, double y) const;  // terrain, bilinear; 0 outside the grid
};

// The oval: 600 m straights joined by half circles of 150 m radius, a turnout on the south
// straight into a 150 m siding that ends in a buffer. `seed` varies the hills only.
GeneratedWorld generateWorld(std::uint32_t seed = 1);

// The terrain as scene triangles (grass by height, grey ballast near the track).
void addTerrain(trainz::view::Scene& scene, const GeneratedWorld& world);
// The track as two rails (lines along the modelled curves).
void addTrack(trainz::view::Scene& scene, const GeneratedWorld& world);

// Simple original rolling stock: boxes, long along +y (as the view expects), with a.bog0 and
// a.bog1 points 1/3 of the length from the middle and a box bogie for them.
trainz::view::PreparedMesh boxLocomotive();
trainz::view::PreparedMesh boxWagon(trainz::view::Rgb colour);
trainz::view::PreparedMesh boxBogie();
// A cuboid from (x0, y0, z0) to (x1, y1, z1) in model space, one colour.
void addBox(trainz::view::PreparedMesh& m, const float lo[3], const float hi[3], trainz::view::Rgb colour);

}  // namespace trainz::game
