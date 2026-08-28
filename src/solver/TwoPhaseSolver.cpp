#include "solver/TwoPhaseSolver.h"

#include "core/Error.h"
#include "solver/Coordinate.h"

#include <array>
#include <cassert>

namespace rubik {
namespace {

/// The ten moves that stay inside G1, in the order phase 2 tries them.
constexpr std::array<Move, 10> kPhase2Moves = {
    Move::U,  Move::U2, Move::Up, Move::D,  Move::D2,
    Move::Dp, Move::R2, Move::L2, Move::F2, Move::B2};

[[nodiscard]] constexpr bool isPhase2Move(Move m) noexcept {
  return ((kPhase2MoveMask >> static_cast<std::uint8_t>(m)) & 1u) != 0u;
}

/// Solutions cannot exceed Options::maxLength, which the caller is not allowed
/// to set above this. God's number is 20, so anything near 30 is already far
/// beyond what a two-phase search would ever return.
constexpr int kMaxPathLength = 32;

/// One solve. Holds all mutable search state, so `TwoPhaseSolver` itself stays
/// immutable and safe to share across threads.
class Search {
 public:
  Search(const TwoPhaseTables& tables, const Cube& cube,
         const TwoPhaseOptions& options)
      : tables_(tables), start_(cube), options_(options) {}

  Solution run();

 private:
  void phase1(int remaining, std::uint16_t twist, std::uint16_t flip,
              std::uint16_t sliceSorted, int depth);
  void tryPhase2(int phase1Length);
  bool phase2(int remaining, std::uint16_t cornerPerm, std::uint16_t udEdgePerm,
              std::uint16_t slicePerm, int depth);

  /// Soft limit: only consulted once a solution exists, so the solver never
  /// returns empty-handed just because time ran out.
  [[nodiscard]] bool shouldStopImproving();

  void recordSolution(int phase1Length, int phase2Length);

  const TwoPhaseTables& tables_;
  Cube start_;
  TwoPhaseOptions options_;

  std::array<Move, kMaxPathLength> path1_{};
  std::array<Move, kMaxPathLength> path2_{};

  /// The last phase-1 move, so phase 2 does not begin with a turn of the same
  /// face and produce a non-canonical (and longer) solution.
  Move boundaryMove_ = Move::U;
  bool hasBoundaryMove_ = false;

  Solution best_;
  int bestLength_ = 0;
  bool stop_ = false;

