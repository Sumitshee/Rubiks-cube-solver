#include "core/Cube.h"
#include "core/Error.h"
#include "core/Move.h"
#include "solver/ByteArray.h"
#include "solver/Coordinate.h"
#include "solver/MoveTable.h"
#include "solver/NibbleArray.h"
#include "solver/PatternDatabase.h"
#include "solver/korf/CornerAbstraction.h"
#include "solver/korf/EdgeAbstraction.h"
#include "solver/korf/KorfHeuristic.h"

#include "TestDatabases.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace rubik;
using namespace rubik::korf;

namespace {

const MoveTables& tables() { return testdb::moveTables(); }

/// A deliberately tiny abstraction: corner twists only, discarding the corner
/// permutation as well as every edge. 3^7 = 2187 states, so generation is
/// instantaneous and the database machinery can be tested without waiting for
/// the real 88-million-state build.
///
/// It is a legitimate abstraction in its own right -- just a very weak one.
class CornerOrientationAbstraction {
 public:
  static constexpr std::uint32_t kSize = coord::kCornerOrientationCount;

  explicit CornerOrientationAbstraction(const MoveTables& t) : tables_(&t) {}

  [[nodiscard]] std::uint32_t size() const noexcept { return kSize; }
  [[nodiscard]] std::string name() const { return "cornerOrientationTest"; }
  [[nodiscard]] std::uint32_t index(const Cube& cube) const noexcept {
    return coord::cornerOrientation(cube);
  }
  [[nodiscard]] std::uint32_t goalIndex() const noexcept { return 0; }
  void successors(std::uint32_t index, std::uint32_t* out) const noexcept {
    const std::uint16_t* row = tables_->cornerOrientation().row(index);
    for (int m = 0; m < kNumMoves; ++m) out[m] = row[m];
  }

 private:
  const MoveTables* tables_;
};

using TinyDatabase = PatternDatabase<CornerOrientationAbstraction, NibbleArray>;

std::string tempPath(const char* stem) {
  return std::string("rubik_test_") + stem + ".db";
}

Cube scrambled(std::uint64_t seed, int depth = 30) {
  Cube c;
  (void)c.scramble(depth, seed);
  return c;
}

}  // namespace

// ===========================================================================
// Generation machinery, exercised on the tiny abstraction
// ===========================================================================

TEST(PatternDatabaseGeneration, FillsEveryStateAndReportsTheMaximum) {
  TinyDatabase db{CornerOrientationAbstraction(tables())};
  EXPECT_FALSE(db.ready());
  db.generate();

  EXPECT_TRUE(db.ready());
  EXPECT_EQ(db.size(), 2187u);
  // Generation throws if any state is unreachable, so reaching here already
  // proves full coverage. Confirm no entry kept the sentinel.
  for (std::uint32_t i = 0; i < db.size(); ++i) {
    ASSERT_NE(db.lookupIndex(i), TinyDatabase::kUnvisited) << "index " << i;
    ASSERT_LE(db.lookupIndex(i), db.maxDistance()) << "index " << i;
  }
  EXPECT_GT(db.maxDistance(), 0);
}

TEST(PatternDatabaseGeneration, GoalHasDistanceZeroAndNothingElseDoes) {
  TinyDatabase db{CornerOrientationAbstraction(tables())};
  db.generate();

  EXPECT_EQ(db.lookup(Cube{}), 0);
  int zeros = 0;
  for (std::uint32_t i = 0; i < db.size(); ++i) {
    if (db.lookupIndex(i) == 0) ++zeros;
  }
  EXPECT_EQ(zeros, 1) << "only the goal state should be at distance 0";
}

TEST(PatternDatabaseGeneration, IsReproducible) {
  TinyDatabase a{CornerOrientationAbstraction(tables())};
  TinyDatabase b{CornerOrientationAbstraction(tables())};
  a.generate();
  b.generate();
  EXPECT_EQ(a.checksum(), b.checksum());
  EXPECT_EQ(a.maxDistance(), b.maxDistance());
}

