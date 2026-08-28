#pragma once

#include "core/Cube.h"
#include "core/Move.h"
#include "solver/Combinatorics.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace rubik::korf {

/// 12 P k, the number of ordered placements of k tracked edges among 12 slots.
[[nodiscard]] constexpr std::uint32_t edgePlacements(std::size_t tracked) noexcept {
  std::uint32_t result = 1;
  for (std::size_t i = 0; i < tracked; ++i) {
    result *= static_cast<std::uint32_t>(kNumEdges - i);
  }
  return result;
}

/// Keeps `Tracked` chosen edge cubies -- where they sit among the twelve slots,
/// and how each is flipped -- and discards the corners and every other edge.
///
/// Index = partialPermutationRank * 2^Tracked + orientationBits.
///
/// | Tracked | states | nibble-packed |
/// |---|---|---|
/// | 6 | 42,577,920 | 20.3 MB |
/// | 7 | 510,935,040 | 243.6 MB |
/// | 8 | 5,109,350,400 | 2.4 GB (and past the uint32 index limit) |
///
/// Note that all 2^Tracked orientation combinations are used, unlike the corner
/// database which stores only 3^7 of 3^8. The "orientations sum to zero" rule
/// constrains all twelve edges together, so it says nothing about a subset:
/// every flip pattern of the tracked edges is reachable, with the untracked ones
/// absorbing the parity.
///
/// Unlike the corner abstraction, this one does *not* factorise into independent
/// permutation and orientation tables: whether a tracked edge flips depends on
/// which slot it occupies (F and B quarter turns flip only the four edges in
/// their own layer), so orientation and position are coupled. Successors are
/// therefore computed by decoding, applying the move's slot map, and re-encoding.
template <std::size_t Tracked>
class EdgeAbstractionT {
 public:
  static_assert(Tracked >= 1 && Tracked <= 7,
                "8 tracked edges would overflow the 32-bit database index");

  static constexpr std::size_t kTracked = Tracked;
  static constexpr std::uint32_t kOrientationCount = 1u << Tracked;
  static constexpr std::uint32_t kPermutationCount = edgePlacements(Tracked);
  static constexpr std::uint32_t kSize = kPermutationCount * kOrientationCount;

  EdgeAbstractionT(std::string name, std::array<std::uint8_t, Tracked> trackedEdges)
      : name_(std::move(name)), tracked_(trackedEdges) {
    buildSlotMaps();
  }

  [[nodiscard]] std::uint32_t size() const noexcept { return kSize; }
  [[nodiscard]] std::string name() const { return name_; }
  [[nodiscard]] const std::array<std::uint8_t, Tracked>& tracked() const noexcept {
    return tracked_;
  }

  [[nodiscard]] std::uint32_t index(const Cube& cube) const noexcept {
    const auto& perm = cube.edgePerm();
    const auto& ori = cube.edgeOri();

    // Invert the permutation once so each tracked edge's slot is a direct read.
    std::array<std::uint8_t, kNumEdges> slotOf{};
    for (std::uint8_t slot = 0; slot < kNumEdges; ++slot) {
      slotOf[perm[slot]] = slot;
    }

    std::array<std::uint8_t, Tracked> positions{};
    std::uint32_t orientationBits = 0;
    for (std::size_t j = 0; j < Tracked; ++j) {
      const std::uint8_t slot = slotOf[tracked_[j]];
      positions[j] = slot;
      orientationBits = (orientationBits << 1) | ori[slot];
    }

    return encodePartialPermutation(positions.data(), kNumEdges,
                                   static_cast<int>(Tracked)) *
               kOrientationCount +
           orientationBits;
  }

  [[nodiscard]] std::uint32_t goalIndex() const noexcept { return index(Cube{}); }

  void successors(std::uint32_t index, std::uint32_t* out) const noexcept {
    std::array<std::uint8_t, Tracked> positions{};
    decodePartialPermutation(index / kOrientationCount, kNumEdges,
                             static_cast<int>(Tracked), positions.data());
    const std::uint32_t orientationBits = index % kOrientationCount;

    for (int m = 0; m < kNumMoves; ++m) {
      const auto& toSlot = slotAfterMove_[static_cast<std::size_t>(m)];
      const auto& flips = flipOnMove_[static_cast<std::size_t>(m)];

      std::array<std::uint8_t, Tracked> moved{};
      std::uint32_t bits = 0;
      for (std::size_t j = 0; j < Tracked; ++j) {
        const std::uint8_t from = positions[j];
        moved[j] = toSlot[from];
        const std::uint32_t oldBit =
            (orientationBits >> (Tracked - 1 - j)) & 1u;
        bits = (bits << 1) | (oldBit ^ flips[from]);
      }

      out[m] = encodePartialPermutation(moved.data(), kNumEdges,
                                       static_cast<int>(Tracked)) *
                   kOrientationCount +
               bits;
    }
  }

 private:
  /// For each move, where an edge sitting in each slot ends up and whether it
  /// flips. Derived from the cube itself rather than transcribed, so the two can
  /// never drift apart.
  void buildSlotMaps() {
    for (int m = 0; m < kNumMoves; ++m) {
      Cube cube;  // solved: slot i holds cubie i, unflipped
      cube.apply(static_cast<Move>(m));

      auto& toSlot = slotAfterMove_[static_cast<std::size_t>(m)];
      auto& flips = flipOnMove_[static_cast<std::size_t>(m)];
      for (std::uint8_t slot = 0; slot < kNumEdges; ++slot) {
        // The cubie now at `slot` started at the slot equal to its own id.
        const std::uint8_t origin = cube.edgePerm()[slot];
        toSlot[origin] = slot;
        flips[origin] = cube.edgeOri()[slot];
      }
    }
  }

  std::string name_;
  std::array<std::uint8_t, Tracked> tracked_;
  std::array<std::array<std::uint8_t, kNumEdges>, kNumMoves> slotAfterMove_{};
  std::array<std::array<std::uint8_t, kNumEdges>, kNumMoves> flipOnMove_{};
};

/// The default configuration tracks six edges per group.
inline constexpr std::size_t kTrackedEdges = 6;
using EdgeAbstraction = EdgeAbstractionT<6>;
using EdgeAbstraction7 = EdgeAbstractionT<7>;

/// The two halves of the edge set used by the default configuration.
/// Any partition works for admissibility; this one is simply the natural split
/// of the standard numbering.
inline EdgeAbstraction makeEdgeGroupA() {
  return EdgeAbstraction("edgeA", {0, 1, 2, 3, 4, 5});  // UR UF UL UB DR DF
}
inline EdgeAbstraction makeEdgeGroupB() {
  return EdgeAbstraction("edgeB", {6, 7, 8, 9, 10, 11});  // DL DB FR FL BL BR
}

/// A seven-edge group, for evaluating a stronger configuration.
///
/// It deliberately straddles the two six-edge groups rather than extending one
/// of them. Overlap costs nothing under a `max` combination -- each database is
/// admissible on its own -- and a group spanning both halves is less likely to
/// be redundant with either.
inline EdgeAbstraction7 makeEdgeGroup7() {
  return EdgeAbstraction7("edge7", {3, 4, 5, 6, 7, 8, 9});  // UB DR DF DL DB FR FL
}

}  // namespace rubik::korf
