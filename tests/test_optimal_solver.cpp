#include "core/Cube.h"
#include "core/Error.h"
#include "core/Move.h"
#include "solver/MoveTable.h"
#include "solver/korf/KorfHeuristic.h"
#include "solver/korf/OptimalSolver.h"

#include "TestDatabases.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <array>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using namespace rubik;
using namespace rubik::korf;

namespace {

/// The three-database configuration most of these tests are written against.
/// Shared with the rest of the binary; see tests/TestDatabases.h.
std::shared_ptr<const KorfHeuristic> loadedHeuristic() {
  return testdb::threeDatabaseHeuristic();
}

#define REQUIRE_SOLVER()                                                    \
  const auto heuristicPtr = loadedHeuristic();                              \
  if (!heuristicPtr) {                                                      \
    GTEST_SKIP() << "pattern databases not found in " << RUBIK_DATA_DIR     \
                 << "; run: rubiks_solver --generate-pdb";                  \
  }                                                                         \
  const OptimalSolver solver(heuristicPtr)

/// A 40-byte cube key, so exact distances can be stored in a hash map.
using CubeKey = std::array<std::uint8_t, 40>;

CubeKey keyOf(const Cube& cube) {
  CubeKey key{};
  std::size_t at = 0;
  for (const std::uint8_t v : cube.cornerPerm()) key[at++] = v;
  for (const std::uint8_t v : cube.cornerOri()) key[at++] = v;
  for (const std::uint8_t v : cube.edgePerm()) key[at++] = v;
  for (const std::uint8_t v : cube.edgeOri()) key[at++] = v;
  return key;
}

struct CubeKeyHash {
  std::size_t operator()(const CubeKey& k) const noexcept {
    std::size_t hash = 1469598103934665603ull;
    for (const std::uint8_t b : k) {
      hash ^= b;
      hash *= 1099511628211ull;
    }
    return hash;
  }
};

/// Exact distances for every state within `maxDepth` moves of solved, built by
/// breadth-first search over the real cube.
///
/// This is the independent source of truth for optimality: it uses no pattern
/// database and no IDA* machinery, only the cube representation itself.
const std::unordered_map<CubeKey, std::uint8_t, CubeKeyHash>& exactDistances() {
  static const auto map = [] {
    constexpr int kMaxDepth = 5;
    std::unordered_map<CubeKey, std::uint8_t, CubeKeyHash> distances;
    distances.reserve(700000);

    std::vector<Cube> frontier{Cube{}};
    distances[keyOf(Cube{})] = 0;

    for (int depth = 1; depth <= kMaxDepth; ++depth) {
      std::vector<Cube> next;
      next.reserve(frontier.size() * 14);
      for (const Cube& cube : frontier) {
        for (int i = 0; i < kNumMoves; ++i) {
          Cube child = cube;
          child.apply(static_cast<Move>(i));
          const CubeKey key = keyOf(child);
          if (distances.find(key) != distances.end()) continue;
          distances[key] = static_cast<std::uint8_t>(depth);
          next.push_back(child);
        }
      }
      frontier.swap(next);
    }
    return distances;
  }();
  return map;
}

/// Brute force: does a solution of exactly `depth` moves exist? Uses no
/// heuristic, so it is fully independent of the pattern databases.
bool existsSolutionAt(Cube& cube, int depth, Move prev, bool hasPrev) {
  if (depth == 0) return cube.isSolved();
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    if (hasPrev && isRedundant(m, prev)) continue;
    cube.apply(m);
    const bool ok = existsSolutionAt(cube, depth - 1, m, true);
    cube.undo(m);
    if (ok) return true;
  }
  return false;
}

bool solvableInFewerThan(const Cube& start, int length) {
  for (int d = 0; d < length; ++d) {
    Cube cube = start;
    if (existsSolutionAt(cube, d, Move::U, false)) return true;
  }
  return false;
}

OptimalOptions quickOptions() {
  OptimalOptions o;
  o.maxDepth = 20;
  o.timeLimit = std::chrono::seconds(60);
  return o;
}

bool verifies(const Cube& start, const OptimalResult& r) {
  Cube cube = start;
  cube.apply(r.moves);
  return cube.isSolved();
}

}  // namespace

// ===========================================================================
// Trivial and known-depth states
// ===========================================================================

