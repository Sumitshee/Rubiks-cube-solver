#include "core/Cube.h"
#include "core/Move.h"
#include "ui/CubeController.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

using namespace rubik;
using namespace rubik::ui;

namespace {

/// Runs the controller until nothing is queued or animating.
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

// ---------------------------------------------------------------------------
// Step-by-step playback.
//
// The user is holding a physical cube and copying each move onto it, so the
// counter must never run ahead of what has actually been performed, and the
// cube must never be left part way through a turn.
// ---------------------------------------------------------------------------

TEST(CubePlayback, LoadingASolutionDoesNotMoveTheCube) {
  CubeController c;
  c.setSpeed(20.0);
  const auto scramble = c.scramble(15, 2024);
  settle(c);
  const Cube entered = c.cube();

  c.loadSolution(invertSequence(scramble));
  EXPECT_EQ(c.cube(), entered) << "loading a solution changed the cube";
  EXPECT_FALSE(c.playing()) << "loading a solution started playback";
  EXPECT_EQ(c.solutionProgress(), 0);
  EXPECT_TRUE(c.hasSolution());

  // And it stays put until something is actually asked for.
  for (int i = 0; i < 120; ++i) c.update(1.0 / 60.0);
  EXPECT_EQ(c.cube(), entered);
  EXPECT_EQ(c.solutionProgress(), 0);
}

TEST(CubePlayback, NextPerformsExactlyOneMove) {
  CubeController c;
  c.setSpeed(20.0);
  const auto scramble = c.scramble(12, 77);
  settle(c);

  const auto solution = invertSequence(scramble);
  c.loadSolution(solution);

  Cube expected = c.cube();
  for (std::size_t i = 0; i < solution.size(); ++i) {
    c.stepForward();
    settle(c);
    expected.apply(solution[i]);
    EXPECT_EQ(c.cube(), expected) << "after step " << i + 1;
    EXPECT_EQ(c.solutionProgress(), static_cast<int>(i) + 1);
    EXPECT_FALSE(c.playing()) << "stepping started playback";
  }
  EXPECT_TRUE(c.cube().isSolved());
  EXPECT_TRUE(c.solutionComplete());
}

TEST(CubePlayback, NextIsIgnoredWhileAMoveIsStillAnimating) {
  CubeController c;
  c.setSpeed(1.0);  // one second per quarter turn
  const auto scramble = c.scramble(10, 5);
  settle(c);
  c.loadSolution(invertSequence(scramble));

  c.stepForward();
  c.update(0.1);
  ASSERT_TRUE(c.animation().active);

  // Hammering the key must not queue a second move behind the first.
  for (int i = 0; i < 10; ++i) c.stepForward();
  EXPECT_EQ(c.queuedMoves(), 0u);

  settle(c);
  EXPECT_EQ(c.solutionProgress(), 1) << "more than one move was performed";
}

TEST(CubePlayback, PreviousUndoesTheMoveJustPerformed) {
  CubeController c;
  c.setSpeed(20.0);
  const auto scramble = c.scramble(14, 909);
  settle(c);
  const Cube entered = c.cube();

  const auto solution = invertSequence(scramble);
  c.loadSolution(solution);

  c.stepForward();
  settle(c);
  Cube afterOne = entered;
  afterOne.apply(solution[0]);
  ASSERT_EQ(c.cube(), afterOne);

  c.stepBackward();
  settle(c);
  EXPECT_EQ(c.cube(), entered) << "previous did not restore the earlier state";
  EXPECT_EQ(c.solutionProgress(), 0);
}

TEST(CubePlayback, PreviousAtTheStartDoesNothing) {
  CubeController c;
  c.setSpeed(20.0);
  const auto scramble = c.scramble(8, 31);
  settle(c);
  const Cube entered = c.cube();
  c.loadSolution(invertSequence(scramble));

  for (int i = 0; i < 5; ++i) {
    c.stepBackward();
    settle(c);
  }
  EXPECT_EQ(c.cube(), entered);
  EXPECT_EQ(c.solutionProgress(), 0);
}

TEST(CubePlayback, ForwardAndBackAgreeAtEveryPosition) {
  // Walk in, walk out, walk in again: the state at index k must be the same
  // however it was reached.
  CubeController c;
  c.setSpeed(25.0);
  const auto scramble = c.scramble(12, 4242);
  settle(c);

  const auto solution = invertSequence(scramble);
  c.loadSolution(solution);

  std::vector<Cube> expected;
  expected.push_back(c.cube());
  Cube walk = c.cube();
  for (const Move m : solution) {
    walk.apply(m);
    expected.push_back(walk);
  }

  for (int i = 0; i < 6; ++i) {
    c.stepForward();
    settle(c);
  }
  ASSERT_EQ(c.solutionProgress(), 6);
  ASSERT_EQ(c.cube(), expected[6]);

  for (int i = 0; i < 4; ++i) {
    c.stepBackward();
    settle(c);
    ASSERT_EQ(c.solutionProgress(), 6 - i - 1);
    ASSERT_EQ(c.cube(), expected[static_cast<std::size_t>(6 - i - 1)]);
  }

  for (int i = 0; i < 3; ++i) {
    c.stepForward();
    settle(c);
    ASSERT_EQ(c.cube(), expected[static_cast<std::size_t>(2 + i + 1)]);
  }
  EXPECT_EQ(c.solutionProgress(), 5);
}

TEST(CubePlayback, SteppingAllTheWayForwardSolvesTheCube) {
  for (std::uint64_t seed = 0; seed < 6; ++seed) {
    CubeController c;
    c.setSpeed(25.0);
    const auto scramble = c.scramble(20, seed);
    settle(c);
    c.loadSolution(invertSequence(scramble));

    int guard = 0;
    while (!c.solutionComplete() && guard++ < 100) {
      c.stepForward();
      settle(c);
      ASSERT_NO_THROW(c.cube().validate());
    }
    EXPECT_TRUE(c.cube().isSolved()) << "seed " << seed;
  }
}

TEST(CubePlayback, RestartRestoresTheCubeAsItWasEntered) {
  CubeController c;
  c.setSpeed(20.0);
  const auto scramble = c.scramble(18, 606);
  settle(c);
  const Cube entered = c.cube();

  c.loadSolution(invertSequence(scramble));
  for (int i = 0; i < 7; ++i) {
    c.stepForward();
    settle(c);
  }
  ASSERT_NE(c.cube(), entered);
  ASSERT_EQ(c.solutionProgress(), 7);

  c.restartSolution();
  EXPECT_EQ(c.cube(), entered) << "restart did not restore the entered cube";
  EXPECT_EQ(c.solutionProgress(), 0);
  EXPECT_FALSE(c.playing());
  EXPECT_FALSE(c.busy());
  EXPECT_TRUE(c.hasSolution()) << "restart threw the solution away";

  // And it can be walked through again from there.
  int guard = 0;
  while (!c.solutionComplete() && guard++ < 100) {
    c.stepForward();
    settle(c);
  }
  EXPECT_TRUE(c.cube().isSolved());
}

TEST(CubePlayback, RestartMidAnimationLeavesAWholeMove) {
  CubeController c;
  c.setSpeed(1.0);
  const auto scramble = c.scramble(10, 8080);
  settle(c);
  const Cube entered = c.cube();
  c.loadSolution(invertSequence(scramble));

  c.stepForward();
  c.update(0.3);
  ASSERT_TRUE(c.animation().active);

  c.restartSolution();
  EXPECT_EQ(c.cube(), entered);
  EXPECT_FALSE(c.animation().active) << "an animation survived the restart";
  EXPECT_NO_THROW(c.cube().validate());
}

TEST(CubePlayback, PauseStopsOnAWholeMoveAndPlayResumes) {
  CubeController c;
  c.setSpeed(3.0);
  const auto scramble = c.scramble(16, 1234);
  settle(c);
  c.loadSolution(invertSequence(scramble));

  c.play();
  for (int i = 0; i < 100; ++i) c.update(1.0 / 60.0);
  c.pause();
  settle(c);  // let the turn in flight finish

  const int progress = c.solutionProgress();
  ASSERT_GT(progress, 0);
  ASSERT_LT(progress, static_cast<int>(c.solution().size()));
  EXPECT_FALSE(c.animation().active);
  EXPECT_EQ(c.queuedMoves(), 0u);
  ASSERT_NO_THROW(c.cube().validate());

  // The cube sits exactly `progress` moves into the solution: applying what is
  // left of it from here must solve it.
  Cube expected = c.cube();
  for (int i = progress; i < static_cast<int>(c.solution().size()); ++i) {
    expected.apply(c.solution()[static_cast<std::size_t>(i)]);
  }
  EXPECT_TRUE(expected.isSolved()) << "the paused state is not on the solution path";

  c.play();
  int guard = 0;
  while ((c.playing() || c.busy()) && guard++ < 20000) c.update(1.0 / 60.0);
  EXPECT_TRUE(c.cube().isSolved());
}

TEST(CubePlayback, NextMoveReportsWhatTheUserShouldDo) {
  CubeController c;
  c.setSpeed(20.0);
  const auto solution = parseSequence("R U2 F L");
  c.loadSolution(solution);

  for (std::size_t i = 0; i < solution.size(); ++i) {
    const auto next = c.nextSolutionMove();
    ASSERT_TRUE(next.has_value()) << "no next move at " << i;
    EXPECT_EQ(*next, solution[i]);
    c.stepForward();
    settle(c);
  }
  EXPECT_FALSE(c.nextSolutionMove().has_value())
      << "a move was offered past the end";
}

TEST(CubePlayback, TheAnimatingMoveIsReportedOnlyWhileItRuns) {
  CubeController c;
  c.setSpeed(1.0);
  c.loadSolution(parseSequence("R U F"));

  EXPECT_FALSE(c.animatingSolutionMove().has_value());
  c.stepForward();
  c.update(0.1);
  const auto running = c.animatingSolutionMove();
  ASSERT_TRUE(running.has_value());
  EXPECT_EQ(*running, Move::R);

  settle(c);
  EXPECT_FALSE(c.animatingSolutionMove().has_value());

  // A turn made by hand is not a solution move, however it looks.
  c.enqueue(Move::D);
  c.update(0.1);
  EXPECT_FALSE(c.animatingSolutionMove().has_value());
}

TEST(CubePlayback, SpeedChangesDurationNotTheMoveSequence) {
  const auto solution = parseSequence("R U R2 U2 F2 L");

  const auto run = [&](double speed) {
    CubeController c;
    c.setSpeed(speed);
    c.loadSolution(solution);
    c.play();
    int frames = 0;
    while ((c.playing() || c.busy()) && frames < 200000) {
      c.update(1.0 / 60.0);
      ++frames;
    }
    return std::pair<int, Cube>{frames, c.cube()};
  };

  const auto slow = run(1.0);
  const auto fast = run(4.0);

  EXPECT_GT(slow.first, fast.first * 2)
      << "a quarter of the speed should take substantially longer";
  // The logical result is identical; only the time taken differs.
  EXPECT_EQ(slow.second, fast.second);

  Cube direct;
  direct.apply(solution);
  EXPECT_EQ(slow.second, direct);
}

TEST(CubePlayback, MoveDescriptionsMatchTheNotation) {
  EXPECT_STREQ(describeMove(Move::R).face, "RIGHT");
  EXPECT_STREQ(describeMove(Move::R).turn, "CLOCKWISE");
  EXPECT_STREQ(describeMove(Move::Rp).turn, "COUNTER-CLOCKWISE");
  EXPECT_STREQ(describeMove(Move::R2).turn, "180 DEGREES");
  EXPECT_STREQ(describeMove(Move::U).face, "UP");
  EXPECT_STREQ(describeMove(Move::D).face, "DOWN");
  EXPECT_STREQ(describeMove(Move::F).face, "FRONT");
  EXPECT_STREQ(describeMove(Move::B).face, "BACK");
  EXPECT_STREQ(describeMove(Move::L).face, "LEFT");

  // Every move has both halves filled in.
  for (int i = 0; i < kNumMoves; ++i) {
    const MoveDescription d = describeMove(static_cast<Move>(i));
    EXPECT_STRNE(d.face, "") << toString(static_cast<Move>(i));
    EXPECT_STRNE(d.turn, "") << toString(static_cast<Move>(i));
  }
}

TEST(CubePlayback, HandMadeTurnsDoNotDisturbTheCounter) {
  // Turning the cube by hand mid-solution must not make the counter lie.
  CubeController c;
  c.setSpeed(20.0);
  c.loadSolution(parseSequence("R U F"));

  c.stepForward();
  settle(c);
  ASSERT_EQ(c.solutionProgress(), 1);

  c.enqueue(Move::L);
  settle(c);
  EXPECT_EQ(c.solutionProgress(), 1) << "a hand-made turn moved the counter";

  c.stepBackward();
  settle(c);
  EXPECT_EQ(c.solutionProgress(), 0);
}

TEST(CubePlayback, TheInvariantHoldsAcrossAMixedSession) {
  // Step, go back, play, pause, restart, step again -- checking every frame
  // that the cube is a legal state and never half way through a move.
  CubeController c;
  c.setSpeed(6.0);
  const auto scramble = c.scramble(20, 555);
  settle(c);
  const Cube entered = c.cube();
  c.loadSolution(invertSequence(scramble));

  const auto tick = [&](int frames) {
    for (int i = 0; i < frames; ++i) {
      c.update(1.0 / 60.0);
      ASSERT_NO_THROW(c.cube().validate());
    }
  };

  for (int i = 0; i < 4; ++i) {
    c.stepForward();
    tick(40);
  }
  for (int i = 0; i < 2; ++i) {
    c.stepBackward();
    tick(40);
  }
  c.play();
  tick(120);
  c.pause();
  tick(40);
  c.restartSolution();
  ASSERT_EQ(c.cube(), entered);

  c.play();
  int guard = 0;
  while ((c.playing() || c.busy()) && guard++ < 20000) {
    c.update(1.0 / 60.0);
    ASSERT_NO_THROW(c.cube().validate());
  }
  EXPECT_TRUE(c.cube().isSolved());
  EXPECT_EQ(c.solutionProgress(), static_cast<int>(c.solution().size()));
}
