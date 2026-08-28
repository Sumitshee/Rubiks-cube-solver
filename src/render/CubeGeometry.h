#pragma once

#include "render/Mesh.h"

#include <vector>

namespace rubik::render {

/// How a single cubie is shaped. The same numbers describe all 27 of them.
struct CubieShape {
  /// Edge length of the plastic body. Slightly under one grid unit, so
  /// neighbouring cubies leave a visible black seam instead of z-fighting.
  float bodySize = 0.94f;
  /// How far the coloured tile is pulled in from the body's edge.
  float stickerInset = 0.07f;
  /// How far the tile floats above the body. Large enough to beat depth
  /// precision at this scale, small enough not to show a gap from a low angle.
  float stickerLift = 0.004f;
};

/// Builds the vertices of one cubie: six body quads and six sticker quads.
///
/// This is called once. All 27 cubies are drawn from this single buffer, each
/// with its own model matrix and its own six sticker colours passed as
/// uniforms, so turning the cube never touches vertex data. The `face` and
/// `sticker` attributes are what let one mesh serve every cubie: the fragment
/// shader reads them to choose between the black body and the colour of that
/// particular side.
[[nodiscard]] std::vector<Vertex> buildCubieVertices(const CubieShape& shape = {});

/// The same, uploaded.
[[nodiscard]] Mesh buildCubieMesh(const CubieShape& shape = {});

}  // namespace rubik::render
