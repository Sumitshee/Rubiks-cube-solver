#include "solver/NibbleArray.h"

#include <gtest/gtest.h>

#include <random>
#include <vector>

using namespace rubik;

TEST(NibbleArray, DefaultConstructedIsEmpty) {
  const NibbleArray a;
  EXPECT_TRUE(a.empty());
  EXPECT_EQ(a.size(), 0u);
  EXPECT_EQ(a.byteSize(), 0u);
}

TEST(NibbleArray, PacksTwoEntriesPerByte) {
  EXPECT_EQ(NibbleArray(0).byteSize(), 0u);
  EXPECT_EQ(NibbleArray(1).byteSize(), 1u);
  EXPECT_EQ(NibbleArray(2).byteSize(), 1u);
  EXPECT_EQ(NibbleArray(3).byteSize(), 2u);
  EXPECT_EQ(NibbleArray(100).byteSize(), 50u);
  EXPECT_EQ(NibbleArray(101).byteSize(), 51u);
}

TEST(NibbleArray, FillValueAppliesToEveryEntry) {
  const NibbleArray a(17, 0x0F);
  for (std::size_t i = 0; i < a.size(); ++i) EXPECT_EQ(a.get(i), 0x0F) << i;

  const NibbleArray b(17, 0x00);
  for (std::size_t i = 0; i < b.size(); ++i) EXPECT_EQ(b.get(i), 0x00) << i;

  const NibbleArray c(17, 0x0A);
  for (std::size_t i = 0; i < c.size(); ++i) EXPECT_EQ(c.get(i), 0x0A) << i;
}

TEST(NibbleArray, StoresAndRetrievesEveryNibbleValue) {
  NibbleArray a(16, 0);
  for (std::size_t i = 0; i < 16; ++i) a.set(i, static_cast<std::uint8_t>(i));
  for (std::size_t i = 0; i < 16; ++i) EXPECT_EQ(a.get(i), i) << "index " << i;
}

TEST(NibbleArray, NeighbouringEntriesDoNotInterfere) {
  // The classic packing bug: writing an odd index clobbering its even partner.
  NibbleArray a(2, 0);
  a.set(0, 0x0F);
  EXPECT_EQ(a.get(0), 0x0F);
  EXPECT_EQ(a.get(1), 0x00);

  a.set(1, 0x0A);
  EXPECT_EQ(a.get(0), 0x0F) << "writing index 1 corrupted index 0";
  EXPECT_EQ(a.get(1), 0x0A);

  a.set(0, 0x03);
  EXPECT_EQ(a.get(0), 0x03);
  EXPECT_EQ(a.get(1), 0x0A) << "writing index 0 corrupted index 1";
}

TEST(NibbleArray, OverwritingIsIdempotentAndLocal) {
  NibbleArray a(64, 0);
  for (std::size_t i = 0; i < 64; ++i) a.set(i, static_cast<std::uint8_t>(i % 16));
  for (std::size_t i = 0; i < 64; ++i) a.set(i, static_cast<std::uint8_t>(i % 16));
  for (std::size_t i = 0; i < 64; ++i) EXPECT_EQ(a.get(i), i % 16) << i;
}

TEST(NibbleArray, HighBitsOfTheValueAreDiscarded) {
  NibbleArray a(4, 0);
  a.set(0, 0xFF);
  a.set(1, 0xF3);
  EXPECT_EQ(a.get(0), 0x0F);
  EXPECT_EQ(a.get(1), 0x03);
}

TEST(NibbleArray, MatchesAPlainByteArrayOverRandomTraffic) {
  constexpr std::size_t kSize = 5000;
  NibbleArray packed(kSize, 0);
  std::vector<std::uint8_t> reference(kSize, 0);

  std::mt19937_64 rng(2024);
  std::uniform_int_distribution<std::size_t> index(0, kSize - 1);
  std::uniform_int_distribution<int> value(0, 15);

  for (int op = 0; op < 200000; ++op) {
    const std::size_t i = index(rng);
    const auto v = static_cast<std::uint8_t>(value(rng));
    packed.set(i, v);
    reference[i] = v;
  }
  for (std::size_t i = 0; i < kSize; ++i) {
    ASSERT_EQ(packed.get(i), reference[i]) << "divergence at index " << i;
  }
}

TEST(NibbleArray, FillResetsEveryEntry) {
  NibbleArray a(33, 0);
  for (std::size_t i = 0; i < a.size(); ++i) a.set(i, 0x07);
  a.fill(0x0F);
  for (std::size_t i = 0; i < a.size(); ++i) EXPECT_EQ(a.get(i), 0x0F) << i;
}

TEST(NibbleArray, ResizeReinitialises) {
  NibbleArray a(4, 0);
  a.set(0, 9);
  a.resize(100, 0x0F);
  EXPECT_EQ(a.size(), 100u);
  EXPECT_EQ(a.byteSize(), 50u);
  for (std::size_t i = 0; i < a.size(); ++i) EXPECT_EQ(a.get(i), 0x0F) << i;
}

TEST(NibbleArray, OddSizedArrayAddressesItsLastEntry) {
  NibbleArray a(7, 0);
  a.set(6, 0x0C);
  EXPECT_EQ(a.get(6), 0x0C);
  EXPECT_EQ(a.byteSize(), 4u);
}