TEST(OptimalSolver, SolvedCubeNeedsNoMoves) {
  REQUIRE_SOLVER();
  const OptimalResult r = solver.solve(Cube{}, quickOptions());
  EXPECT_TRUE(r.isOptimal());
  EXPECT_EQ(r.length(), 0);
  EXPECT_TRUE(r.moves.empty());
  EXPECT_EQ(r.provenLowerBound, 0);
}

TEST(OptimalSolver, EverySingleMoveIsSolvedInOneMove) {
  REQUIRE_SOLVER();
  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    Cube cube;
    cube.apply(m);
    const OptimalResult r = solver.solve(cube, quickOptions());
    ASSERT_TRUE(r.isOptimal()) << toString(m);
    EXPECT_EQ(r.length(), 1) << toString(m);
    EXPECT_EQ(r.moves[0], inverse(m)) << toString(m);
    EXPECT_TRUE(verifies(cube, r)) << toString(m);
  }
}

TEST(OptimalSolver, TwoMoveStatesAreSolvedInTwoMoves) {
  REQUIRE_SOLVER();
  int checked = 0;
  for (int a = 0; a < kNumMoves; ++a) {
    for (int b = 0; b < kNumMoves; ++b) {
      const Move first = static_cast<Move>(a);
      const Move second = static_cast<Move>(b);
      if (isRedundant(second, first)) continue;  // would collapse to fewer moves

      Cube cube;
      cube.apply(first);
      cube.apply(second);
      const OptimalResult r = solver.solve(cube, quickOptions());
      ASSERT_TRUE(r.isOptimal());
      ASSERT_EQ(r.length(), 2) << toString(first) << " " << toString(second);
      ASSERT_TRUE(verifies(cube, r));
      ++checked;
    }
  }
  EXPECT_EQ(checked, 243) << "expected 243 canonical two-move states";
}

// ===========================================================================
// Against an independently computed exact-distance table
//
// The table comes from breadth-first search over the real cube. It knows
// nothing about pattern databases or IDA*, so agreement is genuine evidence
// rather than the solver checking itself.
// ===========================================================================

TEST(OptimalSolver, MatchesBreadthFirstDistancesExactly) {
  REQUIRE_SOLVER();
  const auto& exact = exactDistances();
  ASSERT_GT(exact.size(), 500000u) << "BFS table looks too small";

  // Sample across the table rather than solving all 600k states.
  std::mt19937_64 rng(918273);
  std::vector<const std::pair<const CubeKey, std::uint8_t>*> sample;
  sample.reserve(exact.size());
  for (const auto& entry : exact) sample.push_back(&entry);
  std::shuffle(sample.begin(), sample.end(), rng);

  constexpr std::size_t kSamples = 1500;
  std::array<int, 6> perDepth{};
  for (std::size_t i = 0; i < std::min(kSamples, sample.size()); ++i) {
    const CubeKey& key = sample[i]->first;
    const int expected = sample[i]->second;

    std::array<std::uint8_t, kNumCorners> cp{}, co{};
    std::array<std::uint8_t, kNumEdges> ep{}, eo{};
    std::size_t at = 0;
    for (auto& v : cp) v = key[at++];
    for (auto& v : co) v = key[at++];
    for (auto& v : ep) v = key[at++];
    for (auto& v : eo) v = key[at++];
    const Cube cube = Cube::fromCubies(cp, co, ep, eo);

    const OptimalResult r = solver.solve(cube, quickOptions());
    ASSERT_TRUE(r.isOptimal());
    ASSERT_EQ(r.length(), expected)
        << "IDA* returned " << r.length() << " for a state BFS says is "
        << expected << " moves away";
    ASSERT_TRUE(verifies(cube, r));
    perDepth[static_cast<std::size_t>(expected)]++;
  }

  // Almost the whole table lives in the outer two shells -- only 18 states sit
  // at distance 1 out of ~620,000 -- so a uniform sample lands there and
  // essentially never on the shallow ones. Those are covered exhaustively by
  // EveryStateWithinThreeMovesIsExact instead.
  EXPECT_GT(perDepth[4], 0) << "no states sampled at distance 4";
  EXPECT_GT(perDepth[5], 0) << "no states sampled at distance 5";
  EXPECT_EQ(perDepth[4] + perDepth[5],
            static_cast<int>(std::min(kSamples, sample.size())) -
                (perDepth[0] + perDepth[1] + perDepth[2] + perDepth[3]))
      << "sample depths do not add up";
}

