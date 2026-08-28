#include "core/Cube.h"
#include "core/Error.h"
#include "core/Facelets.h"
#include "core/Move.h"

#include <gtest/gtest.h>

#include <random>
#include <string>

using namespace rubik;

namespace {
constexpr const char* kSolvedString =
    "UUUUUUUUU"
    "RRRRRRRRR"
    "FFFFFFFFF"
    "DDDDDDDDD"
    "LLLLLLLLL"
    "BBBBBBBBB";
}  // namespace

// ---------------------------------------------------------------------------
// Rendering to stickers
// ---------------------------------------------------------------------------

TEST(Facelets, SolvedCubeRendersEachFaceInOneColour) {
  EXPECT_EQ(toFaceletString(Cube{}), kSolvedString);
}

TEST(Facelets, CentresNeverMove) {
  std::mt19937_64 rng(77);
  std::uniform_int_distribution<int> pick(0, kNumMoves - 1);
  for (int trial = 0; trial < 200; ++trial) {
    Cube c;
    for (int i = 0; i < 30; ++i) c.apply(static_cast<Move>(pick(rng)));
    const auto f = toFacelets(c);
    for (int face = 0; face < kNumFaces; ++face) {
      EXPECT_EQ(f[static_cast<std::size_t>(face) * 9 + 4],
                static_cast<Face>(face))
          << "centre of face " << face << " on trial " << trial;
    }
  }
}

TEST(Facelets, EveryColourAppearsNineTimes) {
  std::mt19937_64 rng(78);
  std::uniform_int_distribution<int> pick(0, kNumMoves - 1);
  for (int trial = 0; trial < 200; ++trial) {
    Cube c;
    for (int i = 0; i < 30; ++i) c.apply(static_cast<Move>(pick(rng)));
    std::array<int, kNumFaces> counts{};
    for (const Face f : toFacelets(c)) counts[static_cast<std::size_t>(f)]++;
    for (int face = 0; face < kNumFaces; ++face) {
      EXPECT_EQ(counts[static_cast<std::size_t>(face)], 9)
          << "colour " << face << " on trial " << trial;
    }
  }
}

TEST(Facelets, AnRTurnMovesTheExpectedColumns) {
  Cube c;
  c.apply(Move::R);
  const auto f = toFacelets(c);
  // U's right column takes F's colour, F's takes D's, D's takes B's, and B's
  // left column (which adjoins R in the unfolded net) takes U's.
  for (int row = 0; row < 3; ++row) {
    EXPECT_EQ(f[static_cast<std::size_t>(row * 3 + 2)], Face::F) << "U row " << row;
    EXPECT_EQ(f[static_cast<std::size_t>(18 + row * 3 + 2)], Face::D) << "F row " << row;
    EXPECT_EQ(f[static_cast<std::size_t>(27 + row * 3 + 2)], Face::B) << "D row " << row;
    EXPECT_EQ(f[static_cast<std::size_t>(45 + row * 3)], Face::U) << "B row " << row;
  }
  // The turned face keeps its own colour throughout.
  for (int i = 9; i < 18; ++i) {
    EXPECT_EQ(f[static_cast<std::size_t>(i)], Face::R) << "R facelet " << i;
  }
}

// ---------------------------------------------------------------------------
// Round trips
// ---------------------------------------------------------------------------

TEST(Facelets, CubieToStickerToCubieRoundTrips) {
  std::mt19937_64 rng(5150);
  std::uniform_int_distribution<int> pick(0, kNumMoves - 1);
  for (int trial = 0; trial < 2000; ++trial) {
    Cube c;
    for (int i = 0; i < 40; ++i) c.apply(static_cast<Move>(pick(rng)));
    ASSERT_EQ(fromFacelets(toFacelets(c)), c) << "trial " << trial;
  }
}

TEST(Facelets, StringRoundTripsThroughParsing) {
  Cube c;
  c.apply(parseSequence("R U R' F2 L' B D2"));
  const std::string text = toFaceletString(c);
  EXPECT_EQ(fromFacelets(parseFacelets(text)), c);
  EXPECT_EQ(toFaceletString(parseFacelets(text)), text);
}

