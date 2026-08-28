#pragma once

#include "core/Cube.h"

#include <cstdint>

namespace rubik::coord {

/// Coordinates: bijections from one aspect of a cube state onto a dense
/// integer range.
///
/// A coordinate isolates part of the cube (say, just the corner twists) and
/// numbers every possible configuration of that part contiguously from zero.
/// Two things follow, and they are the whole reason coordinates exist:
///
///   1. A move becomes a table lookup. Because the new value of a coordinate
///      depends only on its old value and the move -- never on the parts of the
///      cube the coordinate ignores -- the entire effect of a move can be
///      precomputed into a flat array. See MoveTable.
///   2. A heuristic becomes an array index. Pattern databases are just arrays
///      indexed by a coordinate.
///
/// The coordinates below serve both the Kociemba two-phase solver and the Korf
/// pattern databases. In particular Korf's corner database index is exactly
///     cornerOrientation(cube) * 40320 + cornerPermutation(cube)
/// giving the 2187 * 40320 = 88,179,840 states his paper describes.

// --- Sizes ------------------------------------------------------------------

inline constexpr std::uint32_t kCornerOrientationCount = 2187;   // 3^7
inline constexpr std::uint32_t kEdgeOrientationCount = 2048;     // 2^11
inline constexpr std::uint32_t kUdSliceCount = 495;              // C(12,4)
inline constexpr std::uint32_t kUdSliceSortedCount = 11880;      // 12P4
inline constexpr std::uint32_t kCornerPermutationCount = 40320;  // 8!
inline constexpr std::uint32_t kUdEdgePermutationCount = 40320;  // 8!
inline constexpr std::uint32_t kSlicePermutationCount = 24;      // 4!

/// The Korf corner pattern database covers permutation and twist together.
inline constexpr std::uint32_t kCornerStateCount =
    kCornerOrientationCount * kCornerPermutationCount;  // 88,179,840

/// `udSlice` takes this value exactly when all four slice edges are in the
/// slice, which is the defining property of the phase-1 goal subgroup G1.
/// (It is the last colexicographic rank, C(12,4) - 1.)
inline constexpr std::uint32_t kUdSliceSolved = kUdSliceCount - 1;

// --- Phase 1 coordinates ----------------------------------------------------
//
// Phase 1 drives the cube into G1 = <U, D, R2, L2, F2, B2>, the subgroup where
// every edge is correctly oriented, every corner correctly twisted, and the
// four slice edges are somewhere in the slice. A state is in G1 exactly when
// all three of these read zero, zero, and kUdSliceSolved.

/// Corner twists, in [0, 2187). The eighth twist is implied by the others.
[[nodiscard]] std::uint32_t cornerOrientation(const Cube& cube) noexcept;

/// Edge flips, in [0, 2048). The twelfth flip is implied by the others.
[[nodiscard]] std::uint32_t edgeOrientation(const Cube& cube) noexcept;

/// Which four slots hold the slice edges (FR, FL, BL, BR), ignoring their
/// order, in [0, 495).
[[nodiscard]] std::uint32_t udSlice(const Cube& cube) noexcept;

/// Which four slots hold the slice edges *and* in what order, in [0, 11880).
///
/// Carrying the order through phase 1 means the phase-2 slice permutation is
/// already known when phase 1 ends, rather than needing a fresh scan. The two
/// coordinates are deliberately laid out so that
///     udSlice(cube) == udSliceSorted(cube) / 24
/// holds for every state.
[[nodiscard]] std::uint32_t udSliceSorted(const Cube& cube) noexcept;

// --- Phase 2 coordinates ----------------------------------------------------
//
// These are only meaningful for a cube already in G1. Outside G1 a U/D edge can
// sit in a slice slot, and the permutations below are then undefined; the
// corresponding move tables are built for the ten G1 moves only.

/// Corner permutation, in [0, 40320).
[[nodiscard]] std::uint32_t cornerPermutation(const Cube& cube) noexcept;

/// Permutation of the eight non-slice edges among slots 0..7, in [0, 40320).
/// Requires the cube to be in G1.
[[nodiscard]] std::uint32_t udEdgePermutation(const Cube& cube) noexcept;

/// Permutation of the four slice edges among slots 8..11, in [0, 24).
/// Requires the cube to be in G1, where it equals `udSliceSorted(cube) % 24`.
[[nodiscard]] std::uint32_t slicePermutation(const Cube& cube) noexcept;

// --- Group membership -------------------------------------------------------

/// True when the cube lies in G1 and phase 2 can begin.
[[nodiscard]] bool isInG1(const Cube& cube) noexcept;

// --- Representative cubes ---------------------------------------------------
//
// Each returns some cube whose corresponding coordinate equals `value`; the
// parts of the cube the coordinate does not describe are left solved. These
// exist to build move tables: a table entry is found by decoding a coordinate
// into a cube, applying the move, and re-encoding.
//
// The results are not required to be legal cube states (a corner permutation
// alone may not match edge parity, for instance), so `validate()` is not
// applicable to them. That is sound precisely because each coordinate's
// transition ignores the parts left unspecified.

[[nodiscard]] Cube cubeWithCornerOrientation(std::uint32_t value) noexcept;
[[nodiscard]] Cube cubeWithEdgeOrientation(std::uint32_t value) noexcept;
[[nodiscard]] Cube cubeWithUdSliceSorted(std::uint32_t value) noexcept;
[[nodiscard]] Cube cubeWithCornerPermutation(std::uint32_t value) noexcept;
[[nodiscard]] Cube cubeWithUdEdgePermutation(std::uint32_t value) noexcept;
[[nodiscard]] Cube cubeWithSlicePermutation(std::uint32_t value) noexcept;

}  // namespace rubik::coord