TEST(OptimalSolver, EveryStateWithinThreeMovesIsExact) {
  // Exhaustive rather than sampled, over all 3,502 states within three moves.
  REQUIRE_SOLVER();
  const auto& exact = exactDistances();

  int checked = 0;
  for (const auto& [key, expected] : exact) {
    if (expected > 3) continue;
    std::array<std::uint8_t, kNumCorners> cp{}, co{};
    std::array<std::uint8_t, kNumEdges> ep{}, eo{};
    std::size_t at = 0;
    for (auto& v : cp) v = key[at++];
    for (auto& v : co) v = key[at++];
    for (auto& v : ep) v = key[at++];
    for (auto& v : eo) v = key[at++];
    const Cube cube = Cube::fromCubies(cp, co, ep, eo);

    const OptimalResult r = solver.solve(cube, quickOptions());
    ASSERT_TRUE(r.isOptimal());
    ASSERT_EQ(r.length(), expected);
    ++checked;
  }
  EXPECT_EQ(checked, 1 + 18 + 243 + 3240) << "unexpected state count within 3 moves";
}

// ===========================================================================
// Independent optimality proof by brute force
// ===========================================================================

TEST(OptimalSolver, NoShorterSolutionExistsForRandomStates) {
  // For each solved state, brute-force every shorter length with no heuristic
  // at all. If any succeeded, IDA* was not optimal.
  REQUIRE_SOLVER();
  for (int depth = 1; depth <= 7; ++depth) {
    for (std::uint64_t seed = 0; seed < 4; ++seed) {
      Cube cube;
      (void)cube.scramble(depth, seed * 31 + static_cast<std::uint64_t>(depth));

      const OptimalResult r = solver.solve(cube, quickOptions());
      ASSERT_TRUE(r.isOptimal());
      ASSERT_TRUE(verifies(cube, r));
      ASSERT_LE(r.length(), depth) << "a scramble of " << depth
                                   << " moves is at most that far from solved";
      ASSERT_FALSE(solvableInFewerThan(cube, r.length()))
          << "brute force found a solution shorter than the reported optimum "
          << r.length();
    }
  }
}

// ===========================================================================
// Random states: the solution must actually solve the cube
// ===========================================================================

TEST(OptimalSolver, SolutionsSolveTheCubeAndRespectKnownBounds) {
  REQUIRE_SOLVER();
  for (std::uint64_t seed = 0; seed < 30; ++seed) {
    Cube cube;
    const auto scramble = cube.scramble(9, seed);

    const OptimalResult r = solver.solve(cube, quickOptions());
    ASSERT_TRUE(r.isOptimal()) << "seed " << seed;
    ASSERT_TRUE(verifies(cube, r)) << "seed " << seed;

    // The inverse scramble is itself a solution, so the optimum cannot exceed
    // the scramble length.
    EXPECT_LE(r.length(), static_cast<int>(scramble.size())) << "seed " << seed;
    // And admissibility bounds it from below.
    EXPECT_GE(r.length(), heuristicPtr->estimate(cube)) << "seed " << seed;
    EXPECT_LE(r.length(), 20) << "no state needs more than God's number";
  }
}

TEST(OptimalSolver, SolutionsContainNoRedundantAdjacentMoves) {
  REQUIRE_SOLVER();
  for (std::uint64_t seed = 0; seed < 20; ++seed) {
    Cube cube;
    (void)cube.scramble(9, seed + 500);
    const OptimalResult r = solver.solve(cube, quickOptions());
    ASSERT_TRUE(r.isOptimal());
    for (std::size_t i = 1; i < r.moves.size(); ++i) {
      EXPECT_NE(face(r.moves[i]), face(r.moves[i - 1]))
          << "seed " << seed << ": " << toString(r.moves);
    }
  }
}

// ===========================================================================
// The optimal length must not depend on how the search explores
// ===========================================================================

TEST(OptimalSolver, MoveOrderingDoesNotChangeTheOptimalLength) {
  REQUIRE_SOLVER();
  for (std::uint64_t seed = 0; seed < 20; ++seed) {
    Cube cube;
    (void)cube.scramble(9, seed + 900);

    OptimalOptions ordered = quickOptions();
    ordered.orderMoves = true;
    OptimalOptions plain = quickOptions();
    plain.orderMoves = false;

    const OptimalResult a = solver.solve(cube, ordered);
    const OptimalResult b = solver.solve(cube, plain);

    ASSERT_TRUE(a.isOptimal());
    ASSERT_TRUE(b.isOptimal());
    EXPECT_EQ(a.length(), b.length())
        << "seed " << seed << ": ordering changed the optimal length";
    EXPECT_TRUE(verifies(cube, a));
    EXPECT_TRUE(verifies(cube, b));
  }
}