TEST(PatternDatabaseGeneration, ReportsProgressOncePerDepth) {
  TinyDatabase db{CornerOrientationAbstraction(tables())};
  std::vector<PdbProgress> reports;
  db.generate([&](const PdbProgress& p) { reports.push_back(p); });

  ASSERT_FALSE(reports.empty());
  EXPECT_EQ(static_cast<int>(reports.size()), db.maxDistance());
  for (std::size_t i = 0; i < reports.size(); ++i) {
    EXPECT_EQ(reports[i].depth, static_cast<int>(i) + 1);
    EXPECT_EQ(reports[i].name, "cornerOrientationTest");
    EXPECT_EQ(reports[i].statesTotal, db.size());
    EXPECT_GT(reports[i].statesAtDepth, 0u);
  }
  // The last report must account for every state.
  EXPECT_EQ(reports.back().statesFilled, db.size());
}

TEST(PatternDatabaseGeneration, DistancesAreConsistentWithMoves) {
  // A defining property of a BFS distance field: every neighbour is within one.
  TinyDatabase db{CornerOrientationAbstraction(tables())};
  db.generate();

  std::array<std::uint32_t, kNumMoves> successors{};
  const CornerOrientationAbstraction abstraction(tables());
  for (std::uint32_t i = 0; i < db.size(); ++i) {
    const int here = db.lookupIndex(i);
    abstraction.successors(i, successors.data());
    for (const std::uint32_t s : successors) {
      const int there = db.lookupIndex(s);
      ASSERT_LE(std::abs(here - there), 1)
          << "index " << i << " (" << here << ") adjacent to " << s << " ("
          << there << ")";
    }
  }
}

// ===========================================================================
// Save and load
// ===========================================================================

TEST(PatternDatabaseIo, LoadedValuesMatchGeneratedValues) {
  const std::string path = tempPath("roundtrip");

  TinyDatabase written{CornerOrientationAbstraction(tables())};
  written.generate();
  written.save(path);

  TinyDatabase read{CornerOrientationAbstraction(tables())};
  ASSERT_TRUE(read.load(path));

  EXPECT_EQ(read.checksum(), written.checksum());
  EXPECT_EQ(read.maxDistance(), written.maxDistance());
  EXPECT_EQ(read.size(), written.size());
  for (std::uint32_t i = 0; i < written.size(); ++i) {
    ASSERT_EQ(read.lookupIndex(i), written.lookupIndex(i)) << "index " << i;
  }
  std::remove(path.c_str());
}

TEST(PatternDatabaseIo, LoadingAMissingFileReturnsFalseRatherThanThrowing) {
  TinyDatabase db{CornerOrientationAbstraction(tables())};
  EXPECT_FALSE(db.load("this_file_does_not_exist_12345.db"));
  EXPECT_FALSE(db.ready());
}

TEST(PatternDatabaseIo, RejectsAFileThatIsNotADatabase) {
  const std::string path = tempPath("notadb");
  {
    std::ofstream out(path, std::ios::binary);
    out << "this is definitely not a pattern database, not even close at all";
  }
  TinyDatabase db{CornerOrientationAbstraction(tables())};
  EXPECT_THROW((void)db.load(path), DatabaseError);
  std::remove(path.c_str());
}

TEST(PatternDatabaseIo, RejectsATruncatedFile) {
  const std::string path = tempPath("truncated");
  TinyDatabase db{CornerOrientationAbstraction(tables())};
  db.generate();
  db.save(path);

  // Chop the file in half.
  std::string contents;
  {
    std::ifstream in(path, std::ios::binary);
    contents.assign(std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>());
  }
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size() / 2));
  }

  TinyDatabase reloaded{CornerOrientationAbstraction(tables())};
  EXPECT_THROW((void)reloaded.load(path), DatabaseError);
  std::remove(path.c_str());
}

TEST(PatternDatabaseIo, RejectsACorruptedPayload) {
  const std::string path = tempPath("corrupt");
  TinyDatabase db{CornerOrientationAbstraction(tables())};
  db.generate();
  db.save(path);

  // Flip a byte well past the header.
  {
    std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
    f.seekp(200);
    char byte = 0;
    f.seekg(200);
    f.read(&byte, 1);
    byte = static_cast<char>(byte ^ 0xFF);
    f.seekp(200);
    f.write(&byte, 1);
  }

  TinyDatabase reloaded{CornerOrientationAbstraction(tables())};
  EXPECT_THROW((void)reloaded.load(path), DatabaseError)
      << "a corrupted payload must be caught by the checksum";
  std::remove(path.c_str());
}

