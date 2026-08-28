#pragma once

#include "core/Move.h"

#include <array>
#include <cstdint>
#include <vector>

namespace rubik {

/// The eight corner cubies, named by the faces they touch.
/// The ordering is the standard Kociemba ordering and is relied upon by the
/// pattern-database indexers.
enum class Corner : std::uint8_t {
  URF = 0, UFL, ULB, UBR, DFR, DLF, DBL, DRB
};

/// The twelve edge cubies. Note that FR, FL, BL, BR occupy indices 8..11:
/// keeping the four UD-slice edges contiguous is what makes the Kociemba
/// phase-2 coordinates cheap to compute.
enum class Edge : std::uint8_t {
  UR = 0, UF, UL, UB, DR, DF, DL, DB, FR, FL, BL, BR
};

inline constexpr int kNumCorners = 8;
inline constexpr int kNumEdges = 12;

/// A Rubik's Cube state in cubie (permutation + orientation) form.
///
/// Four parallel arrays, 40 bytes total:
///   cp[i] = which corner cubie currently sits in corner slot i
///   co[i] = its twist, 0..2 (clockwise turns from the solved orientation)
///   ep[i] = which edge cubie currently sits in edge slot i
///   eo[i] = its flip, 0 or 1
///
/// This is deliberately *not* a 54-sticker array. A face turn touches only 4
/// corners and 4 edges, so `apply` is a pair of 4-cycles over ~40 bytes that
/// stays entirely in L1, and both make and unmake are O(1) with no allocation.
/// That is what lets the IDA* search mutate a single cube in place rather than
/// copying a state per node.
class Cube {
 public:
  /// Constructs the solved cube.
  Cube() noexcept { reset(); }

  /// Constructs a cube directly from cubie arrays.
  ///
  /// Performs no checking: the caller is responsible for calling `validate()`
  /// if the arrays come from an untrusted source. Used by the facelet converter
  /// and by pattern-database generation, both of which validate separately.
  [[nodiscard]] static Cube fromCubies(
      const std::array<std::uint8_t, kNumCorners>& cp,
      const std::array<std::uint8_t, kNumCorners>& co,
      const std::array<std::uint8_t, kNumEdges>& ep,
      const std::array<std::uint8_t, kNumEdges>& eo) noexcept;

  void reset() noexcept;

  /// Applies a single face turn in place.
  void apply(Move m) noexcept;

  /// Applies the inverse of `m` in place. `apply(m); undo(m);` is the identity.
  void undo(Move m) noexcept { apply(inverse(m)); }

  void apply(const std::vector<Move>& moves) noexcept;

  [[nodiscard]] bool isSolved() const noexcept;

  /// The inverse state: the cube that this one undoes.
  ///
  /// Useful because the two are always the same distance from solved. If a
  /// sequence M solves this cube then the reverse-inverse of M solves the
  /// inverse cube, and the eighteen face turns are closed under inversion, so
  /// the two shortest solutions have equal length. A heuristic may therefore be
  /// evaluated on either and still bound this cube's distance from below.
  [[nodiscard]] Cube inverted() const noexcept;

  /// Applies `count` random moves, never producing a redundant pair (so the
  /// scramble length is honest rather than silently collapsing).
  /// Returns the moves applied.
  std::vector<Move> scramble(int count, std::uint64_t seed);

  [[nodiscard]] const std::array<std::uint8_t, kNumCorners>& cornerPerm() const noexcept { return cp_; }
  [[nodiscard]] const std::array<std::uint8_t, kNumCorners>& cornerOri() const noexcept { return co_; }
  [[nodiscard]] const std::array<std::uint8_t, kNumEdges>& edgePerm() const noexcept { return ep_; }
  [[nodiscard]] const std::array<std::uint8_t, kNumEdges>& edgeOri() const noexcept { return eo_; }

  /// Structural self-check: permutations are bijections, orientation sums
  /// vanish (mod 3 for corners, mod 2 for edges), and corner parity matches
  /// edge parity. Every state reachable by legal moves satisfies all three;
  /// a state violating any of them cannot be solved.
  /// Throws `InvalidStateError` with a specific message when violated.
  void validate() const;

  [[nodiscard]] bool operator==(const Cube& other) const noexcept;
  [[nodiscard]] bool operator!=(const Cube& other) const noexcept {
    return !(*this == other);
  }

 private:
  std::array<std::uint8_t, kNumCorners> cp_{};
  std::array<std::uint8_t, kNumCorners> co_{};
  std::array<std::uint8_t, kNumEdges> ep_{};
  std::array<std::uint8_t, kNumEdges> eo_{};
};

}  // namespace rubik
