#include "core/Cube.h"
#include "core/Error.h"
#include "core/Facelets.h"
#include "core/Move.h"
#include "solver/Coordinate.h"
#include "solver/TwoPhaseSolver.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <random>
#include <memory>
#include <thread>
#include <vector>

using namespace rubik;

namespace {

/// Tables are expensive to build, so share one set across the whole binary.
std::shared_ptr<const TwoPhaseTables> sharedTables() {
  static const std::shared_ptr<const TwoPhaseTables> tables =
      std::make_shared<const TwoPhaseTables>();
  return tables;
}

const TwoPhaseSolver& solver() {
  static const TwoPhaseSolver instance(sharedTables());
  return instance;
}

/// Stop at the first solution found. Makes tests fast and deterministic --
/// the default options keep improving until a wall-clock limit, which would
/// make results depend on machine load.
TwoPhaseOptions fastOptions() {
  TwoPhaseOptions o;
  o.timeLimit = std::chrono::milliseconds{0};
  o.targetLength = o.maxLength;  // any solution satisfies this
  return o;
}

/// Search exhaustively: no time limit, no early target, so the search runs
/// until it can prove no shorter solution exists. Only viable for shallow
/// scrambles, but it makes "the shortest answer" assertable.
TwoPhaseOptions exhaustiveOptions() {
  TwoPhaseOptions o;
  o.timeLimit = std::chrono::milliseconds{0};
  o.targetLength = 0;
  return o;
}

const std::vector<Move>& phase2Moves() {
  static const std::vector<Move> moves = {Move::U,  Move::U2, Move::Up,
                                          Move::D,  Move::D2, Move::Dp,
                                          Move::R2, Move::L2, Move::F2,
                                          Move::B2};
  return moves;
}

/// Applies the solution and reports whether the cube ends solved.
bool verifies(const Cube& scrambled, const Solution& solution) {
  Cube cube = scrambled;
  cube.apply(solution.moves);
  return cube.isSolved();
}

}  // namespace

// ---------------------------------------------------------------------------
// Table construction
// ---------------------------------------------------------------------------

TEST(TwoPhaseTables, PruningTablesAreFullyReachable) {
  // PruningTable::build throws if BFS fails to reach every index, so simply
  // constructing the tables proves this. Check the sizes are what we expect.
  const auto& t = *sharedTables();
  EXPECT_EQ(t.flipSlice().size(), 2048u * 495u);
  EXPECT_EQ(t.twistSlice().size(), 2187u * 495u);
  EXPECT_EQ(t.cornerSlice().size(), 40320u * 24u);
  EXPECT_EQ(t.edgeSlice().size(), 40320u * 24u);
}

TEST(TwoPhaseTables, PruningDistancesAreZeroExactlyAtTheGoal) {
  const auto& t = *sharedTables();
  // Phase 1 goal: no flips, no twists, slice edges in the slice.
  EXPECT_EQ(t.phase1Heuristic(0, 0, coord::kUdSliceSolved), 0);
  // Phase 2 goal: everything in place.
  EXPECT_EQ(t.phase2Heuristic(0, 0, 0), 0);
}

TEST(TwoPhaseTables, HeuristicIsZeroOnlyForSolvedStates) {
  const auto& t = *sharedTables();
  // A cube one move from G1 must have a positive phase-1 estimate.
  for (const Move m : {Move::F, Move::R, Move::B, Move::L}) {
    Cube c;
    c.apply(m);
    const int h = t.phase1Heuristic(
        coord::cornerOrientation(c), coord::edgeOrientation(c), coord::udSlice(c));
    EXPECT_GT(h, 0) << toString(m);
  }
}

TEST(TwoPhaseTables, PruningValuesFitInANibble) {
  // Not required today (entries are bytes), but it records the headroom that
  // makes nibble packing a viable optimisation later.
  const auto& t = *sharedTables();
  EXPECT_LE(t.flipSlice().maxDistance(), 14);
  EXPECT_LE(t.twistSlice().maxDistance(), 14);
  EXPECT_LE(t.cornerSlice().maxDistance(), 14);
  EXPECT_LE(t.edgeSlice().maxDistance(), 14);
}

