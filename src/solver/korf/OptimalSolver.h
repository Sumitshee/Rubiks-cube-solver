#pragma once

#include "core/Cube.h"
#include "core/Move.h"
#include "solver/korf/KorfHeuristic.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace rubik::korf {

/// Which heuristic the search should consult. Present so the value of the
/// pattern databases can be measured inside the real solver rather than only in
/// a toy search.
enum class HeuristicMode {
  None,        ///< Uninformed IDDFS. Only viable to about depth 8.
  CornerOnly,  ///< The corner database alone.
  MaxOfThree,  ///< Corner and both edge databases, combined with max.

  /// Adds a seven-edge database (243.6 MB) to the max. Admissible for the same
  /// reason the others are. Requires the database to have been generated.
  MaxOfFour,

  /// Adds a lookup on the inverse cube. A cube and its inverse are exactly the
  /// same distance from solved, so the heuristic evaluated on either is a valid
  /// lower bound for both, and their max is too. Costs no extra memory.
  MaxOfThreeInverse,

  /// Both of the above.
  MaxOfFourInverse,
};

/// How a search ended. The distinction matters: only `Optimal` licenses the
/// claim that the returned solution is a shortest one.
enum class OptimalOutcome {
  Optimal,            ///< Search completed; the solution is provably shortest.
  TimedOut,           ///< Ran out of time. Nothing is claimed about optimality.
  DepthLimitReached,  ///< Exhausted maxDepth without finding a solution.
  Cancelled,          ///< The caller asked it to stop. Nothing is claimed.
};

struct OptimalOptions {
  /// Ceiling on the search. God's number is 20, so a complete search never
  /// needs more; lowering it bounds the work when a quick answer will do.
  int maxDepth = 20;

  /// Hard limit. Zero means no limit.
  ///
  /// Unlike the two-phase solver's soft budget, this one is hard: IDA* has no
  /// "best so far" to fall back on, since it either proves the current bound
  /// or does not. Timing out therefore yields no solution at all -- but it does
  /// yield a proven lower bound, which is reported.
  std::chrono::milliseconds timeLimit{0};

  /// Try the most promising successors first, ordered by their heuristic value.
  ///
  /// **Off by default, because measurement said so.** Ordering can only help the
  /// final iteration -- every earlier one exhausts its tree regardless of order.
  /// It does cut nodes: at optimal length 12, 15,241 expansions become 11,611,
  /// a 24% reduction. But it is slower in wall-clock terms (51.2 ms against
  /// 45.1 ms) because it must compute the *full* heuristic for every child,
  /// forfeiting the early exit that stops at the first database already over
  /// budget, and must then apply each surviving move a second time.
  ///
  /// Fewer nodes is not the goal; less time is. Kept configurable so the
  /// measurement stays reproducible.
  bool orderMoves = false;

  HeuristicMode heuristic = HeuristicMode::MaxOfThree;

  /// Optional external cancellation, owned by the caller and outliving the
  /// call.
  ///
  /// Checked at the same periodic point as the wall clock -- once per 16,384
  /// expanded nodes -- so it adds nothing to the per-node cost. It exists
  /// because the GUI must be able to abandon a search when the user asks or
  /// when the window is closing, and an optimal search can otherwise run for
  /// hours.
  const std::atomic<bool>* cancelled = nullptr;

  /// Worker threads. One means the plain serial search, with no threading
  /// machinery involved at all.
  ///
  /// Parallelism is applied at the root: the eighteen first moves become
  /// independent subtrees handed out to workers. Each worker owns its cube, its
  /// move stack and its counters, so nothing mutable is shared. The pattern
  /// databases are read-only once loaded and are shared rather than copied.
  int threads = 1;
};

struct OptimalStats {
  /// Nodes whose successors were generated.
  std::uint64_t nodesExpanded = 0;
  /// Successors produced, whether or not they were explored.
  std::uint64_t nodesGenerated = 0;
  /// Successors discarded because g + h exceeded the current threshold.
  std::uint64_t nodesPruned = 0;
  /// Successors never generated at all, because the move was redundant.
  std::uint64_t movesSkipped = 0;

