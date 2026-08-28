#pragma once

#include "core/Cube.h"
#include "core/Move.h"
#include "solver/TwoPhaseSolver.h"
#include "solver/korf/OptimalSolver.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace rubik::ui {

/// Which solver a request runs.
enum class SolverChoice { Kociemba, Korf };

[[nodiscard]] const char* toString(SolverChoice choice) noexcept;

/// What a finished solve produced. Plain data, copied out under a lock.
struct SolveReport {
  SolverChoice solver = SolverChoice::Kociemba;
  std::string solverName;
  std::vector<Move> moves;
  bool solved = false;
  /// The moves were applied to the original cube and the result checked, in
  /// the worker, before the report was published.
  bool verified = false;
  /// True only when the optimal solver ran to completion. Never set by
  /// Kociemba, which does not claim optimality.
  bool optimal = false;
  std::string status;

  double seconds = 0.0;
  int length = 0;
  std::uint64_t nodesExpanded = 0;
  int initialHeuristic = -1;
  int threads = 1;
};

/// Runs solves on a worker thread so the render loop never blocks.
///
/// ## Threading
///
/// One request at a time. `start` spawns a worker; the render thread polls
/// `finished()` and takes the report. Nothing is shared with the worker except
/// an atomic status, an atomic cancellation flag, and the report itself behind
/// a mutex -- all touched at most a handful of times per solve.
///
/// The cube is *copied* into the request, so the worker never reads the state
/// the render thread is animating.
///
/// The pattern databases are read-only once loaded and are shared by
/// `shared_ptr`, never copied: at 82.65 MB (326 MB with the seven-edge
/// database) a per-solve copy would be absurd, and the solver is already
/// designed to share them across its own worker threads.
///
/// The destructor cancels and joins, so closing the window during an hour-long
/// optimal search does not hang or leak the thread.
class SolverService {
 public:
  SolverService(std::shared_ptr<const TwoPhaseTables> twoPhase,
                std::shared_ptr<const korf::KorfHeuristic> korf);
  ~SolverService();

  SolverService(const SolverService&) = delete;
  SolverService& operator=(const SolverService&) = delete;

  /// True when the optimal solver can be offered at all.
  [[nodiscard]] bool korfAvailable() const noexcept { return korf_ != nullptr; }

  /// Starts a solve. Ignored while one is already running.
  /// `threads` and `timeLimit` apply to the optimal solver.
  void start(SolverChoice choice, const Cube& cube, int threads,
             std::chrono::milliseconds timeLimit);

  [[nodiscard]] bool running() const noexcept { return running_.load(); }
  /// True when a report is waiting to be taken.
  [[nodiscard]] bool finished() const noexcept { return finished_.load(); }

  /// Asks the worker to stop. Returns immediately; the report will say
  /// "cancelled".
  void cancel();

  /// Takes the report, clearing the finished flag. Returns nullopt when none
  /// is ready.
  [[nodiscard]] std::optional<SolveReport> takeReport();

  /// A short line for the HUD: what the solver is doing right now.
  [[nodiscard]] std::string statusLine() const;

 private:
  void join();
  void runKociemba(Cube cube);
  void runKorf(Cube cube, int threads, std::chrono::milliseconds timeLimit);

  std::shared_ptr<const TwoPhaseTables> twoPhase_;
  std::shared_ptr<const korf::KorfHeuristic> korf_;

  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<bool> finished_{false};
  std::atomic<bool> cancelRequested_{false};

  mutable std::mutex reportMutex_;
  std::optional<SolveReport> report_;
  std::string activity_;
};

}  // namespace rubik::ui
