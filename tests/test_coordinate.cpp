#include "core/Cube.h"
#include "core/Move.h"
#include "solver/Coordinate.h"

#include <gtest/gtest.h>

#include <random>
#include <set>

using namespace rubik;
using namespace rubik::coord;

namespace {

Cube scrambled(std::uint64_t seed, int length = 30) {
  Cube c;
  (void)c.scramble(length, seed);
  return c;
}

/// A cube driven into G1 by applying only G1-preserving moves to a solved cube.
Cube inG1(std::uint64_t seed) {
  static const std::vector<Move> g1 = {Move::U,  Move::U2, Move::Up, Move::D,
                                       Move::D2, Move::Dp, Move::R2, Move::L2,
                                       Move::F2, Move::B2};
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<std::size_t> pick(0, g1.size() - 1);
  Cube c;
  for (int i = 0; i < 30; ++i) c.apply(g1[pick(rng)]);
  return c;
}

}  // namespace

// ---------------------------------------------------------------------------
// The solved cube
// ---------------------------------------------------------------------------

TEST(Coordinate, SolvedCubeHasZeroOrientationCoordinates) {
  const Cube c;
  EXPECT_EQ(cornerOrientation(c), 0u);
  EXPECT_EQ(edgeOrientation(c), 0u);
  EXPECT_EQ(cornerPermutation(c), 0u);
  EXPECT_EQ(udEdgePermutation(c), 0u);
  EXPECT_EQ(slicePermutation(c), 0u);
}

TEST(Coordinate, SolvedCubeHasTheSliceEdgesHome) {
  const Cube c;
  // The four slice edges occupy slots 8..11, the highest colexicographic rank.
  EXPECT_EQ(udSlice(c), kUdSliceSolved);
  EXPECT_EQ(udSlice(c), 494u);
  EXPECT_EQ(udSliceSorted(c), 494u * 24u);
}

TEST(Coordinate, SolvedCubeIsInG1) {
  EXPECT_TRUE(isInG1(Cube{}));
}

TEST(Coordinate, KorfCornerStateCountMatchesTheLiterature) {
  // Korf's corner pattern database covers 8! * 3^7 states.
  EXPECT_EQ(kCornerStateCount, 88179840u);
  EXPECT_EQ(kCornerOrientationCount * kCornerPermutationCount, 88179840u);
}

// ---------------------------------------------------------------------------
// Ranges
// ---------------------------------------------------------------------------

TEST(Coordinate, EveryCoordinateStaysInRangeForRandomStates) {
  for (std::uint64_t seed = 0; seed < 500; ++seed) {
    const Cube c = scrambled(seed);
    ASSERT_LT(cornerOrientation(c), kCornerOrientationCount) << "seed " << seed;
    ASSERT_LT(edgeOrientation(c), kEdgeOrientationCount) << "seed " << seed;
    ASSERT_LT(udSlice(c), kUdSliceCount) << "seed " << seed;
    ASSERT_LT(udSliceSorted(c), kUdSliceSortedCount) << "seed " << seed;
    ASSERT_LT(cornerPermutation(c), kCornerPermutationCount) << "seed " << seed;
  }
}

TEST(Coordinate, Phase2CoordinatesStayInRangeInsideG1) {
  for (std::uint64_t seed = 0; seed < 500; ++seed) {
    const Cube c = inG1(seed);
    ASSERT_TRUE(isInG1(c)) << "seed " << seed;
    ASSERT_LT(udEdgePermutation(c), kUdEdgePermutationCount) << "seed " << seed;
    ASSERT_LT(slicePermutation(c), kSlicePermutationCount) << "seed " << seed;
  }
}

// ---------------------------------------------------------------------------
// Relationships between coordinates
// ---------------------------------------------------------------------------

TEST(Coordinate, UdSliceIsTheSortedCoordinateDividedByTwentyFour) {
  // This identity is what lets phase 1 track only the sorted coordinate and
  // still index a pruning table keyed on the unsorted one.
  for (std::uint64_t seed = 0; seed < 1000; ++seed) {
    const Cube c = scrambled(seed);
    ASSERT_EQ(udSlice(c), udSliceSorted(c) / kSlicePermutationCount)
        << "seed " << seed;
  }
}

TEST(Coordinate, SlicePermutationIsTheSortedCoordinateModuloTwentyFourInG1) {
  for (std::uint64_t seed = 0; seed < 500; ++seed) {
    const Cube c = inG1(seed);
    ASSERT_EQ(slicePermutation(c), udSliceSorted(c) % kSlicePermutationCount)
        << "seed " << seed;
  }
}

// ---------------------------------------------------------------------------
// G1 membership
// ---------------------------------------------------------------------------

TEST(Coordinate, G1MovesPreserveG1Membership) {
  for (std::uint64_t seed = 0; seed < 200; ++seed) {
    const Cube c = inG1(seed);
    EXPECT_TRUE(isInG1(c)) << "seed " << seed;
    EXPECT_EQ(cornerOrientation(c), 0u) << "seed " << seed;
    EXPECT_EQ(edgeOrientation(c), 0u) << "seed " << seed;
    EXPECT_EQ(udSlice(c), kUdSliceSolved) << "seed " << seed;
  }
}

