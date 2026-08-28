#include "bench/Benchmark.h"
#include "bench/HeuristicAnalysis.h"
#include "bench/Microbench.h"
#include "bench/ProcessMemory.h"
#include "core/Error.h"
#include "solver/MoveTable.h"
#include "solver/korf/KorfHeuristic.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace rubik;
using namespace rubik::bench;

constexpr std::string_view kUsage = R"(rubiks_bench -- reproducible benchmark harness

Suites:
  --suite baseline     The four baseline configurations (the default)
  --suite optimal      Korf IDA* at the three-database configuration
  --suite heuristics   IDA* from no heuristic up to max-of-four + inverse
  --suite ordering     IDA* with move ordering off and on
  --suite twophase     Kociemba across several time budgets
  --suite known        The fixed named positions
  --suite micro        Microbenchmarks of every hot-loop primitive
  --suite distribution How strong each heuristic is over random states
  --suite threads      The optimal solver at 1, 2, 4 and 8 threads

Inputs (deterministic):
  --seed <n>           Base seed for scramble generation (default 20260826)
  --depths <a,b,c>     Scramble depths to sample (default 6,8,10,11,12,13)
  --samples <n>        Scrambles per depth (default 4). For --suite
                       distribution this is the number of random states
                       instead, and defaults to 200000.

Limits:
  --timeout <ms>       Per-solve limit, 0 for none (default 300000)
  --threads <n>        Worker threads for the optimal solver (default 1)
  --max-depth <n>      Ceiling for the optimal solver (default 20)

Output:
  --out <file.csv>     Write every record as CSV
  --data-dir <path>    Where the pattern databases live (default ./data)
  --quiet              Suppress the per-case progress lines

Every case is identified by (seed, depth, index), so a run is reproducible from
the command line alone.

