#include "ui/SolverService.h"

#include "core/Error.h"

#include <utility>

namespace rubik::ui {

const char* toString(SolverChoice choice) noexcept {
  return choice == SolverChoice::Kociemba ? "Kociemba two-phase" : "Korf IDA*";
}

SolverService::SolverService(std::shared_ptr<const TwoPhaseTables> twoPhase,
                             std::shared_ptr<const korf::KorfHeuristic> korf)
    : twoPhase_(std::move(twoPhase)), korf_(std::move(korf)) {}

SolverService::~SolverService() {
  cancel();
  join();
}

void SolverService::join() {
  if (worker_.joinable()) worker_.join();
}

void SolverService::cancel() { cancelRequested_.store(true); }

void SolverService::start(SolverChoice choice, const Cube& cube, int threads,
                          std::chrono::milliseconds timeLimit) {
  if (running_.load()) return;
  if (choice == SolverChoice::Korf && korf_ == nullptr) return;

  // A previous worker may have finished but not been joined yet.
  join();

  cancelRequested_.store(false);
  finished_.store(false);
  {
    const std::lock_guard<std::mutex> lock(reportMutex_);
    report_.reset();
    activity_ = std::string("solving with ") + toString(choice);
  }
  running_.store(true);

  // The cube is copied here, on the render thread, before the worker starts.
  // The worker therefore never touches the state the animation is mutating.
  const Cube snapshot = cube;
  worker_ = std::thread([this, choice, snapshot, threads, timeLimit] {
    try {
      if (choice == SolverChoice::Kociemba) {
        runKociemba(snapshot);
      } else {
        runKorf(snapshot, threads, timeLimit);
      }
    } catch (const std::exception& e) {
      SolveReport failure;
      failure.solver = choice;
      failure.solverName = toString(choice);
      failure.status = std::string("failed: ") + e.what();
      const std::lock_guard<std::mutex> lock(reportMutex_);
      report_ = std::move(failure);
    }
    finished_.store(true);
    running_.store(false);
  });
}

void SolverService::runKociemba(Cube cube) {
  SolveReport out;
  out.solver = SolverChoice::Kociemba;
  out.solverName = toString(SolverChoice::Kociemba);
  out.threads = 1;

  TwoPhaseOptions options;
  options.timeLimit = std::chrono::milliseconds{200};

  const TwoPhaseSolver solver(twoPhase_);
  const Solution solution = solver.solve(cube, options);

  out.moves = solution.moves;
  out.length = solution.length();
  out.seconds = solution.stats.elapsedSeconds;
  out.nodesExpanded = solution.stats.totalNodes();
  out.solved = solution.found;
  // Kociemba is not an optimal solver, so `optimal` stays false however good
  // the answer is.
  out.optimal = false;

  if (solution.found) {
    Cube check = cube;
    check.apply(solution.moves);
    out.verified = check.isSolved();
    out.status = out.verified ? "solved" : "SOLUTION DID NOT VERIFY";
  } else {
    out.status = "no solution within limits";
  }

  const std::lock_guard<std::mutex> lock(reportMutex_);
  report_ = std::move(out);
}

void SolverService::runKorf(Cube cube, int threads,
                            std::chrono::milliseconds timeLimit) {
  SolveReport out;
  out.solver = SolverChoice::Korf;
  out.solverName = toString(SolverChoice::Korf);
  out.threads = threads;

  korf::OptimalOptions options;
  options.maxDepth = 20;
  options.threads = threads;
  options.timeLimit = timeLimit;
  options.heuristic = korf::bestAvailableMode(*korf_);
  // The solver reads this every 16,384 nodes, which is how the window can be
  // closed part-way through a search that would otherwise run for hours.
  options.cancelled = &cancelRequested_;

  const korf::OptimalSolver solver(korf_);
  const korf::OptimalResult result = solver.solve(cube, options);

  out.moves = result.moves;
  out.length = result.length();
  out.seconds = result.stats.elapsedSeconds;
  out.nodesExpanded = result.stats.nodesExpanded;
  out.initialHeuristic = result.stats.initialHeuristic;
  out.threads = result.stats.threads;
  out.solved = result.isOptimal();
  out.optimal = result.isOptimal();

  if (result.isOptimal()) {
    Cube check = cube;
    check.apply(result.moves);
    out.verified = check.isSolved();
    out.status = out.verified ? "optimal" : "SOLUTION DID NOT VERIFY";
  } else {
    // Say what actually happened rather than implying a shorter answer exists.
    out.status = std::string(korf::toString(result.outcome)) +
                 " (no shorter solution than " +
                 std::to_string(result.provenLowerBound) + " moves exists)";
  }

  const std::lock_guard<std::mutex> lock(reportMutex_);
  report_ = std::move(out);
}

std::optional<SolveReport> SolverService::takeReport() {
  if (!finished_.load()) return std::nullopt;
  join();
  finished_.store(false);
  const std::lock_guard<std::mutex> lock(reportMutex_);
  std::optional<SolveReport> out = std::move(report_);
  report_.reset();
  activity_.clear();
  return out;
}

std::string SolverService::statusLine() const {
  if (running_.load()) {
    const std::lock_guard<std::mutex> lock(reportMutex_);
    return activity_.empty() ? std::string("solving...") : activity_;
  }
  return {};
}

}  // namespace rubik::ui
