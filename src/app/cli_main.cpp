#include "core/Cube.h"
#include "core/Error.h"
#include "core/Facelets.h"
#include "core/Move.h"
#include "solver/TwoPhaseSolver.h"
#include "solver/korf/KorfHeuristic.h"
#include "solver/korf/OptimalSolver.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace rubik;

constexpr std::string_view kUsage = R"(rubiks_solver -- Rubik's Cube solver

Building the cube:
  --scramble <n> [--seed <n>]   Apply an n-move random scramble
  --apply "<moves>"             Apply a move sequence to a solved cube
  --state "<54 facelets>"       Load a state, e.g. UUUUUUUUURRR...

Solving:
  --solve                       Solve the cube (Kociemba two-phase, fast)
  --optimal                     Solve optimally (Korf IDA*, slow but shortest)
  --time-limit <ms>             Budget for improving the solution (default 200;
                                0 returns the first solution found)
  --max-length <n>              Ceiling on solution length (default 25)
  --order-moves                 Enable heuristic move ordering in --optimal
                                (measured slower; off by default)
  --threads <n>                 Worker threads for --optimal (default 4)

Benchmarking:
  --benchmark [--samples <n>]   Solve random scrambles across several depths

Pattern databases (for the optimal solver):
  --generate-pdb                Build the Korf pattern databases if absent
  --with-7edge                  Also build/use the 7-edge database. 243.6 MB and
                                about 4 minutes, but roughly halves solve time.
  --data-dir <path>             Where they live (default: ./data)
  --heuristic                   Show the Korf lower bound for the cube

Other:
  --net                         Print the cube as an unfolded colour net
  --help                        Show this message

Moves use standard notation: U R F D L B, optionally suffixed with ' or 2.

Examples:
  rubiks_solver --scramble 25 --seed 42 --solve
  rubiks_solver --apply "R U R' F2 L" --solve --time-limit 0
  rubiks_solver --benchmark --samples 50
  rubiks_solver --scramble 9 --seed 3 --optimal
)";

/// Minimal argument reader. Kept explicit rather than pulling in a CLI library:
/// the option set is small and the error messages matter more than the syntax.
class Args {
 public:
  Args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) tokens_.emplace_back(argv[i]);
  }

  [[nodiscard]] bool has(std::string_view flag) const {
    return std::find(tokens_.begin(), tokens_.end(), flag) != tokens_.end();
  }

  [[nodiscard]] std::optional<std::string> value(std::string_view flag) const {
    for (std::size_t i = 0; i < tokens_.size(); ++i) {
      if (tokens_[i] != flag) continue;
      if (i + 1 >= tokens_.size()) {
        throw Error("option " + std::string(flag) + " requires a value");
      }
      return tokens_[i + 1];
    }
    return std::nullopt;
  }

  [[nodiscard]] bool empty() const { return tokens_.empty(); }

  [[nodiscard]] std::optional<std::string> firstUnknown(
      const std::vector<std::string_view>& known,
      const std::vector<std::string_view>& valueless) const {
    for (std::size_t i = 0; i < tokens_.size(); ++i) {
      const std::string& t = tokens_[i];
      if (t.rfind("--", 0) != 0) continue;
      if (std::find(known.begin(), known.end(), t) == known.end()) return t;
      // Skip the value so it is not mistaken for a flag.
      if (std::find(valueless.begin(), valueless.end(), t) == valueless.end()) ++i;
    }
    return std::nullopt;
  }

 private:
  std::vector<std::string> tokens_;
};

long parseCount(const std::string& text, std::string_view what, long lo, long hi) {
  try {
    std::size_t consumed = 0;
    const long v = std::stol(text, &consumed);
    if (consumed != text.size()) throw std::invalid_argument("trailing");
    if (v < lo || v > hi) {
      throw Error(std::string(what) + " must be between " + std::to_string(lo) +
                  " and " + std::to_string(hi) + " (got " + text + ")");
    }
    return v;
  } catch (const std::invalid_argument&) {
    throw Error(std::string(what) + " must be an integer (got '" + text + "')");
  } catch (const std::out_of_range&) {
    throw Error(std::string(what) + " is out of range (got '" + text + "')");
  }
}

