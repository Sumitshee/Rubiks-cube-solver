#include "render/CubeGeometry.h"

#include <array>

namespace rubik::render {
namespace {

/// Outward normal and two tangents for each face, in the Face order
/// U, R, F, D, L, B. The tangents are chosen so that `u x v == normal`, which
/// makes the quad below wind counter-clockwise seen from outside the cube --
/// the winding back-face culling expects.
struct FaceBasis {
  glm::vec3 normal;
  glm::vec3 u;
  glm::vec3 v;
};

constexpr std::array<FaceBasis, 6> kFaceBases{{
    {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},   // U
    {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},   // R
    {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},    // F
    {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},   // D
    {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},   // L
    {{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},  // B
}};

/// Appends the two triangles of one axis-aligned square.
void appendQuad(std::vector<Vertex>& out, const FaceBasis& basis, float offset,
                float extent, int faceIndex, bool sticker) {
  const glm::vec3 centre = basis.normal * offset;
  const glm::vec3 u = basis.u * extent;
  const glm::vec3 v = basis.v * extent;

  const std::array<glm::vec3, 4> corners{{
      centre - u - v,
      centre + u - v,
      centre + u + v,
      centre - u + v,
  }};

  const auto push = [&](const glm::vec3& position) {
    Vertex vertex;
    vertex.position = position;
    vertex.normal = basis.normal;
    vertex.face = static_cast<float>(faceIndex);
    vertex.sticker = sticker ? 1.0f : 0.0f;
    out.push_back(vertex);
  };

  push(corners[0]);
  push(corners[1]);
  push(corners[2]);
  push(corners[0]);
  push(corners[2]);
  push(corners[3]);
}

}  // namespace

std::vector<Vertex> buildCubieVertices(const CubieShape& shape) {
  const float half = shape.bodySize * 0.5f;
  const float stickerExtent = half - shape.stickerInset;

  std::vector<Vertex> vertices;
  vertices.reserve(6 * 2 * 6);  // six faces, body and sticker, six vertices

  for (int faceIndex = 0; faceIndex < 6; ++faceIndex) {
    const FaceBasis& basis = kFaceBases[static_cast<std::size_t>(faceIndex)];
    appendQuad(vertices, basis, half, half, faceIndex, /*sticker=*/false);
    appendQuad(vertices, basis, half + shape.stickerLift, stickerExtent,
               faceIndex, /*sticker=*/true);
  }
  return vertices;
}

Mesh buildCubieMesh(const CubieShape& shape) {
  return Mesh(buildCubieVertices(shape));
}

}  // namespace rubik::render