TEST(PatternDatabaseIo, RefusesToSaveAnUngeneratedDatabase) {
  TinyDatabase db{CornerOrientationAbstraction(tables())};
  EXPECT_THROW(db.save(tempPath("never")), DatabaseError);
}

TEST(PatternDatabaseIo, NibbleAndByteFilesAreNotInterchangeable) {
  const std::string path = tempPath("packing");
  PatternDatabase<CornerOrientationAbstraction, ByteArray> byteDb{
      CornerOrientationAbstraction(tables())};
  byteDb.generate();
  byteDb.save(path);

  TinyDatabase nibbleDb{CornerOrientationAbstraction(tables())};
  EXPECT_THROW((void)nibbleDb.load(path), DatabaseError)
      << "a byte-packed file must not be read as nibble-packed";
  std::remove(path.c_str());
}

TEST(PatternDatabaseIo, BothStorageLayoutsProduceTheSameDistances) {
  PatternDatabase<CornerOrientationAbstraction, NibbleArray> nibble{
      CornerOrientationAbstraction(tables())};
  PatternDatabase<CornerOrientationAbstraction, ByteArray> byteDb{
      CornerOrientationAbstraction(tables())};
  nibble.generate();
  byteDb.generate();

  EXPECT_EQ(nibble.maxDistance(), byteDb.maxDistance());
  // Nibble storage rounds up to a whole byte, so for an odd state count it is
  // ceil(n/2) bytes against n.
  EXPECT_EQ(nibble.byteSize(), (byteDb.byteSize() + 1) / 2);
  for (std::uint32_t i = 0; i < nibble.size(); ++i) {
    ASSERT_EQ(nibble.lookupIndex(i), byteDb.lookupIndex(i)) << "index " << i;
  }
}

// ===========================================================================
// The abstractions themselves
//
// The homomorphism property below is the one that matters: it is what makes a
// pattern database well defined at all, and what the admissibility argument
// rests on.
// ===========================================================================

TEST(CornerAbstractionTest, IndexStaysInRange) {
  const CornerAbstraction abstraction(tables());
  EXPECT_EQ(abstraction.size(), 88179840u);
  for (std::uint64_t seed = 0; seed < 2000; ++seed) {
    ASSERT_LT(abstraction.index(scrambled(seed)), abstraction.size());
  }
}

TEST(CornerAbstractionTest, GoalIndexIsTheSolvedCube) {
  const CornerAbstraction abstraction(tables());
  EXPECT_EQ(abstraction.goalIndex(), abstraction.index(Cube{}));
}

TEST(CornerAbstractionTest, SuccessorsMatchApplyingTheMove) {
  // phi(apply(s, m)) must equal the abstract successor of phi(s) under m --
  // the abstraction must commute with every move. Without this the database is
  // meaningless.
  const CornerAbstraction abstraction(tables());
  std::array<std::uint32_t, kNumMoves> successors{};

  for (std::uint64_t seed = 0; seed < 500; ++seed) {
    const Cube cube = scrambled(seed);
    abstraction.successors(abstraction.index(cube), successors.data());
    for (int m = 0; m < kNumMoves; ++m) {
      Cube moved = cube;
      moved.apply(static_cast<Move>(m));
      ASSERT_EQ(successors[static_cast<std::size_t>(m)], abstraction.index(moved))
          << "seed " << seed << " move " << toString(static_cast<Move>(m));
    }
  }
}

TEST(EdgeAbstractionTest, IndexStaysInRange) {
  for (const auto& abstraction : {makeEdgeGroupA(), makeEdgeGroupB()}) {
    EXPECT_EQ(abstraction.size(), 42577920u);
    for (std::uint64_t seed = 0; seed < 2000; ++seed) {
      ASSERT_LT(abstraction.index(scrambled(seed)), abstraction.size())
          << abstraction.name();
    }
  }
}

