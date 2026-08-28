#pragma once

#include <stdexcept>
#include <string>

namespace rubik {

/// Base class for every error this library raises, so callers can catch one type.
class Error : public std::runtime_error {
 public:
  explicit Error(const std::string& what) : std::runtime_error(what) {}
};

/// Malformed move notation.
class ParseError : public Error {
 public:
  explicit ParseError(const std::string& what) : Error(what) {}
};

/// Which rule a rejected cube state broke.
///
/// The `what()` string says precisely what went wrong and is the right thing to
/// print on a terminal. A graphical front end wants to *group* failures --
/// colour-code them, explain the physics behind each one, point the user at the
/// piece to look at -- and matching on message text to do that would be brittle.
/// So each throw site carries a code as well.
enum class CubeFault {
  /// Not raised by the core; the default for callers that do not classify.
  Unspecified,
  /// Some colour does not appear exactly nine times.
  ColourCount,
  /// A centre shows the wrong colour, so the cube is in an unreadable frame.
  Centre,
  /// A corner slot shows a colour triple that is not a real corner piece.
  CornerPiece,
  /// An edge slot shows a colour pair that is not a real edge piece.
  EdgePiece,
  /// The same cubie appears in two slots.
  DuplicateCubie,
  /// Corner orientations do not sum to zero mod 3: a corner has been twisted.
  CornerTwist,
  /// Edge orientations do not sum to zero mod 2: an edge has been flipped.
  EdgeFlip,
  /// Corner and edge permutation parity disagree: two pieces were swapped.
  Parity,
};

/// A cube state that no sequence of legal moves can produce.
class InvalidStateError : public Error {
 public:
  explicit InvalidStateError(const std::string& what,
                             CubeFault fault = CubeFault::Unspecified)
      : Error(what), fault_(fault) {}

  [[nodiscard]] CubeFault fault() const noexcept { return fault_; }

 private:
  CubeFault fault_;
};

/// A pattern database file that is missing, truncated, or corrupt.
class DatabaseError : public Error {
 public:
  explicit DatabaseError(const std::string& what) : Error(what) {}
};

}  // namespace rubik
