#pragma once

#include "core/Cube.h"
#include "core/Move.h"
#include "solver/TwoPhaseTables.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace rubik {

/// Knobs for a two-phase solve.
struct TwoPhaseOptions {
  /// Absolute ceiling on solution length. The search never looks beyond this.
  int maxLength = 25;

  /// How long to spend *improving* a solution once one has been found.
  ///
  /// Deliberately a soft limit: it is only consulted after the first solution
  /// exists, so the solver always returns something rather than timing out
  /// empty-handed. Zero means "keep improving until provably done".
  std::chrono::milliseconds timeLimit{200};

  /// Stop as soon as a solution this short or shorter is found. Zero disables
  /// the check. Useful when a good-enough answer now beats a better one later.
  int targetLength = 0;
};

/// What the search did. Kept separate from the solution so benchmarks can
/// report it without caring about the moves.
struct SearchStats {
  std::uint64_t phase1Nodes = 0;
  std::uint64_t phase2Nodes = 0;
  /// Phase-1 solutions found, i.e. distinct routes into G1 that were tried.
  std::uint64_t phase1Solutions = 0;
  /// Deepest phase-1 iteration started.
  int phase1Depth = 0;
  /// How many times a shorter total solution replaced the previous best.
  int improvements = 0;
  double elapsedSeconds = 0.0;
  bool stoppedOnTimeLimit = false;
  bool stoppedOnTarget = false;

  [[nodiscard]] std::uint64_t totalNodes() const noexcept {
    return phase1Nodes + phase2Nodes;
  }
};

/// A solution, plus how it was found.
struct Solution {
  std::vector<Move> moves;
  /// How many of `moves` belong to phase 1.
  int phase1Length = 0;
  bool found = false;
  SearchStats stats;

  [[nodiscard]] int length() const noexcept {
    return static_cast<int>(moves.size());
  }
  [[nodiscard]] int phase2Length() const noexcept {
    return length() - phase1Length;
  }
};

/// Kociemba's two-phase algorithm.
///
/// Phase 1 drives the cube into G1 = <U, D, R2, L2, F2, B2>: every edge
/// oriented, every corner twisted correctly, and the four slice edges somewhere
/// in the slice. Phase 2 finishes the cube using only those ten moves, which it
/// can do because G1 is closed under them.
///
/// The value of the split is that each phase searches a far smaller space than
/// the whole cube, and each is guided by exact pruning tables over its own
/// coordinates.
///
/// The catch is that the first solution found is rarely the shortest: a quick
/// route into G1 can leave a state that phase 2 solves slowly. So the search
/// does not stop at the first answer. It keeps enumerating longer phase-1
/// solutions, each of which may admit a shorter phase 2, and keeps the best
/// total. Every additional phase-1 depth trades time for a shorter solution,
/// which is what `TwoPhaseOptions::timeLimit` governs.
///
/// This solver is *not* optimal and does not claim to be -- that is Korf's job.
/// It reliably produces solutions around 20 moves in milliseconds.
class TwoPhaseSolver {
 public:
  /// Builds its own tables. This costs seconds; prefer sharing.
  TwoPhaseSolver();

  /// Shares prebuilt tables. The tables are immutable, so several solvers on
  /// several threads may share one instance safely.
  explicit TwoPhaseSolver(std::shared_ptr<const TwoPhaseTables> tables);

  /// Solves `cube`. Throws `InvalidStateError` if the cube is not a reachable
  /// state -- searching for a solution to an impossible cube would never
  /// terminate usefully.
  [[nodiscard]] Solution solve(const Cube& cube,
                               const TwoPhaseOptions& options = {}) const;

  [[nodiscard]] const TwoPhaseTables& tables() const noexcept { return *tables_; }
  [[nodiscard]] std::shared_ptr<const TwoPhaseTables> sharedTables() const noexcept {
    return tables_;
  }

 private:
  std::shared_ptr<const TwoPhaseTables> tables_;
};

}  // namespace rubik
