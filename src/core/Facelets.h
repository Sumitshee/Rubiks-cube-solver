#pragma once

#include "core/Cube.h"
#include "core/Move.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace rubik {

inline constexpr int kNumFacelets = 54;

/// A cube as 54 stickers, which is what a renderer and a human both want to
/// see. Facelets are numbered face-major in the order U, R, F, D, L, B, and
/// row-major within each face:
///
///              |  0  1  2 |
///              |  3  4  5 |   U
///              |  6  7  8 |
///   ----------- ---------- ----------- -----------
///   | 36 37 38 |  18 19 20 |  9 10 11 | 45 46 47 |
///   | 39 40 41 |  21 22 23 | 12 13 14 | 48 49 50 |    L  F  R  B
///   | 42 43 44 |  24 25 26 | 15 16 17 | 51 52 53 |
///   ----------- ---------- ----------- -----------
///              | 27 28 29 |
///              | 30 31 32 |   D
///              | 33 34 35 |
///
/// Each entry holds the Face whose colour that sticker shows.
using FaceletArray = std::array<Face, kNumFacelets>;

/// The three facelets of each corner slot, listed in the order the corner is
/// named (URF -> the U sticker, then R, then F). Rotating this triple by the
/// corner's orientation is what places its colours.
extern const std::array<std::array<std::uint8_t, 3>, kNumCorners> kCornerFacelets;

/// The two facelets of each edge slot, in the order the edge is named.
extern const std::array<std::array<std::uint8_t, 2>, kNumEdges> kEdgeFacelets;

/// Renders a cubie-form cube as stickers.
[[nodiscard]] FaceletArray toFacelets(const Cube& cube);

/// Reconstructs a cube from stickers.
/// Throws `InvalidStateError` when the stickers do not describe a real cube:
/// wrong colour counts, a cubie that does not exist, a duplicated cubie, or a
/// state that violates one of the three cube invariants.
[[nodiscard]] Cube fromFacelets(const FaceletArray& facelets);

/// Parses the 54-character form, e.g. the solved cube is
/// "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB".
/// Accepts either face letters (URFDLB) or colour letters (WRGYOB).
/// Throws `ParseError` for a malformed string.
[[nodiscard]] FaceletArray parseFacelets(std::string_view text);

/// The inverse of `parseFacelets`, using face letters.
[[nodiscard]] std::string toFaceletString(const FaceletArray& facelets);

/// An unfolded colour net suitable for printing to a terminal.
[[nodiscard]] std::string toNetString(const FaceletArray& facelets);

/// Convenience overloads.
[[nodiscard]] inline std::string toFaceletString(const Cube& cube) {
  return toFaceletString(toFacelets(cube));
}
[[nodiscard]] inline std::string toNetString(const Cube& cube) {
  return toNetString(toFacelets(cube));
}

/// The face letter shown for a face, e.g. Face::U -> 'U'.
[[nodiscard]] char faceLetter(Face f) noexcept;

}  // namespace rubik
