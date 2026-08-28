#include "ui/CubeEditor.h"

#include <algorithm>

namespace rubik::ui {
namespace {

/// Where each face's 3x3 block sits in the 12 x 9 net, as (column, row) of its
/// top-left cell. Indexed by Face: U, R, F, D, L, B.
constexpr std::array<std::array<int, 2>, kNumFaces> kFaceOrigin{{
    {{3, 0}},  // U
    {{6, 3}},  // R
    {{3, 3}},  // F
    {{3, 6}},  // D
    {{0, 3}},  // L
    {{9, 3}},  // B
}};

}  // namespace

bool netCell(int facelet, int& column, int& row) noexcept {
  if (facelet < 0 || facelet >= kNumFacelets) return false;
  const int face = facelet / 9;
  const int within = facelet % 9;
  const auto& origin = kFaceOrigin[static_cast<std::size_t>(face)];
  column = origin[0] + within % 3;
  row = origin[1] + within / 3;
  return true;
}

int faceletAtCell(int column, int row) noexcept {
  if (column < 0 || column >= kNetColumns || row < 0 || row >= kNetRows) return -1;
  for (int face = 0; face < kNumFaces; ++face) {
    const auto& origin = kFaceOrigin[static_cast<std::size_t>(face)];
    const int dc = column - origin[0];
    const int dr = row - origin[1];
    if (dc >= 0 && dc < 3 && dr >= 0 && dr < 3) {
      return face * 9 + dr * 3 + dc;
    }
  }
  return -1;
}

const char* colourName(Face face) noexcept {
  switch (face) {
    case Face::U: return "WHITE";
    case Face::R: return "RED";
    case Face::F: return "GREEN";
    case Face::D: return "YELLOW";
    case Face::L: return "ORANGE";
    case Face::B: return "BLUE";
  }
  return "?";
}

CubeEditor::CubeEditor() { resetToSolved(); }

void CubeEditor::loadFrom(const Cube& cube) {
  const FaceletArray facelets = toFacelets(cube);
  for (std::size_t i = 0; i < kNumFacelets; ++i) stickers_[i] = facelets[i];
}

void CubeEditor::resetToSolved() {
  for (int i = 0; i < kNumFacelets; ++i) {
    stickers_[static_cast<std::size_t>(i)] = static_cast<Face>(i / 9);
  }
}

void CubeEditor::clear() {
  for (int i = 0; i < kNumFacelets; ++i) {
    stickers_[static_cast<std::size_t>(i)] =
        isCentre(i) ? std::optional<Face>(static_cast<Face>(i / 9)) : std::nullopt;
  }
}

bool CubeEditor::isCentre(int facelet) noexcept {
  return facelet >= 0 && facelet < kNumFacelets && facelet % 9 == 4;
}

void CubeEditor::paint(int facelet, Face colour) {
  if (facelet < 0 || facelet >= kNumFacelets) return;
  if (isCentre(facelet)) return;  // centres define the frame; they never move
  stickers_[static_cast<std::size_t>(facelet)] = colour;
}

void CubeEditor::paintAtCursor(Face colour) { paint(cursor_, colour); }

void CubeEditor::erase(int facelet) {
  if (facelet < 0 || facelet >= kNumFacelets) return;
  if (isCentre(facelet)) return;
  stickers_[static_cast<std::size_t>(facelet)] = std::nullopt;
}

std::optional<Face> CubeEditor::colourAt(int facelet) const noexcept {
  if (facelet < 0 || facelet >= kNumFacelets) return std::nullopt;
  return stickers_[static_cast<std::size_t>(facelet)];
}

void CubeEditor::setCursor(int facelet) noexcept {
  if (facelet >= 0 && facelet < kNumFacelets) cursor_ = facelet;
}

void CubeEditor::moveCursor(int dColumn, int dRow) noexcept {
  int column = 0;
  int row = 0;
  if (!netCell(cursor_, column, row)) return;

  // Step until a cell of the net is found, so moving off the U block into the
  // gap beside L lands somewhere sensible instead of getting stuck.
  for (int step = 1; step <= std::max(kNetColumns, kNetRows); ++step) {
    const int candidate =
        faceletAtCell(column + dColumn * step, row + dRow * step);
    if (candidate >= 0) {
      cursor_ = candidate;
      return;
    }
  }
}

int CubeEditor::unsetCount() const noexcept {
  int count = 0;
  for (const auto& sticker : stickers_) {
    if (!sticker.has_value()) ++count;
  }
  return count;
}

std::array<int, kNumFaces> CubeEditor::colourCounts() const noexcept {
  std::array<int, kNumFaces> counts{};
  for (const auto& sticker : stickers_) {
    if (sticker.has_value()) {
      counts[static_cast<std::size_t>(*sticker)]++;
    }
  }
  return counts;
}

CubeDiagnosis CubeEditor::validate() const {
  const int missing = unsetCount();
  if (missing > 0) {
    CubeDiagnosis out;
    out.valid = false;
    out.fault = CubeFault::Unspecified;
    out.headline = "Cube is not finished";
    out.detail = std::to_string(missing) + " sticker" +
                 (missing == 1 ? " has" : "s have") + " not been set yet";
    out.explanation =
        "Fill in every sticker before validating. Pick a colour, then click "
        "the stickers that show it.";
    return out;
  }

  FaceletArray facelets{};
  for (std::size_t i = 0; i < kNumFacelets; ++i) facelets[i] = *stickers_[i];
  // One conversion path for the whole project: this is the same call the CLI
  // makes for --state.
  return diagnose(facelets);
}

}  // namespace rubik::ui