TEST(OptimalSolver, AllHeuristicModesAgreeOnTheOptimalLength) {
  // A weaker heuristic must explore more, but must never reach a different
  // answer. This is the practical consequence of admissibility.
  //
  // The uninformed search is only exercised on shallow states: it expands about
  // 13.4x more nodes per level, so depth 7 already costs seconds and depth 8
  // minutes. Depths where it is affordable still pin the property down, and the
  // two informed modes carry the check deeper.
  REQUIRE_SOLVER();

  const auto agree = [&](int depth, std::uint64_t seed,
                         const std::vector<HeuristicMode>& modes) {
    Cube cube;
    (void)cube.scramble(depth, seed);
    int reference = -1;
    for (const HeuristicMode mode : modes) {
      OptimalOptions o = quickOptions();
      o.heuristic = mode;
      const OptimalResult r = solver.solve(cube, o);
      ASSERT_TRUE(r.isOptimal()) << toString(mode) << " depth " << depth;
      ASSERT_TRUE(verifies(cube, r)) << toString(mode);
      if (reference < 0) {
        reference = r.length();
      } else {
        ASSERT_EQ(r.length(), reference)
            << toString(mode) << " disagreed at depth " << depth << " seed "
            << seed;
      }
    }
  };

  for (int depth = 1; depth <= 6; ++depth) {
    for (std::uint64_t seed = 0; seed < 3; ++seed) {
      agree(depth, seed + 1200, {HeuristicMode::None, HeuristicMode::CornerOnly,
                                 HeuristicMode::MaxOfThree});
    }
  }
  for (int depth = 7; depth <= 9; ++depth) {
    for (std::uint64_t seed = 0; seed < 3; ++seed) {
      agree(depth, seed + 1300,
            {HeuristicMode::CornerOnly, HeuristicMode::MaxOfThree});
    }
  }
}

TEST(OptimalSolver, RepeatedSolvesAreIdentical) {
  REQUIRE_SOLVER();
  Cube cube;
  (void)cube.scramble(9, 4242);
  const OptimalResult a = solver.solve(cube, quickOptions());
  const OptimalResult b = solver.solve(cube, quickOptions());
  ASSERT_TRUE(a.isOptimal());
  EXPECT_EQ(a.moves, b.moves);
  EXPECT_EQ(a.stats.nodesExpanded, b.stats.nodesExpanded);
}

// ===========================================================================
// Timeouts must never be mistaken for completed searches
// ===========================================================================

TEST(OptimalSolver, TimeoutIsReportedAsNotOptimal) {
  REQUIRE_SOLVER();
  Cube cube;
  (void)cube.scramble(40, 7);  // a random state, far too deep for 1 ms

  OptimalOptions o;
  o.maxDepth = 20;
  o.timeLimit = std::chrono::milliseconds(1);

  const OptimalResult r = solver.solve(cube, o);
  EXPECT_EQ(r.outcome, OptimalOutcome::TimedOut);
  EXPECT_FALSE(r.isOptimal()) << "a timed-out search must not claim optimality";
  EXPECT_TRUE(r.moves.empty()) << "no solution should be returned";
  EXPECT_GE(r.provenLowerBound, r.stats.initialHeuristic)
      << "the lower bound must be at least the initial heuristic";
  EXPECT_LE(r.provenLowerBound, 20);
}

TEST(OptimalSolver, TimeoutLowerBoundIsSound) {
  // Whatever the search managed to complete, the true optimum must be at least
  // the reported lower bound. Check against a state we can also solve fully.
  REQUIRE_SOLVER();
  for (std::uint64_t seed = 0; seed < 6; ++seed) {
    Cube cube;
    (void)cube.scramble(8, seed + 77);

    const OptimalResult full = solver.solve(cube, quickOptions());
    ASSERT_TRUE(full.isOptimal());

    OptimalOptions tiny = quickOptions();
    tiny.timeLimit = std::chrono::milliseconds(1);
    const OptimalResult brief = solver.solve(cube, tiny);

    EXPECT_LE(brief.provenLowerBound, full.length())
        << "seed " << seed << ": claimed a lower bound above the true optimum";
  }
}

