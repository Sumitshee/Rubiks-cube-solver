#include "bench/Benchmark.h"

#include "bench/ProcessMemory.h"
#include "core/Error.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>
#include <thread>

#if defined(_WIN32)
// clang-format off
// windows.h defines min/max macros that break std::min and std::max, and pulls
// in a great deal that is not needed here.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// clang-format on
#endif

namespace rubik::bench {

const char* toString(SolverKind kind) noexcept {
  return kind == SolverKind::TwoPhase ? "kociemba-two-phase" : "korf-ida*";
}

// ---------------------------------------------------------------------------
// Machine description
// ---------------------------------------------------------------------------

namespace {

std::string detectCpuName() {
#if defined(_WIN32)
  HKEY key{};
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                    R"(HARDWARE\DESCRIPTION\System\CentralProcessor\0)", 0,
                    KEY_READ, &key) == ERROR_SUCCESS) {
    char buffer[256]{};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const bool ok = RegQueryValueExA(key, "ProcessorNameString", nullptr, &type,
                                     reinterpret_cast<LPBYTE>(buffer),
                                     &size) == ERROR_SUCCESS;
    RegCloseKey(key);
    if (ok) {
      std::string name(buffer);
      // Collapse the padding the registry value carries.
      while (!name.empty() && (name.back() == ' ' || name.back() == '\0')) name.pop_back();
      return name;
    }
  }
#endif
  return "unknown";
}

double detectRamGigabytes() {
#if defined(_WIN32)
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) {
    return static_cast<double>(status.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
  }
#endif
  return 0.0;
}

std::string compilerName() {
#if defined(_MSC_VER)
  return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
  return "Clang " __clang_version__;
#elif defined(__GNUC__)
  return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
  return "unknown";
#endif
}

std::string buildTypeName() {
  // NDEBUG is the only portable signal of an optimised build.
#if defined(NDEBUG)
  return "Release/RelWithDebInfo (NDEBUG)";
#else
  return "Debug (assertions on)";
#endif
}

std::string nowIso8601() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buffer[32]{};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm);
  return buffer;
}

std::string heuristicName(const Config& config) {
  if (config.solver == SolverKind::TwoPhase) return "two-phase pruning tables";
  return korf::toString(config.heuristic);
}

template <typename T>
double mean(const std::vector<T>& values) {
  if (values.empty()) return 0.0;
  return std::accumulate(values.begin(), values.end(), 0.0) /
         static_cast<double>(values.size());
}

template <typename T>
double median(std::vector<T> values) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const std::size_t mid = values.size() / 2;
  if (values.size() % 2 == 1) return static_cast<double>(values[mid]);
  return (static_cast<double>(values[mid - 1]) + static_cast<double>(values[mid])) / 2.0;
}

}  // namespace

MachineInfo MachineInfo::detect() {
  MachineInfo info;
  info.cpu = detectCpuName();
  info.logicalCores = static_cast<int>(std::thread::hardware_concurrency());
  info.ramGigabytes = detectRamGigabytes();
  info.compiler = compilerName();
  info.buildType = buildTypeName();
  info.timestamp = nowIso8601();
  info.cubeVersion = "1.0";
  return info;
}

std::string MachineInfo::describe() const {
  std::ostringstream out;
  out << "CPU:       " << cpu << " (" << logicalCores << " logical cores)\n"
      << "RAM:       " << std::fixed << std::setprecision(2) << ramGigabytes << " GB\n"
      << "Compiler:  " << compiler << "\n"
      << "Build:     " << buildType << "\n"
      << "Timestamp: " << timestamp << "\n";
  return out.str();
}

// ---------------------------------------------------------------------------
// Case sets
// ---------------------------------------------------------------------------

namespace cases {

std::vector<Case> scrambles(const std::vector<int>& depths, int samples,
                            std::uint64_t baseSeed) {
  std::vector<Case> out;
  out.reserve(depths.size() * static_cast<std::size_t>(std::max(samples, 0)));

  for (const int depth : depths) {
    for (int i = 0; i < samples; ++i) {
      // A pure function of the three inputs, so a case keeps its identity no
      // matter what else is asked for in the same run.
      const std::uint64_t seed = baseSeed + static_cast<std::uint64_t>(depth) * 1000003ull +
                                 static_cast<std::uint64_t>(i);
      Case c;
      c.cube = Cube{};
      c.scramble = c.cube.scramble(depth, seed);
      c.id = "d" + std::to_string(depth) + "-" + std::to_string(i);
      out.push_back(std::move(c));
    }
  }
  return out;
}

std::vector<Case> knownPositions() {
  std::vector<Case> out;

  const auto add = [&out](const std::string& id, const std::string& alg,
                          int knownOptimal) {
    Case c;
    c.scramble = parseSequence(alg);
    c.cube.apply(c.scramble);
    c.id = id;
    c.knownOptimal = knownOptimal;
    out.push_back(std::move(c));
  };

  // The superflip: every edge flipped in place. One of the positions that
  // establishes God's number at 20, so its optimal length is known exactly --
  // though far beyond what this configuration can search.
  add("superflip", "U R2 F B R B2 R U2 L B2 R U' D' R2 F R' L B2 U2 F2", 20);
  // Short positions whose optimal length is provable by inspection.
  add("single-R", "R", 1);
  add("sexy-move", "R U R' U'", 4);
  add("sune", "R U R' U R U2 R'", 7);

  return out;
}

}  // namespace cases

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