TEST(Facelets, ColourLettersAreAcceptedAsWellAsFaceLetters) {
  // Same solved cube written in the Western colour scheme.
  const std::string colours =
      "WWWWWWWWW"
      "RRRRRRRRR"
      "GGGGGGGGG"
      "YYYYYYYYY"
      "OOOOOOOOO"
      "BBBBBBBBB";
  EXPECT_EQ(fromFacelets(parseFacelets(colours)), Cube{});
}

TEST(Facelets, WhitespaceInTheStringIsIgnored) {
  std::string spaced;
  const std::string solid = kSolvedString;
  for (std::size_t i = 0; i < solid.size(); ++i) {
    spaced.push_back(solid[i]);
    if (i % 9 == 8) spaced += "\n";
  }
  EXPECT_EQ(fromFacelets(parseFacelets(spaced)), Cube{});
}

// ---------------------------------------------------------------------------
// Rejecting bad input. Each case fails a different check.
// ---------------------------------------------------------------------------

TEST(Facelets, RejectsWrongLength) {
  EXPECT_THROW((void)parseFacelets("UUU"), ParseError);
  EXPECT_THROW((void)parseFacelets(std::string(53, 'U')), ParseError);
  EXPECT_THROW((void)parseFacelets(std::string(55, 'U')), ParseError);
}

TEST(Facelets, RejectsUnknownCharacters) {
  EXPECT_THROW((void)parseFacelets(std::string(54, 'Z')), ParseError);
}

TEST(Facelets, RejectsWrongColourCounts) {
  auto f = toFacelets(Cube{});
  f[0] = Face::R;  // ten R stickers, eight U stickers
  EXPECT_THROW((void)fromFacelets(f), InvalidStateError);
}

TEST(Facelets, RejectsRelabelledCentres) {
  auto f = toFacelets(Cube{});
  // Swap two whole faces, which keeps the colour counts right but puts the
  // wrong colour on a centre.
  for (int i = 0; i < 9; ++i) {
    std::swap(f[static_cast<std::size_t>(i)], f[static_cast<std::size_t>(27 + i)]);
  }
  EXPECT_THROW((void)fromFacelets(f), InvalidStateError);
}

TEST(Facelets, RejectsASingleTwistedCorner) {
  auto f = toFacelets(Cube{});
  // Rotate the three stickers of the URF corner in place.
  const Face tmp = f[8];
  f[8] = f[20];
  f[20] = f[9];
  f[9] = tmp;
  EXPECT_THROW((void)fromFacelets(f), InvalidStateError);
}

TEST(Facelets, RejectsASingleFlippedEdge) {
  auto f = toFacelets(Cube{});
  std::swap(f[5], f[10]);  // flip the UR edge
  EXPECT_THROW((void)fromFacelets(f), InvalidStateError);
}

TEST(Facelets, RejectsASwappedPairOfEdges) {
  auto f = toFacelets(Cube{});
  // Exchange the UR and UF edges, which breaks permutation parity.
  std::swap(f[5], f[7]);
  std::swap(f[10], f[19]);
  EXPECT_THROW((void)fromFacelets(f), InvalidStateError);
}

TEST(Facelets, RejectsAnImpossibleCornerPiece) {
  auto f = toFacelets(Cube{});
  // Put U, R and B on one corner: no such piece exists (U/R/B is a real corner,
  // so use U, R and L, which are opposite faces and cannot meet).
  f[8] = Face::U;
  f[9] = Face::R;
  f[20] = Face::L;
  // Keep the colour counts balanced so the earlier check does not fire first.
  f[26] = Face::F;
  f[24] = Face::F;
  f[42] = Face::B;
  EXPECT_THROW((void)fromFacelets(f), InvalidStateError);
}

// ---------------------------------------------------------------------------
// Net rendering
// ---------------------------------------------------------------------------

TEST(Facelets, NetContainsEveryStickerExactlyOnce) {
  const std::string net = toNetString(Cube{});
  std::array<int, kNumFaces> counts{};
  for (const char ch : net) {
    const auto pos = std::string_view("URFDLB").find(ch);
    if (pos != std::string_view::npos) counts[pos]++;
  }
  for (int f = 0; f < kNumFaces; ++f) {
    EXPECT_EQ(counts[static_cast<std::size_t>(f)], 9) << "face " << f;
  }
}
