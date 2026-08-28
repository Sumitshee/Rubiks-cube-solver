#pragma once

#include "core/Cube.h"
#include "core/Move.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace rubik::ui {

/// A move in words, for somebody holding a cube rather than reading notation.
/// The notation itself stays authoritative; this is the gloss under it.
struct MoveDescription {
  const char* face = "";  ///< RIGHT, LEFT, UP, DOWN, FRONT, BACK
  const char* turn = "";  ///< CLOCKWISE, COUNTER-CLOCKWISE, 180 DEGREES
};

[[nodiscard]] MoveDescription describeMove(Move move) noexcept;

/// The logical cube, an animation queue, and solution playback.
///
/// ## The invariant that keeps animation and state consistent
///
/// **The logical cube is mutated only when an animation finishes.** While a
/// turn is in flight the cube still holds the state *before* that turn, and the
/// renderer draws it with the affected layer rotated part of the way. There is
/// never a moment where the model has half-applied a move, and no second copy
/// of the state to drift.
///
/// Everything else follows from that. A move that arrives mid-animation is
/// queued, not applied. A scramble or a solution is just a longer queue.
/// Cancelling means dropping the queue and finishing (or abandoning) the turn
/// in flight -- either way the cube ends on a whole number of moves.
///
/// This class contains no OpenGL. It is the application layer, and it is
/// testable without a window.
class CubeController {
 public:
  /// How far through a turn the renderer should draw the moving layer.
  struct Animation {
    bool active = false;
    Move move = Move::U;
    /// Signed radians about the face's outward normal, between zero and the
    /// move's full angle.
    float angleRadians = 0.0f;
  };

  CubeController();

  // --- State -------------------------------------------------------------

  /// The committed state. Never mid-move.
  [[nodiscard]] const Cube& cube() const noexcept { return cube_; }
  [[nodiscard]] Animation animation() const noexcept { return animation_; }
  [[nodiscard]] bool busy() const noexcept {
    return animation_.active || !queue_.empty();
  }
  [[nodiscard]] std::size_t queuedMoves() const noexcept { return queue_.size(); }

  // --- Driving -----------------------------------------------------------

  /// Queues a move. Never applies it immediately.
  void enqueue(Move move);
  void enqueue(const std::vector<Move>& moves);

  /// Advances time. Commits any move whose animation completes, and starts the
  /// next queued one. Safe to call with any dt, including a large one after the
  /// window was dragged: at most one move is committed per call so a stalled
  /// frame cannot silently skip the animation of several turns.
  void update(double deltaSeconds);

  /// Drops everything pending and returns to a solved cube.
  void reset();

  /// Replaces the cube outright, dropping any queue, animation and solution.
  ///
  /// The invariant is preserved by construction: nothing is mid-move after
  /// this, because everything mid-move was discarded. Used by the cube editor,
  /// which produces a whole validated state rather than a sequence of moves.
  void setCube(const Cube& cube);

  /// Abandons the queue and the turn in flight without touching the committed
  /// state, leaving the cube on a whole number of moves.
  void cancelPending();

  /// Generates a scramble through the core's own generator and queues it.
  std::vector<Move> scramble(int moveCount, std::uint64_t seed);
  [[nodiscard]] const std::vector<Move>& lastScramble() const noexcept {
    return lastScramble_;
  }

  // --- Solution playback --------------------------------------------------

  void loadSolution(const std::vector<Move>& moves);
  void clearSolution();
  [[nodiscard]] const std::vector<Move>& solution() const noexcept {
    return solution_;
  }
  /// How many solution moves have been committed so far.
  [[nodiscard]] int solutionProgress() const noexcept { return solutionIndex_; }
  [[nodiscard]] bool playing() const noexcept { return playing_; }
  [[nodiscard]] bool solutionComplete() const noexcept {
    return !solution_.empty() &&
           solutionIndex_ >= static_cast<int>(solution_.size());
  }
  void play();
  void pause();
  /// Animates exactly one more solution move, then stops.
  ///
  /// Ignored while something is already animating, so the move counter always
  /// matches what has actually been performed -- which is the whole point when
  /// somebody is copying the moves onto a cube in their hands.
  void stepForward();

  /// Animates the inverse of the move just performed, taking the cube back one
  /// step. Ignored at the start of the solution or while something is animating.
  void stepBackward();

  /// Restores the cube to the state it was in when the solution was loaded and
  /// puts the counter back to zero.
  ///
  /// The state is remembered at `loadSolution` time, so this costs 40 bytes and
  /// does not require re-entering anything. Stepping backwards to the start
  /// would also work but takes as many animations as there are moves.
  void restartSolution();

  /// True once `loadSolution` has been given something to play.
  [[nodiscard]] bool hasSolution() const noexcept { return !solution_.empty(); }

  /// The move the user should perform next, or nullopt at the end.
  [[nodiscard]] std::optional<Move> nextSolutionMove() const noexcept;

  /// The move currently being animated as part of the solution, if any.
  [[nodiscard]] std::optional<Move> animatingSolutionMove() const noexcept;

  /// Turns per second. Clamped to something watchable.
  void setSpeed(double movesPerSecond);
  [[nodiscard]] double speed() const noexcept { return movesPerSecond_; }

 private:
  void startNext();

  Cube cube_;
  std::deque<Move> queue_;
  Animation animation_;
  double elapsed_ = 0.0;
  double movesPerSecond_ = 3.5;

  std::vector<Move> lastScramble_;

  std::vector<Move> solution_;
  int solutionIndex_ = 0;
  /// How the move in flight changes the counter: +1 for a solution move going
  /// forwards, -1 for one being taken back, 0 for a turn the user made by hand.
  /// Keeping it as a delta rather than a flag is what lets stepping backwards
  /// share the one commit path, and therefore the one invariant.
  int solutionDelta_ = 0;
  bool playing_ = false;

  /// The cube as it was when the solution was loaded, so it can be restored.
  Cube solutionStart_;
};

}  // namespace rubik::ui
