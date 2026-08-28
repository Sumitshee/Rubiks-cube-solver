#include "core/Cube.h"
#include "core/Move.h"
#include "ui/CubeController.h"
#include "ui/CubeLayout.h"

#include <gtest/gtest.h>

#include <vector>

using namespace rubik;
using namespace rubik::ui;

namespace {

/// Runs the controller until nothing is queued or animating.
/// `step` is a plausible frame time; the guard stops a runaway loop from
/// hanging the suite.
void settle(CubeController& controller, double step = 1.0 / 60.0,
            int maxFrames = 20000) {
  int frames = 0;
  while (controller.busy() && frames < maxFrames) {
    controller.update(step);
    ++frames;
  }
  ASSERT_LT(frames, maxFrames) << "controller never settled";
}

}  // namespace

TEST(CubeController, StartsSolvedAndIdle) {
  CubeController c;
  EXPECT_TRUE(c.cube().isSolved());
  EXPECT_FALSE(c.busy());
  EXPECT_FALSE(c.animation().active);
}

// ---------------------------------------------------------------------------
// The central invariant: the model changes only when an animation completes.
// ---------------------------------------------------------------------------

TEST(CubeController, DoesNotTouchTheCubeUntilTheAnimationFinishes) {
  CubeController c;
  c.setSpeed(2.0);  // half a second per quarter turn
  c.enqueue(Move::R);

  c.update(0.05);
  ASSERT_TRUE(c.animation().active);
  EXPECT_TRUE(c.cube().isSolved())
      << "the cube changed part-way through the animation";

  // Part-way through, the angle is between zero and the move's full angle.
  const float angle = c.animation().angleRadians;
  EXPECT_LT(angle, 0.0f) << "a clockwise turn rotates negatively";
  EXPECT_GT(angle, moveAngleRadians(Move::R));

  settle(c);
  Cube expected;
  expected.apply(Move::R);
  EXPECT_EQ(c.cube(), expected);
  EXPECT_FALSE(c.animation().active);
}

TEST(CubeController, AnimatedMovesReachTheSameStateAsDirectApplication) {
  const std::vector<Move> moves =
      parseSequence("R U R' F2 L B' D2 L' U2 F D' B R2");

  CubeController c;
  c.setSpeed(20.0);
  c.enqueue(moves);
  settle(c);

  Cube expected;
  expected.apply(moves);
  EXPECT_EQ(c.cube(), expected);
  EXPECT_TRUE(c.cube() == expected);
}

TEST(CubeController, EveryMoveAnimatesToTheCorrectState) {
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    CubeController c;
    c.setSpeed(20.0);
    c.enqueue(m);
    settle(c);

    Cube expected;
    expected.apply(m);
    EXPECT_EQ(c.cube(), expected) << toString(m);
  }
}

TEST(CubeController, CommitsAtMostOneMovePerUpdate) {
  // A stalled frame must not flash several turns past at once.
  CubeController c;
  c.setSpeed(4.0);
  c.enqueue(parseSequence("R U F"));

  c.update(100.0);  // an absurdly long frame
  Cube afterOne;
  afterOne.apply(Move::R);
  EXPECT_EQ(c.cube(), afterOne) << "more than one move committed in a frame";
}

// ---------------------------------------------------------------------------
// Queueing, cancellation and reset
// ---------------------------------------------------------------------------

TEST(CubeController, RapidInputIsQueuedNotDropped) {
  CubeController c;
  c.setSpeed(20.0);
  const auto moves = parseSequence("R U R' U' R U R' U'");
  for (const Move m : moves) c.enqueue(m);
  EXPECT_EQ(c.queuedMoves(), moves.size());

  settle(c);
  Cube expected;
  expected.apply(moves);
  EXPECT_EQ(c.cube(), expected);
}

TEST(CubeController, CancellingLeavesTheCubeOnAWholeMove) {
  CubeController c;
  c.setSpeed(1.0);
  c.enqueue(parseSequence("R U F L"));

  c.update(0.2);  // part-way into R
  ASSERT_TRUE(c.animation().active);
  c.cancelPending();

  EXPECT_FALSE(c.busy());
  EXPECT_FALSE(c.animation().active);
  EXPECT_TRUE(c.cube().isSolved()) << "an abandoned turn must not half-apply";
  EXPECT_NO_THROW(c.cube().validate());
}

TEST(CubeController, ResetReturnsToSolvedFromAnyState) {
  CubeController c;
  c.setSpeed(20.0);
  (void)c.scramble(25, 4242);
  settle(c);
  ASSERT_FALSE(c.cube().isSolved());

  c.reset();
  EXPECT_TRUE(c.cube().isSolved());
  EXPECT_FALSE(c.busy());
  EXPECT_TRUE(c.solution().empty());
}