// ---------------------------------------------------------------------------
// Admissibility: the heuristic must never overestimate.
//
// This is the property the whole search depends on. If a pruning value ever
// exceeded the true distance, the search would prune a branch containing the
// only solution.
// ---------------------------------------------------------------------------

TEST(TwoPhaseTables, Phase1HeuristicNeverExceedsTheTrueDistanceToG1) {
  const auto& t = *sharedTables();
  // Walk backwards from G1: after n moves from a G1 state, the true distance to
  // G1 is at most n, so the heuristic must be at most n too.
  std::mt19937_64 rng(31415);
  std::uniform_int_distribution<int> pick(0, kNumMoves - 1);

  for (int trial = 0; trial < 3000; ++trial) {
    Cube c;
    const int n = 1 + (trial % 12);
    for (int i = 0; i < n; ++i) c.apply(static_cast<Move>(pick(rng)));
    const int h = t.phase1Heuristic(coord::cornerOrientation(c),
                                    coord::edgeOrientation(c), coord::udSlice(c));
    ASSERT_LE(h, n) << "heuristic " << h << " exceeds " << n << " moves from G1";
  }
}

TEST(TwoPhaseTables, Phase2HeuristicNeverExceedsTheTrueDistanceToSolved) {
  const auto& t = *sharedTables();
  std::mt19937_64 rng(27182);
  std::uniform_int_distribution<std::size_t> pick(0, phase2Moves().size() - 1);

  for (int trial = 0; trial < 3000; ++trial) {
    Cube c;
    const int n = 1 + (trial % 14);
    for (int i = 0; i < n; ++i) c.apply(phase2Moves()[pick(rng)]);
    ASSERT_TRUE(coord::isInG1(c));
    const int h = t.phase2Heuristic(coord::cornerPermutation(c),
                                    coord::udEdgePermutation(c),
                                    coord::slicePermutation(c));
    ASSERT_LE(h, n) << "heuristic " << h << " exceeds " << n << " moves";
  }
}

// ---------------------------------------------------------------------------
// Trivial cases
// ---------------------------------------------------------------------------

TEST(TwoPhaseSolver, SolvedCubeNeedsNoMoves) {
  const Solution s = solver().solve(Cube{}, fastOptions());
  EXPECT_TRUE(s.found);
  EXPECT_TRUE(s.moves.empty());
  EXPECT_EQ(s.length(), 0);
}

TEST(TwoPhaseSolver, SingleMoveScramblesSolveInOneMove) {
  // Needs the improving search. Stopping at the first solution would not do:
  // see FirstSolutionIsNotNecessarilyTheShortest below.
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    Cube c;
    c.apply(m);
    const Solution s = solver().solve(c, exhaustiveOptions());
    ASSERT_TRUE(s.found) << toString(m);
    EXPECT_EQ(s.length(), 1) << toString(m);
    EXPECT_EQ(s.moves[0], inverse(m)) << toString(m);
    EXPECT_TRUE(verifies(c, s)) << toString(m);
  }
}

TEST(TwoPhaseSolver, FirstSolutionIsNotNecessarilyTheShortest) {
  // A characterising test for the algorithm's defining weakness, using the
  // smallest case that exhibits it.
  //
  // On a cube scrambled by R, the first phase-1 solution the search reaches is
  // R itself: that lands in the R2 state, which is already in G1. But phase 2
  // may not then open with R2, since two turns of the same face across the
  // boundary would be redundant, so it is forced into a long detour. Only by
  // continuing the enumeration does the search reach the phase-1 solution R',
  // whose phase 2 is empty.
  //
  // This is why the solver keeps improving rather than returning its first
  // answer, and why it is not an optimal solver.
  Cube c;
  c.apply(Move::R);

  const Solution first = solver().solve(c, fastOptions());
  ASSERT_TRUE(first.found);
  EXPECT_TRUE(verifies(c, first)) << "the first solution is still valid";
  EXPECT_GT(first.length(), 1) << "expected the first solution to overshoot";

  const Solution best = solver().solve(c, exhaustiveOptions());
  ASSERT_TRUE(best.found);
  EXPECT_EQ(best.length(), 1);
  EXPECT_EQ(best.moves[0], Move::Rp);
}