TEST(EdgeAbstractionTest, GoalIndexIsTheSolvedCube) {
  EXPECT_EQ(makeEdgeGroupA().goalIndex(), 0u) << "group A tracks edges 0..5";
  const auto b = makeEdgeGroupB();
  EXPECT_EQ(b.goalIndex(), b.index(Cube{}));
}

TEST(EdgeAbstractionTest, SuccessorsMatchApplyingTheMove) {
  std::array<std::uint32_t, kNumMoves> successors{};
  for (const auto& abstraction : {makeEdgeGroupA(), makeEdgeGroupB()}) {
    for (std::uint64_t seed = 0; seed < 500; ++seed) {
      const Cube cube = scrambled(seed);
      abstraction.successors(abstraction.index(cube), successors.data());
      for (int m = 0; m < kNumMoves; ++m) {
        Cube moved = cube;
        moved.apply(static_cast<Move>(m));
        ASSERT_EQ(successors[static_cast<std::size_t>(m)], abstraction.index(moved))
            << abstraction.name() << " seed " << seed << " move "
            << toString(static_cast<Move>(m));
      }
    }
  }
}

TEST(EdgeAbstractionTest, TheTwoGroupsPartitionTheEdges) {
  const auto a = makeEdgeGroupA();
  const auto b = makeEdgeGroupB();
  std::array<int, kNumEdges> seen{};
  for (const std::uint8_t e : a.tracked()) seen[e]++;
  for (const std::uint8_t e : b.tracked()) seen[e]++;
  for (int i = 0; i < kNumEdges; ++i) {
    EXPECT_EQ(seen[static_cast<std::size_t>(i)], 1) << "edge " << i;
  }
}

TEST(EdgeAbstractionTest, DistinctStatesGetDistinctIndices) {
  // Sampled injectivity: two cubes that differ in the tracked edges must not
  // collide, or the database would conflate them.
  const auto abstraction = makeEdgeGroupA();
  std::vector<std::uint32_t> seen;
  seen.reserve(3000);
  for (std::uint64_t seed = 0; seed < 3000; ++seed) {
    seen.push_back(abstraction.index(scrambled(seed)));
  }
  std::sort(seen.begin(), seen.end());
  const auto duplicates =
      static_cast<std::size_t>(std::distance(std::unique(seen.begin(), seen.end()),
                                             seen.end()));
  // A handful of genuine collisions is possible (the abstraction is many-to-one
  // over full cubes), but they should be rare over 3000 random states.
  EXPECT_LT(duplicates, 10u) << duplicates << " colliding indices";
}

// ===========================================================================
// The real databases.
//
// These need the generated files. If they are absent the tests skip with an
// explanation rather than silently passing or spending a minute regenerating.
// ===========================================================================

namespace {

/// Shared with the rest of the binary; see tests/TestDatabases.h.
std::shared_ptr<const KorfHeuristic> realHeuristic() {
  return testdb::threeDatabaseHeuristic();
}

#define REQUIRE_DATABASES()                                                   \
  const auto heuristicPtr = realHeuristic();                                  \
  if (!heuristicPtr) {                                                        \
    GTEST_SKIP() << "pattern databases not found in " << RUBIK_DATA_DIR       \
                 << "; run: rubiks_solver --generate-pdb";                    \
  }                                                                           \
  const KorfHeuristic* h = heuristicPtr.get()

}  // namespace

TEST(KorfDatabases, HaveTheDistancesKorfReports) {
  REQUIRE_DATABASES();
  EXPECT_EQ(h->corners().maxDistance(), 11)
      << "every corner configuration is solvable in 11 moves";
  EXPECT_EQ(h->edgesA().maxDistance(), 10);
  EXPECT_EQ(h->edgesB().maxDistance(), 10);
  EXPECT_EQ(h->corners().size(), 88179840u);
  // 88,179,840/2 for the corners plus 42,577,920/2 for each edge group.
  EXPECT_EQ(h->byteSize(), 44089920u + 2u * 21288960u);
}

