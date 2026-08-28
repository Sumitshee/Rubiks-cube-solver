#include "core/Cube.h"
#include "core/Move.h"
#include "solver/Coordinate.h"
#include "solver/Combinatorics.h"
#include "solver/MoveTable.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <vector>

using namespace rubik;

namespace {

/// The tables cost a little over a second to build under Debug, so share one
/// instance across the whole suite rather than rebuilding per test.
const MoveTables& tables() {
  static const MoveTables instance;
  return instance;
}

const std::vector<Move>& phase2Moves() {
  static const std::vector<Move> moves = {Move::U,  Move::U2, Move::Up,
                                          Move::D,  Move::D2, Move::Dp,
                                          Move::R2, Move::L2, Move::F2,
                                          Move::B2};
  return moves;
}

Cube inG1(std::uint64_t seed, int length = 30) {
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<std::size_t> pick(0, phase2Moves().size() - 1);
  Cube c;
  for (int i = 0; i < length; ++i) c.apply(phase2Moves()[pick(rng)]);
  return c;
}

}  // namespace

// ---------------------------------------------------------------------------
// Shape
// ---------------------------------------------------------------------------

TEST(MoveTable, TablesHaveTheExpectedSizes) {
  EXPECT_EQ(tables().cornerOrientation().size(), coord::kCornerOrientationCount);
  EXPECT_EQ(tables().edgeOrientation().size(), coord::kEdgeOrientationCount);
  EXPECT_EQ(tables().udSliceSorted().size(), coord::kUdSliceSortedCount);
  EXPECT_EQ(tables().cornerPermutation().size(), coord::kCornerPermutationCount);
  EXPECT_EQ(tables().udEdgePermutation().size(), coord::kUdEdgePermutationCount);
  EXPECT_EQ(tables().slicePermutation().size(), coord::kSlicePermutationCount);
}

TEST(MoveTable, Phase1TablesAcceptEveryMove) {
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    EXPECT_TRUE(tables().cornerOrientation().isValidFor(m)) << toString(m);
    EXPECT_TRUE(tables().edgeOrientation().isValidFor(m)) << toString(m);
    EXPECT_TRUE(tables().udSliceSorted().isValidFor(m)) << toString(m);
    EXPECT_TRUE(tables().cornerPermutation().isValidFor(m)) << toString(m);
  }
}

TEST(MoveTable, Phase2TablesAcceptExactlyTheTenG1Moves) {
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    const bool expected =
        std::find(phase2Moves().begin(), phase2Moves().end(), m) !=
        phase2Moves().end();
    EXPECT_EQ(tables().udEdgePermutation().isValidFor(m), expected) << toString(m);
    EXPECT_EQ(tables().slicePermutation().isValidFor(m), expected) << toString(m);
  }
  EXPECT_EQ(popcount(kPhase2MoveMask), 10);
}

TEST(MoveTable, DisallowedEntriesAreMarkedInvalid) {
  const MoveTable& t = tables().slicePermutation();
  for (std::uint32_t c = 0; c < t.size(); ++c) {
    for (int i = 0; i < kNumMoves; ++i) {
      const Move m = static_cast<Move>(i);
      if (t.isValidFor(m)) continue;
      EXPECT_EQ(t.row(c)[i], MoveTable::kInvalid) << "coord " << c << " " << toString(m);
    }
  }
}

// ---------------------------------------------------------------------------
// The central correctness property.
//
// A table is only valid if a coordinate's transition depends solely on its own
// value and the move -- never on the parts of the cube it ignores. These tests
// check that against real, fully-specified cube states.
// ---------------------------------------------------------------------------

TEST(MoveTable, Phase1TablesAgreeWithDirectComputationOnRandomStates) {
  std::mt19937_64 rng(1234);
  std::uniform_int_distribution<int> pickMove(0, kNumMoves - 1);

  for (std::uint64_t seed = 0; seed < 400; ++seed) {
    Cube c;
    (void)c.scramble(25, seed);

    for (int trial = 0; trial < 18; ++trial) {
      const Move m = static_cast<Move>(pickMove(rng));

      const std::uint32_t co = coord::cornerOrientation(c);
      const std::uint32_t eo = coord::edgeOrientation(c);
      const std::uint32_t ss = coord::udSliceSorted(c);
      const std::uint32_t cp = coord::cornerPermutation(c);

      Cube next = c;
      next.apply(m);

      ASSERT_EQ(tables().cornerOrientation().apply(co, m),
                coord::cornerOrientation(next))
          << "seed " << seed << " move " << toString(m);
      ASSERT_EQ(tables().edgeOrientation().apply(eo, m),
                coord::edgeOrientation(next))
          << "seed " << seed << " move " << toString(m);
      ASSERT_EQ(tables().udSliceSorted().apply(ss, m), coord::udSliceSorted(next))
          << "seed " << seed << " move " << toString(m);
      ASSERT_EQ(tables().cornerPermutation().apply(cp, m),
                coord::cornerPermutation(next))
          << "seed " << seed << " move " << toString(m);

      c = next;
    }
  }
}

