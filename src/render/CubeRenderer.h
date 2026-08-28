#pragma once

#include "core/Cube.h"
#include "core/Facelets.h"
#include "core/Move.h"
#include "render/Camera.h"
#include "render/CubeGeometry.h"
#include "render/Mesh.h"
#include "render/Shader.h"

#include <glm/glm.hpp>

#include <array>

namespace rubik::render {

/// One layer caught part-way through a turn.
///
/// Deliberately not `CubeController::Animation`: the renderer is told a face
/// and an angle, and knows nothing about move queues, playback or solvers. The
/// application layer converts.
struct LayerRotation {
  bool active = false;
  Face face = Face::U;
  /// Signed radians about the face's outward normal.
  float angleRadians = 0.0f;
};

/// Draws the cube.
///
/// ## What it does not do
///
/// No solving, no move application, no input. It is handed a `Cube` and a
/// `LayerRotation` and draws exactly that; it never mutates either. The cube
/// state it receives is always a whole number of moves in -- the application
/// layer guarantees that -- so the renderer has no notion of a half-applied
/// move either.
///
/// ## Why nothing is rebuilt per frame
///
/// The geometry of a cubie never changes, so there is one immutable vertex
/// buffer, uploaded once, drawn 27 times. What differs between cubies is a
/// model matrix and six sticker colours, both passed as uniforms. A frame
/// therefore performs no allocation and no buffer upload at all: the sticker
/// colours are read into a member array that was sized at construction.
class CubeRenderer {
 public:
  CubeRenderer();

  CubeRenderer(const CubeRenderer&) = delete;
  CubeRenderer& operator=(const CubeRenderer&) = delete;

  /// Draws all 27 cubies. `rotation`, when active, spins the nine cubies of
  /// that face and leaves the rest still.
  void draw(const Cube& cube, const LayerRotation& rotation,
            const Camera& camera, float aspectRatio);

  /// Clears the framebuffer to the background colour and sets the render state
  /// this renderer expects (depth test on, back faces culled).
  void beginFrame(int framebufferWidth, int framebufferHeight);

 private:
  /// Fills `colours_` with the six sticker colours of one cubie, black for the
  /// sides that face inward.
  void gatherColours(const FaceletArray& facelets, int cubieIndex);

  Shader shader_;
  Mesh mesh_;
  std::array<glm::vec3, 6> colours_{};
  /// Recomputed once per frame, not once per cubie.
  FaceletArray facelets_{};
};

}  // namespace rubik::render
