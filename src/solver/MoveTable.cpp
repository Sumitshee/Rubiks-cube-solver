#include "solver/MoveTable.h"

namespace rubik {

MoveTables::MoveTables()
    : cornerOri_("cornerOrientation", coord::kCornerOrientationCount,
                 kAllMoveMask, coord::cubeWithCornerOrientation,
                 coord::cornerOrientation),
      edgeOri_("edgeOrientation", coord::kEdgeOrientationCount, kAllMoveMask,
               coord::cubeWithEdgeOrientation, coord::edgeOrientation),
      udSliceSorted_("udSliceSorted", coord::kUdSliceSortedCount, kAllMoveMask,
                     coord::cubeWithUdSliceSorted, coord::udSliceSorted),
      // A slice value's successor does not depend on the order of the four
      // slice edges, so any representative of the 24 orderings will do.
      udSlice_("udSlice", coord::kUdSliceCount, kAllMoveMask,
               [](std::uint32_t v) {
                 return coord::cubeWithUdSliceSorted(
                     v * coord::kSlicePermutationCount);
               },
               coord::udSlice),
      cornerPerm_("cornerPermutation", coord::kCornerPermutationCount,
                  kAllMoveMask, coord::cubeWithCornerPermutation,
                  coord::cornerPermutation),
      // The two coordinates below are only defined inside G1, so their tables
      // cover the ten G1 moves.
      udEdgePerm_("udEdgePermutation", coord::kUdEdgePermutationCount,
                  kPhase2MoveMask, coord::cubeWithUdEdgePermutation,
                  coord::udEdgePermutation),
      slicePerm_("slicePermutation", coord::kSlicePermutationCount,
                 kPhase2MoveMask, coord::cubeWithSlicePermutation,
                 coord::slicePermutation) {}

std::size_t MoveTables::byteSize() const noexcept {
  return cornerOri_.byteSize() + edgeOri_.byteSize() +
         udSliceSorted_.byteSize() + udSlice_.byteSize() +
         cornerPerm_.byteSize() + udEdgePerm_.byteSize() +
         slicePerm_.byteSize();
}

}  // namespace rubik