TwoPhaseOptions readOptions(const Args& args) {
  TwoPhaseOptions options;
  if (const auto v = args.value("--time-limit")) {
    options.timeLimit =
        std::chrono::milliseconds{parseCount(*v, "--time-limit", 0, 600000)};
  }
  if (const auto v = args.value("--max-length")) {
    options.maxLength = static_cast<int>(parseCount(*v, "--max-length", 0, 31));
  }
  // A zero budget means "return the first solution", which needs the target to
  // be satisfiable by any solution.
  if (options.timeLimit.count() == 0) options.targetLength = options.maxLength;
  return options;
}

std::shared_ptr<const TwoPhaseTables> buildTables() {
  const auto start = std::chrono::steady_clock::now();
  auto tables = std::make_shared<const TwoPhaseTables>();
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::cout << "Tables: " << std::fixed << std::setprecision(2) << seconds
            << " s, " << static_cast<double>(tables->byteSize()) / (1024 * 1024)
            << " MB\n\n";
  return tables;
}

void reportSolution(const Cube& cube, const Solution& solution) {
  if (!solution.found) {
    std::cout << "No solution found within the configured limits.\n";
    return;
  }

  // Verify by actually applying the solution rather than trusting the search.
  Cube check = cube;
  check.apply(solution.moves);
  const bool solved = check.isSolved();

  std::cout << "Solution:        " << toString(solution.moves) << "\n"
            << "Solution length: " << solution.length() << " ("
            << solution.phase1Length << " + " << solution.phase2Length() << ")\n"
            << "Solver:          Kociemba two-phase\n"
            << "Phase-1 nodes:   " << solution.stats.phase1Nodes << "\n"
            << "Phase-2 nodes:   " << solution.stats.phase2Nodes << "\n"
            << "Phase-1 routes:  " << solution.stats.phase1Solutions << "\n"
            << "Improvements:    " << solution.stats.improvements << "\n"
            << "Max phase-1 depth: " << solution.stats.phase1Depth << "\n"
            << "Execution time:  " << std::fixed << std::setprecision(2)
            << solution.stats.elapsedSeconds * 1000.0 << " ms\n"
            << "Verified solved: " << (solved ? "YES" : "NO") << "\n";

  if (!solved) {
    throw Error(
        "the generated solution does not solve the cube; this is a solver bug");
  }
}

/// Builds the Korf pattern databases, or reports them already present.
int generatePatternDatabases(const std::string& directory, bool withSeven) {
  std::cout << "Korf pattern databases in '" << directory << "'\n\n";

  const MoveTables tables;
  korf::KorfHeuristic heuristic(tables);

  const auto start = std::chrono::steady_clock::now();
  const bool generated =
      heuristic.loadOrGenerate(directory, [](const PdbProgress& p) {
        std::cout << "  [" << p.name << "] depth " << std::setw(2) << p.depth
                  << ": " << std::setw(11) << p.statesFilled << " / "
                  << p.statesTotal << "  (" << std::fixed << std::setprecision(1)
                  << 100.0 * static_cast<double>(p.statesFilled) /
                         static_cast<double>(p.statesTotal)
                  << "%)  " << std::setprecision(2) << p.elapsedSeconds
                  << " s\n";
      });
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();

  std::cout << "\n"
            << (generated ? "Generated" : "Loaded") << " in " << std::fixed
            << std::setprecision(2) << seconds << " s\n"
            << "Max distances: corner "
            << int(heuristic.corners().maxDistance()) << ", edgeA "
            << int(heuristic.edgesA().maxDistance()) << ", edgeB "
            << int(heuristic.edgesB().maxDistance()) << "\n";

  if (withSeven) {
    std::cout << "\nSeven-edge database (510,935,040 states, 243.6 MB)\n";
    const auto sevenStart = std::chrono::steady_clock::now();
    const bool built = heuristic.loadOrGenerateSeven(
        directory, [](const PdbProgress& p) {
          std::cout << "  [" << p.name << "] depth " << std::setw(2) << p.depth
                    << ": " << std::setw(11) << p.statesFilled << " / "
                    << p.statesTotal << "  (" << std::fixed
                    << std::setprecision(1)
                    << 100.0 * static_cast<double>(p.statesFilled) /
                           static_cast<double>(p.statesTotal)
                    << "%)  " << std::setprecision(2) << p.elapsedSeconds
                    << " s\n";
          std::cout.flush();
        });
    std::cout << (built ? "Generated" : "Loaded") << " in " << std::fixed
              << std::setprecision(2)
              << std::chrono::duration<double>(
                     std::chrono::steady_clock::now() - sevenStart)
                     .count()
              << " s, max distance "
              << int(heuristic.edges7().maxDistance()) << "\n";
  }

  std::cout << "Total size:    "
            << static_cast<double>(heuristic.byteSize()) / (1024 * 1024)
            << " MB\n";
  return 0;
}

