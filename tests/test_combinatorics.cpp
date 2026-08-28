#include "solver/Combinatorics.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <functional>
#include <numeric>
#include <utility>
#include <vector>

using namespace rubik;

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

TEST(Combinatorics, FactorialTableIsCorrect) {
  EXPECT_EQ(kFactorial[0], 1u);
  EXPECT_EQ(kFactorial[1], 1u);
  EXPECT_EQ(kFactorial[4], 24u);
  EXPECT_EQ(kFactorial[8], 40320u);
  EXPECT_EQ(kFactorial[12], 479001600u);
  for (std::size_t n = 1; n < kFactorial.size(); ++n) {
    EXPECT_EQ(kFactorial[n], kFactorial[n - 1] * static_cast<std::uint32_t>(n))
        << "n = " << n;
  }
}

TEST(Combinatorics, BinomialSatisfiesPascalsRule) {
  for (int n = 1; n <= 12; ++n) {
    for (int k = 1; k <= n; ++k) {
      EXPECT_EQ(binomial(n, k), binomial(n - 1, k - 1) + binomial(n - 1, k))
          << "C(" << n << "," << k << ")";
    }
  }
}

TEST(Combinatorics, BinomialKnownValues) {
  EXPECT_EQ(binomial(12, 4), 495u);   // the UD-slice coordinate size
  EXPECT_EQ(binomial(8, 4), 70u);
  EXPECT_EQ(binomial(5, 2), 10u);
  EXPECT_EQ(binomial(0, 0), 1u);
  EXPECT_EQ(binomial(12, 0), 1u);
  EXPECT_EQ(binomial(12, 12), 1u);
}

TEST(Combinatorics, BinomialOutOfRangeIsZero) {
  EXPECT_EQ(binomial(4, 5), 0u);
  EXPECT_EQ(binomial(-1, 0), 0u);
  EXPECT_EQ(binomial(4, -1), 0u);
}

TEST(Combinatorics, PopcountMatchesNaiveCount) {
  for (std::uint32_t x = 0; x < 4096; ++x) {
    int expected = 0;
    for (int b = 0; b < 12; ++b) {
      if ((x >> b) & 1u) ++expected;
    }
    ASSERT_EQ(popcount(x), expected) << "x = " << x;
  }
  EXPECT_EQ(popcount(0xFFFFFFFFu), 32);
  EXPECT_EQ(popcount(0u), 0);
}

// ---------------------------------------------------------------------------
// Full permutations
// ---------------------------------------------------------------------------

namespace {

/// Enumerates every permutation of n elements and checks the encoder is a
/// bijection onto [0, n!) that decode inverts.
void checkPermutationBijection(int n) {
  const std::uint32_t total = kFactorial[static_cast<std::size_t>(n)];
  std::vector<bool> seen(total, false);

  std::vector<std::uint8_t> perm(static_cast<std::size_t>(n));
  std::iota(perm.begin(), perm.end(), static_cast<std::uint8_t>(0));

  do {
    const std::uint32_t index = encodePermutation(perm.data(), n);
    ASSERT_LT(index, total) << "n = " << n;
    ASSERT_FALSE(seen[index]) << "index " << index << " produced twice, n = " << n;
    seen[index] = true;

    std::vector<std::uint8_t> back(static_cast<std::size_t>(n));
    decodePermutation(index, n, back.data());
    ASSERT_EQ(back, perm) << "decode did not invert encode at index " << index;
  } while (std::next_permutation(perm.begin(), perm.end()));

  EXPECT_TRUE(std::all_of(seen.begin(), seen.end(), [](bool b) { return b; }))
      << "encoder did not cover every index for n = " << n;
}

}  // namespace

TEST(Combinatorics, PermutationEncodingIsABijectionForSmallN) {
  for (int n = 1; n <= 7; ++n) checkPermutationBijection(n);
}

TEST(Combinatorics, PermutationEncodingIsABijectionForEightElements) {
  // 8! = 40320: the corner permutation coordinate, checked exhaustively.
  checkPermutationBijection(8);
}

TEST(Combinatorics, IdentityPermutationEncodesToZero) {
  for (int n = 1; n <= 12; ++n) {
    std::vector<std::uint8_t> perm(static_cast<std::size_t>(n));
    std::iota(perm.begin(), perm.end(), static_cast<std::uint8_t>(0));
    EXPECT_EQ(encodePermutation(perm.data(), n), 0u) << "n = " << n;
  }
}

TEST(Combinatorics, ReversedPermutationEncodesToTheLastIndex) {
  for (int n = 1; n <= 12; ++n) {
    std::vector<std::uint8_t> perm(static_cast<std::size_t>(n));
    std::iota(perm.rbegin(), perm.rend(), static_cast<std::uint8_t>(0));
    EXPECT_EQ(encodePermutation(perm.data(), n),
              kFactorial[static_cast<std::size_t>(n)] - 1)
        << "n = " << n;
  }
}

