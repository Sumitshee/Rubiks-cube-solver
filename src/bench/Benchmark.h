#pragma once

#include "core/Cube.h"
#include "core/Move.h"
#include "solver/TwoPhaseSolver.h"
#include "solver/korf/OptimalSolver.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rubik::bench {

/// Which solver a configuration exercises.
enum class SolverKind { TwoPhase, Optimal };

[[nodiscard]] const char* toString(SolverKind kind) noexcept;

/// One state to solve, with everything needed to reproduce it.
struct Case {
  std::string id;
  Cube cube;
  std::vector<Move> scramble;

  /// The true optimal distance, once established. Negative while unknown.
  ///
  /// Scramble length is *not* a stand-in for this: a 14-move scramble often has
  /// a 13-move optimum, so grouping by scramble length would blur two different
  /// difficulties together.
  int knownOptimal = -1;

  [[nodiscard]] int scrambleLength() const noexcept {
    return static_cast<int>(scramble.size());
  }
};

/// A solver setup to measure. One configuration is run across a whole case set.
struct Config {
  std::string label;
  SolverKind solver = SolverKind::Optimal;

  // Optimal-solver settings.
  korf::HeuristicMode heuristic = korf::HeuristicMode::MaxOfThree;
  bool orderMoves = false;
  int maxDepth = 20;

  // Two-phase settings.
  int maxLength = 25;
  int targetLength = 0;

  std::chrono::milliseconds timeLimit{0};

  /// Recorded in every result file so that single-threaded and parallel runs
  /// before and after multithreading remain directly comparable.
  int threads = 1;
};

/// One measured result.
struct Record {
  std::string caseId;
  std::string scramble;
  int scrambleLength = 0;
  int knownOptimal = -1;

  std::string config;
  std::string solver;
  std::string heuristic;
  bool orderMoves = false;
  int threads = 1;
  long long timeLimitMs = 0;

  int solutionLength = -1;
  double milliseconds = 0.0;
  std::uint64_t nodesExpanded = 0;
  std::uint64_t nodesGenerated = 0;
  std::uint64_t nodesPruned = 0;
  std::uint64_t movesSkipped = 0;
  int iterations = 0;
  int initialHeuristic = -1;
  int finalThreshold = -1;
  int provenLowerBound = -1;

  std::size_t peakMemoryBytes = 0;

  /// "optimal", "solved", "timeout", "depth-limit", "no-solution".
  std::string status;
  /// The solution was applied to the original cube and the result checked.
  bool verified = false;
};

/// Describes the machine and build, so a results file is self-explanatory.
struct MachineInfo {
  std::string cpu;
  int logicalCores = 0;
  double ramGigabytes = 0.0;
  std::string compiler;
  std::string buildType;
  std::string timestamp;
  std::string cubeVersion;

  [[nodiscard]] static MachineInfo detect();
  [[nodiscard]] std::string describe() const;
};

/// Deterministic case sets.
///
/// Everything is derived from an explicit base seed, so a run is reproducible
/// from the command line alone -- no result file has to carry the states.
namespace cases {

/// `samples` scrambles at each of the given depths. The seed for one case is a
/// pure function of (baseSeed, depth, index), so adding a depth or changing the
/// sample count never disturbs the cases already generated.
[[nodiscard]] std::vector<Case> scrambles(const std::vector<int>& depths,
                                          int samples, std::uint64_t baseSeed);

/// Fixed, named positions whose properties are documented rather than random.
[[nodiscard]] std::vector<Case> knownPositions();

}  // namespace cases

/// Runs configurations across case sets and collects records.
class Runner {
 public:
  /// `heuristic` may be null when only the two-phase solver is used.
  Runner(std::shared_ptr<const TwoPhaseTables> twoPhaseTables,
         std::shared_ptr<const korf::KorfHeuristic> korfHeuristic);

  using ProgressFn = std::function<void(const Record&)>;

  [[nodiscard]] std::vector<Record> run(const std::vector<Case>& cases,
                                        const Config& config,
                                        const ProgressFn& onRecord = {}) const;

  /// Establishes the true optimal length of each case with a full IDA* solve,
  /// so later configurations can be grouped by difficulty rather than by
  /// scramble length. Cases that time out keep `knownOptimal == -1`.
  void resolveOptimalDepths(std::vector<Case>& cases,
                            std::chrono::milliseconds timeLimit,
                            const std::function<void(const Case&)>& onResolved = {}) const;

 private:
  [[nodiscard]] Record runTwoPhase(const Case& c, const Config& config) const;
  [[nodiscard]] Record runOptimal(const Case& c, const Config& config) const;

  std::shared_ptr<const TwoPhaseTables> twoPhaseTables_;
  std::shared_ptr<const korf::KorfHeuristic> korfHeuristic_;
};

/// Writes every record as CSV, preceded by machine and build details as
/// comment lines so a result file stands on its own.
void writeCsv(const std::string& path, const MachineInfo& machine,
              const std::vector<Record>& records);

/// A console summary grouped by `knownOptimal` where available, otherwise by
/// scramble length. Returns the formatted text.
[[nodiscard]] std::string summarise(const std::vector<Record>& records);

}  // namespace rubik::bench
