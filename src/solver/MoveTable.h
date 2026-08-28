#pragma once

#include "core/Cube.h"
#include "core/Move.h"
#include "solver/Coordinate.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rubik {

/// The ten moves that keep a cube inside G1: any turn of U or D, and half turns
/// only of R, L, F and B. Quarter turns of the other four faces would flip an
/// edge or move an edge out of the slice, leaving the subgroup.
[[nodiscard]] constexpr std::uint32_t phase2MoveMask() noexcept {
  const Move allowed[] = {Move::U,  Move::U2, Move::Up, Move::D, Move::D2,
                          Move::Dp, Move::R2, Move::L2, Move::F2, Move::B2};
  std::uint32_t mask = 0;
  for (const Move m : allowed) mask |= 1u << static_cast<std::uint8_t>(m);
  return mask;
}

inline constexpr std::uint32_t kAllMoveMask = (1u << kNumMoves) - 1u;
inline constexpr std::uint32_t kPhase2MoveMask = phase2MoveMask();

/// A precomputed transition table for one coordinate.
///
/// The central fact that makes this possible: a coordinate's new value after a
/// move depends only on its old value and the move, never on the parts of the
/// cube the coordinate ignores. So the entire dynamics of the puzzle, as seen
/// by that coordinate, collapses into a flat array -- and applying a move in
/// the search becomes a single load instead of permuting 40 bytes.
///
/// Storage is row-major by coordinate, so the 18 successors of one state are
/// contiguous (36 bytes, comfortably inside a cache line). That matches how the
/// search reads it: expand one node, try every move.
///
/// Coordinates that are only defined inside G1 (the phase-2 permutations) are
/// built for the ten G1 moves; their other entries hold `kInvalid`.
class MoveTable {
 public:
  static constexpr std::uint16_t kInvalid = 0xFFFF;

  MoveTable() = default;

  /// Builds the table by decoding each coordinate value into a representative
  /// cube, applying each permitted move, and re-encoding.
  ///
  /// `decode` maps a coordinate to some cube carrying that value; `encode`
  /// reads the coordinate back out.
  template <typename DecodeFn, typename EncodeFn>
  MoveTable(std::string name, std::uint32_t size, std::uint32_t moveMask,
            DecodeFn decode, EncodeFn encode)
      : name_(std::move(name)),
        size_(size),
        moveMask_(moveMask),
        data_(static_cast<std::size_t>(size) * kNumMoves, kInvalid) {
    assert(size <= kInvalid && "coordinate range does not fit in uint16_t");

    for (std::uint32_t c = 0; c < size; ++c) {
      const Cube base = decode(c);
      for (int m = 0; m < kNumMoves; ++m) {
        if (((moveMask >> m) & 1u) == 0u) continue;
        Cube next = base;
        next.apply(static_cast<Move>(m));
        data_[static_cast<std::size_t>(c) * kNumMoves + static_cast<std::size_t>(m)] =
            static_cast<std::uint16_t>(encode(next));
      }
    }
  }

  /// The coordinate reached by applying `m` to state `coord`.
  [[nodiscard]] std::uint16_t apply(std::uint32_t coord, Move m) const noexcept {
    assert(coord < size_);
    assert(isValidFor(m) && "move is outside this table's move set");
    return data_[static_cast<std::size_t>(coord) * kNumMoves +
                 static_cast<std::size_t>(m)];
  }

  /// The 18 successors of `coord`, contiguous. Entries for moves outside the
  /// table's move set are `kInvalid`.
  [[nodiscard]] const std::uint16_t* row(std::uint32_t coord) const noexcept {
    assert(coord < size_);
    return data_.data() + static_cast<std::size_t>(coord) * kNumMoves;
  }

  [[nodiscard]] bool isValidFor(Move m) const noexcept {
    return ((moveMask_ >> static_cast<std::uint8_t>(m)) & 1u) != 0u;
  }

  [[nodiscard]] std::uint32_t size() const noexcept { return size_; }
  [[nodiscard]] std::uint32_t moveMask() const noexcept { return moveMask_; }
  [[nodiscard]] const std::string& name() const noexcept { return name_; }

  /// Bytes held by the table.
  [[nodiscard]] std::size_t byteSize() const noexcept {
    return data_.size() * sizeof(std::uint16_t);
  }

 private:
  std::string name_;
  std::uint32_t size_ = 0;
  std::uint32_t moveMask_ = 0;
  std::vector<std::uint16_t> data_;
};

/// The full set of coordinate transition tables.
///
/// Built once and then read-only, so a single instance can be shared by any
/// number of solver threads without synchronisation. It is an ordinary object
/// rather than a global: the solver owns one, which keeps it testable and
/// avoids static initialisation order problems.
class MoveTables {
 public:
  MoveTables();

  // Phase 1 (also used by the Korf corner database).
  [[nodiscard]] const MoveTable& cornerOrientation() const noexcept { return cornerOri_; }
  [[nodiscard]] const MoveTable& edgeOrientation() const noexcept { return edgeOri_; }
  [[nodiscard]] const MoveTable& udSliceSorted() const noexcept { return udSliceSorted_; }

  /// The unsorted slice coordinate (495 values).
  ///
  /// The phase-1 search itself tracks `udSliceSorted` and divides by 24, so it
  /// never needs this table. Pruning-table generation does: a BFS over
  /// (flip, slice) visits 1.0M states, whereas (flip, sliceSorted) would visit
  /// 24 times as many for no extra information.
  [[nodiscard]] const MoveTable& udSlice() const noexcept { return udSlice_; }

  // Phase 2.
  [[nodiscard]] const MoveTable& cornerPermutation() const noexcept { return cornerPerm_; }
  [[nodiscard]] const MoveTable& udEdgePermutation() const noexcept { return udEdgePerm_; }
  [[nodiscard]] const MoveTable& slicePermutation() const noexcept { return slicePerm_; }

  /// Total bytes across every table.
  [[nodiscard]] std::size_t byteSize() const noexcept;

 private:
  MoveTable cornerOri_;
  MoveTable edgeOri_;
  MoveTable udSliceSorted_;
  MoveTable udSlice_;
  MoveTable cornerPerm_;
  MoveTable udEdgePerm_;
  MoveTable slicePerm_;
};

}  // namespace rubik
