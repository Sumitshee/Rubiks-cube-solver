#pragma once

#include "core/Cube.h"
#include "core/CubeValidation.h"
#include "core/Facelets.h"

#include <array>
#include <optional>
#include <string>

namespace rubik::ui {

/// The 2D cube net the user fills in, and the geometry both the renderer and
/// the mouse hit-test read from.
///
/// ```
///         [ U ]
///  [ L ] [ F ] [ R ] [ B ]
///         [ D ]
/// ```
///
/// A 12 x 9 grid of cells, of which 54 are stickers. `netCell` and
/// `faceletAtCell` are inverses, and both are pure integer arithmetic: keeping
/// them here rather than in the renderer means clicking a sticker and drawing a
/// sticker cannot disagree, and both can be tested without a window.
inline constexpr int kNetColumns = 12;
inline constexpr int kNetRows = 9;

/// Grid cell of a facelet. Returns false for an index outside 0..53.
[[nodiscard]] bool netCell(int facelet, int& column, int& row) noexcept;

/// The facelet at a grid cell, or -1 where the net has a hole.
[[nodiscard]] int faceletAtCell(int column, int row) noexcept;

/// The colour a user picks from the palette, in the canonical face order.
[[nodiscard]] const char* colourName(Face face) noexcept;

/// A cube being entered by hand.
///
/// Stickers may be *unset*, which a `FaceletArray` cannot express -- that is
/// the only reason this type exists rather than editing a `FaceletArray`
/// directly. Centres are fixed and cannot be painted: they define the frame the
/// rest of the cube is read in, so letting the user change them would only ever
/// produce a cube the solver has to reject.
///
/// No OpenGL, no input handling. The GUI reads this and draws it.
class CubeEditor {
 public:
  /// Starts from the solved cube.
  CubeEditor();

  /// Seeds the editor from an existing cube, so "edit" starts from what is on
  /// screen rather than from nothing.
  void loadFrom(const Cube& cube);

  /// Every sticker set to its solved colour.
  void resetToSolved();

  /// Clears every sticker except the six centres.
  void clear();

  // --- Editing -------------------------------------------------------------

  /// Paints one facelet. Ignores centres and out-of-range indices.
  void paint(int facelet, Face colour);
  /// Paints the facelet under the cursor.
  void paintAtCursor(Face colour);
  /// Clears one facelet. Ignores centres.
  void erase(int facelet);

  [[nodiscard]] std::optional<Face> colourAt(int facelet) const noexcept;
  [[nodiscard]] static bool isCentre(int facelet) noexcept;

  // --- Cursor --------------------------------------------------------------

  [[nodiscard]] int cursor() const noexcept { return cursor_; }
  void setCursor(int facelet) noexcept;
  /// Moves the cursor across the net, skipping the holes in it. A move that
  /// would leave the net leaves the cursor where it is.
  void moveCursor(int dColumn, int dRow) noexcept;

  /// The colour the next click or key paints with.
  [[nodiscard]] Face brush() const noexcept { return brush_; }
  void setBrush(Face colour) noexcept { brush_ = colour; }

  // --- Result --------------------------------------------------------------

  [[nodiscard]] int unsetCount() const noexcept;
  [[nodiscard]] bool complete() const noexcept { return unsetCount() == 0; }

  /// How many stickers of each colour have been placed so far, so the user can
  /// see they have entered ten greens before they press validate.
  [[nodiscard]] std::array<int, kNumFaces> colourCounts() const noexcept;

  /// Validates the entry. An incomplete net is reported as such rather than
  /// being guessed at; a complete one is handed to the core validator, which is
  /// the same code path the CLI uses.
  [[nodiscard]] CubeDiagnosis validate() const;

 private:
  std::array<std::optional<Face>, kNumFacelets> stickers_{};
  int cursor_ = 0;
  Face brush_ = Face::U;
};

}  // namespace rubik::ui
