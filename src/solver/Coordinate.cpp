#include "solver/Coordinate.h"

#include "solver/Combinatorics.h"

#include <array>

namespace rubik::coord {
namespace {

/// Slice edges FR, FL, BL, BR occupy cubie indices 8..11.
constexpr std::uint8_t kFirstSliceEdge = 8;

/// Collects the slots holding the four slice edges, ascending, together with
/// which slice edge sits in each. Returns the occupancy bitmask.
std::uint32_t sliceLayout(const Cube& cube,
                          std::array<std::uint8_t, 4>& cubieAtSlot) noexcept {
  const auto& ep = cube.edgePerm();

  std::uint32_t mask = 0;
  for (std::size_t slot = 0; slot < kNumEdges; ++slot) {
    if (ep[slot] >= kFirstSliceEdge) mask |= (1u << slot);
  }

  // Walk the occupied slots in ascending order.
  std::size_t seen = 0;
  for (std::size_t slot = 0; slot < kNumEdges && seen < 4; ++slot) {
    if ((mask & (1u << slot)) != 0) {
      cubieAtSlot[seen++] = static_cast<std::uint8_t>(ep[slot] - kFirstSliceEdge);
    }
  }
  return mask;
}

}  // namespace

// ---------------------------------------------------------------------------
// Phase 1
// ---------------------------------------------------------------------------

std::uint32_t cornerOrientation(const Cube& cube) noexcept {
  return encodeOrientation(cube.cornerOri().data(), kNumCorners, 3);
}

std::uint32_t edgeOrientation(const Cube& cube) noexcept {
  return encodeOrientation(cube.edgeOri().data(), kNumEdges, 2);
}

std::uint32_t udSlice(const Cube& cube) noexcept {
  std::array<std::uint8_t, 4> cubieAtSlot{};
  return rankCombination(sliceLayout(cube, cubieAtSlot));
}

std::uint32_t udSliceSorted(const Cube& cube) noexcept {
  std::array<std::uint8_t, 4> cubieAtSlot{};
  const std::uint32_t mask = sliceLayout(cube, cubieAtSlot);
  return rankCombination(mask) * kSlicePermutationCount +
         encodePermutation(cubieAtSlot.data(), 4);
}

// ---------------------------------------------------------------------------
// Phase 2
// ---------------------------------------------------------------------------

std::uint32_t cornerPermutation(const Cube& cube) noexcept {
  return encodePermutation(cube.cornerPerm().data(), kNumCorners);
}

std::uint32_t udEdgePermutation(const Cube& cube) noexcept {
  // Valid only in G1, where slots 0..7 hold exactly cubies 0..7.
  return encodePermutation(cube.edgePerm().data(), 8);
}

std::uint32_t slicePermutation(const Cube& cube) noexcept {
  const auto& ep = cube.edgePerm();
  std::array<std::uint8_t, 4> perm{};
  for (std::size_t i = 0; i < 4; ++i) {
    perm[i] = static_cast<std::uint8_t>(ep[8 + i] - kFirstSliceEdge);
  }
  return encodePermutation(perm.data(), 4);
}

bool isInG1(const Cube& cube) noexcept {
  return cornerOrientation(cube) == 0 && edgeOrientation(cube) == 0 &&
         udSlice(cube) == kUdSliceSolved;
}

// ---------------------------------------------------------------------------
// Representative cubes
// ---------------------------------------------------------------------------

namespace {

/// Identity permutation of the given length.
template <std::size_t N>
std::array<std::uint8_t, N> identity() noexcept {
  std::array<std::uint8_t, N> a{};
  for (std::size_t i = 0; i < N; ++i) a[i] = static_cast<std::uint8_t>(i);
  return a;
}

}  // namespace

Cube cubeWithCornerOrientation(std::uint32_t value) noexcept {
  std::array<std::uint8_t, kNumCorners> co{};
  decodeOrientation(value, kNumCorners, 3, co.data());
  return Cube::fromCubies(identity<kNumCorners>(), co, identity<kNumEdges>(),
                          std::array<std::uint8_t, kNumEdges>{});
}

Cube cubeWithEdgeOrientation(std::uint32_t value) noexcept {
  std::array<std::uint8_t, kNumEdges> eo{};
  decodeOrientation(value, kNumEdges, 2, eo.data());
  return Cube::fromCubies(identity<kNumCorners>(),
                          std::array<std::uint8_t, kNumCorners>{},
                          identity<kNumEdges>(), eo);
}

Cube cubeWithUdSliceSorted(std::uint32_t value) noexcept {
  const std::uint32_t combo = value / kSlicePermutationCount;
  const std::uint32_t permRank = value % kSlicePermutationCount;

  const std::uint32_t mask = unrankCombination(combo, kNumEdges, 4);

  std::array<std::uint8_t, 4> cubieAtSlot{};
  decodePermutation(permRank, 4, cubieAtSlot.data());

  std::array<std::uint8_t, kNumEdges> ep{};
  std::size_t sliceSeen = 0;
  std::uint8_t nextFiller = 0;
  for (std::size_t slot = 0; slot < kNumEdges; ++slot) {
    if ((mask & (1u << slot)) != 0) {
      ep[slot] = static_cast<std::uint8_t>(kFirstSliceEdge +
                                           cubieAtSlot[sliceSeen++]);
    } else {
      ep[slot] = nextFiller++;
    }
  }

  return Cube::fromCubies(identity<kNumCorners>(),
                          std::array<std::uint8_t, kNumCorners>{}, ep,
                          std::array<std::uint8_t, kNumEdges>{});
}

Cube cubeWithCornerPermutation(std::uint32_t value) noexcept {
  std::array<std::uint8_t, kNumCorners> cp{};
  decodePermutation(value, kNumCorners, cp.data());
  return Cube::fromCubies(cp, std::array<std::uint8_t, kNumCorners>{},
                          identity<kNumEdges>(),
                          std::array<std::uint8_t, kNumEdges>{});
}

Cube cubeWithUdEdgePermutation(std::uint32_t value) noexcept {
  std::array<std::uint8_t, kNumEdges> ep{};
  decodePermutation(value, 8, ep.data());
  // The slice keeps its solved arrangement so the cube stays inside G1.
  for (std::size_t i = 8; i < kNumEdges; ++i) ep[i] = static_cast<std::uint8_t>(i);
  return Cube::fromCubies(identity<kNumCorners>(),
                          std::array<std::uint8_t, kNumCorners>{}, ep,
                          std::array<std::uint8_t, kNumEdges>{});
}

Cube cubeWithSlicePermutation(std::uint32_t value) noexcept {
  std::array<std::uint8_t, 4> perm{};
  decodePermutation(value, 4, perm.data());

  std::array<std::uint8_t, kNumEdges> ep{};
  for (std::size_t i = 0; i < 8; ++i) ep[i] = static_cast<std::uint8_t>(i);
  for (std::size_t i = 0; i < 4; ++i) {
    ep[8 + i] = static_cast<std::uint8_t>(kFirstSliceEdge + perm[i]);
  }

  return Cube::fromCubies(identity<kNumCorners>(),
                          std::array<std::uint8_t, kNumCorners>{}, ep,
                          std::array<std::uint8_t, kNumEdges>{});
}

}  // namespace rubik::coord
