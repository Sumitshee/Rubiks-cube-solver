#pragma once

#include "core/Cube.h"
#include "core/Facelets.h"
#include "core/Move.h"

#include <array>

namespace rubik::ui {

/// Integer grid coordinate. Deliberately not a glm type: this header sits in
/// the application layer, which must not depend on anything graphical.
struct Vec3i {
  int x = 0;
  int y = 0;
  int z = 0;

  [[nodiscard]] bool operator==(const Vec3i& other) const noexcept {
    return x == other.x && y == other.y && z == other.z;
  }
  [[nodiscard]] bool operator!=(const Vec3i& other) const noexcept {
    return !(*this == other);
  }
};

inline constexpr int kNumCubies = 27;

/// Where the cube sits in space, and how that lines up with the solver's
/// sticker numbering.
///
/// The renderer needs to answer two questions for every frame: which of the 27
/// grid slots show which colours, and which slots a given move turns. Both are
/// pure arithmetic over the existing `Face`/`Move` types, so they live here
/// rather than in the renderer -- which means they can be tested without an
/// OpenGL context, against the very move tables they have to agree with.
///
/// Axes: +x is the R face, +y is U, +z is F. Each coordinate is -1, 0 or 1.

/// Outward normals, indexed by `Face` (U, R, F, D, L, B).
[[nodiscard]] Vec3i faceNormal(Face face) noexcept;

/// The 27 grid positions, in a fixed order.
[[nodiscard]] const std::array<Vec3i, kNumCubies>& cubiePositions() noexcept;

/// The facelet index that the cubie in `position` presents on `face`, or -1
/// when that side of the cubie faces inward and is never seen.
[[nodiscard]] int faceletIndex(Vec3i position, Face face) noexcept;

/// True when a turn of `face` moves the cubie in `position`.
[[nodiscard]] bool inLayer(Vec3i position, Face face) noexcept;

/// Where a cubie moves under one clockwise quarter turn of `face`, seen from
/// outside the cube.
///
/// Verified against `Cube`'s own move tables by the layout tests: applying this
/// to every grid slot must reproduce exactly the permutation the solver uses.
[[nodiscard]] Vec3i rotateClockwise(Vec3i position, Face face) noexcept;

/// The signed angle, in radians about the face's outward normal, that a
/// completed `move` turns its layer through.
///
/// Negative for a clockwise quarter turn, because a clockwise turn seen from
/// outside is a negative rotation about the outward normal under the
/// right-hand rule.
[[nodiscard]] float moveAngleRadians(Move move) noexcept;

/// The colour shown on `face` of the cubie in `position`, for a given cube
/// state. Returns nullopt-like `-1` cast when interior; callers check
/// `faceletIndex` first.
[[nodiscard]] Face stickerColour(const FaceletArray& facelets, Vec3i position,
                                 Face face) noexcept;

}  // namespace rubik::ui
