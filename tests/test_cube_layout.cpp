#include "core/Cube.h"
#include "core/Facelets.h"
#include "core/Move.h"
#include "ui/CubeLayout.h"

#include <gtest/gtest.h>

#include <array>
#include <set>

using namespace rubik;
using namespace rubik::ui;

namespace {

/// The 27 grid slots, keyed so they can be looked up.
bool isValidGrid(Vec3i p) {
  const auto ok = [](int v) { return v >= -1 && v <= 1; };
  return ok(p.x) && ok(p.y) && ok(p.z);
}

}  // namespace

TEST(CubeLayout, CoversTwentySevenDistinctSlots) {
  const auto& positions = cubiePositions();
  EXPECT_EQ(positions.size(), 27u);

  std::set<std::tuple<int, int, int>> seen;
  for (const Vec3i p : positions) {
    EXPECT_TRUE(isValidGrid(p));
    seen.insert({p.x, p.y, p.z});
  }
  EXPECT_EQ(seen.size(), 27u);
}

TEST(CubeLayout, EveryFaceletIsClaimedExactlyOnce) {
  // 54 stickers, 26 visible cubies. If the mapping double-claimed or missed
  // one, the rendered cube would show a wrong or missing colour.
  std::array<int, kNumFacelets> claims{};
  for (const Vec3i p : cubiePositions()) {
    for (int f = 0; f < kNumFaces; ++f) {
      const int index = faceletIndex(p, static_cast<Face>(f));
      if (index < 0) continue;
      ASSERT_GE(index, 0);
      ASSERT_LT(index, kNumFacelets);
      claims[static_cast<std::size_t>(index)]++;
    }
  }
  for (std::size_t i = 0; i < claims.size(); ++i) {
    EXPECT_EQ(claims[i], 1) << "facelet " << i << " claimed " << claims[i] << " times";
  }
}

TEST(CubeLayout, CentrePiecesSitAtTheFaceCentres) {
  for (int f = 0; f < kNumFaces; ++f) {
    const Face face = static_cast<Face>(f);
    const Vec3i normal = faceNormal(face);
    EXPECT_EQ(faceletIndex(normal, face), f * 9 + 4)
        << "centre of face " << f;
  }
}

TEST(CubeLayout, SolvedCubeShowsEachFaceItsOwnColour) {
  const FaceletArray facelets = toFacelets(Cube{});
  for (const Vec3i p : cubiePositions()) {
    for (int f = 0; f < kNumFaces; ++f) {
      const Face face = static_cast<Face>(f);
      if (faceletIndex(p, face) < 0) continue;
      EXPECT_EQ(stickerColour(facelets, p, face), face)
          << "cubie (" << p.x << "," << p.y << "," << p.z << ") face " << f;
    }
  }
}

TEST(CubeLayout, KnownCornerFaceletsMatchTheSolverNumbering) {
  // Spot-checks against the tables in Facelets.cpp, which the renderer has to
  // agree with exactly.
  EXPECT_EQ(faceletIndex({1, 1, 1}, Face::U), 8);    // URF -> U9
  EXPECT_EQ(faceletIndex({1, 1, 1}, Face::R), 9);    // URF -> R1
  EXPECT_EQ(faceletIndex({1, 1, 1}, Face::F), 20);   // URF -> F3
  EXPECT_EQ(faceletIndex({1, -1, 1}, Face::D), 29);  // DFR -> D3
  EXPECT_EQ(faceletIndex({1, -1, 1}, Face::F), 26);  // DFR -> F9
  EXPECT_EQ(faceletIndex({1, -1, 1}, Face::R), 15);  // DFR -> R7
  EXPECT_EQ(faceletIndex({-1, 1, -1}, Face::U), 0);  // ULB -> U1
  EXPECT_EQ(faceletIndex({-1, 1, -1}, Face::L), 36); // ULB -> L1
  EXPECT_EQ(faceletIndex({-1, 1, -1}, Face::B), 47); // ULB -> B3
}