TEST(OptimalSolver, DepthLimitIsReportedDistinctlyFromTimeout) {
  REQUIRE_SOLVER();
  Cube cube;
  (void)cube.scramble(9, 31337);

  OptimalOptions o = quickOptions();
  o.maxDepth = 2;  // far too shallow

  const OptimalResult r = solver.solve(cube, o);
  EXPECT_EQ(r.outcome, OptimalOutcome::DepthLimitReached);
  EXPECT_FALSE(r.isOptimal());
  EXPECT_TRUE(r.moves.empty());
}

// ===========================================================================
// Statistics and input validation
// ===========================================================================

TEST(OptimalSolver, StatisticsAreInternallyConsistent) {
  REQUIRE_SOLVER();
  Cube cube;
  (void)cube.scramble(9, 24680);
  const OptimalResult r = solver.solve(cube, quickOptions());
  ASSERT_TRUE(r.isOptimal());

  EXPECT_GT(r.stats.nodesExpanded, 0u);
  EXPECT_GT(r.stats.nodesGenerated, 0u);
  EXPECT_GE(r.stats.iterations, 1);
  EXPECT_EQ(static_cast<int>(r.stats.nodesPerIteration.size()), r.stats.iterations);
  EXPECT_EQ(std::accumulate(r.stats.nodesPerIteration.begin(),
                            r.stats.nodesPerIteration.end(), std::uint64_t{0}),
            r.stats.nodesExpanded);

  // The initial heuristic is admissible, and the final threshold is the length.
  EXPECT_LE(r.stats.initialHeuristic, r.length());
  EXPECT_EQ(r.stats.finalThreshold, r.length());
  EXPECT_EQ(r.provenLowerBound, r.length());
}

TEST(OptimalSolver, RejectsUnreachableStates) {
  REQUIRE_SOLVER();
  std::array<std::uint8_t, kNumCorners> cp{}, co{};
  std::array<std::uint8_t, kNumEdges> ep{}, eo{};
  for (std::size_t i = 0; i < kNumCorners; ++i) cp[i] = static_cast<std::uint8_t>(i);
  for (std::size_t i = 0; i < kNumEdges; ++i) ep[i] = static_cast<std::uint8_t>(i);
  eo[0] = 1;  // a single flipped edge cannot occur
  const Cube illegal = Cube::fromCubies(cp, co, ep, eo);

  EXPECT_THROW((void)solver.solve(illegal, quickOptions()), InvalidStateError);
}

TEST(OptimalSolver, RejectsAnOutOfRangeMaxDepth) {
  REQUIRE_SOLVER();
  OptimalOptions o = quickOptions();
  o.maxDepth = 99;
  EXPECT_THROW((void)solver.solve(Cube{}, o), Error);
  o.maxDepth = -1;
  EXPECT_THROW((void)solver.solve(Cube{}, o), Error);
}

TEST(OptimalSolver, RejectsANullHeuristic) {
  EXPECT_THROW(OptimalSolver(nullptr), Error);
}

// ===========================================================================
// Stronger heuristic configurations
//
// A stronger heuristic must still be admissible, so it must reach exactly the
// same optimal length as the baseline on every state. These skip when the
// optional seven-edge database is absent.
// ===========================================================================

namespace {

/// The seven-edge database, or null when it has not been generated.
std::shared_ptr<const KorfHeuristic> heuristicWithSeven() {
  return testdb::sevenEdgeHeuristic();
}

#define REQUIRE_SEVEN()                                                     \
  const auto sevenPtr = heuristicWithSeven();                               \
  if (!sevenPtr) {                                                          \
    GTEST_SKIP() << "seven-edge database absent; run: rubiks_solver "        \
                    "--generate-pdb --with-7edge";                          \
  }                                                                         \
  const OptimalSolver sevenSolver(sevenPtr)

}  // namespace

TEST(OptimalSolverPhase9, SevenEdgeDatabaseHasTheExpectedShape) {
  REQUIRE_SEVEN();
  EXPECT_TRUE(sevenPtr->hasSevenEdge());
  EXPECT_EQ(sevenPtr->edges7().size(), 510935040u);
  EXPECT_EQ(sevenPtr->edges7().maxDistance(), 11);
}

