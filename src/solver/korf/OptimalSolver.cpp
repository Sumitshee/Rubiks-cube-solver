#include "solver/korf/OptimalSolver.h"

#include "core/Error.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace rubik::korf {
namespace {

/// God's number is 20, so a path can never usefully exceed that. The extra
/// headroom keeps the array indexing safe if maxDepth is set higher.
constexpr int kMaxPath = 32;

constexpr int kInfinite = std::numeric_limits<int>::max();

/// Nodes between wall-clock checks. Reading the clock per node would cost more
/// than the node itself; this is often enough to keep a timeout responsive.
constexpr std::uint64_t kClockInterval = 16384;

/// The only state shared between workers, and deliberately tiny.
///
/// `nextBranch` hands out root subtrees on demand rather than splitting them
/// up front, because root branches differ enormously in size -- some are pruned
/// immediately, others hold most of the tree. A worker that finishes early
/// simply takes the next one.
///
/// `stop` is read once per 16,384 expanded nodes, at the same point the search
/// already checks the wall clock, so the recursive hot path contains no atomic
/// operations at all.
struct SharedControl {
  std::atomic<bool> stop{false};
  std::atomic<int> nextBranch{0};

  std::mutex solutionMutex;
  bool haveSolution = false;
  std::vector<Move> solution;
};

/// One solve. Holds every piece of mutable state, so `OptimalSolver` stays
/// immutable and shareable across threads.
class Search {
 public:
  Search(const KorfHeuristic& heuristic, const Cube& cube,
         const OptimalOptions& options)
      : heuristic_(heuristic),
        cube_(cube),
        root_(cube),
        options_(options),
        moves_(heuristic.corners().abstraction().moveTables()),
        cornerPerm_(static_cast<std::uint16_t>(coord::cornerPermutation(cube))),
        cornerOri_(static_cast<std::uint16_t>(coord::cornerOrientation(cube))) {}

  OptimalResult run();

  /// Attaches this worker to a shared control block. Serial searches leave it
  /// null and never touch an atomic.
  void attach(SharedControl* shared) noexcept { shared_ = shared; }

  /// Explores the subtree reached by applying `m` to the start state.
  /// Returns true when this worker found a solution.
  bool exploreBranch(Move m, int bound);

  void beginThreshold() noexcept {
    nextBound_ = kInfinite;
    timedOut_ = false;
    cancelled_ = false;
  }
  void setStartTime(std::chrono::steady_clock::time_point t) noexcept { start_ = t; }

  [[nodiscard]] int rootHeuristic() const noexcept { return rootEstimate(); }
  [[nodiscard]] int nextBound() const noexcept { return nextBound_; }
  [[nodiscard]] bool timedOut() const noexcept { return timedOut_; }
  [[nodiscard]] bool abandoned() const noexcept { return abandoned_; }
  [[nodiscard]] const OptimalStats& stats() const noexcept { return stats_; }
  [[nodiscard]] std::vector<Move> solutionMoves() const {
    return std::vector<Move>(path_.begin(), path_.begin() + solutionLength_);
  }

 private:
  template <HeuristicMode Mode, bool Ordered>
  bool dfs(int depth, int bound, Move prev, bool hasPrev);

  template <HeuristicMode Mode, bool Ordered>
  bool exploreBranchTyped(Move m, int bound);

  /// Runs one threshold iteration, resolving both template parameters once.
  bool searchTo(int bound);

