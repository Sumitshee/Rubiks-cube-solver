#include "core/Error.h"
#include "core/Move.h"

#include <gtest/gtest.h>

using namespace rubik;

// ---------------------------------------------------------------------------
// Encoding invariants. The rest of the codebase derives face, axis and inverse
// arithmetically from the enum value, so these identities must hold exactly.
// ---------------------------------------------------------------------------

TEST(MoveEncoding, FaceAndTurnsDecomposeTheEnumValue) {
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    EXPECT_EQ(static_cast<int>(face(m)), i / 3) << toString(m);
    EXPECT_EQ(turns(m), i % 3) << toString(m);
  }
}

TEST(MoveEncoding, OppositeFacesShareAnAxis) {
  EXPECT_EQ(axis(Move::U), axis(Move::D));
  EXPECT_EQ(axis(Move::R), axis(Move::L));
  EXPECT_EQ(axis(Move::F), axis(Move::B));
  EXPECT_NE(axis(Move::U), axis(Move::R));
  EXPECT_NE(axis(Move::R), axis(Move::F));
  EXPECT_NE(axis(Move::U), axis(Move::F));
}

TEST(MoveEncoding, InverseIsAnInvolution) {
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    EXPECT_EQ(inverse(inverse(m)), m) << toString(m);
    EXPECT_EQ(face(inverse(m)), face(m)) << toString(m);
  }
}

TEST(MoveEncoding, HalfTurnsAreSelfInverse) {
  for (int f = 0; f < kNumFaces; ++f) {
    const Move half = static_cast<Move>(f * 3 + 1);
    EXPECT_EQ(inverse(half), half) << toString(half);
  }
}

TEST(MoveEncoding, QuarterTurnsInvertToEachOther) {
  for (int f = 0; f < kNumFaces; ++f) {
    const Move cw = static_cast<Move>(f * 3);
    const Move ccw = static_cast<Move>(f * 3 + 2);
    EXPECT_EQ(inverse(cw), ccw) << toString(cw);
    EXPECT_EQ(inverse(ccw), cw) << toString(ccw);
  }
}

// ---------------------------------------------------------------------------
// Combining turns of one face
// ---------------------------------------------------------------------------

TEST(MoveCombine, QuarterPlusQuarterIsHalf) {
  EXPECT_EQ(combine(Move::R, Move::R), Move::R2);
  EXPECT_EQ(combine(Move::U, Move::U), Move::U2);
}

TEST(MoveCombine, MoveAndItsInverseCancel) {
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    EXPECT_FALSE(combine(m, inverse(m)).has_value()) << toString(m);
  }
}

TEST(MoveCombine, HalfPlusQuarterIsThePrime) {
  EXPECT_EQ(combine(Move::R2, Move::R), Move::Rp);
  EXPECT_EQ(combine(Move::R2, Move::Rp), Move::R);
}

TEST(MoveCombine, DifferentFacesDoNotCombine) {
  EXPECT_FALSE(combine(Move::R, Move::U).has_value());
  EXPECT_FALSE(combine(Move::F, Move::B).has_value());
}

// ---------------------------------------------------------------------------
// Parsing and formatting
// ---------------------------------------------------------------------------

TEST(MoveParsing, RoundTripsEveryMove) {
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    const auto parsed = parseMove(toString(m));
    ASSERT_TRUE(parsed.has_value()) << toString(m);
    EXPECT_EQ(*parsed, m) << toString(m);
  }
}

TEST(MoveParsing, AcceptsAlternativePrimeSpellings) {
  EXPECT_EQ(parseMove("R'"), Move::Rp);
  EXPECT_EQ(parseMove("Ri"), Move::Rp);
  EXPECT_EQ(parseMove("R3"), Move::Rp);
  EXPECT_EQ(parseMove("r"), Move::R);
}

TEST(MoveParsing, RejectsMalformedTokens) {
  EXPECT_FALSE(parseMove("").has_value());
  EXPECT_FALSE(parseMove("X").has_value());
  EXPECT_FALSE(parseMove("R5").has_value());
  EXPECT_FALSE(parseMove("RU").has_value());
  EXPECT_FALSE(parseMove("R''").has_value());
}