TEST(TwoPhaseSolver, CubesAlreadyInG1AreSolvedWithG1MovesOnly) {
  std::mt19937_64 rng(2718);
  std::uniform_int_distribution<std::size_t> pick(0, phase2Moves().size() - 1);
  for (int trial = 0; trial < 40; ++trial) {
    Cube c;
    for (int i = 0; i < 20; ++i) c.apply(phase2Moves()[pick(rng)]);
    ASSERT_TRUE(coord::isInG1(c));

    const Solution s = solver().solve(c, fastOptions());
    ASSERT_TRUE(s.found) << "trial " << trial;
    EXPECT_EQ(s.phase1Length, 0) << "already in G1, phase 1 should be empty";
    for (const Move m : s.moves) {
      EXPECT_NE(std::find(phase2Moves().begin(), phase2Moves().end(), m),
                phase2Moves().end())
          << "non-G1 move " << toString(m) << " used on a G1 cube";
    }
    EXPECT_TRUE(verifies(c, s)) << "trial " << trial;
  }
}

// ---------------------------------------------------------------------------
// The central property: solutions actually solve the cube.
// ---------------------------------------------------------------------------

TEST(TwoPhaseSolver, SolvesRandomScramblesAtEveryDepth) {
  for (const int depth : {1, 2, 3, 5, 8, 10, 15, 20, 25, 30, 50, 100}) {
    for (std::uint64_t seed = 0; seed < 12; ++seed) {
      Cube cube;
      const auto scramble = cube.scramble(depth, seed * 1000 + static_cast<std::uint64_t>(depth));

      const Solution s = solver().solve(cube, fastOptions());
      ASSERT_TRUE(s.found) << "depth " << depth << " seed " << seed;
      ASSERT_TRUE(verifies(cube, s))
          << "depth " << depth << " seed " << seed
          << "\n  scramble: " << toString(scramble)
          << "\n  solution: " << toString(s.moves);
    }
  }
}

TEST(TwoPhaseSolver, SolvesManyRandomStates) {
  int longest = 0;
  for (std::uint64_t seed = 0; seed < 300; ++seed) {
    Cube cube;
    (void)cube.scramble(40, seed);
    const Solution s = solver().solve(cube, fastOptions());
    ASSERT_TRUE(s.found) << "seed " << seed;
    ASSERT_TRUE(verifies(cube, s)) << "seed " << seed;
    longest = std::max(longest, s.length());
  }
  // Even taking the first solution found, two-phase stays well under the
  // configured ceiling.
  EXPECT_LE(longest, 25) << "longest first-solution length was " << longest;
}

TEST(TwoPhaseSolver, SolvesTheSuperflip) {
  Cube cube;
  cube.apply(parseSequence("U R2 F B R B2 R U2 L B2 R U' D' R2 F R' L B2 U2 F2"));
  const Solution s = solver().solve(cube, fastOptions());
  ASSERT_TRUE(s.found);
  EXPECT_TRUE(verifies(cube, s));
  // The superflip needs 20 moves optimally, so no solution can be shorter.
  EXPECT_GE(s.length(), 20);
}

TEST(TwoPhaseSolver, SolvesStatesGivenAsFacelets) {
  Cube cube;
  (void)cube.scramble(30, 77);
  const Cube fromStickers = fromFacelets(parseFacelets(toFaceletString(cube)));
  ASSERT_EQ(fromStickers, cube);

  const Solution s = solver().solve(fromStickers, fastOptions());
  ASSERT_TRUE(s.found);
  EXPECT_TRUE(verifies(cube, s));
}

// ---------------------------------------------------------------------------
// Structure of the solution
// ---------------------------------------------------------------------------

