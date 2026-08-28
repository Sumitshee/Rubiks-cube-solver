#include "solver/TwoPhaseTables.h"

namespace rubik {
namespace {

constexpr std::uint32_t kSliceN = coord::kUdSliceCount;          // 495
constexpr std::uint32_t kSlicePermN = coord::kSlicePermutationCount;  // 24

}  // namespace

TwoPhaseTables::TwoPhaseTables() {
  const MoveTable& flipT = moves_.edgeOrientation();
  const MoveTable& twistT = moves_.cornerOrientation();
  const MoveTable& sliceT = moves_.udSlice();
  const MoveTable& cornerT = moves_.cornerPermutation();
  const MoveTable& udEdgeT = moves_.udEdgePermutation();
  const MoveTable& slicePermT = moves_.slicePermutation();

  // --- Phase 1 -------------------------------------------------------------
  // The goal is G1: no flipped edges, no twisted corners, slice edges in the
  // slice. Each table relaxes that goal by ignoring one of the three parts.

  const std::uint32_t phase1Goal = 0 * kSliceN + coord::kUdSliceSolved;

  flipSlice_ = PruningTable::build(
      "flipSlice", coord::kEdgeOrientationCount * kSliceN, kAllMoveMask,
      phase1Goal, [&](std::uint32_t index, Move m) {
        const std::uint32_t flip = index / kSliceN;
        const std::uint32_t slice = index % kSliceN;
        return static_cast<std::uint32_t>(flipT.apply(flip, m)) * kSliceN +
               sliceT.apply(slice, m);
      });

  twistSlice_ = PruningTable::build(
      "twistSlice", coord::kCornerOrientationCount * kSliceN, kAllMoveMask,
      phase1Goal, [&](std::uint32_t index, Move m) {
        const std::uint32_t twist = index / kSliceN;
        const std::uint32_t slice = index % kSliceN;
        return static_cast<std::uint32_t>(twistT.apply(twist, m)) * kSliceN +
               sliceT.apply(slice, m);
      });

  // --- Phase 2 -------------------------------------------------------------
  // Restricted to the ten G1 moves. The goal is the solved cube, index 0.

  cornerSlice_ = PruningTable::build(
      "cornerSlice", coord::kCornerPermutationCount * kSlicePermN,
      kPhase2MoveMask, 0, [&](std::uint32_t index, Move m) {
        const std::uint32_t corner = index / kSlicePermN;
        const std::uint32_t slice = index % kSlicePermN;
        return static_cast<std::uint32_t>(cornerT.apply(corner, m)) * kSlicePermN +
               slicePermT.apply(slice, m);
      });

  edgeSlice_ = PruningTable::build(
      "edgeSlice", coord::kUdEdgePermutationCount * kSlicePermN, kPhase2MoveMask,
      0, [&](std::uint32_t index, Move m) {
        const std::uint32_t edge = index / kSlicePermN;
        const std::uint32_t slice = index % kSlicePermN;
        return static_cast<std::uint32_t>(udEdgeT.apply(edge, m)) * kSlicePermN +
               slicePermT.apply(slice, m);
      });
}

std::size_t TwoPhaseTables::byteSize() const noexcept {
  return moves_.byteSize() + flipSlice_.byteSize() + twistSlice_.byteSize() +
         cornerSlice_.byteSize() + edgeSlice_.byteSize();
}

}  // namespace rubik