TEST(MoveParsing, HandlesArbitraryWhitespace) {
  const auto moves = parseSequence("  R\t U'\n F2  ");
  ASSERT_EQ(moves.size(), 3u);
  EXPECT_EQ(moves[0], Move::R);
  EXPECT_EQ(moves[1], Move::Up);
  EXPECT_EQ(moves[2], Move::F2);
}

TEST(MoveParsing, EmptyInputYieldsEmptySequence) {
  EXPECT_TRUE(parseSequence("").empty());
  EXPECT_TRUE(parseSequence("   ").empty());
}

TEST(MoveParsing, ReportsTheOffendingToken) {
  try {
    (void)parseSequence("R U X2 F");
    FAIL() << "expected ParseError";
  } catch (const ParseError& e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("X2"), std::string::npos) << what;
  }
}

TEST(MoveParsing, SequenceRoundTrips) {
  const std::string text = "R U R' F2 L B'";
  EXPECT_EQ(toString(parseSequence(text)), text);
}

// ---------------------------------------------------------------------------
// Sequence inversion
// ---------------------------------------------------------------------------

TEST(SequenceInversion, ReversesOrderAndInvertsEachMove) {
  const auto moves = parseSequence("R U F2");
  const auto inv = invertSequence(moves);
  ASSERT_EQ(inv.size(), 3u);
  EXPECT_EQ(inv[0], Move::F2);
  EXPECT_EQ(inv[1], Move::Up);
  EXPECT_EQ(inv[2], Move::Rp);
}

TEST(SequenceInversion, IsAnInvolution) {
  const auto moves = parseSequence("R U R' U' F2 B L' D2");
  EXPECT_EQ(invertSequence(invertSequence(moves)), moves);
}

// ---------------------------------------------------------------------------
// Redundancy pruning
// ---------------------------------------------------------------------------

TEST(Pruning, RejectsTwoTurnsOfTheSameFace) {
  for (int a = 0; a < 3; ++a) {
    for (int b = 0; b < 3; ++b) {
      const Move first = static_cast<Move>(a);   // some U turn
      const Move second = static_cast<Move>(b);  // another U turn
      EXPECT_TRUE(isRedundant(second, first));
    }
  }
}

TEST(Pruning, AllowsCommutingPairsInExactlyOneOrder) {
  // U and D commute, so only one of the two orders survives.
  EXPECT_FALSE(isRedundant(Move::D, Move::U));
  EXPECT_TRUE(isRedundant(Move::U, Move::D));

  EXPECT_FALSE(isRedundant(Move::L, Move::R));
  EXPECT_TRUE(isRedundant(Move::R, Move::L));

  EXPECT_FALSE(isRedundant(Move::B, Move::F));
  EXPECT_TRUE(isRedundant(Move::F, Move::B));
}

TEST(Pruning, AllowsMovesOnDifferentAxes) {
  EXPECT_FALSE(isRedundant(Move::R, Move::U));
  EXPECT_FALSE(isRedundant(Move::U, Move::R));
  EXPECT_FALSE(isRedundant(Move::F, Move::R));
}

TEST(Pruning, YieldsTheExpectedBranchingFactor) {
  // Counting the surviving successors for each possible previous move gives the
  // effective branching factor the IDA* cost model assumes.
  int total = 0;
  for (int p = 0; p < kNumMoves; ++p) {
    for (int n = 0; n < kNumMoves; ++n) {
      if (!isRedundant(static_cast<Move>(n), static_cast<Move>(p))) ++total;
    }
  }
  // Every move forbids the three turns of its own face. Moves on the "high"
  // face of each axis (D, L, B) additionally forbid the three turns of the
  // opposite face, since that ordering is covered by the other direction.
  // So the 9 moves on U/R/F keep 15 successors and the 9 on D/L/B keep 12.
  EXPECT_EQ(total, 9 * 15 + 9 * 12);
  // Geometric mean of the branching factor, ~13.35.
  EXPECT_NEAR(static_cast<double>(total) / kNumMoves, 13.5, 0.01);
}
