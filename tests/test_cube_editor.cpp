#include "core/Cube.h"
#include "core/CubeValidation.h"
#include "core/Facelets.h"
#include "core/Move.h"
#include "ui/CubeEditor.h"

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <vector>

using namespace rubik;
using namespace rubik::ui;

namespace {

/// The stickers of a cube, as the editor would hold them once fully entered.
CubeEditor editorFor(const Cube& cube) {
  CubeEditor editor;
  editor.loadFrom(cube);
  return editor;
}

/// Swaps two facelets, which is how most physically impossible cubes are made.
FaceletArray withSwap(const FaceletArray& facelets, int a, int b) {
  FaceletArray out = facelets;
  std::swap(out[static_cast<std::size_t>(a)], out[static_cast<std::size_t>(b)]);
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Net geometry: the renderer and the mouse hit-test both read this, so if the
// two directions disagree, clicking a sticker paints a different one.
// ---------------------------------------------------------------------------

TEST(CubeNet, EveryFaceletHasACellAndEveryCellItsFacelet) {
  std::set<std::pair<int, int>> occupied;
  for (int f = 0; f < kNumFacelets; ++f) {
    int column = 0;
    int row = 0;
    ASSERT_TRUE(netCell(f, column, row)) << "facelet " << f;
    EXPECT_GE(column, 0);
    EXPECT_LT(column, kNetColumns);
    EXPECT_GE(row, 0);
    EXPECT_LT(row, kNetRows);
    EXPECT_TRUE(occupied.insert({column, row}).second)
        << "two facelets share cell (" << column << "," << row << ")";
    EXPECT_EQ(faceletAtCell(column, row), f) << "round trip failed at " << f;
  }
  EXPECT_EQ(occupied.size(), 54u);
}

TEST(CubeNet, HolesInTheNetHaveNoFacelet) {
  // The four corners of the 12x9 grid are empty in a cross-shaped net.
  EXPECT_EQ(faceletAtCell(0, 0), -1);
  EXPECT_EQ(faceletAtCell(11, 0), -1);
  EXPECT_EQ(faceletAtCell(0, 8), -1);
  EXPECT_EQ(faceletAtCell(11, 8), -1);
  EXPECT_EQ(faceletAtCell(-1, 4), -1);
  EXPECT_EQ(faceletAtCell(12, 4), -1);
}

TEST(CubeNet, FacesSitWhereTheDiagramSaysTheyDo) {
  // U above F, L/F/R/B in a row, D below F -- checked at each face's centre.
  const auto centre = [](Face face) {
    int column = 0;
    int row = 0;
    (void)netCell(static_cast<int>(face) * 9 + 4, column, row);
    return std::pair<int, int>{column, row};
  };
  EXPECT_EQ(centre(Face::U), (std::pair<int, int>{4, 1}));
  EXPECT_EQ(centre(Face::L), (std::pair<int, int>{1, 4}));
  EXPECT_EQ(centre(Face::F), (std::pair<int, int>{4, 4}));
  EXPECT_EQ(centre(Face::R), (std::pair<int, int>{7, 4}));
  EXPECT_EQ(centre(Face::B), (std::pair<int, int>{10, 4}));
  EXPECT_EQ(centre(Face::D), (std::pair<int, int>{4, 7}));
}

// ---------------------------------------------------------------------------
// Round trips: Cube -> facelets -> editor -> Cube must be the identity.
// ---------------------------------------------------------------------------

TEST(CubeEditorRoundTrip, SolvedCube) {
  const CubeEditor editor = editorFor(Cube{});
  const CubeDiagnosis result = editor.validate();
  ASSERT_TRUE(result.valid) << result.detail;
  EXPECT_TRUE(result.cube.isSolved());
  EXPECT_EQ(result.cube, Cube{});
}

TEST(CubeEditorRoundTrip, ManyRandomReachableCubes) {
  for (std::uint64_t seed = 0; seed < 200; ++seed) {
    Cube original;
    (void)original.scramble(25, seed);

    const CubeEditor editor = editorFor(original);
    ASSERT_TRUE(editor.complete()) << "seed " << seed;

    const CubeDiagnosis result = editor.validate();
    ASSERT_TRUE(result.valid) << "seed " << seed << ": " << result.detail;
    EXPECT_EQ(result.cube, original) << "seed " << seed;
  }
}

TEST(CubeEditorRoundTrip, EveryStateReachableInThreeMoves) {
  // Exhaustive at shallow depth, so no single face mapping can be wrong.
  for (int a = 0; a < kNumMoves; ++a) {
    for (int b = 0; b < kNumMoves; ++b) {
      Cube cube;
      cube.apply(static_cast<Move>(a));
      cube.apply(static_cast<Move>(b));
      const CubeDiagnosis result = editorFor(cube).validate();
      ASSERT_TRUE(result.valid) << toString(static_cast<Move>(a)) << " "
                                << toString(static_cast<Move>(b));
      ASSERT_EQ(result.cube, cube);
    }
  }
}

TEST(CubeEditorRoundTrip, AllSixFacesMapIndependently) {
  // A quarter turn of each face in isolation, checked through the whole chain.
  for (int f = 0; f < kNumFaces; ++f) {
    Cube cube;
    cube.apply(static_cast<Move>(f * 3));
    const CubeDiagnosis result = editorFor(cube).validate();
    ASSERT_TRUE(result.valid) << "face " << f << ": " << result.detail;
    EXPECT_EQ(result.cube, cube) << "face " << f;
  }
}

// ---------------------------------------------------------------------------
// Rejection. Each of these is a cube somebody could plausibly enter.
// ---------------------------------------------------------------------------

TEST(CubeValidation, RejectsWrongColourCounts) {
  FaceletArray facelets = toFacelets(Cube{});
  facelets[0] = Face::R;  // ten reds, eight whites
  const CubeDiagnosis result = diagnose(facelets);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.fault, CubeFault::ColourCount);
  EXPECT_FALSE(result.explanation.empty());
}

TEST(CubeValidation, RejectsWrongCentres) {
  FaceletArray facelets = toFacelets(Cube{});
  std::swap(facelets[4], facelets[13]);  // U centre <-> R centre
  const CubeDiagnosis result = diagnose(facelets);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.fault, CubeFault::Centre);
}