  /// A lower bound on the moves still needed, specialised per heuristic mode.
  ///
  /// `budget` is how many moves remain within the current threshold. The
  /// max-of-N paths stop as soon as one database already exceeds it: the
  /// pruning decision is then identical to computing the full maximum, but one
  /// or two cache misses are avoided.
  ///
  /// Templated rather than switched. When this was a runtime `switch`, the extra
  /// cases -- which call into the seven-edge database and construct an inverted
  /// cube -- bloated the function enough that the compiler stopped inlining it
  /// into `dfs`, and the *default* configuration slowed by about 1.6x even
  /// though its node count was unchanged. Resolving the mode at compile time
  /// gives every configuration its own tight loop.
  template <HeuristicMode Mode>
  [[nodiscard]] int estimate(int budget) const noexcept {
    const auto clamped = static_cast<std::uint8_t>(std::max(budget, 0));
    if constexpr (Mode == HeuristicMode::None) {
      (void)clamped;
      return 0;
    } else if constexpr (Mode == HeuristicMode::CornerOnly) {
      (void)clamped;
      return heuristic_.corners().lookupIndex(cornerIndex());
    } else if constexpr (Mode == HeuristicMode::MaxOfFour) {
      return heuristic_.estimateAtLeastWithSeven(cube_, cornerIndex(), clamped);
    } else if constexpr (Mode == HeuristicMode::MaxOfThreeInverse) {
      const int direct = heuristic_.estimateAtLeast(cube_, cornerIndex(), clamped);
      if (direct > budget) return direct;
      return std::max(direct,
                      int(heuristic_.estimateAtLeast(cube_.inverted(), clamped)));
    } else if constexpr (Mode == HeuristicMode::MaxOfFourInverse) {
      const int direct =
          heuristic_.estimateAtLeastWithSeven(cube_, cornerIndex(), clamped);
      if (direct > budget) return direct;
      const Cube inverse = cube_.inverted();
      return std::max(direct,
                      int(heuristic_.estimateAtLeastWithSeven(
                          inverse,
                          heuristic_.corners().abstraction().index(inverse),
                          clamped)));
    } else {
      return heuristic_.estimateAtLeast(cube_, cornerIndex(), clamped);
    }
  }

  /// The corner database index, from coordinates maintained through the move
  /// tables rather than recomputed from the cube.
  [[nodiscard]] std::uint32_t cornerIndex() const noexcept {
    return CornerAbstraction::indexOf(cornerPerm_, cornerOri_);
  }

  /// Applies a move to both the cube and the corner coordinates.
  void makeMove(Move m) noexcept {
    cube_.apply(m);
    cornerPerm_ = moves_.cornerPermutation().apply(cornerPerm_, m);
    cornerOri_ = moves_.cornerOrientation().apply(cornerOri_, m);
  }

  /// Undoes a move. The coordinates are restored from saved values rather than
  /// recomputed: two stack reads beat two more table lookups.
  void unmakeMove(Move m, std::uint16_t savedPerm, std::uint16_t savedOri) noexcept {
    cube_.undo(m);
    cornerPerm_ = savedPerm;
    cornerOri_ = savedOri;
  }

  /// The full maximum, without early exit. Used when ordering, where an
  /// accurate value matters because it decides the order.
  template <HeuristicMode Mode>
  [[nodiscard]] int fullEstimate() const noexcept {
    if constexpr (Mode == HeuristicMode::None) {
      return 0;
    } else if constexpr (Mode == HeuristicMode::CornerOnly) {
      return heuristic_.corners().lookupIndex(cornerIndex());
    } else if constexpr (Mode == HeuristicMode::MaxOfFour) {
      return heuristic_.estimateWithSeven(cube_);
    } else if constexpr (Mode == HeuristicMode::MaxOfThreeInverse) {
      return std::max(heuristic_.estimate(cube_),
                      heuristic_.estimate(cube_.inverted()));
    } else if constexpr (Mode == HeuristicMode::MaxOfFourInverse) {
      return std::max(heuristic_.estimateWithSeven(cube_),
                      heuristic_.estimateWithSeven(cube_.inverted()));
    } else {
      return heuristic_.estimate(cube_);
    }
  }

