#include "core/Move.h"

#include "core/Error.h"

#include <array>
#include <cctype>

namespace rubik {
namespace {

constexpr std::array<std::string_view, kNumMoves> kNames = {
    "U", "U2", "U'", "R", "R2", "R'", "F", "F2", "F'",
    "D", "D2", "D'", "L", "L2", "L'", "B", "B2", "B'",
};

constexpr std::string_view kFaceLetters = "URFDLB";

}  // namespace

std::optional<Move> combine(Move a, Move b) noexcept {
  if (face(a) != face(b)) return std::nullopt;
  // Turn counts are 1, 2, 3 quarter-turns for indices 0, 1, 2 respectively.
  const int qa = turns(a) + 1;
  const int qb = turns(b) + 1;
  const int total = (qa + qb) % 4;
  if (total == 0) return std::nullopt;
  return static_cast<Move>(static_cast<std::uint8_t>(face(a)) * 3 + (total - 1));
}

std::string_view toString(Move m) noexcept {
  return kNames[static_cast<std::uint8_t>(m)];
}

std::optional<Move> parseMove(std::string_view token) noexcept {
  if (token.empty() || token.size() > 2) return std::nullopt;

  const auto faceIdx = kFaceLetters.find(static_cast<char>(std::toupper(
      static_cast<unsigned char>(token[0]))));
  if (faceIdx == std::string_view::npos) return std::nullopt;

  int suffix = 0;  // 90 CW by default.
  if (token.size() == 2) {
    switch (token[1]) {
      case '2': suffix = 1; break;
      case '\'':
      case '`':
      case 'i':
      case 'I':
      case '3': suffix = 2; break;
      default: return std::nullopt;
    }
  }
  return static_cast<Move>(static_cast<int>(faceIdx) * 3 + suffix);
}

std::vector<Move> parseSequence(std::string_view text) {
  std::vector<Move> moves;
  std::size_t i = 0;
  while (i < text.size()) {
    if (std::isspace(static_cast<unsigned char>(text[i])) != 0) {
      ++i;
      continue;
    }
    std::size_t end = i;
    while (end < text.size() &&
           std::isspace(static_cast<unsigned char>(text[end])) == 0) {
      ++end;
    }
    const std::string_view token = text.substr(i, end - i);
    const auto move = parseMove(token);
    if (!move) {
      throw ParseError("invalid move '" + std::string(token) +
                       "' at position " + std::to_string(i) +
                       " (expected one of U, R, F, D, L, B optionally "
                       "followed by ' or 2)");
    }
    moves.push_back(*move);
    i = end;
  }
  return moves;
}

std::string toString(const std::vector<Move>& moves) {
  std::string out;
  for (std::size_t i = 0; i < moves.size(); ++i) {
    if (i != 0) out.push_back(' ');
    out.append(toString(moves[i]));
  }
  return out;
}

std::vector<Move> invertSequence(const std::vector<Move>& moves) {
  std::vector<Move> out;
  out.reserve(moves.size());
  for (auto it = moves.rbegin(); it != moves.rend(); ++it) {
    out.push_back(inverse(*it));
  }
  return out;
}

}  // namespace rubik