TEST(Coordinate, QuarterTurnsOfFAndBLeaveG1) {
  for (const Move m : {Move::F, Move::Fp, Move::B, Move::Bp}) {
    Cube c;
    c.apply(m);
    EXPECT_FALSE(isInG1(c)) << toString(m);
    // Specifically, they flip edges.
    EXPECT_NE(edgeOrientation(c), 0u) << toString(m);
  }
}

TEST(Coordinate, QuarterTurnsOfRAndLLeaveG1) {
  for (const Move m : {Move::R, Move::Rp, Move::L, Move::Lp}) {
    Cube c;
    c.apply(m);
    EXPECT_FALSE(isInG1(c)) << toString(m);
    // They twist corners and pull edges out of the slice, but do not flip.
    EXPECT_EQ(edgeOrientation(c), 0u) << toString(m);
    EXPECT_NE(cornerOrientation(c), 0u) << toString(m);
    EXPECT_NE(udSlice(c), kUdSliceSolved) << toString(m);
  }
}

TEST(Coordinate, UAndDTurnsNeverLeaveG1) {
  for (const Move m : {Move::U, Move::U2, Move::Up, Move::D, Move::D2, Move::Dp}) {
    Cube c;
    c.apply(m);
    EXPECT_TRUE(isInG1(c)) << toString(m);
  }
}

// ---------------------------------------------------------------------------
// Representative cubes round-trip
// ---------------------------------------------------------------------------

TEST(Coordinate, CornerOrientationRepresentativesRoundTrip) {
  for (std::uint32_t v = 0; v < kCornerOrientationCount; ++v) {
    ASSERT_EQ(cornerOrientation(cubeWithCornerOrientation(v)), v);
  }
}

TEST(Coordinate, EdgeOrientationRepresentativesRoundTrip) {
  for (std::uint32_t v = 0; v < kEdgeOrientationCount; ++v) {
    ASSERT_EQ(edgeOrientation(cubeWithEdgeOrientation(v)), v);
  }
}

TEST(Coordinate, UdSliceSortedRepresentativesRoundTrip) {
  for (std::uint32_t v = 0; v < kUdSliceSortedCount; ++v) {
    ASSERT_EQ(udSliceSorted(cubeWithUdSliceSorted(v)), v);
  }
}

TEST(Coordinate, CornerPermutationRepresentativesRoundTrip) {
  for (std::uint32_t v = 0; v < kCornerPermutationCount; ++v) {
    ASSERT_EQ(cornerPermutation(cubeWithCornerPermutation(v)), v);
  }
}

TEST(Coordinate, UdEdgePermutationRepresentativesRoundTrip) {
  for (std::uint32_t v = 0; v < kUdEdgePermutationCount; ++v) {
    ASSERT_EQ(udEdgePermutation(cubeWithUdEdgePermutation(v)), v);
  }
}

TEST(Coordinate, SlicePermutationRepresentativesRoundTrip) {
  for (std::uint32_t v = 0; v < kSlicePermutationCount; ++v) {
    ASSERT_EQ(slicePermutation(cubeWithSlicePermutation(v)), v);
  }
}

TEST(Coordinate, Phase2RepresentativesAreThemselvesInG1) {
  // Otherwise their move tables would be built from meaningless states.
  for (std::uint32_t v = 0; v < kSlicePermutationCount; ++v) {
    EXPECT_TRUE(isInG1(cubeWithSlicePermutation(v))) << "slicePerm " << v;
  }
  for (std::uint32_t v = 0; v < 1000; ++v) {
    EXPECT_TRUE(isInG1(cubeWithUdEdgePermutation(v))) << "udEdgePerm " << v;
  }
}

// ---------------------------------------------------------------------------
// Coverage: the coordinates really do reach their whole range
// ---------------------------------------------------------------------------

TEST(Coordinate, RandomStatesSpreadAcrossTheSliceCoordinate) {
  std::set<std::uint32_t> observed;
  for (std::uint64_t seed = 0; seed < 5000; ++seed) {
    observed.insert(udSlice(scrambled(seed, 20)));
  }
  // With 5000 samples over 495 buckets, essentially all should appear.
  EXPECT_GT(observed.size(), 480u) << "only " << observed.size() << " of 495";
}

TEST(Coordinate, CoordinatesDistinguishStatesThatDifferInThatAspect) {
  // A U turn changes the corner permutation but not the corner twists.
  Cube a;
  Cube b;
  b.apply(Move::U);
  EXPECT_EQ(cornerOrientation(a), cornerOrientation(b));
  EXPECT_NE(cornerPermutation(a), cornerPermutation(b));

  // An R turn changes the twists.
  Cube d;
  d.apply(Move::R);
  EXPECT_NE(cornerOrientation(a), cornerOrientation(d));
}