TEST(MoveTable, Phase2TablesAgreeWithDirectComputationInsideG1) {
  std::mt19937_64 rng(5678);
  std::uniform_int_distribution<std::size_t> pick(0, phase2Moves().size() - 1);

  for (std::uint64_t seed = 0; seed < 400; ++seed) {
    Cube c = inG1(seed);

    for (int trial = 0; trial < 10; ++trial) {
      const Move m = phase2Moves()[pick(rng)];

      const std::uint32_t ue = coord::udEdgePermutation(c);
      const std::uint32_t sp = coord::slicePermutation(c);
      const std::uint32_t cp = coord::cornerPermutation(c);

      Cube next = c;
      next.apply(m);
      ASSERT_TRUE(coord::isInG1(next)) << "G1 move left G1: " << toString(m);

      ASSERT_EQ(tables().udEdgePermutation().apply(ue, m),
                coord::udEdgePermutation(next))
          << "seed " << seed << " move " << toString(m);
      ASSERT_EQ(tables().slicePermutation().apply(sp, m),
                coord::slicePermutation(next))
          << "seed " << seed << " move " << toString(m);
      ASSERT_EQ(tables().cornerPermutation().apply(cp, m),
                coord::cornerPermutation(next))
          << "seed " << seed << " move " << toString(m);

      c = next;
    }
  }
}

// ---------------------------------------------------------------------------
// Algebraic properties every table must satisfy
// ---------------------------------------------------------------------------

namespace {

void checkMoveAndInverseCancel(const MoveTable& t) {
  for (std::uint32_t c = 0; c < t.size(); ++c) {
    for (int i = 0; i < kNumMoves; ++i) {
      const Move m = static_cast<Move>(i);
      if (!t.isValidFor(m) || !t.isValidFor(inverse(m))) continue;
      ASSERT_EQ(t.apply(t.apply(c, m), inverse(m)), c)
          << t.name() << " coord " << c << " move " << toString(m);
    }
  }
}

void checkQuarterTurnsHaveOrderFour(const MoveTable& t) {
  for (std::uint32_t c = 0; c < t.size(); ++c) {
    for (int i = 0; i < kNumMoves; ++i) {
      const Move m = static_cast<Move>(i);
      if (turns(m) != 0 || !t.isValidFor(m)) continue;
      std::uint32_t v = c;
      for (int k = 0; k < 4; ++k) v = t.apply(v, m);
      ASSERT_EQ(v, c) << t.name() << " coord " << c << " move " << toString(m);
    }
  }
}

void checkHalfTurnsAreInvolutions(const MoveTable& t) {
  for (std::uint32_t c = 0; c < t.size(); ++c) {
    for (int i = 0; i < kNumMoves; ++i) {
      const Move m = static_cast<Move>(i);
      if (turns(m) != 1 || !t.isValidFor(m)) continue;
      ASSERT_EQ(t.apply(t.apply(c, m), m), c)
          << t.name() << " coord " << c << " move " << toString(m);
    }
  }
}

void checkIsAPermutationOfTheRange(const MoveTable& t) {
  // Each move must map the coordinate range onto itself bijectively, because
  // every move is invertible on the cube.
  std::vector<bool> seen(t.size());
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    if (!t.isValidFor(m)) continue;
    std::fill(seen.begin(), seen.end(), false);
    for (std::uint32_t c = 0; c < t.size(); ++c) {
      const std::uint16_t next = t.apply(c, m);
      ASSERT_LT(next, t.size()) << t.name() << " " << toString(m);
      ASSERT_FALSE(seen[next]) << t.name() << " " << toString(m)
                               << " maps two states onto " << next;
      seen[next] = true;
    }
  }
}

}  // namespace

TEST(MoveTable, MoveFollowedByItsInverseIsTheIdentity) {
  checkMoveAndInverseCancel(tables().cornerOrientation());
  checkMoveAndInverseCancel(tables().edgeOrientation());
  checkMoveAndInverseCancel(tables().udSliceSorted());
  checkMoveAndInverseCancel(tables().slicePermutation());
}

