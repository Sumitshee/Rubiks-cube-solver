#pragma once

#include "core/Cube.h"
#include "core/Move.h"
#include "solver/Coordinate.h"
#include "solver/MoveTable.h"

#include <cstdint>
#include <string>

namespace rubik::korf {

/// Keeps the eight corner cubies -- permutation and twist -- and discards every
/// edge.
///
/// Index = cornerPermutation * 3^7 + cornerOrientation, giving Korf's
/// 8! * 3^7 = 88,179,840 states.
///
/// The transition is unusually cheap here. A face turn permutes the corner
/// orientation array among slots and adds fixed twists, so the new orientation
/// depends only on the old orientation and the move -- not on the permutation.
/// The permutation likewise depends only on itself. The abstract move therefore
/// factorises into two independent lookups in the Phase-4 move tables, with no
/// cube reconstruction at all. (The edge abstraction does not factorise this
/// way; see EdgeAbstraction.)
class CornerAbstraction {
 public:
  static constexpr std::uint32_t kSize =
      coord::kCornerPermutationCount * coord::kCornerOrientationCount;  // 88,179,840

  explicit CornerAbstraction(const MoveTables& tables) : tables_(&tables) {}

  [[nodiscard]] std::uint32_t size() const noexcept { return kSize; }

  /// The move tables this abstraction was built against. Exposed so a search
  /// can maintain the corner coordinates incrementally instead of recomputing
  /// the index from the cube at every node.
  [[nodiscard]] const MoveTables& moveTables() const noexcept { return *tables_; }

  /// Combines already-known corner coordinates into a database index.
  [[nodiscard]] static std::uint32_t indexOf(std::uint32_t permutation,
                                             std::uint32_t orientation) noexcept {
    return permutation * coord::kCornerOrientationCount + orientation;
  }
  [[nodiscard]] std::string name() const { return "corner"; }

  [[nodiscard]] std::uint32_t index(const Cube& cube) const noexcept {
    return coord::cornerPermutation(cube) * coord::kCornerOrientationCount +
           coord::cornerOrientation(cube);
  }

  /// The solved cube has permutation 0 and orientation 0.
  [[nodiscard]] std::uint32_t goalIndex() const noexcept { return 0; }

  /// Writes the 18 successors of `index` into `out`.
  void successors(std::uint32_t index, std::uint32_t* out) const noexcept {
    const std::uint32_t perm = index / coord::kCornerOrientationCount;
    const std::uint32_t ori = index % coord::kCornerOrientationCount;

    const std::uint16_t* permRow = tables_->cornerPermutation().row(perm);
    const std::uint16_t* oriRow = tables_->cornerOrientation().row(ori);

    for (int m = 0; m < kNumMoves; ++m) {
      out[m] = static_cast<std::uint32_t>(permRow[m]) *
                   coord::kCornerOrientationCount +
               oriRow[m];
    }
  }

 private:
  const MoveTables* tables_;
};

}  // namespace rubik::korf
