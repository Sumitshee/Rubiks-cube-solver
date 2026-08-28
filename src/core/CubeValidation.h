#pragma once

#include "core/Cube.h"
#include "core/Error.h"
#include "core/Facelets.h"

#include <string>

namespace rubik {

/// The verdict on a set of 54 stickers, as data rather than as an exception.
///
/// `fromFacelets` already performs every check this needs and throws a precise
/// message. That is the right interface for the CLI, which prints the message
/// and exits. A graphical front end wants something else: to keep running, to
/// group failures by kind, and to explain *why* the cube is impossible rather
/// than only what is wrong with it. So this wraps the existing conversion
/// instead of reimplementing it -- there is exactly one facelet-to-cubie path
/// in the project, and one set of rules.
struct CubeDiagnosis {
  bool valid = false;
  CubeFault fault = CubeFault::Unspecified;

  /// A short line naming the problem, for a heading.
  std::string headline;
  /// The precise message from the core, naming the offending piece or colour.
  std::string detail;
  /// Why a physical cube cannot be in this state, in plain language.
  std::string explanation;

  /// Only meaningful when `valid`. The solved cube otherwise.
  Cube cube;
};

/// Validates `facelets` and converts them to the cube representation.
///
/// Never throws for a bad cube -- that is the whole point of returning a
/// diagnosis. It still propagates genuinely exceptional failures.
[[nodiscard]] CubeDiagnosis diagnose(const FaceletArray& facelets);

/// A short human-readable name for a fault, for logs and headings.
[[nodiscard]] const char* toString(CubeFault fault) noexcept;

}  // namespace rubik