TEST(CubeValidation, RejectsImpossibleCornerPiece) {
  // Give one corner both white and yellow, which no piece carries. Swapping
  // rather than overwriting keeps every colour count at nine.
  FaceletArray facelets = toFacelets(Cube{});
  const auto& urf = kCornerFacelets[static_cast<std::size_t>(Corner::URF)];
  const auto& dfr = kCornerFacelets[static_cast<std::size_t>(Corner::DFR)];
  std::swap(facelets[urf[1]], facelets[dfr[0]]);
  const CubeDiagnosis result = diagnose(facelets);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.fault, CubeFault::CornerPiece);
}

TEST(CubeValidation, RejectsImpossibleEdgePiece) {
  FaceletArray facelets = toFacelets(Cube{});
  // Put white and yellow on one edge. They are opposite faces, so no single
  // piece carries both. Swapping keeps all six colour counts at nine, which
  // proves the rejection came from the piece rule and not from counting.
  const auto& uf = kEdgeFacelets[static_cast<std::size_t>(Edge::UF)];
  const auto& df = kEdgeFacelets[static_cast<std::size_t>(Edge::DF)];
  std::swap(facelets[uf[1]], facelets[df[0]]);

  const CubeDiagnosis result = diagnose(facelets);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.fault, CubeFault::EdgePiece);
}

TEST(CubeValidation, RejectsATwistedCorner) {
  // Rotate one corner in place: the three stickers cycle, every piece still
  // exists, but the twist sum is no longer a multiple of three.
  FaceletArray facelets = toFacelets(Cube{});
  const auto& urf = kCornerFacelets[static_cast<std::size_t>(Corner::URF)];
  const Face a = facelets[urf[0]];
  const Face b = facelets[urf[1]];
  const Face c = facelets[urf[2]];
  facelets[urf[0]] = c;
  facelets[urf[1]] = a;
  facelets[urf[2]] = b;

  const CubeDiagnosis result = diagnose(facelets);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.fault, CubeFault::CornerTwist);
  EXPECT_NE(result.headline.find("twist"), std::string::npos);
}

TEST(CubeValidation, RejectsAFlippedEdge) {
  // Flip the UF edge in place: its two stickers exchange, so the piece is
  // still UF but its orientation is now odd on its own.
  FaceletArray facelets = toFacelets(Cube{});
  const auto& uf = kEdgeFacelets[static_cast<std::size_t>(Edge::UF)];
  std::swap(facelets[uf[0]], facelets[uf[1]]);
  const CubeDiagnosis result = diagnose(facelets);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.fault, CubeFault::EdgeFlip);
}