  int iterations = 0;
  int initialHeuristic = 0;
  int finalThreshold = 0;
  double elapsedSeconds = 0.0;
  /// Threads actually used.
  int threads = 1;
  /// Root branches explored across every threshold, summed. Used to judge how
  /// much work there was to hand out.
  std::uint64_t rootBranches = 0;
  /// Nodes expanded by the busiest and least busy worker during the final
  /// threshold. The gap between them is the load imbalance.
  std::uint64_t busiestWorkerNodes = 0;
  std::uint64_t idlestWorkerNodes = 0;

  /// Nodes expanded during each threshold iteration, in order. The geometric
  /// growth here is what makes IDA*'s re-expansion overhead a constant factor
  /// rather than a problem.
  std::vector<std::uint64_t> nodesPerIteration;
};

struct OptimalResult {
  OptimalOutcome outcome = OptimalOutcome::TimedOut;
  std::vector<Move> moves;
  OptimalStats stats;

  /// No solution shorter than this exists. Meaningful even when the search did
  /// not finish: every completed iteration rules out its own threshold.
  int provenLowerBound = 0;

  /// True only when the search completed and the solution is a shortest one.
  [[nodiscard]] bool isOptimal() const noexcept {
    return outcome == OptimalOutcome::Optimal;
  }
  [[nodiscard]] int length() const noexcept {
    return static_cast<int>(moves.size());
  }
};

/// Korf's optimal solver: IDA* over the full cube, guided by pattern databases.
///
/// ## Why IDA* rather than A* or BFS
///
/// The cube has 4.3 x 10^19 states and a solution can be 20 moves deep. A* or
/// breadth-first search would have to hold the frontier in memory, which at
/// depth 20 is astronomically beyond any machine. IDA* keeps only the current
/// path -- O(d) memory, about 20 moves -- and pays for it by re-expanding the
/// shallow part of the tree once per threshold. Because the tree grows
/// geometrically with an effective branching factor around 13.35, the total
/// work across all iterations is only about b/(b-1) ~ 1.08 times the final
/// iteration alone. Trading an 8% overhead for bounded memory is what makes the
/// problem tractable at all.
///
/// ## How the search is structured
///
/// One mutable cube, mutated in place by make/unmake, and one reusable move
/// stack. No cube is copied, nothing is allocated, and no container is built
/// during expansion.
///
/// The alternative -- pushing whole cube copies onto an explicit node stack and
/// building a container of successors at each node -- costs roughly eighteen
/// cube copies plus an allocation per expansion. At tens of millions of nodes
/// that allocation traffic dominates everything else.
///
/// ## What optimality rests on
///
/// IDA* returns a shortest solution provided the heuristic never overestimates.
/// That is argued in PatternDatabase's documentation and checked by its tests.
/// Given admissibility, a node pruned because g + h > bound cannot lie on any
/// solution of length <= bound, so an iteration that completes without success
/// proves no solution of that length exists.
class OptimalSolver {
 public:
  explicit OptimalSolver(std::shared_ptr<const KorfHeuristic> heuristic);

  /// Throws `InvalidStateError` for an unreachable cube, and `Error` if the
  /// databases have not been generated.
  [[nodiscard]] OptimalResult solve(const Cube& cube,
                                    const OptimalOptions& options = {}) const;

  [[nodiscard]] const KorfHeuristic& heuristic() const noexcept {
    return *heuristic_;
  }

 private:
  std::shared_ptr<const KorfHeuristic> heuristic_;
};

/// The strongest heuristic the loaded databases support.
///
/// Measured on the standard case set: adding the seven-edge database cuts nodes
/// to 0.46x and time to about 0.58x, and adding the inverse lookup on top takes
/// those to 0.29x and about 0.48x. Both are admissible, so nothing is traded
/// away for the speed. Falls back to the three-database maximum when the
/// seven-edge database has not been generated.
[[nodiscard]] HeuristicMode bestAvailableMode(const KorfHeuristic& heuristic) noexcept;

/// Human-readable outcome, for reports and error messages.
[[nodiscard]] const char* toString(OptimalOutcome outcome) noexcept;
[[nodiscard]] const char* toString(HeuristicMode mode) noexcept;

}  // namespace rubik::korf