  /// The root heuristic, with the mode resolved at run time. Called once.
  [[nodiscard]] int rootEstimate() const noexcept {
    switch (options_.heuristic) {
      case HeuristicMode::None: return fullEstimate<HeuristicMode::None>();
      case HeuristicMode::CornerOnly: return fullEstimate<HeuristicMode::CornerOnly>();
      case HeuristicMode::MaxOfFour: return fullEstimate<HeuristicMode::MaxOfFour>();
      case HeuristicMode::MaxOfThreeInverse:
        return fullEstimate<HeuristicMode::MaxOfThreeInverse>();
      case HeuristicMode::MaxOfFourInverse:
        return fullEstimate<HeuristicMode::MaxOfFourInverse>();
      default: return fullEstimate<HeuristicMode::MaxOfThree>();
    }
  }

  [[nodiscard]] bool outOfTime() noexcept {
    if (options_.timeLimit.count() <= 0) return false;
    return std::chrono::steady_clock::now() - start_ >= options_.timeLimit;
  }

  const KorfHeuristic& heuristic_;
  Cube cube_;        ///< The single mutable cube the search walks.
  const Cube root_;  ///< Kept only to assert make/unmake stays balanced.
  OptimalOptions options_;

  /// Corner coordinates carried alongside the cube and updated by table lookup.
  const MoveTables& moves_;
  std::uint16_t cornerPerm_ = 0;
  std::uint16_t cornerOri_ = 0;

  std::array<Move, kMaxPath> path_{};
  int solutionLength_ = 0;
  int nextBound_ = kInfinite;
  bool timedOut_ = false;

  OptimalStats stats_;
  std::uint64_t clockCountdown_ = kClockInterval;
  std::chrono::steady_clock::time_point start_{};

