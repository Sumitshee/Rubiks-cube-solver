#pragma once

#include "solver/MoveTable.h"
#include "solver/PruningTable.h"

#include <algorithm>
#include <cstdint>

namespace rubik {

/// Everything the two-phase solver needs precomputed: the coordinate move
/// tables plus four pruning tables.
///
/// Built once, then read-only, so one instance can be shared by any number of
/// solver threads without synchronisation.
///
/// Each phase uses the maximum of two pruning tables. Taking the maximum of
/// admissible heuristics is itself admissible -- if neither ever overestimates
/// the true distance, neither does the larger of the two -- so the search stays
/// correct while pruning strictly harder than either table alone.
///
/// Note the tables are *not* additive. Both phase-1 tables count the same
/// moves, so their values cannot be summed without breaking admissibility.
/// Disjoint pattern databases can be added; these overlap, so they cannot.
class TwoPhaseTables {
 public:
  TwoPhaseTables();

  [[nodiscard]] const MoveTables& moves() const noexcept { return moves_; }

  /// Lower bound on the number of moves to reach G1.
  [[nodiscard]] int phase1Heuristic(std::uint32_t twist, std::uint32_t flip,
                                    std::uint32_t slice) const noexcept {
    const std::uint8_t a = flipSlice_.get(flip * coord::kUdSliceCount + slice);
    const std::uint8_t b = twistSlice_.get(twist * coord::kUdSliceCount + slice);
    return std::max(a, b);
  }

  /// Lower bound on the number of moves to solve a cube already in G1.
  [[nodiscard]] int phase2Heuristic(std::uint32_t cornerPerm,
                                    std::uint32_t udEdgePerm,
                                    std::uint32_t slicePerm) const noexcept {
    const std::uint8_t a = cornerSlice_.get(
        cornerPerm * coord::kSlicePermutationCount + slicePerm);
    const std::uint8_t b = edgeSlice_.get(
        udEdgePerm * coord::kSlicePermutationCount + slicePerm);
    return std::max(a, b);
  }

  [[nodiscard]] const PruningTable& flipSlice() const noexcept { return flipSlice_; }
  [[nodiscard]] const PruningTable& twistSlice() const noexcept { return twistSlice_; }
  [[nodiscard]] const PruningTable& cornerSlice() const noexcept { return cornerSlice_; }
  [[nodiscard]] const PruningTable& edgeSlice() const noexcept { return edgeSlice_; }

  /// Total bytes across move tables and pruning tables.
  [[nodiscard]] std::size_t byteSize() const noexcept;

 private:
  MoveTables moves_;
  PruningTable flipSlice_;    // edge orientation x slice, 2048 * 495
  PruningTable twistSlice_;   // corner orientation x slice, 2187 * 495
  PruningTable cornerSlice_;  // corner permutation x slice permutation, 8! * 24
  PruningTable edgeSlice_;    // UD edge permutation x slice permutation, 8! * 24
};

}  // namespace rubik
