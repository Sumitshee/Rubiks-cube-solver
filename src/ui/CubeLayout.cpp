#include "ui/CubeLayout.h"

namespace rubik::ui {
namespace {

constexpr float kPi = 3.14159265358979323846f;

/// Outward normals in `Face` order: U, R, F, D, L, B.
constexpr std::array<Vec3i, kNumFaces> kNormals = {{
    {0, 1, 0},   // U
    {1, 0, 0},   // R
    {0, 0, 1},   // F
    {0, -1, 0},  // D
    {-1, 0, 0},  // L
    {0, 0, -1},  // B
}};

/// The component of a position along a face's axis: +1 on that face, -1 on the
/// opposite one, 0 in the middle slice.
[[nodiscard]] int along(Vec3i position, Face face) noexcept {
  const Vec3i n = kNormals[static_cast<std::size_t>(face)];
  return position.x * n.x + position.y * n.y + position.z * n.z;
}

}  // namespace

Vec3i faceNormal(Face face) noexcept {
  return kNormals[static_cast<std::size_t>(face)];
}

const std::array<Vec3i, kNumCubies>& cubiePositions() noexcept {
  static const std::array<Vec3i, kNumCubies> positions = [] {
    std::array<Vec3i, kNumCubies> out{};
    std::size_t at = 0;
    for (int x = -1; x <= 1; ++x) {
      for (int y = -1; y <= 1; ++y) {
        for (int z = -1; z <= 1; ++z) {
          out[at++] = Vec3i{x, y, z};
        }
      }
    }
    return out;
  }();
  return positions;
}

int faceletIndex(Vec3i position, Face face) noexcept {
  // Only the outer slice of each axis shows a sticker on that face.
  if (along(position, face) != 1) return -1;

  // Each face is numbered row-major as seen from outside, with U up. Turning
  // that into (row, column) is a small piece of arithmetic per face; the values
  // below were checked against the corner and edge facelet tables in
  // Facelets.cpp -- URF is U8/R9/F20, DFR is D29/F26/R15, and so on.
  int row = 0;
  int column = 0;
  switch (face) {
    case Face::U:  // seen from +y, back row first
      row = position.z + 1;
      column = position.x + 1;
      break;
    case Face::R:  // seen from +x, front on the left
      row = 1 - position.y;
      column = 1 - position.z;
      break;
    case Face::F:  // seen from +z
      row = 1 - position.y;
      column = position.x + 1;
      break;
    case Face::D:  // seen from -y, front row first
      row = 1 - position.z;
      column = position.x + 1;
      break;
    case Face::L:  // seen from -x, back on the left
      row = 1 - position.y;
      column = position.z + 1;
      break;
    default:  // Face::B, seen from -z
      row = 1 - position.y;
      column = 1 - position.x;
      break;
  }
  return static_cast<int>(face) * 9 + row * 3 + column;
}

bool inLayer(Vec3i position, Face face) noexcept {
  return along(position, face) == 1;
}

Vec3i rotateClockwise(Vec3i position, Face face) noexcept {
  // A clockwise turn seen from outside is a -90 degree rotation about the
  // outward normal. For the three faces whose normal points along a negative
  // axis, that is +90 degrees about the corresponding positive axis.
  const int x = position.x;
  const int y = position.y;
  const int z = position.z;
  switch (face) {
    case Face::U: return {-z, y, x};   // -90 about +y
    case Face::R: return {x, z, -y};   // -90 about +x
    case Face::F: return {y, -x, z};   // -90 about +z
    case Face::D: return {z, y, -x};   // +90 about +y
    case Face::L: return {x, -z, y};   // +90 about +x
    default:      return {-y, x, z};   // Face::B, +90 about +z
  }
}

float moveAngleRadians(Move move) noexcept {
  switch (turns(move)) {
    case 0: return -kPi / 2.0f;  // quarter turn clockwise
    case 1: return -kPi;         // half turn
    default: return kPi / 2.0f;  // quarter turn counter-clockwise
  }
}

Face stickerColour(const FaceletArray& facelets, Vec3i position,
                   Face face) noexcept {
  const int index = faceletIndex(position, face);
  if (index < 0) return face;  // interior; callers filter these out first
  return facelets[static_cast<std::size_t>(index)];
}

}  // namespace rubik::ui