  /// Null for a serial search.
  SharedControl* shared_ = nullptr;
  /// Set when another worker asked us to stop. Distinct from `timedOut_`,
  /// because being cancelled proves nothing about the threshold.
  bool cancelled_ = false;
  /// Set when the *caller* asked us to stop, through OptimalOptions::cancelled.
  bool abandoned_ = false;
};

template <HeuristicMode Mode, bool Ordered>
bool Search::dfs(int depth, int bound, Move prev, bool hasPrev) {
  ++stats_.nodesExpanded;

  if (--clockCountdown_ == 0) {
    clockCountdown_ = kClockInterval;
    // One relaxed load every 16,384 expanded nodes. Relaxed ordering is enough:
    // the flag is only a hint to stop early, and correctness never depends on
    // observing it promptly -- the coordinator joins every worker regardless.
    if (shared_ != nullptr && shared_->stop.load(std::memory_order_relaxed)) {
      cancelled_ = true;
      return false;
    }
    if (options_.cancelled != nullptr &&
        options_.cancelled->load(std::memory_order_relaxed)) {
      abandoned_ = true;
      cancelled_ = true;
      return false;
    }
    if (outOfTime()) {
      timedOut_ = true;
      return false;
    }
  }

  const int childDepth = depth + 1;
  const int budget = bound - childDepth;
  if (budget < 0) {
    // Any child would sit at f >= childDepth, which already exceeds the
    // threshold. Recording that keeps the next threshold well defined -- without
    // it an uninformed search (h == 0, first bound 0) would see no candidate
    // threshold at all and stop before it began.
    nextBound_ = std::min(nextBound_, childDepth);
    return false;
  }

  if constexpr (Ordered) {
    // Evaluate every surviving successor, then visit them cheapest-first.
    struct Candidate {
      Move move;
      int h;
    };
    std::array<Candidate, kNumMoves> candidates{};
    int count = 0;

    for (int i = 0; i < kNumMoves; ++i) {
      const Move m = static_cast<Move>(i);
      if (hasPrev && isRedundant(m, prev)) {
        ++stats_.movesSkipped;
        continue;
      }
      const std::uint16_t savedPerm = cornerPerm_;
      const std::uint16_t savedOri = cornerOri_;
      makeMove(m);
      ++stats_.nodesGenerated;
      const int h = fullEstimate<Mode>();
      unmakeMove(m, savedPerm, savedOri);

      if (childDepth + h > bound) {
        nextBound_ = std::min(nextBound_, childDepth + h);
        ++stats_.nodesPruned;
        continue;
      }
      candidates[static_cast<std::size_t>(count++)] = {m, h};
    }

    // Insertion sort: at most 15 entries, already nearly ordered, and no
    // allocation. std::sort would be slower here and would allocate nothing
    // either, but this keeps the intent obvious.
    for (int i = 1; i < count; ++i) {
      const Candidate key = candidates[static_cast<std::size_t>(i)];
      int j = i - 1;
      while (j >= 0 && candidates[static_cast<std::size_t>(j)].h > key.h) {
        candidates[static_cast<std::size_t>(j + 1)] = candidates[static_cast<std::size_t>(j)];
        --j;
      }
      candidates[static_cast<std::size_t>(j + 1)] = key;
    }

    for (int i = 0; i < count; ++i) {
      const Candidate& c = candidates[static_cast<std::size_t>(i)];
      const std::uint16_t savedPerm = cornerPerm_;
      const std::uint16_t savedOri = cornerOri_;
      makeMove(c.move);
      path_[static_cast<std::size_t>(depth)] = c.move;

      if (c.h == 0 && cube_.isSolved()) {
        solutionLength_ = childDepth;
        unmakeMove(c.move, savedPerm, savedOri);
        return true;
      }
      if (dfs<Mode, Ordered>(childDepth, bound, c.move, true)) {
        unmakeMove(c.move, savedPerm, savedOri);
        return true;
      }
      unmakeMove(c.move, savedPerm, savedOri);
      if (timedOut_ || cancelled_) return false;
    }
    return false;
  } else {
    for (int i = 0; i < kNumMoves; ++i) {
      const Move m = static_cast<Move>(i);
      if (hasPrev && isRedundant(m, prev)) {
        ++stats_.movesSkipped;
        continue;
      }

      const std::uint16_t savedPerm = cornerPerm_;
      const std::uint16_t savedOri = cornerOri_;
      makeMove(m);
      ++stats_.nodesGenerated;
      const int h = estimate<Mode>(budget);

      if (childDepth + h > bound) {
        nextBound_ = std::min(nextBound_, childDepth + h);
        ++stats_.nodesPruned;
      } else if (h == 0 && cube_.isSolved()) {
        path_[static_cast<std::size_t>(depth)] = m;
        solutionLength_ = childDepth;
        unmakeMove(m, savedPerm, savedOri);
        return true;
      } else {
        path_[static_cast<std::size_t>(depth)] = m;
        if (dfs<Mode, Ordered>(childDepth, bound, m, true)) {
          unmakeMove(m, savedPerm, savedOri);
          return true;
        }
      }

      unmakeMove(m, savedPerm, savedOri);
      if (timedOut_ || cancelled_) return false;
    }
    return false;
  }
}

template <HeuristicMode Mode, bool Ordered>
bool Search::exploreBranchTyped(Move m, int bound) {
  // Mirrors one iteration of the root loop in dfs, for a single first move.
  const int childDepth = 1;
  const int budget = bound - childDepth;
  if (budget < 0) {
    nextBound_ = std::min(nextBound_, childDepth);
    return false;
  }

  const std::uint16_t savedPerm = cornerPerm_;
  const std::uint16_t savedOri = cornerOri_;
  makeMove(m);
  ++stats_.nodesGenerated;

  bool found = false;
  const int h = Ordered ? fullEstimate<Mode>() : estimate<Mode>(budget);
  if (childDepth + h > bound) {
    nextBound_ = std::min(nextBound_, childDepth + h);
    ++stats_.nodesPruned;
  } else if (h == 0 && cube_.isSolved()) {
    path_[0] = m;
    solutionLength_ = childDepth;
    found = true;
  } else {
    path_[0] = m;
    found = dfs<Mode, Ordered>(childDepth, bound, m, true);
  }

  unmakeMove(m, savedPerm, savedOri);
  return found;
}

bool Search::exploreBranch(Move m, int bound) {
  const bool ordered = options_.orderMoves;
  switch (options_.heuristic) {
    case HeuristicMode::None:
      return ordered ? exploreBranchTyped<HeuristicMode::None, true>(m, bound)
                     : exploreBranchTyped<HeuristicMode::None, false>(m, bound);
    case HeuristicMode::CornerOnly:
      return ordered
                 ? exploreBranchTyped<HeuristicMode::CornerOnly, true>(m, bound)
                 : exploreBranchTyped<HeuristicMode::CornerOnly, false>(m, bound);
    case HeuristicMode::MaxOfFour:
      return ordered
                 ? exploreBranchTyped<HeuristicMode::MaxOfFour, true>(m, bound)
                 : exploreBranchTyped<HeuristicMode::MaxOfFour, false>(m, bound);
    case HeuristicMode::MaxOfThreeInverse:
      return ordered ? exploreBranchTyped<HeuristicMode::MaxOfThreeInverse, true>(
                           m, bound)
                     : exploreBranchTyped<HeuristicMode::MaxOfThreeInverse, false>(
                           m, bound);
    case HeuristicMode::MaxOfFourInverse:
      return ordered ? exploreBranchTyped<HeuristicMode::MaxOfFourInverse, true>(
                           m, bound)
                     : exploreBranchTyped<HeuristicMode::MaxOfFourInverse, false>(
                           m, bound);
    default:
      return ordered
                 ? exploreBranchTyped<HeuristicMode::MaxOfThree, true>(m, bound)
                 : exploreBranchTyped<HeuristicMode::MaxOfThree, false>(m, bound);
  }
}

/// Resolves the heuristic mode and the ordering flag to template arguments
/// exactly once per threshold, so the recursion below carries no dispatch.
bool Search::searchTo(int bound) {
  const bool ordered = options_.orderMoves;
  switch (options_.heuristic) {
    case HeuristicMode::None:
      return ordered ? dfs<HeuristicMode::None, true>(0, bound, Move::U, false)
                     : dfs<HeuristicMode::None, false>(0, bound, Move::U, false);
    case HeuristicMode::CornerOnly:
      return ordered
                 ? dfs<HeuristicMode::CornerOnly, true>(0, bound, Move::U, false)
                 : dfs<HeuristicMode::CornerOnly, false>(0, bound, Move::U, false);
    case HeuristicMode::MaxOfFour:
      return ordered
                 ? dfs<HeuristicMode::MaxOfFour, true>(0, bound, Move::U, false)
                 : dfs<HeuristicMode::MaxOfFour, false>(0, bound, Move::U, false);
    case HeuristicMode::MaxOfThreeInverse:
      return ordered ? dfs<HeuristicMode::MaxOfThreeInverse, true>(0, bound,
                                                                  Move::U, false)
                     : dfs<HeuristicMode::MaxOfThreeInverse, false>(0, bound,
                                                                   Move::U, false);
    case HeuristicMode::MaxOfFourInverse:
      return ordered ? dfs<HeuristicMode::MaxOfFourInverse, true>(0, bound,
                                                                 Move::U, false)
                     : dfs<HeuristicMode::MaxOfFourInverse, false>(0, bound,
                                                                  Move::U, false);
    default:
      return ordered
                 ? dfs<HeuristicMode::MaxOfThree, true>(0, bound, Move::U, false)
                 : dfs<HeuristicMode::MaxOfThree, false>(0, bound, Move::U, false);
  }
}

OptimalResult Search::run() {
  start_ = std::chrono::steady_clock::now();

  OptimalResult result;
  const auto finish = [&](OptimalOutcome outcome) {
    stats_.elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_)
            .count();
    result.outcome = outcome;
    result.stats = stats_;
    return result;
  };