Runner::Runner(std::shared_ptr<const TwoPhaseTables> twoPhaseTables,
               std::shared_ptr<const korf::KorfHeuristic> korfHeuristic)
    : twoPhaseTables_(std::move(twoPhaseTables)),
      korfHeuristic_(std::move(korfHeuristic)) {}

Record Runner::runTwoPhase(const Case& c, const Config& config) const {
  if (!twoPhaseTables_) throw Error("two-phase tables were not supplied");

  TwoPhaseOptions options;
  options.maxLength = config.maxLength;
  options.timeLimit = config.timeLimit;
  options.targetLength = config.targetLength;

  const TwoPhaseSolver solver(twoPhaseTables_);
  const Solution solution = solver.solve(c.cube, options);

  Record record;
  record.solutionLength = solution.found ? solution.length() : -1;
  record.milliseconds = solution.stats.elapsedSeconds * 1000.0;
  record.nodesExpanded = solution.stats.totalNodes();
  record.nodesGenerated = solution.stats.totalNodes();
  record.iterations = solution.stats.phase1Depth;
  record.status = solution.found ? "solved" : "no-solution";

  if (solution.found) {
    Cube check = c.cube;
    check.apply(solution.moves);
    record.verified = check.isSolved();
  }
  return record;
}

Record Runner::runOptimal(const Case& c, const Config& config) const {
  if (!korfHeuristic_) throw Error("the Korf heuristic was not supplied");

  korf::OptimalOptions options;
  options.maxDepth = config.maxDepth;
  options.timeLimit = config.timeLimit;
  options.orderMoves = config.orderMoves;
  options.heuristic = config.heuristic;
  options.threads = config.threads;

  const korf::OptimalSolver solver(korfHeuristic_);
  const korf::OptimalResult result = solver.solve(c.cube, options);

  Record record;
  record.solutionLength = result.isOptimal() ? result.length() : -1;
  record.milliseconds = result.stats.elapsedSeconds * 1000.0;
  record.nodesExpanded = result.stats.nodesExpanded;
  record.nodesGenerated = result.stats.nodesGenerated;
  record.nodesPruned = result.stats.nodesPruned;
  record.movesSkipped = result.stats.movesSkipped;
  record.iterations = result.stats.iterations;
  record.initialHeuristic = result.stats.initialHeuristic;
  record.finalThreshold = result.stats.finalThreshold;
  record.provenLowerBound = result.provenLowerBound;
  record.threads = result.stats.threads;

  switch (result.outcome) {
    case korf::OptimalOutcome::Optimal: record.status = "optimal"; break;
    case korf::OptimalOutcome::TimedOut: record.status = "timeout"; break;
    default: record.status = "depth-limit"; break;
  }

  if (result.isOptimal()) {
    Cube check = c.cube;
    check.apply(result.moves);
    record.verified = check.isSolved();
  }
  return record;
}

std::vector<Record> Runner::run(const std::vector<Case>& cases,
                                const Config& config,
                                const ProgressFn& onRecord) const {
  std::vector<Record> records;
  records.reserve(cases.size());

  for (const Case& c : cases) {
    Record record = config.solver == SolverKind::TwoPhase ? runTwoPhase(c, config)
                                                          : runOptimal(c, config);
    record.caseId = c.id;
    record.scramble = toString(c.scramble);
    record.scrambleLength = c.scrambleLength();
    record.knownOptimal = c.knownOptimal;
    record.config = config.label;
    record.solver = toString(config.solver);
    record.heuristic = heuristicName(config);
    record.orderMoves = config.orderMoves;
    record.threads = config.threads;
    record.timeLimitMs = config.timeLimit.count();
    record.peakMemoryBytes = processMemory().peakBytes;

    if (onRecord) onRecord(record);
    records.push_back(std::move(record));
  }
  return records;
}