TEST(KorfDatabases, SolvedCubeEstimatesZero) {
  REQUIRE_DATABASES();
  EXPECT_EQ(h->estimate(Cube{}), 0);
  EXPECT_EQ(h->corners().lookup(Cube{}), 0);
  EXPECT_EQ(h->edgesA().lookup(Cube{}), 0);
  EXPECT_EQ(h->edgesB().lookup(Cube{}), 0);
}

TEST(KorfDatabases, AdmissibleAgainstKnownDistances) {
  // The core admissibility check. A cube reached by n moves from solved is at
  // most n moves from solved, so no database may report more than n.
  REQUIRE_DATABASES();
  std::mt19937_64 rng(20240607);
  std::uniform_int_distribution<int> pick(0, kNumMoves - 1);

  for (int trial = 0; trial < 20000; ++trial) {
    const int n = 1 + (trial % 11);
    Cube cube;
    for (int i = 0; i < n; ++i) cube.apply(static_cast<Move>(pick(rng)));

    ASSERT_LE(h->corners().lookup(cube), n) << "corner, " << n << " moves";
    ASSERT_LE(h->edgesA().lookup(cube), n) << "edgeA, " << n << " moves";
    ASSERT_LE(h->edgesB().lookup(cube), n) << "edgeB, " << n << " moves";
    ASSERT_LE(h->estimate(cube), n) << "combined, " << n << " moves";
  }
}

TEST(KorfDatabases, ExactForShallowScrambles) {
  // Stronger than admissibility: for a state genuinely n moves away with n
  // small, the corner database should often report exactly n. Build states by
  // canonical (non-redundant) sequences so the true distance really is n.
  REQUIRE_DATABASES();
  int exact = 0;
  int total = 0;
  for (std::uint64_t seed = 0; seed < 2000; ++seed) {
    Cube cube;
    const auto moves = cube.scramble(4, seed);
    ASSERT_EQ(moves.size(), 4u);
    const int estimate = h->estimate(cube);
    ASSERT_LE(estimate, 4);
    if (estimate == 4) ++exact;
    ++total;
  }
  EXPECT_GT(exact, total / 2)
      << "only " << exact << " of " << total << " four-move states were exact";
}

TEST(KorfDatabases, EstimateIsTheMaximumOfTheThree) {
  REQUIRE_DATABASES();
  for (std::uint64_t seed = 0; seed < 2000; ++seed) {
    const Cube cube = scrambled(seed);
    const std::uint8_t expected =
        std::max({h->corners().lookup(cube), h->edgesA().lookup(cube),
                  h->edgesB().lookup(cube)});
    ASSERT_EQ(h->estimate(cube), expected) << "seed " << seed;
  }
}

TEST(KorfDatabases, BoundedEstimateStaysAValidLowerBound) {
  // estimateAtLeast may return early with a value below the true maximum, but
  // it must never exceed it, and it must agree whenever it is under budget.
  REQUIRE_DATABASES();
  for (std::uint64_t seed = 0; seed < 3000; ++seed) {
    const Cube cube = scrambled(seed);
    const std::uint8_t full = h->estimate(cube);
    for (std::uint8_t budget = 0; budget <= 12; ++budget) {
      const std::uint8_t bounded = h->estimateAtLeast(cube, budget);
      ASSERT_LE(bounded, full) << "seed " << seed << " budget " << int(budget);
      if (full <= budget) {
        ASSERT_EQ(bounded, full)
            << "under budget the two must agree, seed " << seed;
      } else {
        ASSERT_GT(bounded, budget)
            << "over budget it must still prove the bound is exceeded, seed "
            << seed;
      }
    }
  }
}

TEST(KorfDatabases, EstimateNeverExceedsGodsNumber) {
  REQUIRE_DATABASES();
  for (std::uint64_t seed = 0; seed < 5000; ++seed) {
    ASSERT_LE(h->estimate(scrambled(seed)), 20);
  }
}

TEST(KorfDatabases, LookupAgreesWithTheAbstractionIndex) {
  // Consistency between the database and the cube representation: looking up a
  // cube must equal looking up its computed index.
  REQUIRE_DATABASES();
  const CornerAbstraction cornerAbstraction(tables());
  for (std::uint64_t seed = 0; seed < 2000; ++seed) {
    const Cube cube = scrambled(seed);
    ASSERT_EQ(h->corners().lookup(cube),
              h->corners().lookupIndex(cornerAbstraction.index(cube)));
  }
}