/// Loads the pattern databases, failing with a pointed message if absent.
std::shared_ptr<const korf::KorfHeuristic> loadHeuristic(
    const MoveTables& tables, const std::string& directory, bool withSeven) {
  auto heuristic = std::make_shared<korf::KorfHeuristic>(tables);
  if (!heuristic->corners().load(directory + "/corner.db") ||
      !heuristic->edgesA().load(directory + "/edge_a.db") ||
      !heuristic->edgesB().load(directory + "/edge_b.db")) {
    throw DatabaseError("pattern databases not found in '" + directory +
                        "'; run with --generate-pdb first");
  }
  // Optional, and silently skipped when absent.
  if (withSeven) (void)heuristic->loadSeven(directory);
  return heuristic;
}

void reportOptimal(const Cube& cube, const korf::OptimalResult& r) {
  std::cout << "Solver:            Korf IDA*\n"
            << "Outcome:           " << korf::toString(r.outcome) << "\n";

  if (!r.isOptimal()) {
    // Say plainly that nothing was proved. A timed-out search has no solution
    // to offer, only a lower bound.
    std::cout << "Optimality:        NOT ESTABLISHED\n"
              << "Proven lower bound: " << r.provenLowerBound
              << " moves (no shorter solution exists)\n"
              << "Nodes expanded:    " << r.stats.nodesExpanded << "\n"
              << "Threshold reached: " << r.stats.finalThreshold << "\n"
              << "Execution time:    " << std::fixed << std::setprecision(2)
              << r.stats.elapsedSeconds << " s\n";
    return;
  }

  Cube check = cube;
  check.apply(r.moves);
  const bool solved = check.isSolved();

  std::cout << "Solution:          " << toString(r.moves) << "\n"
            << "Solution length:   " << r.length() << " moves (OPTIMAL)\n"
            << "Initial heuristic: " << r.stats.initialHeuristic << "\n"
            << "Final threshold:   " << r.stats.finalThreshold << "\n"
            << "Iterations:        " << r.stats.iterations << "\n"
            << "Nodes expanded:    " << r.stats.nodesExpanded << "\n"
            << "Nodes pruned:      " << r.stats.nodesPruned << "\n"
            << "Moves skipped:     " << r.stats.movesSkipped << "\n"
            << "Execution time:    " << std::fixed << std::setprecision(3)
            << r.stats.elapsedSeconds << " s\n"
            << "Verified solved:   " << (solved ? "YES" : "NO") << "\n";

  std::cout << "Nodes per iteration:";
  for (const std::uint64_t n : r.stats.nodesPerIteration) std::cout << " " << n;
  std::cout << "\n";

  if (r.stats.threads > 1) {
    // How evenly the root subtrees divided. The gap between the busiest and
    // idlest worker during the final threshold is the load imbalance.
    std::cout << "Root branches:     " << r.stats.rootBranches << "\n"
              << "Worker nodes:      busiest " << r.stats.busiestWorkerNodes
              << ", idlest " << r.stats.idlestWorkerNodes;
    if (r.stats.busiestWorkerNodes > 0) {
      const double idle = static_cast<double>(r.stats.idlestWorkerNodes);
      const double busy = static_cast<double>(r.stats.busiestWorkerNodes);
      std::cout << "  (imbalance " << std::fixed << std::setprecision(1)
                << 100.0 * (1.0 - idle / busy) << "%)";
    }
    std::cout << "\n";
  }

  if (!solved) {
    throw Error(
        "the generated solution does not solve the cube; this is a solver bug");
  }
}

