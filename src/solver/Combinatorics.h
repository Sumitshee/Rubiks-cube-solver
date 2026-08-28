#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rubik {

/// Generic combinatorial ranking used to turn parts of a cube state into dense
/// array indices.
///
/// Everything here is a bijection onto a contiguous range starting at zero,
/// which is what makes a pattern database a flat array rather than a hash map.
/// These routines are shared by the Kociemba coordinates and by the Korf
/// pattern-database indexers, so they are written generically rather than being
/// specialised to either.

/// n! for n <= 12. 12! = 479,001,600, which still fits in a uint32_t.
inline constexpr std::array<std::uint32_t, 13> kFactorial = {
    1u,     1u,      2u,       6u,        24u,       120u,      720u,
    5040u,  40320u,  362880u,  3628800u,  39916800u, 479001600u};

namespace detail {

constexpr std::array<std::array<std::uint32_t, 13>, 13> makeBinomial() {
  std::array<std::array<std::uint32_t, 13>, 13> c{};
  for (std::size_t n = 0; n < 13; ++n) {
    c[n][0] = 1u;
    for (std::size_t k = 1; k <= n; ++k) {
      c[n][k] = c[n - 1][k - 1] + c[n - 1][k];
    }
  }
  return c;
}

}  // namespace detail

inline constexpr auto kBinomial = detail::makeBinomial();

/// C(n, k), zero when k is out of range.
[[nodiscard]] constexpr std::uint32_t binomial(int n, int k) noexcept {
  if (n < 0 || k < 0 || k > n || n > 12) return 0u;
  return kBinomial[static_cast<std::size_t>(n)][static_cast<std::size_t>(k)];
}

/// Population count.
///
/// Written as portable SWAR rather than an intrinsic on purpose: `__popcnt`
/// needs hardware POPCNT support, and the inputs here are at most 12 bits, so
/// the difference is a couple of cycles inside a routine already dominated by
/// its memory access. If profiling later says otherwise this is a one-line
/// change.
[[nodiscard]] inline constexpr int popcount(std::uint32_t x) noexcept {
  x = x - ((x >> 1) & 0x55555555u);
  x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
  x = (x + (x >> 4)) & 0x0F0F0F0Fu;
  return static_cast<int>((x * 0x01010101u) >> 24);
}

// ---------------------------------------------------------------------------
// Partial permutations
// ---------------------------------------------------------------------------

/// Ranks an ordered selection of `k` distinct values from `0..n-1`.
///
/// `positions[i]` is where the i-th tracked piece sits. The result is in
/// `[0, n!/(n-k)!)`.
///
/// This is the linear-time variant of the Lehmer code: instead of rescanning
/// the prefix to count smaller values (quadratic), it keeps a bitmask of values
/// already consumed and gets the count with a single popcount. Korf highlights
/// this in his large-scale BFS paper, and it is the reason a permutation index
/// costs a handful of cycles rather than tens.
///
/// With k == n this reduces exactly to the classic factorial-base Lehmer code:
/// expanding the Horner form gives d_0*(n-1)! + d_1*(n-2)! + ... + d_(n-1)*0!.
[[nodiscard]] inline std::uint32_t encodePartialPermutation(
    const std::uint8_t* positions, int n, int k) noexcept {
  std::uint32_t index = 0;
  std::uint32_t used = 0;
  for (int i = 0; i < k; ++i) {
    const std::uint32_t p = positions[static_cast<std::size_t>(i)];
    const std::uint32_t smaller =
        static_cast<std::uint32_t>(popcount(used & ((1u << p) - 1u)));
    index = index * static_cast<std::uint32_t>(n - i) + (p - smaller);
    used |= (1u << p);
  }
  return index;
}