  std::chrono::steady_clock::time_point startTime_{};
  std::uint64_t timeCheckCountdown_ = 0;
};

bool Search::shouldStopImproving() {
  if (!best_.found) return false;
  if (options_.timeLimit.count() <= 0) return false;
  const auto elapsed = std::chrono::steady_clock::now() - startTime_;
  return elapsed >= options_.timeLimit;
}

void Search::recordSolution(int phase1Length, int phase2Length) {
  best_.moves.clear();
  best_.moves.reserve(static_cast<std::size_t>(phase1Length + phase2Length));
  for (int i = 0; i < phase1Length; ++i) best_.moves.push_back(path1_[static_cast<std::size_t>(i)]);
  for (int i = 0; i < phase2Length; ++i) best_.moves.push_back(path2_[static_cast<std::size_t>(i)]);
  best_.phase1Length = phase1Length;
  best_.found = true;
  bestLength_ = phase1Length + phase2Length;
  ++best_.stats.improvements;

  if (options_.targetLength > 0 && bestLength_ <= options_.targetLength) {
    best_.stats.stoppedOnTarget = true;
    stop_ = true;
  }
}

bool Search::phase2(int remaining, std::uint16_t cornerPerm,
                    std::uint16_t udEdgePerm, std::uint16_t slicePerm,
                    int depth) {
  if (remaining == 0) {
    return cornerPerm == 0 && udEdgePerm == 0 && slicePerm == 0;
  }

  const MoveTable& cornerT = tables_.moves().cornerPermutation();
  const MoveTable& edgeT = tables_.moves().udEdgePermutation();
  const MoveTable& sliceT = tables_.moves().slicePermutation();

  // The move to prune against is the previous phase-2 move, or -- at the very
  // start -- the last move of phase 1.
  const bool hasPrev = depth > 0 || hasBoundaryMove_;
  const Move prev = depth > 0 ? path2_[static_cast<std::size_t>(depth - 1)]
                              : boundaryMove_;

  for (const Move m : kPhase2Moves) {
    if (hasPrev && isRedundant(m, prev)) continue;

    const std::uint16_t nextCorner = cornerT.apply(cornerPerm, m);
    const std::uint16_t nextEdge = edgeT.apply(udEdgePerm, m);
    const std::uint16_t nextSlice = sliceT.apply(slicePerm, m);

    if (tables_.phase2Heuristic(nextCorner, nextEdge, nextSlice) > remaining - 1) {
      continue;
    }

    ++best_.stats.phase2Nodes;
    path2_[static_cast<std::size_t>(depth)] = m;
    if (phase2(remaining - 1, nextCorner, nextEdge, nextSlice, depth + 1)) {
      return true;
    }
  }
  return false;
}

void Search::tryPhase2(int phase1Length) {
  ++best_.stats.phase1Solutions;

  // Replay phase 1 onto a copy of the cube to read the phase-2 coordinates.
  //
  // The alternative -- threading extra coordinates through phase 1 the way the
  // reference implementation does -- is measurably faster per phase-1 solution
  // but needs two more coordinate families and their move tables purely to
  // handle the transition. Replaying costs at most 12 cube moves against a
  // phase-2 search that expands thousands of nodes, so the simpler version is
  // the right trade here.
  Cube cube = start_;
  for (int i = 0; i < phase1Length; ++i) {
    cube.apply(path1_[static_cast<std::size_t>(i)]);
  }
  assert(coord::isInG1(cube) && "phase 1 ended outside G1");

  const auto cornerPerm = static_cast<std::uint16_t>(coord::cornerPermutation(cube));
  const auto udEdgePerm = static_cast<std::uint16_t>(coord::udEdgePermutation(cube));
  const auto slicePerm = static_cast<std::uint16_t>(coord::slicePermutation(cube));

  // Only a phase 2 strictly shorter than the remaining budget can improve on
  // the best solution so far.
  const int maxPhase2 = bestLength_ - 1 - phase1Length;
  if (maxPhase2 < 0) return;

  boundaryMove_ = phase1Length > 0
                      ? path1_[static_cast<std::size_t>(phase1Length - 1)]
                      : Move::U;
  hasBoundaryMove_ = phase1Length > 0;

  const int lower = tables_.phase2Heuristic(cornerPerm, udEdgePerm, slicePerm);
  for (int depth = lower; depth <= maxPhase2; ++depth) {
    if (phase2(depth, cornerPerm, udEdgePerm, slicePerm, 0)) {
      recordSolution(phase1Length, depth);
      return;
    }
    if (stop_) return;
  }
}

void Search::phase1(int remaining, std::uint16_t twist, std::uint16_t flip,
                    std::uint16_t sliceSorted, int depth) {
  if (stop_) return;

  if (remaining == 0) {
    if (twist != 0 || flip != 0) return;
    if (sliceSorted / coord::kSlicePermutationCount != coord::kUdSliceSolved) {
      return;
    }
    // A phase-1 solution whose last move is itself a G1 move is redundant: G1
    // is closed under its own generators, so the prefix was already in G1 and
    // was enumerated one depth earlier, with that move available to phase 2.
    if (depth > 0 && isPhase2Move(path1_[static_cast<std::size_t>(depth - 1)])) {
      return;
    }
    tryPhase2(depth);
    return;
  }

  const MoveTable& twistT = tables_.moves().cornerOrientation();
  const MoveTable& flipT = tables_.moves().edgeOrientation();
  const MoveTable& sliceT = tables_.moves().udSliceSorted();

  const bool hasPrev = depth > 0;
  const Move prev = hasPrev ? path1_[static_cast<std::size_t>(depth - 1)] : Move::U;

  for (int i = 0; i < kNumMoves; ++i) {
    const Move m = static_cast<Move>(i);
    if (hasPrev && isRedundant(m, prev)) continue;

    const std::uint16_t nextTwist = twistT.apply(twist, m);
    const std::uint16_t nextFlip = flipT.apply(flip, m);
    const std::uint16_t nextSlice = sliceT.apply(sliceSorted, m);

    const int h = tables_.phase1Heuristic(
        nextTwist, nextFlip, nextSlice / coord::kSlicePermutationCount);
    if (h > remaining - 1) continue;

    ++best_.stats.phase1Nodes;

    // Checking the clock on every node would cost more than the search itself,
    // so sample it periodically instead.
    if (--timeCheckCountdown_ == 0) {
      timeCheckCountdown_ = 8192;
      if (shouldStopImproving()) {
        best_.stats.stoppedOnTimeLimit = true;
        stop_ = true;
        return;
      }
    }

    path1_[static_cast<std::size_t>(depth)] = m;
    phase1(remaining - 1, nextTwist, nextFlip, nextSlice, depth + 1);
    if (stop_) return;
  }
}

Solution Search::run() {
  startTime_ = std::chrono::steady_clock::now();
  timeCheckCountdown_ = 8192;

  const auto finish = [&]() {
    best_.stats.elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_)
            .count();
    return best_;
  };

  if (start_.isSolved()) {
    best_.found = true;
    best_.phase1Length = 0;
    return finish();
  }

  const auto twist = static_cast<std::uint16_t>(coord::cornerOrientation(start_));
  const auto flip = static_cast<std::uint16_t>(coord::edgeOrientation(start_));
  const auto sliceSorted = static_cast<std::uint16_t>(coord::udSliceSorted(start_));

  bestLength_ = options_.maxLength + 1;

  const int lower = tables_.phase1Heuristic(
      twist, flip, sliceSorted / coord::kSlicePermutationCount);

  for (int depth = lower; depth <= options_.maxLength; ++depth) {
    // No phase-1 solution of this length can beat what we already have.
    if (depth >= bestLength_) break;

    best_.stats.phase1Depth = depth;
    phase1(depth, twist, flip, sliceSorted, 0);

    if (stop_) break;
    if (shouldStopImproving()) {
      best_.stats.stoppedOnTimeLimit = true;
      break;
    }
  }

  return finish();
}

}  // namespace

TwoPhaseSolver::TwoPhaseSolver()
    : tables_(std::make_shared<const TwoPhaseTables>()) {}

TwoPhaseSolver::TwoPhaseSolver(std::shared_ptr<const TwoPhaseTables> tables)
    : tables_(std::move(tables)) {
  if (!tables_) throw Error("TwoPhaseSolver constructed with null tables");
}

Solution TwoPhaseSolver::solve(const Cube& cube,
                                               const TwoPhaseOptions& options) const {
  // An unreachable state has no solution at all; better to say so than to
  // exhaust the search space discovering it.
  cube.validate();

  if (options.maxLength < 0 || options.maxLength >= kMaxPathLength) {
    throw Error("maxLength must be between 0 and " +
                std::to_string(kMaxPathLength - 1) + " (got " +
                std::to_string(options.maxLength) + ")");
  }

  Search search(*tables_, cube, options);
  return search.run();
}

}  // namespace rubik