TEST(KorfDatabases, NeighbouringStatesDifferByAtMostOne) {
  // The distance field is a BFS result, so applying one move can change any
  // database's value by at most one.
  REQUIRE_DATABASES();
  for (std::uint64_t seed = 0; seed < 500; ++seed) {
    const Cube cube = scrambled(seed);
    const int before = h->corners().lookup(cube);
    for (int m = 0; m < kNumMoves; ++m) {
      Cube moved = cube;
      moved.apply(static_cast<Move>(m));
      ASSERT_LE(std::abs(h->corners().lookup(moved) - before), 1)
          << "seed " << seed << " move " << toString(static_cast<Move>(m));
    }
  }
}

// ===========================================================================
// The seven-edge abstraction
// ===========================================================================

TEST(EdgeAbstraction7Test, HasTheExpectedSize) {
  // 12P7 * 2^7 = 3,991,680 * 128.
  EXPECT_EQ(EdgeAbstraction7::kPermutationCount, 3991680u);
  EXPECT_EQ(EdgeAbstraction7::kOrientationCount, 128u);
  EXPECT_EQ(EdgeAbstraction7::kSize, 510935040u);
}

TEST(EdgeAbstraction7Test, IndexStaysInRange) {
  const auto abstraction = makeEdgeGroup7();
  for (std::uint64_t seed = 0; seed < 2000; ++seed) {
    ASSERT_LT(abstraction.index(scrambled(seed)), abstraction.size());
  }
}

TEST(EdgeAbstraction7Test, GoalIndexIsTheSolvedCube) {
  const auto abstraction = makeEdgeGroup7();
  EXPECT_EQ(abstraction.goalIndex(), abstraction.index(Cube{}));
}

TEST(EdgeAbstraction7Test, SuccessorsMatchApplyingTheMove) {
  // The homomorphism property, which is what makes the database admissible.
  const auto abstraction = makeEdgeGroup7();
  std::array<std::uint32_t, kNumMoves> successors{};
  for (std::uint64_t seed = 0; seed < 500; ++seed) {
    const Cube cube = scrambled(seed);
    abstraction.successors(abstraction.index(cube), successors.data());
    for (int m = 0; m < kNumMoves; ++m) {
      Cube moved = cube;
      moved.apply(static_cast<Move>(m));
      ASSERT_EQ(successors[static_cast<std::size_t>(m)], abstraction.index(moved))
          << "seed " << seed << " move " << toString(static_cast<Move>(m));
    }
  }
}

TEST(EdgeAbstraction7Test, TracksSevenDistinctEdges) {
  const auto abstraction = makeEdgeGroup7();
  std::array<bool, kNumEdges> seen{};
  for (const std::uint8_t e : abstraction.tracked()) {
    ASSERT_LT(e, kNumEdges);
    ASSERT_FALSE(seen[e]) << "edge " << int(e) << " tracked twice";
    seen[e] = true;
  }
}

// ---------------------------------------------------------------------------
// Inverse lookup: a memory-free admissible strengthening
// ---------------------------------------------------------------------------

TEST(KorfDatabases, InverseLookupIsAdmissible) {
  // A cube and its inverse are the same distance from solved, so the heuristic
  // evaluated on either bounds the original from below. Check that against
  // states of known distance.
  REQUIRE_DATABASES();
  std::mt19937_64 rng(60613);
  std::uniform_int_distribution<int> pick(0, kNumMoves - 1);

  for (int trial = 0; trial < 20000; ++trial) {
    const int n = 1 + (trial % 11);
    Cube cube;
    for (int i = 0; i < n; ++i) cube.apply(static_cast<Move>(pick(rng)));

    const int combined =
        std::max(h->estimate(cube), h->estimate(cube.inverted()));
    ASSERT_LE(combined, n) << "inverse-augmented heuristic overestimated at "
                           << n << " moves";
  }
}
