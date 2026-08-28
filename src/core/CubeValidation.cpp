#include "core/CubeValidation.h"

namespace rubik {
namespace {

/// Why the cube cannot physically be in this state.
///
/// Each of these is a consequence of the same fact: a face turn is a 4-cycle on
/// corners and a 4-cycle on edges, and it adds a fixed pattern of twists and
/// flips. Whatever is invariant under all eighteen turns stays invariant for
/// every reachable state, so a state breaking one of them cannot be reached --
/// no matter how long you search.
const char* explanationFor(CubeFault fault) noexcept {
  switch (fault) {
    case CubeFault::ColourCount:
      return "A cube has exactly nine stickers of each colour. Count the "
             "stickers of the colour named above and correct the entry.";
    case CubeFault::Centre:
      return "Centre pieces never move relative to each other, so they define "
             "the frame the rest of the cube is read in. Hold the cube with "
             "white up and green in front.";
    case CubeFault::CornerPiece:
      return "The three colours on a corner must match a corner that exists on "
             "a real cube. Check the colours you entered for that corner.";
    case CubeFault::EdgePiece:
      return "The two colours on an edge must match an edge that exists on a "
             "real cube. Opposite colours never share a piece.";
    case CubeFault::DuplicateCubie:
      return "The same piece has been entered in two places. Every cubie "
             "appears exactly once on a cube.";
    case CubeFault::CornerTwist:
      return "Corner twists always sum to a multiple of three. A single corner "
             "rotated in place cannot happen by turning faces -- it happens "
             "when a corner is forced back into its slot the wrong way.";
    case CubeFault::EdgeFlip:
      return "Edge flips always sum to an even number. A single flipped edge "
             "cannot be produced by turning faces.";
    case CubeFault::Parity:
      return "Every face turn moves four corners and four edges together, so "
             "corner and edge permutation parity always agree. They disagree "
             "here, which is the signature of two pieces having been swapped.";
    case CubeFault::Unspecified:
      break;
  }
  return "This sticker arrangement cannot occur on a physical cube.";
}

const char* headlineFor(CubeFault fault) noexcept {
  switch (fault) {
    case CubeFault::ColourCount:   return "Wrong number of stickers of a colour";
    case CubeFault::Centre:        return "Centre colours are not in the expected frame";
    case CubeFault::CornerPiece:   return "That corner piece does not exist";
    case CubeFault::EdgePiece:     return "That edge piece does not exist";
    case CubeFault::DuplicateCubie:return "The same piece appears twice";
    case CubeFault::CornerTwist:   return "Corner twist parity is inconsistent";
    case CubeFault::EdgeFlip:      return "Edge flip parity is inconsistent";
    case CubeFault::Parity:        return "Permutation parity is inconsistent";
    case CubeFault::Unspecified:   break;
  }
  return "Invalid cube configuration";
}

}  // namespace

const char* toString(CubeFault fault) noexcept { return headlineFor(fault); }

CubeDiagnosis diagnose(const FaceletArray& facelets) {
  CubeDiagnosis out;
  try {
    out.cube = fromFacelets(facelets);
    out.valid = true;
    out.fault = CubeFault::Unspecified;
    out.headline = "Valid cube";
    out.detail = "This state is reachable from a solved cube.";
    out.explanation.clear();
  } catch (const InvalidStateError& e) {
    out.valid = false;
    out.fault = e.fault();
    out.headline = headlineFor(out.fault);
    out.detail = e.what();
    out.explanation = explanationFor(out.fault);
  }
  return out;
}

}  // namespace rubik