TEST(Combinatorics, TwelveElementPermutationRoundTrips) {
  // 12! is too large to enumerate, so spot-check across the whole range.
  const std::uint32_t total = kFactorial[12];
  for (std::uint32_t step = 0; step < 20000; ++step) {
    const std::uint32_t index =
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(step) * 2654435761u) % total);
    std::array<std::uint8_t, 12> perm{};
    decodePermutation(index, 12, perm.data());

    // Decoded value must be a genuine permutation.
    std::array<bool, 12> seen{};
    for (const std::uint8_t v : perm) {
      ASSERT_LT(v, 12) << "index " << index;
      ASSERT_FALSE(seen[v]) << "index " << index;
      seen[v] = true;
    }
    ASSERT_EQ(encodePermutation(perm.data(), 12), index);
  }
}

// ---------------------------------------------------------------------------
// Partial permutations (the Korf edge databases will use these)
// ---------------------------------------------------------------------------

namespace {

/// n!/(n-k)!
std::uint32_t fallingFactorial(int n, int k) {
  std::uint32_t r = 1;
  for (int i = 0; i < k; ++i) r *= static_cast<std::uint32_t>(n - i);
  return r;
}

void checkPartialBijection(int n, int k) {
  const std::uint32_t total = fallingFactorial(n, k);
  std::vector<bool> seen(total, false);

  // Enumerate every ordered selection of k distinct values from 0..n-1.
  std::vector<std::uint8_t> positions(static_cast<std::size_t>(k));

  const std::function<void(int, std::uint32_t)> recurse =
      [&](int depth, std::uint32_t used) {
        if (depth == k) {
          const std::uint32_t index =
              encodePartialPermutation(positions.data(), n, k);
          ASSERT_LT(index, total);
          ASSERT_FALSE(seen[index]) << "index " << index << " produced twice";
          seen[index] = true;

          std::vector<std::uint8_t> back(static_cast<std::size_t>(k));
          decodePartialPermutation(index, n, k, back.data());
          ASSERT_EQ(back, positions);
          return;
        }
        for (int v = 0; v < n; ++v) {
          if ((used >> v) & 1u) continue;
          positions[static_cast<std::size_t>(depth)] = static_cast<std::uint8_t>(v);
          recurse(depth + 1, used | (1u << v));
          if (::testing::Test::HasFatalFailure()) return;
        }
      };

  recurse(0, 0);
  ASSERT_FALSE(::testing::Test::HasFatalFailure());
  EXPECT_TRUE(std::all_of(seen.begin(), seen.end(), [](bool b) { return b; }));
}

}  // namespace

TEST(Combinatorics, PartialPermutationIsABijection) {
  checkPartialBijection(5, 2);
  checkPartialBijection(6, 3);
  checkPartialBijection(12, 2);
}

TEST(Combinatorics, PartialPermutationCoversTheUdSliceSortedRange) {
  // 12P4 = 11880, the sorted UD-slice coordinate.
  checkPartialBijection(12, 4);
}

TEST(Combinatorics, PartialPermutationAgreesWithFullWhenKEqualsN) {
  std::array<std::uint8_t, 8> perm = {3, 1, 7, 0, 5, 2, 6, 4};
  EXPECT_EQ(encodePartialPermutation(perm.data(), 8, 8),
            encodePermutation(perm.data(), 8));
}

TEST(Combinatorics, PartialPermutationRangeMatchesFallingFactorial) {
  // 12P6 = 665,280 is the size a six-edge Korf database would use.
  EXPECT_EQ(fallingFactorial(12, 6), 665280u);
  std::array<std::uint8_t, 6> highest = {11, 10, 9, 8, 7, 6};
  EXPECT_EQ(encodePartialPermutation(highest.data(), 12, 6), 665280u - 1u);
  std::array<std::uint8_t, 6> lowest = {0, 1, 2, 3, 4, 5};
  EXPECT_EQ(encodePartialPermutation(lowest.data(), 12, 6), 0u);
}

// ---------------------------------------------------------------------------
// Permutation parity
// ---------------------------------------------------------------------------

TEST(Combinatorics, ParityOfIdentityIsEven) {
  std::array<std::uint8_t, 8> perm = {0, 1, 2, 3, 4, 5, 6, 7};
  EXPECT_FALSE(permutationIsOdd(perm.data(), 8));
}