TEST(OptimalSolverPhase9, SevenEdgeDatabaseIsAdmissible) {
  REQUIRE_SEVEN();
  std::mt19937_64 rng(90909);
  std::uniform_int_distribution<int> pick(0, kNumMoves - 1);
  for (int trial = 0; trial < 20000; ++trial) {
    const int n = 1 + (trial % 11);
    Cube cube;
    for (int i = 0; i < n; ++i) cube.apply(static_cast<Move>(pick(rng)));
    ASSERT_LE(sevenPtr->edges7().lookup(cube), n);
    ASSERT_LE(sevenPtr->estimateWithSeven(cube), n);
  }
}

TEST(OptimalSolverPhase9, EveryHeuristicModeReachesTheSameOptimum) {
  // The whole point: a stronger heuristic explores less but must never change
  // the answer. If any of these disagreed, admissibility would be broken.
  REQUIRE_SEVEN();
  for (int depth = 1; depth <= 9; ++depth) {
    for (std::uint64_t seed = 0; seed < 3; ++seed) {
      Cube cube;
      (void)cube.scramble(depth, seed + 7000);

      int reference = -1;
      for (const HeuristicMode mode :
           {HeuristicMode::MaxOfThree, HeuristicMode::MaxOfThreeInverse,
            HeuristicMode::MaxOfFour, HeuristicMode::MaxOfFourInverse}) {
        OptimalOptions o = quickOptions();
        o.heuristic = mode;
        const OptimalResult r = sevenSolver.solve(cube, o);
        ASSERT_TRUE(r.isOptimal()) << toString(mode) << " depth " << depth;

        Cube check = cube;
        check.apply(r.moves);
        ASSERT_TRUE(check.isSolved()) << toString(mode);

        if (reference < 0) {
          reference = r.length();
        } else {
          ASSERT_EQ(r.length(), reference)
              << toString(mode) << " disagreed at depth " << depth;
        }
      }
    }
  }
}

TEST(OptimalSolverPhase9, StrongerHeuristicsExploreFewerNodes) {
  // Not a correctness property, but it pins the reason for adopting them.
  REQUIRE_SEVEN();
  std::uint64_t baseline = 0;
  std::uint64_t strongest = 0;
  for (std::uint64_t seed = 0; seed < 6; ++seed) {
    Cube cube;
    (void)cube.scramble(10, seed + 8000);

    OptimalOptions three = quickOptions();
    three.heuristic = HeuristicMode::MaxOfThree;
    OptimalOptions four = quickOptions();
    four.heuristic = HeuristicMode::MaxOfFourInverse;

    baseline += sevenSolver.solve(cube, three).stats.nodesExpanded;
    strongest += sevenSolver.solve(cube, four).stats.nodesExpanded;
  }
  EXPECT_LT(strongest, baseline)
      << "max-of-four plus inverse expanded " << strongest << " against "
      << baseline;
}

TEST(OptimalSolverPhase9, BestAvailableModeReflectsWhatIsLoaded) {
  REQUIRE_SEVEN();
  EXPECT_EQ(bestAvailableMode(*sevenPtr), HeuristicMode::MaxOfFourInverse);

  const auto threeOnly = loadedHeuristic();
  if (threeOnly && !threeOnly->hasSevenEdge()) {
    EXPECT_EQ(bestAvailableMode(*threeOnly), HeuristicMode::MaxOfThree);
  }
}

TEST(OptimalSolverPhase9, SevenEdgeModesRequireTheDatabase) {
  const auto threeOnly = loadedHeuristic();
  if (!threeOnly) GTEST_SKIP() << "databases absent";
  if (threeOnly->hasSevenEdge()) {
    GTEST_SKIP() << "this instance has the seven-edge database loaded";
  }
  const OptimalSolver solver(threeOnly);
  OptimalOptions o = quickOptions();
  o.heuristic = HeuristicMode::MaxOfFour;
  EXPECT_THROW((void)solver.solve(Cube{}.inverted(), o), Error);
}

// ===========================================================================
// Root-parallel search
//
// The contract is deliberately weaker than "identical output". Several optimal
// solutions usually exist, and which one a parallel run finds depends on how
// the workers race. What must hold is that the solution is valid, that its
// length is optimal, and that the cube ends solved.
// ===========================================================================

namespace {

OptimalOptions parallelOptions(int threads) {
  OptimalOptions o = quickOptions();
  o.threads = threads;
  return o;
}

}  // namespace