int runBenchmark(const Args& args) {
  const int samples =
      args.value("--samples")
          ? static_cast<int>(parseCount(*args.value("--samples"), "--samples", 1, 10000))
          : 25;
  const TwoPhaseOptions options = readOptions(args);

  const auto tables = buildTables();
  const TwoPhaseSolver solver(tables);

  std::cout << "Samples per depth: " << samples << "    Improve budget: "
            << options.timeLimit.count() << " ms\n\n"
            << "Depth  Length (min/mean/max)   Time ms (mean/max)   "
               "Nodes (mean)   Verified\n"
            << "-------------------------------------------------------------"
               "-------------------\n";

  bool allVerified = true;
  for (const int depth : {5, 10, 15, 20, 25, 30}) {
    std::vector<int> lengths;
    std::vector<double> times;
    std::vector<double> nodes;
    int verified = 0;

    for (int i = 0; i < samples; ++i) {
      Cube cube;
      (void)cube.scramble(depth, static_cast<std::uint64_t>(depth) * 100003 +
                                     static_cast<std::uint64_t>(i));
      const Solution s = solver.solve(cube, options);
      if (!s.found) {
        allVerified = false;
        continue;
      }
      Cube check = cube;
      check.apply(s.moves);
      if (check.isSolved()) ++verified; else allVerified = false;

      lengths.push_back(s.length());
      times.push_back(s.stats.elapsedSeconds * 1000.0);
      nodes.push_back(static_cast<double>(s.stats.totalNodes()));
    }

    if (lengths.empty()) continue;
    const auto mean = [](const auto& v) {
      return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    };

    std::cout << std::setw(5) << depth << "  " << std::setw(6)
              << *std::min_element(lengths.begin(), lengths.end()) << " /"
              << std::setw(6) << std::fixed << std::setprecision(2)
              << mean(lengths) << " /" << std::setw(4)
              << *std::max_element(lengths.begin(), lengths.end()) << "   "
              << std::setw(8) << std::setprecision(2) << mean(times) << " /"
              << std::setw(8) << *std::max_element(times.begin(), times.end())
              << "   " << std::setw(12) << std::setprecision(0) << mean(nodes)
              << "   " << verified << "/" << samples << "\n";
  }

  std::cout << "\n" << (allVerified ? "All solutions verified."
                                    : "SOME SOLUTIONS FAILED VERIFICATION.")
            << "\n";
  return allVerified ? 0 : 1;
}