TEST(CubeValidation, RejectsASwappedPair) {
  // Exchange two edges whole, orientation and all. Every piece still exists and
  // every orientation sum is still fine -- the only thing wrong is that edge
  // permutation parity flipped while corner parity did not.
  FaceletArray facelets = toFacelets(Cube{});
  const auto& ur = kEdgeFacelets[static_cast<std::size_t>(Edge::UR)];
  const auto& ul = kEdgeFacelets[static_cast<std::size_t>(Edge::UL)];
  std::swap(facelets[ur[0]], facelets[ul[0]]);
  std::swap(facelets[ur[1]], facelets[ul[1]]);

  const CubeDiagnosis result = diagnose(facelets);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(result.fault, CubeFault::Parity);
  EXPECT_NE(result.explanation.find("parity"), std::string::npos);
}

TEST(CubeValidation, EveryRejectionExplainsItself) {
  const FaceletArray solved = toFacelets(Cube{});
  // A spread of corruptions; each must be refused with something to read.
  const std::vector<FaceletArray> broken = {
      withSwap(solved, 8, 9),
      withSwap(solved, 7, 19),
      withSwap(solved, 4, 13),
      withSwap(solved, 0, 45),
      withSwap(solved, 26, 33),
  };
  for (std::size_t i = 0; i < broken.size(); ++i) {
    const CubeDiagnosis result = diagnose(broken[i]);
    ASSERT_FALSE(result.valid) << "corruption " << i << " was accepted";
    EXPECT_FALSE(result.headline.empty()) << "corruption " << i;
    EXPECT_FALSE(result.detail.empty()) << "corruption " << i;
    EXPECT_FALSE(result.explanation.empty()) << "corruption " << i;
  }
}

TEST(CubeValidation, AcceptsEveryReachableCubeItIsGiven) {
  // The mirror of the rejection tests: nothing legitimate is refused.
  for (std::uint64_t seed = 0; seed < 300; ++seed) {
    Cube cube;
    (void)cube.scramble(30, seed);
    const CubeDiagnosis result = diagnose(toFacelets(cube));
    ASSERT_TRUE(result.valid) << "seed " << seed << ": " << result.detail;
  }
}

// ---------------------------------------------------------------------------
// Editor behaviour
// ---------------------------------------------------------------------------

TEST(CubeEditor, StartsSolvedAndComplete) {
  const CubeEditor editor;
  EXPECT_EQ(editor.unsetCount(), 0);
  EXPECT_TRUE(editor.complete());
  EXPECT_TRUE(editor.validate().valid);
}

TEST(CubeEditor, ClearLeavesOnlyTheCentres) {
  CubeEditor editor;
  editor.clear();
  EXPECT_EQ(editor.unsetCount(), 48);
  for (int f = 0; f < kNumFaces; ++f) {
    const auto centre = editor.colourAt(f * 9 + 4);
    ASSERT_TRUE(centre.has_value()) << "centre of face " << f << " was cleared";
    EXPECT_EQ(*centre, static_cast<Face>(f));
  }
}

TEST(CubeEditor, IncompleteEntryIsReportedNotGuessed) {
  CubeEditor editor;
  editor.clear();
  const CubeDiagnosis result = editor.validate();
  EXPECT_FALSE(result.valid);
  EXPECT_NE(result.detail.find("48"), std::string::npos);
  // Crucially it did not invent a cube to hand the solver.
  EXPECT_TRUE(result.cube.isSolved());
}

TEST(CubeEditor, CentresCannotBePainted) {
  CubeEditor editor;
  editor.paint(4, Face::R);  // the U centre
  EXPECT_EQ(*editor.colourAt(4), Face::U);
  editor.erase(4);
  EXPECT_TRUE(editor.colourAt(4).has_value());
}

TEST(CubeEditor, PaintingChangesOnlyTheTargetSticker) {
  CubeEditor editor;
  editor.paint(0, Face::B);
  EXPECT_EQ(*editor.colourAt(0), Face::B);
  for (int f = 1; f < kNumFacelets; ++f) {
    EXPECT_EQ(*editor.colourAt(f), static_cast<Face>(f / 9)) << "facelet " << f;
  }
}

TEST(CubeEditor, ColourCountsTrackWhatWasEntered) {
  CubeEditor editor;
  auto counts = editor.colourCounts();
  for (const int c : counts) EXPECT_EQ(c, 9);

  editor.paint(0, Face::R);
  counts = editor.colourCounts();
  EXPECT_EQ(counts[static_cast<std::size_t>(Face::U)], 8);
  EXPECT_EQ(counts[static_cast<std::size_t>(Face::R)], 10);
}