TEST(CubeLayout, InteriorFacesHaveNoSticker) {
  // The hidden core, and the inward sides of every other cubie.
  EXPECT_EQ(faceletIndex({0, 0, 0}, Face::U), -1);
  EXPECT_EQ(faceletIndex({0, 0, 0}, Face::R), -1);
  // A corner shows exactly three stickers, an edge two, a centre one.
  const auto visibleFaces = [](Vec3i p) {
    int count = 0;
    for (int f = 0; f < kNumFaces; ++f) {
      if (faceletIndex(p, static_cast<Face>(f)) >= 0) ++count;
    }
    return count;
  };
  EXPECT_EQ(visibleFaces({1, 1, 1}), 3);
  EXPECT_EQ(visibleFaces({1, 1, 0}), 2);
  EXPECT_EQ(visibleFaces({1, 0, 0}), 1);
  EXPECT_EQ(visibleFaces({0, 0, 0}), 0);
}

TEST(CubeLayout, LayerMembershipMatchesTheFace) {
  for (int f = 0; f < kNumFaces; ++f) {
    const Face face = static_cast<Face>(f);
    int count = 0;
    for (const Vec3i p : cubiePositions()) {
      if (inLayer(p, face)) ++count;
    }
    EXPECT_EQ(count, 9) << "face " << f << " should turn nine cubies";
  }
}

// ---------------------------------------------------------------------------
// The important one: the geometric rotation must agree with the solver.
// ---------------------------------------------------------------------------

TEST(CubeLayout, ClockwiseRotationPermutesTheGridLikeTheSolverMovesCubies) {
  // For each face, applying the geometric quarter turn to every grid slot must
  // reproduce the permutation the Cube's own move tables apply to its cubies.
  // If these disagreed, the animation would spin the layer the wrong way and
  // the cube would appear to jump when the move committed.
  for (int f = 0; f < kNumFaces; ++f) {
    const Face face = static_cast<Face>(f);
    const Move move = static_cast<Move>(f * 3);  // the clockwise quarter turn

    Cube turned;
    turned.apply(move);
    const FaceletArray after = toFacelets(turned);
    const FaceletArray solved = toFacelets(Cube{});

    for (const Vec3i from : cubiePositions()) {
      if (!inLayer(from, face)) continue;
      const Vec3i to = rotateClockwise(from, face);
      ASSERT_TRUE(isValidGrid(to)) << "rotation left the grid";
      ASSERT_TRUE(inLayer(to, face)) << "rotation left the layer";

      // The colour that lands on each side of the destination slot must be the
      // colour that side of the source slot carried before the turn.
      for (int d = 0; d < kNumFaces; ++d) {
        const Face side = static_cast<Face>(d);
        const int sourceIndex = faceletIndex(from, side);
        if (sourceIndex < 0) continue;

        const Face rotatedSide = [&] {
          // The sticker's own normal is carried around by the same rotation.
          const Vec3i n = rotateClockwise(faceNormal(side), face);
          for (int k = 0; k < kNumFaces; ++k) {
            if (faceNormal(static_cast<Face>(k)) == n) return static_cast<Face>(k);
          }
          return side;
        }();

        const int destIndex = faceletIndex(to, rotatedSide);
        ASSERT_GE(destIndex, 0) << "destination side is interior";
        EXPECT_EQ(after[static_cast<std::size_t>(destIndex)],
                  solved[static_cast<std::size_t>(sourceIndex)])
            << "face " << f << ": sticker did not land where the geometry says";
      }
    }
  }
}

TEST(CubeLayout, FourClockwiseRotationsReturnEverySlot) {
  for (int f = 0; f < kNumFaces; ++f) {
    const Face face = static_cast<Face>(f);
    for (const Vec3i p : cubiePositions()) {
      Vec3i q = p;
      for (int i = 0; i < 4; ++i) q = rotateClockwise(q, face);
      EXPECT_EQ(q, p) << "face " << f << " rotation does not have order four";
    }
  }
}

TEST(CubeLayout, MoveAnglesMatchTheirTurnCounts) {
  constexpr float kPi = 3.14159265358979323846f;
  // Clockwise is negative about the outward normal, under the right-hand rule.
  EXPECT_NEAR(moveAngleRadians(Move::U), -kPi / 2.0f, 1e-5f);
  EXPECT_NEAR(moveAngleRadians(Move::U2), -kPi, 1e-5f);
  EXPECT_NEAR(moveAngleRadians(Move::Up), kPi / 2.0f, 1e-5f);
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    EXPECT_NEAR(std::abs(moveAngleRadians(m)),
                turns(m) == 1 ? kPi : kPi / 2.0f, 1e-5f)
        << toString(m);
  }
}
