#include "core/Cube.h"
#include "core/Error.h"
#include "core/Move.h"

#include <gtest/gtest.h>

#include <random>
#include <string_view>

using namespace rubik;

namespace {

Cube fromMoves(std::string_view alg) {
  Cube c;
  c.apply(parseSequence(alg));
  return c;
}

/// Applies `alg` repeatedly and returns the number of repetitions needed to
/// return to the solved state, or 0 if it has not returned within `limit`.
int orderOf(std::string_view alg, int limit = 1440) {
  const auto moves = parseSequence(alg);
  Cube c;
  for (int i = 1; i <= limit; ++i) {
    c.apply(moves);
    if (c.isSolved()) return i;
  }
  return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Basic state
// ---------------------------------------------------------------------------

TEST(CubeBasics, DefaultConstructedCubeIsSolved) {
  const Cube c;
  EXPECT_TRUE(c.isSolved());
}

TEST(CubeBasics, SolvedCubeHasIdentityPermutations) {
  const Cube c;
  for (int i = 0; i < kNumCorners; ++i) {
    EXPECT_EQ(c.cornerPerm()[i], i);
    EXPECT_EQ(c.cornerOri()[i], 0);
  }
  for (int i = 0; i < kNumEdges; ++i) {
    EXPECT_EQ(c.edgePerm()[i], i);
    EXPECT_EQ(c.edgeOri()[i], 0);
  }
}

TEST(CubeBasics, EverySingleMoveDisturbsTheCube) {
  for (int i = 0; i < kNumMoves; ++i) {
    Cube c;
    c.apply(static_cast<Move>(i));
    EXPECT_FALSE(c.isSolved()) << toString(static_cast<Move>(i));
  }
}

TEST(CubeBasics, ResetRestoresTheSolvedState) {
  Cube c;
  c.apply(parseSequence("R U R' F2 L B'"));
  ASSERT_FALSE(c.isSolved());
  c.reset();
  EXPECT_TRUE(c.isSolved());
}

TEST(CubeBasics, EqualityComparesFullState) {
  EXPECT_EQ(fromMoves("R U"), fromMoves("R U"));
  EXPECT_NE(fromMoves("R U"), fromMoves("U R"));
  EXPECT_EQ(Cube{}, fromMoves("R R'"));
}

// ---------------------------------------------------------------------------
// Group axioms. These are what prove the hand-written move tables correct.
// ---------------------------------------------------------------------------

TEST(CubeGroup, QuarterTurnsHaveOrderFour) {
  for (int f = 0; f < kNumFaces; ++f) {
    for (const int suffix : {0, 2}) {  // clockwise and counter-clockwise
      const Move m = static_cast<Move>(f * 3 + suffix);
      Cube c;
      c.apply(m);
      EXPECT_FALSE(c.isSolved()) << toString(m) << "^1";
      c.apply(m);
      EXPECT_FALSE(c.isSolved()) << toString(m) << "^2";
      c.apply(m);
      EXPECT_FALSE(c.isSolved()) << toString(m) << "^3";
      c.apply(m);
      EXPECT_TRUE(c.isSolved()) << toString(m) << "^4 should be the identity";
    }
  }
}

TEST(CubeGroup, HalfTurnsHaveOrderTwo) {
  for (int f = 0; f < kNumFaces; ++f) {
    const Move m = static_cast<Move>(f * 3 + 1);
    Cube c;
    c.apply(m);
    EXPECT_FALSE(c.isSolved()) << toString(m);
    c.apply(m);
    EXPECT_TRUE(c.isSolved()) << toString(m) << "^2 should be the identity";
  }
}

TEST(CubeGroup, UndoInvertsApplyForEveryMove) {
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    Cube c;
    c.apply(m);
    c.undo(m);
    EXPECT_TRUE(c.isSolved()) << "apply/undo " << toString(m);
  }
}

TEST(CubeGroup, TwoQuarterTurnsEqualOneHalfTurn) {
  for (int f = 0; f < kNumFaces; ++f) {
    const Move quarter = static_cast<Move>(f * 3);
    const Move half = static_cast<Move>(f * 3 + 1);
    Cube a;
    a.apply(quarter);
    a.apply(quarter);
    Cube b;
    b.apply(half);
    EXPECT_EQ(a, b) << toString(half);
  }
}

TEST(CubeGroup, ThreeQuarterTurnsEqualThePrimeMove) {
  for (int f = 0; f < kNumFaces; ++f) {
    const Move quarter = static_cast<Move>(f * 3);
    const Move prime = static_cast<Move>(f * 3 + 2);
    Cube a;
    a.apply(quarter);
    a.apply(quarter);
    a.apply(quarter);
    Cube b;
    b.apply(prime);
    EXPECT_EQ(a, b) << toString(prime);
  }
}

TEST(CubeGroup, OppositeFacesCommute) {
  EXPECT_EQ(fromMoves("U D"), fromMoves("D U"));
  EXPECT_EQ(fromMoves("R2 L'"), fromMoves("L' R2"));
  EXPECT_EQ(fromMoves("F B2"), fromMoves("B2 F"));
}

TEST(CubeGroup, AdjacentFacesDoNotCommute) {
  EXPECT_NE(fromMoves("R U"), fromMoves("U R"));
  EXPECT_NE(fromMoves("F L"), fromMoves("L F"));
}

// ---------------------------------------------------------------------------
// Known algorithms with known orders. Each exercises a different mix of
// permutation and orientation, so a wrong table entry shows up here.
// ---------------------------------------------------------------------------

TEST(CubeAlgorithms, SexyMoveHasOrderSix) {
  EXPECT_EQ(orderOf("R U R' U'"), 6);
}

TEST(CubeAlgorithms, SuneHasOrderSix) {
  EXPECT_EQ(orderOf("R U R' U R U2 R'"), 6);
}

TEST(CubeAlgorithms, TPermIsAnInvolution) {
  EXPECT_EQ(orderOf("R U R' U' R' F R2 U' R' U' R U R' F'"), 2);
}

TEST(CubeAlgorithms, YPermIsAnInvolution) {
  EXPECT_EQ(orderOf("F R U' R' U' R U R' F' R U R' U' R' F R F'"), 2);
}

TEST(CubeAlgorithms, ThePeriodOfRUIs105) {
  // A classic result: the sequence "R U" returns the cube to solved after 105
  // repetitions and not before.
  EXPECT_EQ(orderOf("R U"), 105);
}

TEST(CubeAlgorithms, SuperflipFlipsEveryEdgeAndNothingElse) {
  const auto moves =
      parseSequence("U R2 F B R B2 R U2 L B2 R U' D' R2 F R' L B2 U2 F2");
  ASSERT_EQ(moves.size(), 20u);

  Cube c;
  c.apply(moves);

  // Corners entirely untouched.
  for (int i = 0; i < kNumCorners; ++i) {
    EXPECT_EQ(c.cornerPerm()[i], i) << "corner slot " << i;
    EXPECT_EQ(c.cornerOri()[i], 0) << "corner slot " << i;
  }
  // Edges in place but every one of them flipped.
  for (int i = 0; i < kNumEdges; ++i) {
    EXPECT_EQ(c.edgePerm()[i], i) << "edge slot " << i;
    EXPECT_EQ(c.edgeOri()[i], 1) << "edge slot " << i;
  }
  EXPECT_NO_THROW(c.validate());
}

TEST(CubeAlgorithms, SuperflipIsAnInvolution) {
  EXPECT_EQ(orderOf("U R2 F B R B2 R U2 L B2 R U' D' R2 F R' L B2 U2 F2"), 2);
}

// ---------------------------------------------------------------------------
// Randomised round trips
// ---------------------------------------------------------------------------

TEST(CubeRandom, ScrambleThenInverseScrambleSolves) {
  for (std::uint64_t seed = 0; seed < 300; ++seed) {
    Cube c;
    const auto moves = c.scramble(30, seed);
    ASSERT_EQ(moves.size(), 30u) << "seed " << seed;
    ASSERT_FALSE(c.isSolved()) << "seed " << seed;
    c.apply(invertSequence(moves));
    EXPECT_TRUE(c.isSolved()) << "seed " << seed;
  }
}

TEST(CubeRandom, UndoingMovesInReverseOrderSolves) {
  std::mt19937_64 rng(4242);
  std::uniform_int_distribution<int> pick(0, kNumMoves - 1);
  for (int trial = 0; trial < 300; ++trial) {
    Cube c;
    std::vector<Move> applied;
    for (int i = 0; i < 40; ++i) {
      const Move m = static_cast<Move>(pick(rng));
      c.apply(m);
      applied.push_back(m);
    }
    for (auto it = applied.rbegin(); it != applied.rend(); ++it) c.undo(*it);
    EXPECT_TRUE(c.isSolved()) << "trial " << trial;
  }
}

TEST(CubeRandom, ScrambleIsFreeOfRedundantPairs) {
  Cube c;
  const auto moves = c.scramble(2000, 7);
  for (std::size_t i = 1; i < moves.size(); ++i) {
    EXPECT_FALSE(isRedundant(moves[i], moves[i - 1]))
        << "at index " << i << ": " << toString(moves[i - 1]) << " "
        << toString(moves[i]);
  }
}

TEST(CubeRandom, ScrambleIsDeterministicForAGivenSeed) {
  Cube a;
  Cube b;
  EXPECT_EQ(a.scramble(25, 123), b.scramble(25, 123));
  EXPECT_EQ(a, b);
}

TEST(CubeRandom, ZeroLengthScrambleLeavesTheCubeSolved) {
  Cube c;
  EXPECT_TRUE(c.scramble(0, 1).empty());
  EXPECT_TRUE(c.isSolved());
}

// ---------------------------------------------------------------------------
// State validation
// ---------------------------------------------------------------------------

TEST(CubeValidation, SolvedCubeIsValid) {
  EXPECT_NO_THROW(Cube{}.validate());
}

TEST(CubeValidation, EveryReachableStateIsValid) {
  std::mt19937_64 rng(9001);
  std::uniform_int_distribution<int> pick(0, kNumMoves - 1);
  for (int trial = 0; trial < 3000; ++trial) {
    Cube c;
    for (int i = 0; i < 50; ++i) c.apply(static_cast<Move>(pick(rng)));
    ASSERT_NO_THROW(c.validate()) << "trial " << trial;
  }
}

TEST(CubeValidation, InvariantsHoldAtEveryStepOfALongWalk) {
  std::mt19937_64 rng(31337);
  std::uniform_int_distribution<int> pick(0, kNumMoves - 1);
  Cube c;
  for (int i = 0; i < 5000; ++i) {
    c.apply(static_cast<Move>(pick(rng)));
    ASSERT_NO_THROW(c.validate()) << "after move " << i;
  }
}

// ---------------------------------------------------------------------------
// Inversion
//
// Needed because a cube and its inverse are always the same distance from
// solved, which makes a heuristic evaluated on the inverse a valid bound for
// the original. See KorfHeuristic.
// ---------------------------------------------------------------------------

TEST(CubeInverse, SolvedCubeIsItsOwnInverse) {
  EXPECT_TRUE(Cube{}.inverted().isSolved());
}

TEST(CubeInverse, InvertingTwiceIsTheIdentity) {
  for (std::uint64_t seed = 0; seed < 300; ++seed) {
    Cube c;
    (void)c.scramble(25, seed);
    EXPECT_EQ(c.inverted().inverted(), c) << "seed " << seed;
  }
}

TEST(CubeInverse, MatchesApplyingTheInvertedMoveSequence) {
  // The defining property: the state reached by M, inverted, is the state
  // reached by the reverse-inverse of M.
  for (std::uint64_t seed = 0; seed < 300; ++seed) {
    Cube forward;
    const auto moves = forward.scramble(20, seed);

    Cube backward;
    backward.apply(invertSequence(moves));

    EXPECT_EQ(forward.inverted(), backward) << "seed " << seed;
  }
}

TEST(CubeInverse, PreservesValidityAndSolvedness) {
  for (std::uint64_t seed = 0; seed < 300; ++seed) {
    Cube c;
    (void)c.scramble(25, seed);
    const Cube inverse = c.inverted();
    EXPECT_NO_THROW(inverse.validate()) << "seed " << seed;
    EXPECT_EQ(inverse.isSolved(), c.isSolved()) << "seed " << seed;
  }
}

TEST(CubeInverse, SingleMovesInvertToTheirOpposites) {
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    Cube c;
    c.apply(m);
    Cube expected;
    expected.apply(inverse(m));
    EXPECT_EQ(c.inverted(), expected) << toString(m);
  }
}
