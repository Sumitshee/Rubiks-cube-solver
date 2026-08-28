#include "core/Cube.h"

#include "core/Error.h"

#include <numeric>
#include <random>
#include <string>

namespace rubik {
namespace {

/// How one face turn rearranges the cubies it touches.
///
/// cornerCycle lists four corner slots c0..c3 such that the cubie in c0 moves
/// to c1, c1 to c2, c2 to c3, and c3 back to c0. cornerTwist[k] is the twist
/// (mod 3) picked up by whatever lands in slot cornerCycle[k]. Edges work the
/// same way, except a quarter turn either flips all four of its edges or none.
///
/// These cycles follow the standard Kociemba cubie numbering. They are verified
/// by the group-axiom tests in tests/test_cube.cpp: every move has order 4, a
/// move followed by its inverse is the identity, and the three cube invariants
/// hold after arbitrary random sequences. An error in any entry below would
/// break at least one of those tests.
struct FaceTurn {
  std::array<std::uint8_t, 4> cornerCycle;
  std::array<std::uint8_t, 4> cornerTwist;
  std::array<std::uint8_t, 4> edgeCycle;
  bool flipsEdges;
};

// Indexed by Face: U, R, F, D, L, B.
constexpr std::array<FaceTurn, kNumFaces> kFaceTurns = {{
    // U: no twists, no flips.
    {{0, 1, 2, 3}, {0, 0, 0, 0}, {0, 1, 2, 3}, false},
    // R: alternating twists, no flips.
    {{0, 3, 7, 4}, {2, 1, 2, 1}, {0, 11, 4, 8}, false},
    // F: alternating twists, flips all four edges.
    {{0, 4, 5, 1}, {1, 2, 1, 2}, {1, 8, 5, 9}, true},
    // D: no twists, no flips.
    {{4, 7, 6, 5}, {0, 0, 0, 0}, {4, 7, 6, 5}, false},
    // L: alternating twists, no flips.
    {{1, 5, 6, 2}, {1, 2, 1, 2}, {2, 9, 6, 10}, false},
    // B: alternating twists, flips all four edges.
    {{2, 6, 7, 3}, {1, 2, 1, 2}, {3, 10, 7, 11}, true},
}};

/// Reduces a value known to be below 2 * Modulus, without a division.
///
/// Every orientation sum here stays in that range: a corner twist and its delta
/// are each at most 2, so their sum is at most 4 (< 6), and the backward form
/// `ori + Modulus - delta` is at most 5. One conditional subtract is therefore
/// exact.
///
/// This matters more than it looks. The modulus used to be a runtime argument,
/// which left MSVC no choice but to emit a real `div` -- eight per move
/// application and eight more per undo. Profiling put `apply`+`undo` at
/// 41.4 ns, by far the largest single cost per generated node, and this is why.
template <std::uint8_t Modulus>
[[nodiscard]] inline std::uint8_t reduce(unsigned value) noexcept {
  return static_cast<std::uint8_t>(value >= Modulus ? value - Modulus : value);
}

/// Rotates four slots forward along cyc (the cubie in cyc[k] moves to cyc[k+1])
/// while adding delta[k] to the orientation that lands in cyc[k].
template <std::uint8_t Modulus, std::size_t N>
inline void cycleForward(std::array<std::uint8_t, N>& perm,
                         std::array<std::uint8_t, N>& ori,
                         const std::array<std::uint8_t, 4>& cyc,
                         const std::array<std::uint8_t, 4>& delta) noexcept {
  const std::uint8_t lastP = perm[cyc[3]];
  const std::uint8_t lastO = ori[cyc[3]];
  for (std::size_t k = 3; k > 0; --k) {
    perm[cyc[k]] = perm[cyc[k - 1]];
    ori[cyc[k]] = reduce<Modulus>(
        static_cast<unsigned>(ori[cyc[k - 1]]) + delta[k]);
  }
  perm[cyc[0]] = lastP;
  ori[cyc[0]] = reduce<Modulus>(static_cast<unsigned>(lastO) + delta[0]);
}

/// The inverse of cycleForward: the cubie in cyc[k+1] moves back to cyc[k],
/// undoing the orientation delta it would have picked up.
template <std::uint8_t Modulus, std::size_t N>
inline void cycleBackward(std::array<std::uint8_t, N>& perm,
                          std::array<std::uint8_t, N>& ori,
                          const std::array<std::uint8_t, 4>& cyc,
                          const std::array<std::uint8_t, 4>& delta) noexcept {
  const std::uint8_t firstP = perm[cyc[0]];
  const std::uint8_t firstO = ori[cyc[0]];
  for (std::size_t k = 0; k < 3; ++k) {
    perm[cyc[k]] = perm[cyc[k + 1]];
    ori[cyc[k]] = reduce<Modulus>(static_cast<unsigned>(ori[cyc[k + 1]]) +
                                  Modulus - delta[k + 1]);
  }
  perm[cyc[3]] = firstP;
  ori[cyc[3]] =
      reduce<Modulus>(static_cast<unsigned>(firstO) + Modulus - delta[0]);
}

/// A half turn is two disjoint swaps. Orientation rides along untouched: the
/// alternating corner twists (1,2,1,2) and the four edge flips both cancel when
/// the quarter turn is applied twice.
template <std::size_t N>
inline void cycleHalf(std::array<std::uint8_t, N>& perm,
                      std::array<std::uint8_t, N>& ori,
                      const std::array<std::uint8_t, 4>& cyc) noexcept {
  std::swap(perm[cyc[0]], perm[cyc[2]]);
  std::swap(ori[cyc[0]], ori[cyc[2]]);
  std::swap(perm[cyc[1]], perm[cyc[3]]);
  std::swap(ori[cyc[1]], ori[cyc[3]]);
}

constexpr std::array<std::uint8_t, 4> kNoDelta = {0, 0, 0, 0};
constexpr std::array<std::uint8_t, 4> kAllFlip = {1, 1, 1, 1};

/// Parity of a permutation, computed by walking its cycles: a cycle of length L
/// contributes L-1 transpositions. Returns true when the permutation is odd.
template <std::size_t N>
bool permutationParity(const std::array<std::uint8_t, N>& perm) noexcept {
  std::array<bool, N> seen{};
  bool odd = false;
  for (std::size_t i = 0; i < N; ++i) {
    if (seen[i]) continue;
    std::size_t len = 0;
    for (std::size_t j = i; !seen[j]; j = perm[j]) {
      seen[j] = true;
      ++len;
    }
    if (len % 2 == 0) odd = !odd;
  }
  return odd;
}

}  // namespace

Cube Cube::fromCubies(const std::array<std::uint8_t, kNumCorners>& cp,
                      const std::array<std::uint8_t, kNumCorners>& co,
                      const std::array<std::uint8_t, kNumEdges>& ep,
                      const std::array<std::uint8_t, kNumEdges>& eo) noexcept {
  Cube c;
  c.cp_ = cp;
  c.co_ = co;
  c.ep_ = ep;
  c.eo_ = eo;
  return c;
}

void Cube::reset() noexcept {
  std::iota(cp_.begin(), cp_.end(), static_cast<std::uint8_t>(0));
  std::iota(ep_.begin(), ep_.end(), static_cast<std::uint8_t>(0));
  co_.fill(0);
  eo_.fill(0);
}

void Cube::apply(Move m) noexcept {
  const FaceTurn& t = kFaceTurns[static_cast<std::uint8_t>(face(m))];
  const auto& edgeDelta = t.flipsEdges ? kAllFlip : kNoDelta;

  switch (turns(m)) {
    case 0:  // 90 degrees clockwise
      cycleForward<3>(cp_, co_, t.cornerCycle, t.cornerTwist);
      cycleForward<2>(ep_, eo_, t.edgeCycle, edgeDelta);
      break;
    case 1:  // 180 degrees
      cycleHalf(cp_, co_, t.cornerCycle);
      cycleHalf(ep_, eo_, t.edgeCycle);
      break;
    default:  // 90 degrees counter-clockwise
      cycleBackward<3>(cp_, co_, t.cornerCycle, t.cornerTwist);
      cycleBackward<2>(ep_, eo_, t.edgeCycle, edgeDelta);
      break;
  }
}

void Cube::apply(const std::vector<Move>& moves) noexcept {
  for (const Move m : moves) apply(m);
}

bool Cube::isSolved() const noexcept {
  for (std::size_t i = 0; i < kNumCorners; ++i) {
    if (cp_[i] != i || co_[i] != 0) return false;
  }
  for (std::size_t i = 0; i < kNumEdges; ++i) {
    if (ep_[i] != i || eo_[i] != 0) return false;
  }
  return true;
}

Cube Cube::inverted() const noexcept {
  Cube out;
  for (std::size_t slot = 0; slot < kNumCorners; ++slot) {
    const std::uint8_t cubie = cp_[slot];
    out.cp_[cubie] = static_cast<std::uint8_t>(slot);
    // A twist of t going one way is a twist of -t coming back.
    out.co_[cubie] = static_cast<std::uint8_t>((3 - co_[slot]) % 3);
  }
  for (std::size_t slot = 0; slot < kNumEdges; ++slot) {
    const std::uint8_t cubie = ep_[slot];
    out.ep_[cubie] = static_cast<std::uint8_t>(slot);
    // Flips are self-inverse.
    out.eo_[cubie] = eo_[slot];
  }
  return out;
}

bool Cube::operator==(const Cube& other) const noexcept {
  return cp_ == other.cp_ && co_ == other.co_ && ep_ == other.ep_ &&
         eo_ == other.eo_;
}

std::vector<Move> Cube::scramble(int count, std::uint64_t seed) {
  std::vector<Move> moves;
  if (count <= 0) return moves;
  moves.reserve(static_cast<std::size_t>(count));

  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<int> pick(0, kNumMoves - 1);

  // Rejecting redundant successors keeps the scramble length meaningful. A
  // naive random walk would emit sequences like "R R'" that collapse to
  // nothing, so an n-move scramble would not actually be n moves deep.
  Move prev = Move::U;
  bool havePrev = false;
  while (static_cast<int>(moves.size()) < count) {
    const Move candidate = static_cast<Move>(pick(rng));
    if (havePrev && isRedundant(candidate, prev)) continue;
    moves.push_back(candidate);
    prev = candidate;
    havePrev = true;
  }
  apply(moves);
  return moves;
}

void Cube::validate() const {
  // 1. Both permutations must be bijections over their index ranges.
  std::array<bool, kNumCorners> cornerSeen{};
  for (const std::uint8_t c : cp_) {
    if (c >= kNumCorners) {
      throw InvalidStateError(
          "corner permutation contains out-of-range value " + std::to_string(c),
          CubeFault::CornerPiece);
    }
    if (cornerSeen[c]) {
      throw InvalidStateError("corner cubie " + std::to_string(c) +
                                  " appears more than once",
                              CubeFault::DuplicateCubie);
    }
    cornerSeen[c] = true;
  }
  std::array<bool, kNumEdges> edgeSeen{};
  for (const std::uint8_t e : ep_) {
    if (e >= kNumEdges) {
      throw InvalidStateError("edge permutation contains out-of-range value " +
                                  std::to_string(e),
                              CubeFault::EdgePiece);
    }
    if (edgeSeen[e]) {
      throw InvalidStateError("edge cubie " + std::to_string(e) +
                                  " appears more than once",
                              CubeFault::DuplicateCubie);
    }
    edgeSeen[e] = true;
  }

  // 2. Corner twists sum to 0 mod 3. No sequence of face turns can twist a
  //    single corner in isolation.
  int twistSum = 0;
  for (const std::uint8_t o : co_) {
    if (o > 2) {
      throw InvalidStateError("corner orientation " + std::to_string(o) +
                                  " out of range (expected 0..2)",
                              CubeFault::CornerTwist);
    }
    twistSum += o;
  }
  if (twistSum % 3 != 0) {
    throw InvalidStateError(
        "corner twist parity violated: orientations sum to " +
        std::to_string(twistSum) +
        ", which is not a multiple of 3 (a single corner has been twisted)",
        CubeFault::CornerTwist);
  }

  // 3. Edge flips sum to 0 mod 2.
  int flipSum = 0;
  for (const std::uint8_t o : eo_) {
    if (o > 1) {
      throw InvalidStateError("edge orientation " + std::to_string(o) +
                                  " out of range (expected 0 or 1)",
                              CubeFault::EdgeFlip);
    }
    flipSum += o;
  }
  if (flipSum % 2 != 0) {
    throw InvalidStateError(
        "edge flip parity violated: an odd number of edges are flipped (a "
        "single edge has been flipped)",
        CubeFault::EdgeFlip);
  }

  // 4. Every face turn is a 4-cycle on corners and a 4-cycle on edges, so each
  //    turn flips both parities together. They must therefore always agree.
  if (permutationParity(cp_) != permutationParity(ep_)) {
    throw InvalidStateError(
        "permutation parity violated: corner and edge permutations have "
        "opposite parity (two cubies have been swapped)",
        CubeFault::Parity);
  }
}

}  // namespace rubik