TEST(TwoPhaseSolver, Phase1PrefixReachesG1) {
  for (std::uint64_t seed = 0; seed < 60; ++seed) {
    Cube cube;
    (void)cube.scramble(30, seed);
    const Solution s = solver().solve(cube, fastOptions());
    ASSERT_TRUE(s.found) << "seed " << seed;

    Cube afterPhase1 = cube;
    for (int i = 0; i < s.phase1Length; ++i) afterPhase1.apply(s.moves[static_cast<std::size_t>(i)]);
    EXPECT_TRUE(coord::isInG1(afterPhase1))
        << "seed " << seed << ": phase-1 prefix did not reach G1";
  }
}

TEST(TwoPhaseSolver, Phase2SuffixUsesOnlyG1Moves) {
  for (std::uint64_t seed = 0; seed < 60; ++seed) {
    Cube cube;
    (void)cube.scramble(30, seed);
    const Solution s = solver().solve(cube, fastOptions());
    ASSERT_TRUE(s.found) << "seed " << seed;

    for (int i = s.phase1Length; i < s.length(); ++i) {
      const Move m = s.moves[static_cast<std::size_t>(i)];
      EXPECT_NE(std::find(phase2Moves().begin(), phase2Moves().end(), m),
                phase2Moves().end())
          << "seed " << seed << ": phase 2 used " << toString(m);
    }
  }
}

TEST(TwoPhaseSolver, SolutionsContainNoRedundantAdjacentMoves) {
  // Pruning applies inside each phase and across the boundary, so a solution
  // should never contain two turns of the same face in a row.
  for (std::uint64_t seed = 0; seed < 100; ++seed) {
    Cube cube;
    (void)cube.scramble(30, seed);
    const Solution s = solver().solve(cube, fastOptions());
    ASSERT_TRUE(s.found) << "seed " << seed;

    for (std::size_t i = 1; i < s.moves.size(); ++i) {
      EXPECT_NE(face(s.moves[i]), face(s.moves[i - 1]))
          << "seed " << seed << ": " << toString(s.moves)
          << " repeats a face at index " << i;
    }
  }
}