int run(int argc, char** argv) {
  const Args args(argc, argv);

  if (args.empty() || args.has("--help") || args.has("-h")) {
    std::cout << kUsage;
    return 0;
  }

  const std::vector<std::string_view> known = {
      "--scramble", "--seed",       "--apply",      "--state",   "--solve",
      "--net",      "--time-limit", "--max-length", "--benchmark",
      "--samples",  "--generate-pdb", "--data-dir", "--heuristic",
      "--optimal",  "--order-moves",  "--with-7edge", "--threads",
      "--help",     "-h"};
  const std::vector<std::string_view> valueless = {
      "--solve",     "--net",         "--benchmark",   "--generate-pdb",
      "--heuristic", "--optimal",     "--order-moves", "--with-7edge",
      "--help",      "-h"};
  if (const auto unknown = args.firstUnknown(known, valueless)) {
    std::cerr << "error: unknown option " << *unknown << "\n\n" << kUsage;
    return 2;
  }

  const std::string dataDir =
      args.value("--data-dir").value_or(std::string("data"));

  if (args.has("--generate-pdb")) {
    return generatePatternDatabases(dataDir, args.has("--with-7edge"));
  }
  if (args.has("--benchmark")) return runBenchmark(args);

  Cube cube;
  std::vector<Move> setupMoves;

  if (const auto v = args.value("--scramble")) {
    const long n = parseCount(*v, "--scramble", 0, 100000);
    auto seed = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    if (const auto seedArg = args.value("--seed")) {
      seed = static_cast<std::uint64_t>(parseCount(*seedArg, "--seed", 0, 1L << 30));
    }
    setupMoves = cube.scramble(static_cast<int>(n), seed);
    std::cout << "Seed:            " << seed << "\n";
  } else if (const auto applyArg = args.value("--apply")) {
    setupMoves = parseSequence(*applyArg);
    cube.apply(setupMoves);
  } else if (const auto stateArg = args.value("--state")) {
    cube = fromFacelets(parseFacelets(*stateArg));
  }

  if (!setupMoves.empty()) {
    std::cout << "Scramble:        " << toString(setupMoves) << "\n";
  }
  std::cout << "Scramble length: " << setupMoves.size() << "\n";

  cube.validate();
  std::cout << "State:           valid, " << (cube.isSolved() ? "solved" : "scrambled")
            << "\n";

  if (args.has("--net")) std::cout << "\n" << toNetString(cube) << "\n";

  if (args.has("--heuristic")) {
    const MoveTables tables;
    korf::KorfHeuristic heuristic(tables);
    if (!heuristic.corners().load(dataDir + "/corner.db") ||
        !heuristic.edgesA().load(dataDir + "/edge_a.db") ||
        !heuristic.edgesB().load(dataDir + "/edge_b.db")) {
      throw DatabaseError("pattern databases not found in '" + dataDir +
                          "'; run with --generate-pdb first");
    }
    (void)heuristic.loadSeven(dataDir);
    std::cout << "\nKorf lower bound: " << int(heuristic.estimate(cube))
              << " moves  (corner " << int(heuristic.corners().lookup(cube))
              << ", edgeA " << int(heuristic.edgesA().lookup(cube)) << ", edgeB "
              << int(heuristic.edgesB().lookup(cube)) << ")\n";
  }

  if (args.has("--optimal")) {
    korf::OptimalOptions o;
    o.maxDepth = 20;
    o.orderMoves = args.has("--order-moves");
    o.threads = args.value("--threads")
                    ? static_cast<int>(parseCount(*args.value("--threads"),
                                                  "--threads", 1, 64))
                    : 4;
    if (const auto v = args.value("--time-limit")) {
      o.timeLimit =
          std::chrono::milliseconds{parseCount(*v, "--time-limit", 0, 86400000)};
    }

    const MoveTables tables;
    const auto heuristic = loadHeuristic(tables, dataDir, true);
    // Use the strongest heuristic the loaded databases support.
    o.heuristic = korf::bestAvailableMode(*heuristic);
    const korf::OptimalSolver optimal(heuristic);
    std::cout << "\nHeuristic:         " << korf::toString(o.heuristic)
              << "\nThreads:           " << o.threads << "\n";
    reportOptimal(cube, optimal.solve(cube, o));
  }

  if (args.has("--solve")) {
    // Parse the options before building tables, so a typo is reported straight
    // away rather than after a needless half-second of work.
    const TwoPhaseOptions options = readOptions(args);
    std::cout << "\n";
    const auto tables = buildTables();
    const TwoPhaseSolver solver(tables);
    reportSolution(cube, solver.solve(cube, options));
  }

  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const rubik::Error& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "internal error: " << e.what() << "\n";
    return 1;
  }
}