void Runner::resolveOptimalDepths(
    std::vector<Case>& cases, std::chrono::milliseconds timeLimit,
    const std::function<void(const Case&)>& onResolved) const {
  if (!korfHeuristic_) throw Error("the Korf heuristic was not supplied");

  korf::OptimalOptions options;
  options.maxDepth = 20;
  options.timeLimit = timeLimit;
  const korf::OptimalSolver solver(korfHeuristic_);

  for (Case& c : cases) {
    if (c.knownOptimal >= 0) continue;  // already established
    const korf::OptimalResult result = solver.solve(c.cube, options);
    if (result.isOptimal()) c.knownOptimal = result.length();
    if (onResolved) onResolved(c);
  }
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

namespace {

std::string csvEscape(const std::string& field) {
  if (field.find_first_of(",\"\n") == std::string::npos) return field;
  std::string out = "\"";
  for (const char ch : field) {
    if (ch == '"') out += "\"\"";
    else out += ch;
  }
  out += "\"";
  return out;
}

}  // namespace

void writeCsv(const std::string& path, const MachineInfo& machine,
              const std::vector<Record>& records) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) throw Error("cannot open '" + path + "' for writing");

  out << "# cpu," << csvEscape(machine.cpu) << "\n"
      << "# logical_cores," << machine.logicalCores << "\n"
      << "# ram_gb," << std::fixed << std::setprecision(2) << machine.ramGigabytes << "\n"
      << "# compiler," << csvEscape(machine.compiler) << "\n"
      << "# build," << csvEscape(machine.buildType) << "\n"
      << "# timestamp," << machine.timestamp << "\n";

  out << "case_id,scramble,scramble_length,known_optimal,config,solver,heuristic,"
         "order_moves,threads,time_limit_ms,solution_length,milliseconds,"
         "nodes_expanded,nodes_generated,nodes_pruned,moves_skipped,iterations,"
         "initial_heuristic,final_threshold,proven_lower_bound,peak_memory_bytes,"
         "status,verified\n";

  for (const Record& r : records) {
    out << csvEscape(r.caseId) << ',' << csvEscape(r.scramble) << ','
        << r.scrambleLength << ',' << r.knownOptimal << ','
        << csvEscape(r.config) << ',' << csvEscape(r.solver) << ','
        << csvEscape(r.heuristic) << ',' << (r.orderMoves ? 1 : 0) << ','
        << r.threads << ',' << r.timeLimitMs << ',' << r.solutionLength << ','
        << std::fixed << std::setprecision(3) << r.milliseconds << ','
        << r.nodesExpanded << ',' << r.nodesGenerated << ',' << r.nodesPruned
        << ',' << r.movesSkipped << ',' << r.iterations << ','
        << r.initialHeuristic << ',' << r.finalThreshold << ','
        << r.provenLowerBound << ',' << r.peakMemoryBytes << ','
        << csvEscape(r.status) << ',' << (r.verified ? 1 : 0) << '\n';
  }
}

std::string summarise(const std::vector<Record>& records) {
  // Group by difficulty: true optimal length when known, scramble length
  // otherwise. Mixing the two would be misleading, so the key records which.
  struct Key {
    int value;
    bool isOptimal;
    bool operator<(const Key& other) const {
      if (isOptimal != other.isOptimal) return isOptimal > other.isOptimal;
      return value < other.value;
    }
  };

  std::map<Key, std::vector<const Record*>> groups;
  for (const Record& r : records) {
    const bool known = r.knownOptimal >= 0;
    groups[Key{known ? r.knownOptimal : r.scrambleLength, known}].push_back(&r);
  }

  std::ostringstream out;
  out << std::left << std::setw(12) << "difficulty" << std::right << std::setw(6)
      << "n" << std::setw(7) << "ok" << std::setw(11) << "mean ms"
      << std::setw(11) << "median ms" << std::setw(11) << "max ms"
      << std::setw(15) << "mean expanded" << std::setw(15) << "mean pruned"
      << std::setw(7) << "iters" << std::setw(6) << "len" << "\n";
  out << std::string(106, '-') << "\n";

  for (const auto& [key, rows] : groups) {
    std::vector<double> times;
    std::vector<double> expanded;
    std::vector<double> pruned;
    std::vector<double> iterations;
    std::vector<double> lengths;
    int succeeded = 0;

    for (const Record* r : rows) {
      const bool ok = r->status == "optimal" || r->status == "solved";
      if (!ok) continue;
      ++succeeded;
      times.push_back(r->milliseconds);
      expanded.push_back(static_cast<double>(r->nodesExpanded));
      pruned.push_back(static_cast<double>(r->nodesPruned));
      iterations.push_back(r->iterations);
      lengths.push_back(r->solutionLength);
    }

    const std::string label =
        (key.isOptimal ? "opt " : "scr ") + std::to_string(key.value);
    out << std::left << std::setw(12) << label << std::right << std::setw(6)
        << rows.size() << std::setw(7) << succeeded << std::fixed;
    if (succeeded == 0) {
      out << std::setw(11) << "-" << std::setw(11) << "-" << std::setw(11) << "-"
          << std::setw(15) << "-" << std::setw(15) << "-" << std::setw(7) << "-"
          << std::setw(6) << "-" << "\n";
      continue;
    }
    out << std::setprecision(2) << std::setw(11) << mean(times) << std::setw(11)
        << median(times) << std::setw(11)
        << *std::max_element(times.begin(), times.end()) << std::setprecision(0)
        << std::setw(15) << mean(expanded) << std::setw(15) << mean(pruned)
        << std::setprecision(1) << std::setw(7) << mean(iterations)
        << std::setw(6) << mean(lengths) << "\n";
  }
  return out.str();
}

}  // namespace rubik::bench