TEST(CubeController, ScrambleMidAnimationReplacesWhatWasPending) {
  CubeController c;
  c.setSpeed(1.0);
  c.enqueue(parseSequence("R U F L B D"));
  c.update(0.1);
  ASSERT_TRUE(c.animation().active);

  const auto scramble = c.scramble(20, 99);
  EXPECT_EQ(c.queuedMoves(), scramble.size())
      << "the old queue should have been dropped";
  settle(c);

  // The scramble is applied to whatever the cube was, and it was still solved
  // because the interrupted turn never committed.
  Cube expected;
  expected.apply(scramble);
  EXPECT_EQ(c.cube(), expected);
}

TEST(CubeController, ScrambleUsesTheCoreGeneratorAndIsReproducible) {
  CubeController a;
  CubeController b;
  EXPECT_EQ(a.scramble(20, 777), b.scramble(20, 777));

  Cube reference;
  const auto direct = reference.scramble(20, 777);
  EXPECT_EQ(a.lastScramble(), direct)
      << "the GUI must not have its own scramble implementation";
}

// ---------------------------------------------------------------------------
// Solution playback
// ---------------------------------------------------------------------------

TEST(CubeController, PlayingASolutionEndsSolved) {
  CubeController c;
  c.setSpeed(20.0);
  const auto scramble = c.scramble(18, 31337);
  settle(c);
  ASSERT_FALSE(c.cube().isSolved());

  c.loadSolution(invertSequence(scramble));
  c.play();
  EXPECT_TRUE(c.playing());

  int frames = 0;
  while ((c.playing() || c.busy()) && frames < 20000) {
    c.update(1.0 / 60.0);
    ++frames;
  }
  ASSERT_LT(frames, 20000);

  EXPECT_TRUE(c.cube().isSolved()) << "playback did not solve the cube";
  EXPECT_TRUE(c.solutionComplete());
  EXPECT_FALSE(c.playing()) << "playback should stop at the end";
  EXPECT_EQ(c.solutionProgress(), static_cast<int>(scramble.size()));
}

TEST(CubeController, PauseStopsProgressAndPlayResumes) {
  CubeController c;
  c.setSpeed(20.0);
  const auto scramble = c.scramble(12, 5150);
  settle(c);

  c.loadSolution(invertSequence(scramble));
  c.play();
  for (int i = 0; i < 30; ++i) c.update(1.0 / 60.0);

  const int progress = c.solutionProgress();
  ASSERT_GT(progress, 0);
  ASSERT_LT(progress, static_cast<int>(scramble.size()));

  c.pause();
  settle(c);  // let the turn in flight finish
  const int afterPause = c.solutionProgress();
  for (int i = 0; i < 120; ++i) c.update(1.0 / 60.0);
  EXPECT_EQ(c.solutionProgress(), afterPause) << "paused playback advanced";

  c.play();
  int frames = 0;
  while ((c.playing() || c.busy()) && frames < 20000) {
    c.update(1.0 / 60.0);
    ++frames;
  }
  EXPECT_TRUE(c.cube().isSolved());
}

TEST(CubeController, StepForwardAdvancesExactlyOneMove) {
  CubeController c;
  c.setSpeed(20.0);
  const auto scramble = c.scramble(10, 606);
  settle(c);

  const auto solution = invertSequence(scramble);
  c.loadSolution(solution);

  for (std::size_t i = 0; i < solution.size(); ++i) {
    EXPECT_EQ(c.solutionProgress(), static_cast<int>(i));
    c.stepForward();
    settle(c);
    EXPECT_EQ(c.solutionProgress(), static_cast<int>(i) + 1);
    EXPECT_FALSE(c.playing()) << "stepping should not start playback";
  }
  EXPECT_TRUE(c.cube().isSolved());
  EXPECT_TRUE(c.solutionComplete());
}

TEST(CubeController, ManualMovesDoNotCountAsSolutionProgress) {
  CubeController c;
  c.setSpeed(20.0);
  c.loadSolution(parseSequence("R U F"));

  c.enqueue(Move::L);  // a hand-made move, not from the solution
  settle(c);
  EXPECT_EQ(c.solutionProgress(), 0);
}

TEST(CubeController, SpeedIsClampedToSomethingWatchable) {
  CubeController c;
  c.setSpeed(1000.0);
  EXPECT_LE(c.speed(), 30.0);
  c.setSpeed(-5.0);
  EXPECT_GE(c.speed(), 0.5);
}

TEST(CubeController, StateStaysValidThroughoutALongSession) {
  // Scramble, solve by inversion, reset, repeat -- checking the invariant at
  // every frame rather than only at the end.
  CubeController c;
  c.setSpeed(25.0);
  for (std::uint64_t round = 0; round < 5; ++round) {
    const auto scramble = c.scramble(20, round);
    while (c.busy()) {
      c.update(1.0 / 60.0);
      ASSERT_NO_THROW(c.cube().validate());
    }
    c.loadSolution(invertSequence(scramble));
    c.play();
    while (c.playing() || c.busy()) {
      c.update(1.0 / 60.0);
      ASSERT_NO_THROW(c.cube().validate());
    }
    ASSERT_TRUE(c.cube().isSolved()) << "round " << round;
    c.reset();
  }
}