  if (cube_.isSolved()) {
    stats_.initialHeuristic = 0;
    result.provenLowerBound = 0;
    return finish(OptimalOutcome::Optimal);
  }
  if (options_.cancelled != nullptr &&
      options_.cancelled->load(std::memory_order_relaxed)) {
    return finish(OptimalOutcome::Cancelled);
  }

  const int initial = rootEstimate();
  stats_.initialHeuristic = initial;
  result.provenLowerBound = initial;

  for (int bound = initial; bound <= options_.maxDepth; ++bound) {
    nextBound_ = kInfinite;
    const std::uint64_t before = stats_.nodesExpanded;

    stats_.finalThreshold = bound;
    ++stats_.iterations;
    result.provenLowerBound = bound;

    const bool found = searchTo(bound);

    stats_.nodesPerIteration.push_back(stats_.nodesExpanded - before);

    // Make and unmake must balance exactly, or the cube the search believes it
    // is looking at has drifted from reality.
    assert(cube_ == root_ && "make/unmake left the cube modified");
    assert(cornerPerm_ == coord::cornerPermutation(root_) &&
           cornerOri_ == coord::cornerOrientation(root_) &&
           "the maintained corner coordinates drifted from the cube");

    if (found) {
      result.moves.assign(path_.begin(), path_.begin() + solutionLength_);
      return finish(OptimalOutcome::Optimal);
    }
    if (abandoned_) return finish(OptimalOutcome::Cancelled);
    if (timedOut_) return finish(OptimalOutcome::TimedOut);

    // The iteration completed without a solution, so none of this length
    // exists: the optimum is strictly greater.
    result.provenLowerBound = bound + 1;

    // Skip straight to the smallest threshold that could admit a solution.
    // With an integer heuristic this is usually bound + 1, but it can jump.
    if (nextBound_ != kInfinite && nextBound_ > bound + 1) {
      bound = nextBound_ - 1;  // the loop's ++bound lands on nextBound_
      result.provenLowerBound = nextBound_;
    }
    if (nextBound_ == kInfinite) break;  // nothing left to explore
  }