TEST(TwoPhaseSolver, PhaseLengthsSumToTheTotal) {
  for (std::uint64_t seed = 0; seed < 40; ++seed) {
    Cube cube;
    (void)cube.scramble(30, seed);
    const Solution s = solver().solve(cube, fastOptions());
    ASSERT_TRUE(s.found);
    EXPECT_EQ(s.phase1Length + s.phase2Length(), s.length());
    EXPECT_GE(s.phase1Length, 0);
    EXPECT_GE(s.phase2Length(), 0);
  }
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

TEST(TwoPhaseSolver, ImprovingSearchNeverReturnsALongerSolution) {
  for (std::uint64_t seed = 0; seed < 15; ++seed) {
    Cube cube;
    (void)cube.scramble(30, seed);

    const Solution first = solver().solve(cube, fastOptions());

    TwoPhaseOptions improving;
    improving.timeLimit = std::chrono::milliseconds{100};
    const Solution better = solver().solve(cube, improving);

    ASSERT_TRUE(first.found);
    ASSERT_TRUE(better.found);
    EXPECT_LE(better.length(), first.length()) << "seed " << seed;
    EXPECT_TRUE(verifies(cube, better)) << "seed " << seed;
  }
}

TEST(TwoPhaseSolver, TargetLengthStopsTheSearchEarly) {
  Cube cube;
  (void)cube.scramble(30, 5);

  TwoPhaseOptions o;
  o.timeLimit = std::chrono::milliseconds{0};
  o.targetLength = 25;
  const Solution s = solver().solve(cube, o);

  ASSERT_TRUE(s.found);
  EXPECT_TRUE(s.stats.stoppedOnTarget);
  EXPECT_LE(s.length(), 25);
  EXPECT_TRUE(verifies(cube, s));
}

TEST(TwoPhaseSolver, MaxLengthTooSmallYieldsNoSolution) {
  Cube cube;
  (void)cube.scramble(30, 11);

  TwoPhaseOptions o;
  o.maxLength = 2;  // no 30-move scramble solves in two moves
  o.timeLimit = std::chrono::milliseconds{0};
  const Solution s = solver().solve(cube, o);

  EXPECT_FALSE(s.found);
  EXPECT_TRUE(s.moves.empty());
}

TEST(TwoPhaseSolver, RejectsAnOutOfRangeMaxLength) {
  TwoPhaseOptions o;
  o.maxLength = 100;
  EXPECT_THROW((void)solver().solve(Cube{}, o), Error);
  o.maxLength = -1;
  EXPECT_THROW((void)solver().solve(Cube{}, o), Error);
}

TEST(TwoPhaseSolver, RejectsUnreachableCubeStates) {
  // A single flipped edge cannot occur on a real cube.
  auto facelets = toFacelets(Cube{});
  std::swap(facelets[5], facelets[10]);
  EXPECT_THROW((void)fromFacelets(facelets), InvalidStateError);

  // Feed the solver such a state directly, bypassing the facelet check.
  std::array<std::uint8_t, kNumEdges> eo{};
  eo[0] = 1;
  std::array<std::uint8_t, kNumCorners> cp{};
  std::array<std::uint8_t, kNumCorners> co{};
  std::array<std::uint8_t, kNumEdges> ep{};
  for (std::size_t i = 0; i < kNumCorners; ++i) cp[i] = static_cast<std::uint8_t>(i);
  for (std::size_t i = 0; i < kNumEdges; ++i) ep[i] = static_cast<std::uint8_t>(i);
  const Cube illegal = Cube::fromCubies(cp, co, ep, eo);

  EXPECT_THROW((void)solver().solve(illegal, fastOptions()), InvalidStateError);
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

TEST(TwoPhaseSolver, StatisticsAreConsistent) {
  Cube cube;
  (void)cube.scramble(30, 42);
  const Solution s = solver().solve(cube, fastOptions());

  ASSERT_TRUE(s.found);
  EXPECT_GT(s.stats.phase1Nodes, 0u);
  EXPECT_GT(s.stats.phase2Nodes, 0u);
  EXPECT_GE(s.stats.phase1Solutions, 1u);
  EXPECT_EQ(s.stats.totalNodes(), s.stats.phase1Nodes + s.stats.phase2Nodes);
  EXPECT_GE(s.stats.improvements, 1);
  EXPECT_GE(s.stats.phase1Depth, 0);
  EXPECT_GE(s.stats.elapsedSeconds, 0.0);
}

// ---------------------------------------------------------------------------
// Sharing tables across threads
// ---------------------------------------------------------------------------

TEST(TwoPhaseSolver, SolversSharingTablesRunCorrectlyInParallel) {
  // The tables are immutable and each solve keeps its own search state, so
  // several threads may share one table set. This is the property the
  // multithreaded mode will rely on.
  constexpr int kThreads = 4;
  constexpr int kPerThread = 25;

  std::atomic<int> failures{0};
  std::vector<std::thread> workers;
  workers.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([t, &failures] {
      const TwoPhaseSolver local(sharedTables());
      for (int i = 0; i < kPerThread; ++i) {
        Cube cube;
        (void)cube.scramble(30, static_cast<std::uint64_t>(t * 1000 + i));
        const Solution s = local.solve(cube, fastOptions());
        if (!s.found || !verifies(cube, s)) ++failures;
      }
    });
  }
  for (auto& w : workers) w.join();
  EXPECT_EQ(failures.load(), 0);
}

TEST(TwoPhaseSolver, RepeatedSolvesOfTheSameCubeAgree) {
  Cube cube;
  (void)cube.scramble(30, 999);
  const Solution a = solver().solve(cube, fastOptions());
  const Solution b = solver().solve(cube, fastOptions());
  // With the time limit disabled the search is deterministic.
  EXPECT_EQ(a.moves, b.moves);
  EXPECT_EQ(a.phase1Length, b.phase1Length);
}