Examples:
  rubiks_bench --suite baseline --samples 4 --out baseline.csv
  rubiks_bench --suite heuristics --depths 6,7,8 --samples 6
  rubiks_bench --suite distribution --with-7edge
)";

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

  [[nodiscard]] std::optional<std::string> firstUnknown(
      const std::vector<std::string_view>& known,
      const std::vector<std::string_view>& valueless) const {
    for (std::size_t i = 0; i < tokens_.size(); ++i) {
      const std::string& t = tokens_[i];
      if (t.rfind("--", 0) != 0) continue;
      if (std::find(known.begin(), known.end(), t) == known.end()) return t;
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

std::vector<int> parseDepths(const std::string& text) {
  std::vector<int> depths;
  std::string current;
  for (const char ch : text + ",") {
    if (ch == ',') {
      if (!current.empty()) {
        depths.push_back(static_cast<int>(parseCount(current, "--depths", 0, 30)));
        current.clear();
      }
    } else if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
      current.push_back(ch);
    }
  }
  if (depths.empty()) throw Error("--depths listed no depths");
  return depths;
}

/// Which solver configurations each suite runs.
///
/// Lifted out of `main` so the answer to "what does --suite X actually
/// measure?" is one table in one place rather than a branch buried in a
/// hundred lines of setup.
std::vector<Config> buildConfigs(const std::string& suite, bool haveKorf,
                                 bool haveSeven, int maxDepth,
                                 std::chrono::milliseconds timeoutMs,
                                 int threads) {
  const auto twoPhase = [](const std::string& label, long budgetMs) {
    Config c;
    c.label = label;
    c.solver = SolverKind::TwoPhase;
    c.timeLimit = std::chrono::milliseconds{budgetMs};
    c.maxLength = 25;
    if (budgetMs == 0) c.targetLength = 25;  // first solution
    return c;
  };
  const auto optimal = [&](const std::string& label, korf::HeuristicMode mode,
                           bool ordering) {
    Config c;
    c.label = label;
    c.solver = SolverKind::Optimal;
    c.heuristic = mode;
    c.orderMoves = ordering;
    c.maxDepth = maxDepth;
    c.timeLimit = timeoutMs;
    c.threads = threads;
    return c;
  };

  std::vector<Config> configs;
  if (suite == "baseline") {
    configs.push_back(twoPhase("kociemba-200ms", 200));
    if (haveKorf) {
      configs.push_back(optimal("ida-no-heuristic", korf::HeuristicMode::None, false));
      configs.push_back(optimal("ida-corner-pdb", korf::HeuristicMode::CornerOnly, false));
      configs.push_back(optimal("ida-max-of-three", korf::HeuristicMode::MaxOfThree, false));
    }
  } else if (suite == "optimal" || suite == "known") {
    // Named for the heuristic it actually runs. The three-database mode is kept
    // here for continuity with the committed baseline results; the four-database
    // modes the CLI and viewer default to are measured by --suite heuristics.
    configs.push_back(optimal("ida-max-of-three", korf::HeuristicMode::MaxOfThree, false));
  } else if (suite == "heuristics") {
    configs.push_back(optimal("ida-no-heuristic", korf::HeuristicMode::None, false));
    configs.push_back(optimal("ida-corner-pdb", korf::HeuristicMode::CornerOnly, false));
    configs.push_back(optimal("ida-max-of-three", korf::HeuristicMode::MaxOfThree, false));
    // The four-database modes need the optional seven-edge database; without
    // it the solver would (rightly) refuse the request.
    if (haveSeven) {
      configs.push_back(optimal("ida-max-of-four", korf::HeuristicMode::MaxOfFour, false));
      configs.push_back(
          optimal("ida-max-of-four-inv", korf::HeuristicMode::MaxOfFourInverse, false));
    }
  } else if (suite == "threads") {
    // One variable changed at a time: same heuristic, same states, same
    // limits -- only the worker count differs.
    for (const int n : {1, 2, 4, 8}) {
      Config c = optimal("ida-" + std::to_string(n) + "-thread",
                         korf::HeuristicMode::MaxOfThree, false);
      c.threads = n;
      configs.push_back(c);
    }
  } else if (suite == "ordering") {
    configs.push_back(optimal("ida-ordering-off", korf::HeuristicMode::MaxOfThree, false));
    configs.push_back(optimal("ida-ordering-on", korf::HeuristicMode::MaxOfThree, true));
  } else if (suite == "twophase") {
    configs.push_back(twoPhase("kociemba-first", 0));
    configs.push_back(twoPhase("kociemba-10ms", 10));
    configs.push_back(twoPhase("kociemba-50ms", 50));
    configs.push_back(twoPhase("kociemba-200ms", 200));
  } else {
    throw Error("unknown suite '" + suite + "'");
  }
  return configs;
}

/// The heuristic-strength report: how large a lower bound each configuration
/// actually produces over random states.
///
/// This is the measurement that explains the optimal solver's depth ceiling --
/// the strongest configuration averages around 9 where a random cube needs
/// about 18 -- so it belongs in the committed harness rather than in a
/// throwaway program.
int runDistribution(const korf::KorfHeuristic& korf, std::uint64_t seed,
                    int stateCount) {
  std::cout << "=== Heuristic strength over " << stateCount
            << " random states ===\n\n";

  std::vector<Cube> states;
  states.reserve(static_cast<std::size_t>(stateCount));
  for (int i = 0; i < stateCount; ++i) {
    Cube cube;
    // 25 moves is past the diameter of the group, so these are random states.
    (void)cube.scramble(25, seed + static_cast<std::uint64_t>(i));
    states.push_back(cube);
  }
  // No true distances: solving 200,000 states optimally is not possible, so the
  // correlation column reads n/a. The means and the distribution are the point.
  const std::vector<int> trueDistances(states.size(), -1);

  std::vector<NamedHeuristic> heuristics;
  heuristics.emplace_back("corner", [&](const Cube& c) {
    return int(korf.corners().lookup(c));
  });
  heuristics.emplace_back("edge 6-A", [&](const Cube& c) {
    return int(korf.edgesA().lookup(c));
  });
  heuristics.emplace_back("edge 6-B", [&](const Cube& c) {
    return int(korf.edgesB().lookup(c));
  });
  heuristics.emplace_back("max of three", [&](const Cube& c) {
    return int(korf.estimate(c));
  });
  heuristics.emplace_back("max of three + inverse", [&](const Cube& c) {
    return std::max(int(korf.estimate(c)), int(korf.estimate(c.inverted())));
  });
  if (korf.hasSevenEdge()) {
    heuristics.emplace_back("edge 7", [&](const Cube& c) {
      return int(korf.edges7().lookup(c));
    });
    heuristics.emplace_back("max of four", [&](const Cube& c) {
      return int(korf.estimateWithSeven(c));
    });
    heuristics.emplace_back("max of four + inverse", [&](const Cube& c) {
      return std::max(int(korf.estimateWithSeven(c)),
                      int(korf.estimateWithSeven(c.inverted())));
    });
  } else {
    std::cout << "(the seven-edge database is not loaded; pass --with-7edge "
                 "after generating it to include the max-of-four rows)\n\n";
  }

  const auto distributions = analyseHeuristics(states, trueDistances, heuristics);
  std::cout << formatDistributions(distributions) << "\n";
  return 0;
}

void printRecord(const Record& r) {
  std::cout << "  " << std::left << std::setw(10) << r.caseId << std::setw(18)
            << r.config << std::right << std::setw(5)
            << (r.solutionLength >= 0 ? std::to_string(r.solutionLength) : "-")
            << std::setw(11) << std::fixed << std::setprecision(2) << r.milliseconds
            << " ms" << std::setw(13) << r.nodesExpanded << "  " << r.status
            << (r.verified ? " verified" : "") << "\n";
  std::cout.flush();
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Args args(argc, argv);
    if (args.has("--help") || args.has("-h")) {
      std::cout << kUsage;
      return 0;
    }

    const std::vector<std::string_view> known = {
        "--suite",   "--seed",      "--depths",  "--samples", "--timeout",
        "--max-depth", "--out",     "--data-dir", "--quiet",  "--threads",
        "--with-7edge", "--help",  "-h"};
    const std::vector<std::string_view> valueless = {"--quiet", "--with-7edge",
                                                     "--help", "-h"};
    if (const auto unknown = args.firstUnknown(known, valueless)) {
      std::cerr << "error: unknown option " << *unknown << "\n\n" << kUsage;
      return 2;
    }

    const std::string suite = args.value("--suite").value_or("baseline");
    const auto seed = static_cast<std::uint64_t>(
        args.value("--seed") ? parseCount(*args.value("--seed"), "--seed", 0, 2000000000L)
                             : 20260826L);
    const std::vector<int> depths =
        parseDepths(args.value("--depths").value_or("6,8,10,11,12,13"));
    // The distribution suite counts states rather than scrambles per depth,
    // so it needs a much larger ceiling and a much larger default.
    const bool distributionSuite = args.value("--suite").value_or("") == "distribution";
    const int samples = static_cast<int>(
        args.value("--samples")
            ? parseCount(*args.value("--samples"), "--samples", 1,
                         distributionSuite ? 2000000L : 1000L)
            : (distributionSuite ? 200000 : 4));
    const auto timeoutMs = std::chrono::milliseconds{
        args.value("--timeout") ? parseCount(*args.value("--timeout"), "--timeout", 0, 86400000)
                                : 300000L};
    const int maxDepth = static_cast<int>(
        args.value("--max-depth") ? parseCount(*args.value("--max-depth"), "--max-depth", 0, 20)
                                  : 20);
    const std::string dataDir = args.value("--data-dir").value_or("data");
    const int threads = static_cast<int>(
        args.value("--threads")
            ? parseCount(*args.value("--threads"), "--threads", 1, 64)
            : 1);
    const bool quiet = args.has("--quiet");

    const MachineInfo machine = MachineInfo::detect();
    std::cout << machine.describe() << "\n";

    // --- Load everything the suites need ---------------------------------
    const auto tableStart = std::chrono::steady_clock::now();
    auto moveTables = std::make_shared<const MoveTables>();
    auto twoPhase = std::make_shared<const TwoPhaseTables>();

    auto korf = std::make_shared<korf::KorfHeuristic>(*moveTables);
    const bool haveKorf = korf->corners().load(dataDir + "/corner.db") &&
                          korf->edgesA().load(dataDir + "/edge_a.db") &&
                          korf->edgesB().load(dataDir + "/edge_b.db");
    if (haveKorf && args.has("--with-7edge")) (void)korf->loadSeven(dataDir);
    const double setupSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - tableStart)
            .count();

    std::cout << "Setup:     " << std::fixed << std::setprecision(2) << setupSeconds
              << " s  (two-phase tables " << twoPhase->byteSize() / (1024 * 1024)
              << " MB";
    if (haveKorf) {
      std::cout << ", pattern databases " << korf->byteSize() / (1024 * 1024) << " MB";
    }
    std::cout << ")\n";
    if (!haveKorf) {
      std::cout << "WARNING:   pattern databases not found in '" << dataDir
                << "'; optimal-solver suites will be skipped.\n"
                << "           Run: rubiks_solver --generate-pdb\n";
    }
    std::cout << "\n";

    const Runner runner(twoPhase,
                        haveKorf ? std::shared_ptr<const korf::KorfHeuristic>(korf)
                                 : nullptr);

    if (suite == "distribution") {
      if (!haveKorf) {
        std::cerr << "error: --suite distribution needs the pattern databases\n";
        return 2;
      }
      return runDistribution(*korf, seed, samples);
    }

    if (suite == "micro") {
      std::cout << "=== Hot-loop primitives ===\n\n";
      const auto results =
          runMicrobenchmarks(*moveTables, haveKorf ? korf.get() : nullptr);
      std::cout << formatMicroResults(results) << "\n";
      return 0;
    }

    // --- Build the case set ----------------------------------------------
    std::vector<Case> caseSet = suite == "known"
                                    ? cases::knownPositions()
                                    : cases::scrambles(depths, samples, seed);

    std::cout << "Cases:     " << caseSet.size() << "  (seed " << seed
              << ", samples " << samples << ")\n";

    // Establish true optimal depths so results group by difficulty rather than
    // by scramble length.
    if (haveKorf && suite != "twophase" && suite != "known") {
      std::cout << "Resolving true optimal depths...\n";
      runner.resolveOptimalDepths(caseSet, timeoutMs, [&](const Case& c) {
        if (quiet) return;
        std::cout << "  " << std::left << std::setw(10) << c.id << " optimal = "
                  << (c.knownOptimal >= 0 ? std::to_string(c.knownOptimal)
                                          : std::string("unresolved"))
                  << "  (scramble " << c.scrambleLength() << ")\n";
        std::cout.flush();
      });
      std::cout << "\n";
    }

    const std::vector<Config> configs = buildConfigs(
        suite, haveKorf, haveKorf && korf->hasSevenEdge(), maxDepth, timeoutMs,
        threads);

    // --- Run ---------------------------------------------------------------
    std::vector<Record> all;
    for (const Config& config : configs) {
      if (config.solver == SolverKind::Optimal && !haveKorf) continue;

      // The uninformed search is only affordable on shallow cases; running it
      // on deep ones would stall the whole suite for hours.
      std::vector<Case> subset = caseSet;
      if (config.heuristic == korf::HeuristicMode::None &&
          config.solver == SolverKind::Optimal) {
        subset.erase(std::remove_if(subset.begin(), subset.end(),
                                    [](const Case& c) {
                                      return c.knownOptimal < 0 || c.knownOptimal > 7;
                                    }),
                     subset.end());
      } else if (config.heuristic == korf::HeuristicMode::CornerOnly &&
                 config.solver == SolverKind::Optimal) {
        subset.erase(std::remove_if(subset.begin(), subset.end(),
                                    [](const Case& c) {
                                      return c.knownOptimal < 0 || c.knownOptimal > 11;
                                    }),
                     subset.end());
      }

      std::cout << "=== " << config.label << " (" << subset.size() << " cases) ===\n";
      if (subset.empty()) {
        std::cout << "  (no cases in the affordable range)\n\n";
        continue;
      }
      auto records = runner.run(subset, config, quiet ? Runner::ProgressFn{}
                                                      : Runner::ProgressFn(printRecord));
      std::cout << "\n" << summarise(records) << "\n";
      all.insert(all.end(), records.begin(), records.end());
    }

    // --- Report ------------------------------------------------------------
    std::cout << "=== All records ===\n" << summarise(all) << "\n";

    const MemoryUsage memory = processMemory();
    if (memory.available()) {
      std::cout << "Peak working set: " << std::fixed << std::setprecision(1)
                << static_cast<double>(memory.peakBytes) / (1024 * 1024) << " MB\n";
    }

    int failures = 0;
    for (const Record& r : all) {
      if ((r.status == "optimal" || r.status == "solved") && !r.verified) ++failures;
    }
    std::cout << "Verification failures: " << failures << "\n";

    if (const auto out = args.value("--out")) {
      writeCsv(*out, machine, all);
      std::cout << "Wrote " << all.size() << " records to " << *out << "\n";
    }

    return failures == 0 ? 0 : 1;
  } catch (const rubik::Error& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "internal error: " << e.what() << "\n";
    return 1;
  }
}