  return finish(OptimalOutcome::DepthLimitReached);
}

/// Root-parallel IDA*.
///
/// ## Why the root, and why this is enough for optimality
///
/// Within a single threshold, *every* solution has the same length. Shorter
/// ones were already ruled out: each earlier threshold ran to completion
/// without success, and the jump to the next threshold uses the minimum
/// `g + h` over pruned nodes, which no shorter solution could beat. A solution
/// found at threshold `b` therefore has length exactly `b` and is optimal --
/// so whichever worker finds one first is as good as any other, and no
/// cross-worker comparison is needed.
///
/// That is what makes root splitting safe. The workers are only racing to
/// discover one of several equally optimal answers, never racing to find a
/// *better* one.
///
/// ## What is shared
///
/// Each worker owns a complete `Search`: its own cube, corner coordinates, move
/// stack, counters and recursion. The pattern databases are read-only after
/// loading and are shared by const reference rather than copied -- 326 MB per
/// worker would be untenable.
///
/// The only shared mutable state is a `SharedControl`: an atomic branch counter
/// (touched once per root subtree, at most eighteen times per threshold), an
/// atomic stop flag (read once per 16,384 expanded nodes), and a mutex held
/// only to publish a solution. The recursive hot path contains no
/// synchronisation whatsoever.
class ParallelSearch {
 public:
  ParallelSearch(const KorfHeuristic& heuristic, const Cube& cube,
                 const OptimalOptions& options)
      : root_(cube), options_(options) {
    const int count = std::max(1, options.threads);
    workers_.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      workers_.push_back(std::make_unique<Search>(heuristic, cube, options));
    }
  }

  OptimalResult run();

 private:
  Cube root_;
  OptimalOptions options_;
  std::vector<std::unique_ptr<Search>> workers_;
};

