#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rubik {

/// The six faces, in the standard Kociemba ordering.
///
/// The ordering matters: `static_cast<int>(f) % 3` yields the *axis* of the
/// face (U/D -> 0, R/L -> 1, F/B -> 2), which the move pruner relies on to
/// detect commuting face pairs without a lookup table.
enum class Face : std::uint8_t { U = 0, R = 1, F = 2, D = 3, L = 4, B = 5 };

inline constexpr int kNumFaces = 6;

/// The 18 face turns, grouped three-per-face as {quarter CW, half, quarter CCW}.
///
/// This layout gives two useful identities for free:
///   face(m)  == m / 3
///   turns(m) == m % 3   (0 = 90 CW, 1 = 180, 2 = 90 CCW)
enum class Move : std::uint8_t {
  U = 0, U2, Up,
  R,     R2, Rp,
  F,     F2, Fp,
  D,     D2, Dp,
  L,     L2, Lp,
  B,     B2, Bp,
};

inline constexpr int kNumMoves = 18;

[[nodiscard]] inline constexpr Face face(Move m) noexcept {
  return static_cast<Face>(static_cast<std::uint8_t>(m) / 3);
}

/// 0 = 90 degrees clockwise, 1 = 180 degrees, 2 = 90 degrees counter-clockwise.
[[nodiscard]] inline constexpr int turns(Move m) noexcept {
  return static_cast<std::uint8_t>(m) % 3;
}

/// U and D share axis 0, R and L axis 1, F and B axis 2.
[[nodiscard]] inline constexpr int axis(Move m) noexcept {
  return static_cast<std::uint8_t>(face(m)) % 3;
}

/// The move that undoes `m`. Half turns are self-inverse.
[[nodiscard]] inline constexpr Move inverse(Move m) noexcept {
  const int v = static_cast<std::uint8_t>(m);
  const int t = v % 3;
  return static_cast<Move>(v - t + (2 - t));
}

/// Compose two turns of the *same* face into a single move.
/// Returns nullopt when the turns cancel out entirely (e.g. R followed by R').
[[nodiscard]] std::optional<Move> combine(Move a, Move b) noexcept;

/// Standard notation, e.g. "R", "R2", "R'".
[[nodiscard]] std::string_view toString(Move m) noexcept;

/// Parses a single move token. Accepts "R", "R2", "R'" and "Ri" / "R3".
[[nodiscard]] std::optional<Move> parseMove(std::string_view token) noexcept;

/// Parses a whitespace-separated move sequence such as "R U R' F2".
/// Throws `ParseError` describing the offending token when parsing fails.
[[nodiscard]] std::vector<Move> parseSequence(std::string_view text);

/// Renders a move sequence back to space-separated standard notation.
[[nodiscard]] std::string toString(const std::vector<Move>& moves);

/// The move sequence that undoes `moves` (reversed, each move inverted).
[[nodiscard]] std::vector<Move> invertSequence(const std::vector<Move>& moves);

/// True when `next` may follow `prev` in a canonical solution.
///
/// Two prunings are applied:
///   1. Same face twice in a row (R R' is a no-op, R R is just R2).
///   2. Commuting faces in only one order. U and D commute, so the search need
///      only ever consider U-then-D and can drop D-then-U as a duplicate.
///
/// Together these cut the effective branching factor from 18 to ~13.35.
[[nodiscard]] inline constexpr bool isRedundant(Move next, Move prev) noexcept {
  const int fn = static_cast<std::uint8_t>(face(next));
  const int fp = static_cast<std::uint8_t>(face(prev));
  if (fn == fp) return true;
  return (fn % 3 == fp % 3) && (fn < fp);
}

}  // namespace rubik