TEST(MoveTable, MoveFollowedByItsInverseIsTheIdentityForLargeTables) {
  checkMoveAndInverseCancel(tables().cornerPermutation());
  checkMoveAndInverseCancel(tables().udEdgePermutation());
}

TEST(MoveTable, QuarterTurnsHaveOrderFour) {
  checkQuarterTurnsHaveOrderFour(tables().cornerOrientation());
  checkQuarterTurnsHaveOrderFour(tables().edgeOrientation());
  checkQuarterTurnsHaveOrderFour(tables().udSliceSorted());
  checkQuarterTurnsHaveOrderFour(tables().slicePermutation());
}

TEST(MoveTable, HalfTurnsAreInvolutions) {
  checkHalfTurnsAreInvolutions(tables().cornerOrientation());
  checkHalfTurnsAreInvolutions(tables().edgeOrientation());
  checkHalfTurnsAreInvolutions(tables().udSliceSorted());
  checkHalfTurnsAreInvolutions(tables().slicePermutation());
  checkHalfTurnsAreInvolutions(tables().udEdgePermutation());
}

TEST(MoveTable, EachMoveIsABijectionOnTheCoordinateRange) {
  checkIsAPermutationOfTheRange(tables().cornerOrientation());
  checkIsAPermutationOfTheRange(tables().edgeOrientation());
  checkIsAPermutationOfTheRange(tables().udSliceSorted());
  checkIsAPermutationOfTheRange(tables().slicePermutation());
  checkIsAPermutationOfTheRange(tables().cornerPermutation());
  checkIsAPermutationOfTheRange(tables().udEdgePermutation());
}

// ---------------------------------------------------------------------------
// Solved-state fixed points
// ---------------------------------------------------------------------------

TEST(MoveTable, UAndDTurnsFixTheOrientationCoordinates) {
  for (const Move m : {Move::U, Move::U2, Move::Up, Move::D, Move::D2, Move::Dp}) {
    // U and D turns never twist a corner or flip an edge, so from the solved
    // orientation they stay at zero.
    EXPECT_EQ(tables().cornerOrientation().apply(0, m), 0u) << toString(m);
    EXPECT_EQ(tables().edgeOrientation().apply(0, m), 0u) << toString(m);
  }
}

TEST(MoveTable, G1MovesKeepTheSliceCoordinateSolved) {
  const std::uint32_t solvedSorted = coord::kUdSliceSolved * 24u;
  for (const Move m : phase2Moves()) {
    const std::uint16_t next = tables().udSliceSorted().apply(solvedSorted, m);
    EXPECT_EQ(next / 24u, coord::kUdSliceSolved)
        << toString(m) << " moved an edge out of the slice";
  }
}

TEST(MoveTable, QuarterTurnsOfFRLBMoveTheSliceCoordinateAway) {
  const std::uint32_t solvedSorted = coord::kUdSliceSolved * 24u;
  for (const Move m : {Move::F, Move::Fp, Move::R, Move::Rp, Move::L, Move::Lp,
                       Move::B, Move::Bp}) {
    const std::uint16_t next = tables().udSliceSorted().apply(solvedSorted, m);
    EXPECT_NE(next / 24u, coord::kUdSliceSolved) << toString(m);
  }
}

// ---------------------------------------------------------------------------
// Row access matches the scalar accessor
// ---------------------------------------------------------------------------

TEST(MoveTable, RowMatchesApply) {
  const MoveTable& t = tables().udSliceSorted();
  for (std::uint32_t c = 0; c < t.size(); c += 37) {
    const std::uint16_t* r = t.row(c);
    for (int i = 0; i < kNumMoves; ++i) {
      const Move m = static_cast<Move>(i);
      if (!t.isValidFor(m)) continue;
      ASSERT_EQ(r[i], t.apply(c, m)) << "coord " << c << " " << toString(m);
    }
  }
}

// ---------------------------------------------------------------------------
// Footprint
// ---------------------------------------------------------------------------

TEST(MoveTable, TotalFootprintIsSmall) {
  const std::size_t bytes = tables().byteSize();
  // Roughly 3.4 MB: negligible next to the Korf pattern databases, and worth
  // knowing so the figure in the README stays honest.
  EXPECT_GT(bytes, 1u * 1024 * 1024);
  EXPECT_LT(bytes, 8u * 1024 * 1024);
}