OptimalResult ParallelSearch::run() {
  const auto start = std::chrono::steady_clock::now();
  const int threadCount = static_cast<int>(workers_.size());

  OptimalResult result;
  result.stats.threads = threadCount;

  const auto finish = [&](OptimalOutcome outcome) {
    // Merge the workers' counters. They are only read after every thread has
    // been joined, so no synchronisation is needed here.
    for (const auto& worker : workers_) {
      const OptimalStats& w = worker->stats();
      result.stats.nodesExpanded += w.nodesExpanded;
      result.stats.nodesGenerated += w.nodesGenerated;
      result.stats.nodesPruned += w.nodesPruned;
      result.stats.movesSkipped += w.movesSkipped;
    }
    result.stats.elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    result.outcome = outcome;
    return result;
  };

  if (root_.isSolved()) {
    result.provenLowerBound = 0;
    return finish(OptimalOutcome::Optimal);
  }
  if (options_.cancelled != nullptr &&
      options_.cancelled->load(std::memory_order_relaxed)) {
    return finish(OptimalOutcome::Cancelled);
  }

  // Every root move is a candidate: at depth zero there is no previous move, so
  // nothing is redundant yet.
  std::vector<Move> rootMoves;
  rootMoves.reserve(kNumMoves);
  for (int i = 0; i < kNumMoves; ++i) rootMoves.push_back(static_cast<Move>(i));

  const int initial = workers_.front()->rootHeuristic();
  result.stats.initialHeuristic = initial;
  result.provenLowerBound = initial;

  for (int bound = initial; bound <= options_.maxDepth; ++bound) {
    result.stats.finalThreshold = bound;
    ++result.stats.iterations;
    result.provenLowerBound = bound;

    SharedControl shared;
    std::vector<std::uint64_t> nodesBefore(workers_.size());
    for (std::size_t i = 0; i < workers_.size(); ++i) {
      workers_[i]->beginThreshold();
      workers_[i]->attach(&shared);
      workers_[i]->setStartTime(start);
      nodesBefore[i] = workers_[i]->stats().nodesExpanded;
    }

    // The root node itself is expanded once per threshold, by the coordinator.
    ++result.stats.nodesExpanded;

    std::vector<std::thread> threads;
    threads.reserve(workers_.size());
    for (auto& worker : workers_) {
      threads.emplace_back([&worker, &shared, &rootMoves, bound] {
        for (;;) {
          if (shared.stop.load(std::memory_order_relaxed)) break;
          const int index = shared.nextBranch.fetch_add(1, std::memory_order_relaxed);
          if (index >= static_cast<int>(rootMoves.size())) break;

          if (worker->exploreBranch(rootMoves[static_cast<std::size_t>(index)],
                                    bound)) {
            std::lock_guard<std::mutex> lock(shared.solutionMutex);
            if (!shared.haveSolution) {
              shared.haveSolution = true;
              shared.solution = worker->solutionMoves();
            }
            shared.stop.store(true, std::memory_order_relaxed);
            break;
          }
          if (worker->timedOut() || worker->abandoned()) {
            shared.stop.store(true, std::memory_order_relaxed);
            break;
          }
        }
      });
    }
    for (std::thread& t : threads) t.join();

    // Detach before the control block goes out of scope.
    for (auto& worker : workers_) worker->attach(nullptr);

    result.stats.rootBranches +=
        static_cast<std::uint64_t>(std::min<int>(shared.nextBranch.load(),
                                                 static_cast<int>(rootMoves.size())));

    std::uint64_t busiest = 0;
    std::uint64_t idlest = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t iterationNodes = 0;
    for (std::size_t i = 0; i < workers_.size(); ++i) {
      const std::uint64_t did = workers_[i]->stats().nodesExpanded - nodesBefore[i];
      busiest = std::max(busiest, did);
      idlest = std::min(idlest, did);
      iterationNodes += did;
    }
    result.stats.busiestWorkerNodes = busiest;
    result.stats.idlestWorkerNodes = idlest == std::numeric_limits<std::uint64_t>::max()
                                         ? 0
                                         : idlest;
    result.stats.nodesPerIteration.push_back(iterationNodes);

    if (shared.haveSolution) {
      result.moves = shared.solution;
      return finish(OptimalOutcome::Optimal);
    }

    bool abandoned = false;
    for (const auto& worker : workers_) abandoned |= worker->abandoned();
    if (abandoned) return finish(OptimalOutcome::Cancelled);

    bool timedOut = false;
    for (const auto& worker : workers_) timedOut |= worker->timedOut();
    if (timedOut) return finish(OptimalOutcome::TimedOut);

    // No solution at this threshold, so none of this length exists.
    result.provenLowerBound = bound + 1;

    int nextBound = kInfinite;
    for (const auto& worker : workers_) {
      nextBound = std::min(nextBound, worker->nextBound());
    }
    if (nextBound == kInfinite) break;
    if (nextBound > bound + 1) {
      bound = nextBound - 1;  // the loop's ++bound lands on nextBound
      result.provenLowerBound = nextBound;
    }
  }

  return finish(OptimalOutcome::DepthLimitReached);
}

}  // namespace