TEST(OptimalSolverParallel, MatchesTheSerialOptimalLength) {
  REQUIRE_SOLVER();
  for (int depth = 1; depth <= 10; ++depth) {
    for (std::uint64_t seed = 0; seed < 2; ++seed) {
      Cube cube;
      (void)cube.scramble(depth, seed + 40000);

      const OptimalResult serial = solver.solve(cube, parallelOptions(1));
      ASSERT_TRUE(serial.isOptimal()) << "depth " << depth;

      for (const int threads : {2, 4, 8}) {
        const OptimalResult parallel = solver.solve(cube, parallelOptions(threads));
        ASSERT_TRUE(parallel.isOptimal())
            << threads << " threads, depth " << depth;
        EXPECT_EQ(parallel.length(), serial.length())
            << threads << " threads disagreed at depth " << depth;
        EXPECT_TRUE(verifies(cube, parallel))
            << threads << " threads, depth " << depth;
        EXPECT_EQ(parallel.stats.threads, threads);
      }
    }
  }
}

TEST(OptimalSolverParallel, MatchesIndependentBreadthFirstDistances) {
  // The same independent source of truth the serial solver is held to.
  REQUIRE_SOLVER();
  const auto& exact = exactDistances();

  std::mt19937_64 rng(41414);
  std::vector<const std::pair<const CubeKey, std::uint8_t>*> sample;
  sample.reserve(exact.size());
  for (const auto& entry : exact) sample.push_back(&entry);
  std::shuffle(sample.begin(), sample.end(), rng);

  for (std::size_t i = 0; i < std::min<std::size_t>(200, sample.size()); ++i) {
    const CubeKey& key = sample[i]->first;
    const int expected = sample[i]->second;

    std::array<std::uint8_t, kNumCorners> cp{}, co{};
    std::array<std::uint8_t, kNumEdges> ep{}, eo{};
    std::size_t at = 0;
    for (auto& v : cp) v = key[at++];
    for (auto& v : co) v = key[at++];
    for (auto& v : ep) v = key[at++];
    for (auto& v : eo) v = key[at++];
    const Cube cube = Cube::fromCubies(cp, co, ep, eo);

    const OptimalResult r = solver.solve(cube, parallelOptions(4));
    ASSERT_TRUE(r.isOptimal());
    ASSERT_EQ(r.length(), expected)
        << "parallel returned " << r.length() << " where BFS says " << expected;
    ASSERT_TRUE(verifies(cube, r));
  }
}

TEST(OptimalSolverParallel, SolvesRandomStatesAtEveryThreadCount) {
  REQUIRE_SOLVER();
  for (const int threads : {1, 2, 4, 8}) {
    for (std::uint64_t seed = 0; seed < 8; ++seed) {
      Cube cube;
      const auto scramble = cube.scramble(9, seed + 50000);
      const OptimalResult r = solver.solve(cube, parallelOptions(threads));
      ASSERT_TRUE(r.isOptimal()) << threads << " threads, seed " << seed;
      ASSERT_TRUE(verifies(cube, r)) << threads << " threads, seed " << seed;
      EXPECT_LE(r.length(), static_cast<int>(scramble.size()));
    }
  }
}

TEST(OptimalSolverParallel, RepeatedSolvesAreStable) {
  // A stress test for races: the same state solved many times over, at several
  // thread counts. Any data race on the shared cube, path or counters would
  // show up as a wrong length or an unsolved cube.
  REQUIRE_SOLVER();
  Cube cube;
  (void)cube.scramble(10, 60000);

  const OptimalResult reference = solver.solve(cube, parallelOptions(1));
  ASSERT_TRUE(reference.isOptimal());

  for (int round = 0; round < 30; ++round) {
    const int threads = 2 + (round % 7);
    const OptimalResult r = solver.solve(cube, parallelOptions(threads));
    ASSERT_TRUE(r.isOptimal()) << "round " << round;
    ASSERT_EQ(r.length(), reference.length()) << "round " << round;
    ASSERT_TRUE(verifies(cube, r)) << "round " << round;
  }
}