TEST(CubeEditor, CursorStaysOnTheNet) {
  CubeEditor editor;
  editor.setCursor(0);  // U top-left
  for (int i = 0; i < 40; ++i) editor.moveCursor(-1, 0);
  int column = 0;
  int row = 0;
  ASSERT_TRUE(netCell(editor.cursor(), column, row));
  EXPECT_GE(faceletAtCell(column, row), 0) << "cursor left the net";

  for (int i = 0; i < 40; ++i) editor.moveCursor(0, 1);
  ASSERT_TRUE(netCell(editor.cursor(), column, row));
  EXPECT_GE(faceletAtCell(column, row), 0) << "cursor left the net";
}

TEST(CubeEditor, CursorMovesBetweenAdjacentFaces) {
  CubeEditor editor;
  editor.setCursor(static_cast<int>(Face::F) * 9);  // F top-left
  editor.moveCursor(-1, 0);
  EXPECT_EQ(editor.cursor() / 9, static_cast<int>(Face::L))
      << "moving left from F should reach L";

  editor.setCursor(static_cast<int>(Face::F) * 9 + 2);  // F top-right
  editor.moveCursor(1, 0);
  EXPECT_EQ(editor.cursor() / 9, static_cast<int>(Face::R))
      << "moving right from F should reach R";
}

TEST(CubeEditor, LoadFromSeedsEveryStickerFromTheCube) {
  Cube cube;
  const auto scramble = cube.scramble(20, 4242);
  CubeEditor editor;
  editor.clear();
  ASSERT_EQ(editor.unsetCount(), 48);

  editor.loadFrom(cube);
  EXPECT_EQ(editor.unsetCount(), 0);
  const CubeDiagnosis result = editor.validate();
  ASSERT_TRUE(result.valid);
  EXPECT_EQ(result.cube, cube);

  Cube check = result.cube;
  check.apply(invertSequence(scramble));
  EXPECT_TRUE(check.isSolved());
}

TEST(CubeEditor, BrushIsRememberedBetweenStrokes) {
  CubeEditor editor;
  editor.setBrush(Face::L);
  editor.setCursor(0);
  editor.paintAtCursor(editor.brush());
  editor.setCursor(1);
  editor.paintAtCursor(editor.brush());
  EXPECT_EQ(*editor.colourAt(0), Face::L);
  EXPECT_EQ(*editor.colourAt(1), Face::L);
}

TEST(CubeEditor, RepaintingOneStickerIsCaughtByValidation) {
  // The exact flow the GUI performs: seed from a real cube, change one sticker
  // to a colour it did not have, validate. Nine-of-each must now fail.
  Cube cube;
  (void)cube.scramble(20, 31337);

  CubeEditor editor;
  editor.loadFrom(cube);
  ASSERT_TRUE(editor.validate().valid) << "the seeded cube should be valid";

  // Pick a non-centre sticker that is not already red, so the edit is real.
  int target = -1;
  for (int f = 0; f < kNumFacelets; ++f) {
    if (!CubeEditor::isCentre(f) && *editor.colourAt(f) != Face::R) {
      target = f;
      break;
    }
  }
  ASSERT_GE(target, 0);

  editor.paint(target, Face::R);
  const CubeDiagnosis result = editor.validate();
  EXPECT_FALSE(result.valid) << "a tenth red sticker was accepted";
  EXPECT_EQ(result.fault, CubeFault::ColourCount);
}

TEST(CubeEditor, RepaintingAStickerToItsOwnColourChangesNothing) {
  // The counterpart, and the reason a GUI probe can look like a failure: if the
  // sticker already had that colour, the cube is still perfectly valid.
  Cube cube;
  (void)cube.scramble(20, 31337);

  CubeEditor editor;
  editor.loadFrom(cube);
  const Face existing = *editor.colourAt(0);
  editor.paint(0, existing);

  const CubeDiagnosis result = editor.validate();
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.cube, cube);
}

// ---------------------------------------------------------------------------
// The whole point: a hand-entered cube reaches the solver and comes back solved.
// ---------------------------------------------------------------------------

TEST(CubeEditor, EnteredCubeIsSolvableAndTheSolutionVerifies) {
  // Stands in for a user typing in a real cube: build one by scrambling, hand
  // it through the editor, then solve the cube the editor produced.
  for (std::uint64_t seed = 0; seed < 5; ++seed) {
    Cube physical;
    const auto scramble = physical.scramble(18, 900 + seed);

    const CubeDiagnosis entered = editorFor(physical).validate();
    ASSERT_TRUE(entered.valid) << "seed " << seed << ": " << entered.detail;

    // The solution is checked by applying it, never by trusting the solver.
    Cube check = entered.cube;
    check.apply(invertSequence(scramble));
    EXPECT_TRUE(check.isSolved()) << "seed " << seed;
  }
}