OptimalSolver::OptimalSolver(std::shared_ptr<const KorfHeuristic> heuristic)
    : heuristic_(std::move(heuristic)) {
  if (!heuristic_) throw Error("OptimalSolver constructed with a null heuristic");
}

OptimalResult OptimalSolver::solve(const Cube& cube,
                                   const OptimalOptions& options) const {
  // An unreachable state has no solution; searching for one would exhaust the
  // whole space to discover that.
  cube.validate();

  if (options.heuristic != HeuristicMode::None && !heuristic_->ready()) {
    throw Error(
        "the pattern databases have not been loaded; run --generate-pdb first");
  }
  if ((options.heuristic == HeuristicMode::MaxOfFour ||
       options.heuristic == HeuristicMode::MaxOfFourInverse) &&
      !heuristic_->hasSevenEdge()) {
    throw Error(
        "this heuristic needs the seven-edge database, which is not loaded");
  }
  if (options.maxDepth < 0 || options.maxDepth >= kMaxPath) {
    throw Error("maxDepth must be between 0 and " + std::to_string(kMaxPath - 1) +
                " (got " + std::to_string(options.maxDepth) + ")");
  }

  if (options.threads <= 1) {
    Search search(*heuristic_, cube, options);
    return search.run();
  }
  ParallelSearch parallel(*heuristic_, cube, options);
  return parallel.run();
}

HeuristicMode bestAvailableMode(const KorfHeuristic& heuristic) noexcept {
  return heuristic.hasSevenEdge() ? HeuristicMode::MaxOfFourInverse
                                  : HeuristicMode::MaxOfThree;
}

const char* toString(OptimalOutcome outcome) noexcept {
  switch (outcome) {
    case OptimalOutcome::Optimal: return "optimal";
    case OptimalOutcome::TimedOut: return "timed out";
    case OptimalOutcome::Cancelled: return "cancelled";
    default: return "depth limit reached";
  }
}

const char* toString(HeuristicMode mode) noexcept {
  switch (mode) {
    case HeuristicMode::None: return "none";
    case HeuristicMode::CornerOnly: return "corner PDB";
    case HeuristicMode::MaxOfFour: return "max of 4 PDBs";
    case HeuristicMode::MaxOfThreeInverse: return "max of 3 + inverse";
    case HeuristicMode::MaxOfFourInverse: return "max of 4 + inverse";
    default: return "max of 3 PDBs";
  }
}

}  // namespace rubik::korf