TEST(OptimalSolverParallel, ConcurrentSolversShareTheDatabases) {
  // Several solvers, each itself multithreaded, all reading one heuristic. The
  // pattern databases are read-only after loading, so this must be safe -- and
  // it is what keeps memory at one copy rather than one per worker.
  REQUIRE_SOLVER();
  constexpr int kOuter = 4;
  std::atomic<int> failures{0};
  std::vector<std::thread> drivers;

  for (int t = 0; t < kOuter; ++t) {
    drivers.emplace_back([t, &failures, heuristicPtr] {
      const OptimalSolver local(heuristicPtr);  // shares the same databases
      for (int i = 0; i < 4; ++i) {
        Cube cube;
        (void)cube.scramble(8, static_cast<std::uint64_t>(t * 100 + i + 70000));
        OptimalOptions o;
        o.maxDepth = 20;
        o.timeLimit = std::chrono::seconds(60);
        o.threads = 2;
        const OptimalResult r = local.solve(cube, o);
        Cube check = cube;
        check.apply(r.moves);
        if (!r.isOptimal() || !check.isSolved()) ++failures;
      }
    });
  }
  for (auto& d : drivers) d.join();
  EXPECT_EQ(failures.load(), 0);
}

TEST(OptimalSolverParallel, TimeoutIsReportedAsNotOptimal) {
  REQUIRE_SOLVER();
  Cube cube;
  (void)cube.scramble(40, 80000);  // a random state, hopeless in 1 ms

  OptimalOptions o;
  o.maxDepth = 20;
  o.timeLimit = std::chrono::milliseconds(1);
  o.threads = 4;

  const OptimalResult r = solver.solve(cube, o);
  EXPECT_EQ(r.outcome, OptimalOutcome::TimedOut);
  EXPECT_FALSE(r.isOptimal());
  EXPECT_TRUE(r.moves.empty());
  EXPECT_GE(r.provenLowerBound, r.stats.initialHeuristic);
}

TEST(OptimalSolverParallel, CancellationLeavesNoSolutionBehind) {
  // When one worker finds a solution the others are cancelled mid-branch.
  // Cancellation must never surface as a second, different answer, and a
  // cancelled worker's partial state must never be published.
  REQUIRE_SOLVER();
  for (std::uint64_t seed = 0; seed < 10; ++seed) {
    Cube cube;
    (void)cube.scramble(10, seed + 90000);
    const OptimalResult serial = solver.solve(cube, parallelOptions(1));
    const OptimalResult parallel = solver.solve(cube, parallelOptions(8));

    ASSERT_TRUE(serial.isOptimal());
    ASSERT_TRUE(parallel.isOptimal());
    EXPECT_EQ(parallel.length(), serial.length()) << "seed " << seed;
    // Whatever moves came back, they must be a genuine solution.
    EXPECT_TRUE(verifies(cube, parallel)) << "seed " << seed;
    for (const Move m : parallel.moves) {
      EXPECT_LT(static_cast<int>(m), kNumMoves);
    }
  }
}

TEST(OptimalSolverParallel, DepthLimitIsReportedDistinctlyFromTimeout) {
  REQUIRE_SOLVER();
  Cube cube;
  (void)cube.scramble(9, 95000);

  OptimalOptions o = parallelOptions(4);
  o.maxDepth = 2;
  const OptimalResult r = solver.solve(cube, o);
  EXPECT_EQ(r.outcome, OptimalOutcome::DepthLimitReached);
  EXPECT_FALSE(r.isOptimal());
  EXPECT_TRUE(r.moves.empty());
}

TEST(OptimalSolverParallel, StatisticsAreRecordedPerThreadCount) {
  REQUIRE_SOLVER();
  Cube cube;
  (void)cube.scramble(10, 96000);

  for (const int threads : {1, 2, 4}) {
    const OptimalResult r = solver.solve(cube, parallelOptions(threads));
    ASSERT_TRUE(r.isOptimal());
    EXPECT_EQ(r.stats.threads, threads);
    EXPECT_GT(r.stats.nodesExpanded, 0u);
    EXPECT_EQ(r.stats.finalThreshold, r.length());
    EXPECT_EQ(r.provenLowerBound, r.length());
    if (threads > 1) {
      // Root branches are handed out on demand, so at least one was taken.
      EXPECT_GT(r.stats.rootBranches, 0u);
      EXPECT_GE(r.stats.busiestWorkerNodes, r.stats.idlestWorkerNodes);
    }
  }
}

TEST(OptimalSolverParallel, SolvedCubeNeedsNoThreads) {
  REQUIRE_SOLVER();
  for (const int threads : {1, 2, 4, 8}) {
    const OptimalResult r = solver.solve(Cube{}, parallelOptions(threads));
    EXPECT_TRUE(r.isOptimal());
    EXPECT_EQ(r.length(), 0);
  }
}