TEST(Combinatorics, ParityFlipsWithEachTransposition) {
  std::array<std::uint8_t, 6> perm = {0, 1, 2, 3, 4, 5};
  bool expected = false;
  for (int i = 0; i + 1 < 6; ++i) {
    std::swap(perm[static_cast<std::size_t>(i)], perm[static_cast<std::size_t>(i + 1)]);
    expected = !expected;
    EXPECT_EQ(permutationIsOdd(perm.data(), 6), expected) << "after swap " << i;
  }
}

// ---------------------------------------------------------------------------
// Orientations
// ---------------------------------------------------------------------------

TEST(Combinatorics, CornerOrientationRoundTripsOverItsWholeRange) {
  for (std::uint32_t v = 0; v < 2187; ++v) {  // 3^7
    std::array<std::uint8_t, 8> ori{};
    decodeOrientation(v, 8, 3, ori.data());

    int sum = 0;
    for (const std::uint8_t o : ori) {
      ASSERT_LT(o, 3) << "value " << v;
      sum += o;
    }
    ASSERT_EQ(sum % 3, 0) << "decoded twists must sum to 0 mod 3, value " << v;
    ASSERT_EQ(encodeOrientation(ori.data(), 8, 3), v);
  }
}

TEST(Combinatorics, EdgeOrientationRoundTripsOverItsWholeRange) {
  for (std::uint32_t v = 0; v < 2048; ++v) {  // 2^11
    std::array<std::uint8_t, 12> ori{};
    decodeOrientation(v, 12, 2, ori.data());

    int sum = 0;
    for (const std::uint8_t o : ori) {
      ASSERT_LT(o, 2) << "value " << v;
      sum += o;
    }
    ASSERT_EQ(sum % 2, 0) << "decoded flips must sum to 0 mod 2, value " << v;
    ASSERT_EQ(encodeOrientation(ori.data(), 12, 2), v);
  }
}

TEST(Combinatorics, OrientationEncodingIgnoresTheRedundantLastValue) {
  // The final entry is reconstructed from the invariant, so corrupting it must
  // not change the encoding.
  std::array<std::uint8_t, 8> a = {1, 2, 0, 1, 0, 2, 1, 0};
  std::array<std::uint8_t, 8> b = a;
  b[7] = 2;
  EXPECT_EQ(encodeOrientation(a.data(), 8, 3), encodeOrientation(b.data(), 8, 3));
}

TEST(Combinatorics, AllZeroOrientationEncodesToZero) {
  std::array<std::uint8_t, 8> corners{};
  EXPECT_EQ(encodeOrientation(corners.data(), 8, 3), 0u);
  std::array<std::uint8_t, 12> edges{};
  EXPECT_EQ(encodeOrientation(edges.data(), 12, 2), 0u);
}

// ---------------------------------------------------------------------------
// Combinations
// ---------------------------------------------------------------------------

TEST(Combinatorics, CombinationRankIsABijectionForTheUdSlice) {
  // Every 4-subset of 12 slots must map onto a distinct value in [0, 495).
  std::vector<bool> seen(495, false);
  int count = 0;
  for (std::uint32_t mask = 0; mask < (1u << 12); ++mask) {
    if (popcount(mask) != 4) continue;
    ++count;
    const std::uint32_t rank = rankCombination(mask);
    ASSERT_LT(rank, 495u) << "mask " << mask;
    ASSERT_FALSE(seen[rank]) << "rank " << rank << " produced twice";
    seen[rank] = true;
    ASSERT_EQ(unrankCombination(rank, 12, 4), mask) << "rank " << rank;
  }
  EXPECT_EQ(count, 495);
  EXPECT_TRUE(std::all_of(seen.begin(), seen.end(), [](bool b) { return b; }));
}

TEST(Combinatorics, CombinationRankIsABijectionForOtherSizes) {
  for (const auto& [n, k] : std::vector<std::pair<int, int>>{
           {4, 2}, {6, 3}, {8, 4}, {10, 5}, {12, 6}}) {
    std::vector<bool> seen(binomial(n, k), false);
    for (std::uint32_t mask = 0; mask < (1u << n); ++mask) {
      if (popcount(mask) != k) continue;
      const std::uint32_t rank = rankCombination(mask);
      ASSERT_LT(rank, binomial(n, k)) << "n=" << n << " k=" << k;
      ASSERT_FALSE(seen[rank]);
      seen[rank] = true;
      ASSERT_EQ(unrankCombination(rank, n, k), mask);
    }
    EXPECT_TRUE(std::all_of(seen.begin(), seen.end(), [](bool b) { return b; }))
        << "n=" << n << " k=" << k;
  }
}

TEST(Combinatorics, LowestSubsetRanksZeroAndHighestRanksLast) {
  EXPECT_EQ(rankCombination(0b0000'0000'1111u), 0u);
  // Slots 8..11, which is where the slice edges sit on a solved cube.
  EXPECT_EQ(rankCombination(0b1111'0000'0000u), 494u);
}