/// The inverse of `encodePartialPermutation`.
inline void decodePartialPermutation(std::uint32_t index, int n, int k,
                                     std::uint8_t* positions) noexcept {
  // Recover the digits back-to-front; digit i was combined with multiplier
  // (n - i) on the way in.
  std::array<std::uint32_t, 12> digits{};
  for (int i = k - 1; i >= 0; --i) {
    const auto radix = static_cast<std::uint32_t>(n - i);
    digits[static_cast<std::size_t>(i)] = index % radix;
    index /= radix;
  }

  // Each digit is a rank among the values still unused, so replay the
  // selection against a shrinking list.
  std::array<std::uint8_t, 12> available{};
  for (int i = 0; i < n; ++i) {
    available[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
  }
  int remaining = n;

  for (int i = 0; i < k; ++i) {
    const auto d = static_cast<int>(digits[static_cast<std::size_t>(i)]);
    positions[static_cast<std::size_t>(i)] = available[static_cast<std::size_t>(d)];
    for (int j = d; j + 1 < remaining; ++j) {
      available[static_cast<std::size_t>(j)] = available[static_cast<std::size_t>(j + 1)];
    }
    --remaining;
  }
}

/// Full permutation of `n` elements, in `[0, n!)`.
[[nodiscard]] inline std::uint32_t encodePermutation(const std::uint8_t* perm,
                                                     int n) noexcept {
  return encodePartialPermutation(perm, n, n);
}

inline void decodePermutation(std::uint32_t index, int n,
                              std::uint8_t* perm) noexcept {
  decodePartialPermutation(index, n, n, perm);
}

/// Parity of a permutation: false when even, true when odd.
[[nodiscard]] inline bool permutationIsOdd(const std::uint8_t* perm,
                                           int n) noexcept {
  // Counting inversions is O(n^2) but n is at most 12 and this is not on the
  // per-node path.
  int inversions = 0;
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if (perm[static_cast<std::size_t>(i)] > perm[static_cast<std::size_t>(j)]) {
        ++inversions;
      }
    }
  }
  return (inversions & 1) != 0;
}

// ---------------------------------------------------------------------------
// Orientations
// ---------------------------------------------------------------------------

/// Packs `n` orientation values in `0..base-1` into `[0, base^(n-1))`.
///
/// Only the first n-1 values are stored: the cube's orientation invariants make
/// the last one redundant (corner twists sum to 0 mod 3, edge flips to 0 mod 2).
/// Dropping it is not a micro-optimisation -- it is what keeps the corner
/// database at 3^7 rather than 3^8 entries.
[[nodiscard]] inline std::uint32_t encodeOrientation(const std::uint8_t* ori,
                                                     int n, int base) noexcept {
  std::uint32_t index = 0;
  for (int i = 0; i < n - 1; ++i) {
    index = index * static_cast<std::uint32_t>(base) +
            ori[static_cast<std::size_t>(i)];
  }
  return index;
}

/// The inverse of `encodeOrientation`, reconstructing the dropped final value
/// from the invariant.
inline void decodeOrientation(std::uint32_t index, int n, int base,
                              std::uint8_t* ori) noexcept {
  const auto radix = static_cast<std::uint32_t>(base);
  int sum = 0;
  for (int i = n - 2; i >= 0; --i) {
    const auto digit = static_cast<std::uint8_t>(index % radix);
    ori[static_cast<std::size_t>(i)] = digit;
    index /= radix;
    sum += digit;
  }
  ori[static_cast<std::size_t>(n - 1)] =
      static_cast<std::uint8_t>((base - sum % base) % base);
}

// ---------------------------------------------------------------------------
// Combinations
// ---------------------------------------------------------------------------

/// Colexicographic rank of the subset described by `mask`, in `[0, C(n,k))`
/// where k is the popcount of the mask.
///
/// Used for "which slots hold the four UD-slice edges", where the identity of
/// the individual edges does not matter.
[[nodiscard]] inline std::uint32_t rankCombination(std::uint32_t mask) noexcept {
  std::uint32_t rank = 0;
  int chosen = 0;
  while (mask != 0) {
    // Index of the lowest set bit.
    const std::uint32_t low = mask & (~mask + 1u);
    int p = 0;
    for (std::uint32_t probe = low; probe > 1u; probe >>= 1) ++p;
    rank += binomial(p, chosen + 1);
    ++chosen;
    mask ^= low;
  }
  return rank;
}

/// The inverse of `rankCombination` for a known n and k.
[[nodiscard]] inline std::uint32_t unrankCombination(std::uint32_t rank, int n,
                                                     int k) noexcept {
  std::uint32_t mask = 0;
  for (int i = k; i >= 1; --i) {
    int p = i - 1;
    while (p + 1 < n && binomial(p + 1, i) <= rank) ++p;
    mask |= (1u << p);
    rank -= binomial(p, i);
  }
  return mask;
}

}  // namespace rubik
