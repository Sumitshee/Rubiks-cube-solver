#include "render/TextOverlay.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstdint>

namespace rubik::render {
namespace {

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
/// One pixel of padding on the right and bottom of each cell, so nearest
/// filtering at a non-integer scale cannot bleed a neighbouring glyph in.
constexpr int kCellWidth = kGlyphWidth + 1;
constexpr int kCellHeight = kGlyphHeight + 1;
constexpr int kColumns = 16;
constexpr int kRows = 5;  // four rows of glyphs, plus one holding the solid cell
constexpr int kTextureWidth = kColumns * kCellWidth;    // 96
constexpr int kTextureHeight = kRows * kCellHeight;     // 40

constexpr char kFirstGlyph = 0x20;  // space
constexpr char kLastGlyph = 0x5F;   // underscore
constexpr int kGlyphCount = kLastGlyph - kFirstGlyph + 1;  // 64
/// Cell index of the fully opaque block used to draw panels.
constexpr int kSolidCell = kGlyphCount;

/// The font: 64 glyphs, five columns each, one bit per row with bit 0 at the
/// top. Drawn by hand on a 5x7 grid -- see the generator comment in the class
/// documentation for why the font is built in rather than loaded.
constexpr std::array<std::array<std::uint8_t, kGlyphWidth>, kGlyphCount> kFont{{
    {0x00, 0x00, 0x00, 0x00, 0x00},  // ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00},  // '!'
    {0x00, 0x03, 0x00, 0x03, 0x00},  // '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14},  // '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},  // '$'
    {0x23, 0x13, 0x08, 0x64, 0x62},  // '%'
    {0x36, 0x49, 0x55, 0x22, 0x50},  // '&'
    {0x00, 0x00, 0x03, 0x00, 0x00},  // '''
    {0x00, 0x1C, 0x22, 0x41, 0x00},  // '('
    {0x00, 0x41, 0x22, 0x1C, 0x00},  // ')'
    {0x2A, 0x1C, 0x3E, 0x1C, 0x2A},  // '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08},  // '+'
    {0x00, 0x00, 0x60, 0x20, 0x00},  // ','
    {0x08, 0x08, 0x08, 0x08, 0x08},  // '-'
    {0x00, 0x60, 0x60, 0x00, 0x00},  // '.'
    {0x40, 0x30, 0x08, 0x06, 0x01},  // '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00},  // '1'
    {0x42, 0x61, 0x51, 0x49, 0x46},  // '2'
    {0x21, 0x41, 0x45, 0x47, 0x39},  // '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // '4'
    {0x27, 0x45, 0x45, 0x45, 0x39},  // '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // '6'
    {0x01, 0x71, 0x09, 0x05, 0x03},  // '7'
    {0x36, 0x49, 0x49, 0x49, 0x36},  // '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // '9'
    {0x00, 0x36, 0x36, 0x00, 0x00},  // ':'
    {0x00, 0x56, 0x36, 0x00, 0x00},  // ';'
    {0x08, 0x14, 0x22, 0x41, 0x00},  // '<'
    {0x14, 0x14, 0x14, 0x14, 0x14},  // '='
    {0x00, 0x41, 0x22, 0x14, 0x08},  // '>'
    {0x02, 0x01, 0x51, 0x09, 0x06},  // '?'
    {0x3E, 0x41, 0x5D, 0x55, 0x1E},  // '@'
    {0x7C, 0x12, 0x11, 0x12, 0x7C},  // 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // 'C'
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01},  // 'F'
    {0x3E, 0x41, 0x49, 0x49, 0x3A},  // 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // 'L'
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},  // 'M'
    {0x7F, 0x06, 0x08, 0x30, 0x7F},  // 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // 'R'
    {0x26, 0x49, 0x49, 0x49, 0x32},  // 'S'
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // 'V'
    {0x7F, 0x20, 0x18, 0x20, 0x7F},  // 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63},  // 'X'
    {0x03, 0x04, 0x78, 0x04, 0x03},  // 'Y'
    {0x61, 0x51, 0x49, 0x45, 0x43},  // 'Z'
    {0x00, 0x7F, 0x41, 0x41, 0x00},  // '['
    {0x01, 0x06, 0x08, 0x30, 0x40},  // 'backslash'
    {0x00, 0x41, 0x41, 0x7F, 0x00},  // ']'
    {0x04, 0x02, 0x01, 0x02, 0x04},  // '^'
    {0x40, 0x40, 0x40, 0x40, 0x40},  // '_'
}};

const char* const kVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColour;

uniform mat4 uProjection;

out vec2 vUv;
out vec4 vColour;

void main() {
  vUv = aUv;
  vColour = aColour;
  gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
}
)";

const char* const kFragmentShader = R"(#version 330 core
in vec2 vUv;
in vec4 vColour;

uniform sampler2D uFont;

out vec4 fragColour;

void main() {
  float coverage = texture(uFont, vUv).r;
  fragColour = vec4(vColour.rgb, vColour.a * coverage);
}
)";

/// Where a cell sits in the atlas, in texture coordinates.
void cellUv(int cell, float& u0, float& v0, float& u1, float& v1) {
  const int column = cell % kColumns;
  const int row = cell / kColumns;
  u0 = static_cast<float>(column * kCellWidth) / kTextureWidth;
  v0 = static_cast<float>(row * kCellHeight) / kTextureHeight;
  u1 = static_cast<float>(column * kCellWidth + kGlyphWidth) / kTextureWidth;
  v1 = static_cast<float>(row * kCellHeight + kGlyphHeight) / kTextureHeight;
}

}  // namespace

TextOverlay::TextOverlay() : shader_(kVertexShader, kFragmentShader) {
  // Room for roughly 1,300 characters, which is far more than the HUD uses.
  // Reserved once so no frame ever grows the vector.
  vertices_.reserve(8192);

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);

  bufferCapacity_ = vertices_.capacity();
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(bufferCapacity_ * sizeof(OverlayVertex)),
               nullptr, GL_DYNAMIC_DRAW);

  const auto stride = static_cast<GLsizei>(sizeof(OverlayVertex));
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(
      0, 2, GL_FLOAT, GL_FALSE, stride,
      reinterpret_cast<void*>(offsetof(OverlayVertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<void*>(offsetof(OverlayVertex, uv)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(
      2, 4, GL_FLOAT, GL_FALSE, stride,
      reinterpret_cast<void*>(offsetof(OverlayVertex, colour)));

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  buildFontTexture();
}

TextOverlay::~TextOverlay() {
  if (texture_ != 0) glDeleteTextures(1, &texture_);
  if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
  if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
}

void TextOverlay::buildFontTexture() {
  std::array<std::uint8_t, static_cast<std::size_t>(kTextureWidth) * kTextureHeight>
      pixels{};

  for (int glyph = 0; glyph < kGlyphCount; ++glyph) {
    const int column = glyph % kColumns;
    const int row = glyph / kColumns;
    for (int x = 0; x < kGlyphWidth; ++x) {
      const std::uint8_t bits = kFont[static_cast<std::size_t>(glyph)]
                                     [static_cast<std::size_t>(x)];
      for (int y = 0; y < kGlyphHeight; ++y) {
        if ((bits & (1u << y)) == 0) continue;
        const int px = column * kCellWidth + x;
        const int py = row * kCellHeight + y;
        pixels[static_cast<std::size_t>(py) * kTextureWidth +
               static_cast<std::size_t>(px)] = 255;
      }
    }
  }

  // The solid cell, used for panel backgrounds.
  const int solidColumn = kSolidCell % kColumns;
  const int solidRow = kSolidCell / kColumns;
  for (int y = 0; y < kGlyphHeight; ++y) {
    for (int x = 0; x < kGlyphWidth; ++x) {
      const int px = solidColumn * kCellWidth + x;
      const int py = solidRow * kCellHeight + y;
      pixels[static_cast<std::size_t>(py) * kTextureWidth +
             static_cast<std::size_t>(px)] = 255;
    }
  }

  glGenTextures(1, &texture_);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kTextureWidth, kTextureHeight, 0, GL_RED,
               GL_UNSIGNED_BYTE, pixels.data());
  // Nearest filtering: a bitmap font scaled up should look like crisp blocks,
  // not a blur.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
}

float TextOverlay::advance(float scale) noexcept {
  return static_cast<float>(kCellWidth) * scale;
}

float TextOverlay::lineHeight(float scale) noexcept {
  return static_cast<float>(kCellHeight + 2) * scale;
}

float TextOverlay::widthOf(std::string_view value, float scale) noexcept {
  std::size_t longest = 0;
  std::size_t current = 0;
  for (const char c : value) {
    if (c == '\n') {
      longest = current > longest ? current : longest;
      current = 0;
    } else {
      ++current;
    }
  }
  longest = current > longest ? current : longest;
  return static_cast<float>(longest) * advance(scale);
}

void TextOverlay::begin(int framebufferWidth, int framebufferHeight) {
  width_ = framebufferWidth;
  height_ = framebufferHeight;
  // y grows downward, so the HUD is laid out from the top left.
  projection_ = glm::ortho(0.0f, static_cast<float>(framebufferWidth),
                           static_cast<float>(framebufferHeight), 0.0f);
  vertices_.clear();
}

void TextOverlay::appendQuad(float x, float y, float width, float height,
                             float u0, float v0, float u1, float v1,
                             const glm::vec4& colour) {
  const glm::vec2 topLeft{x, y};
  const glm::vec2 topRight{x + width, y};
  const glm::vec2 bottomRight{x + width, y + height};
  const glm::vec2 bottomLeft{x, y + height};

  vertices_.push_back({topLeft, {u0, v0}, colour});
  vertices_.push_back({bottomLeft, {u0, v1}, colour});
  vertices_.push_back({bottomRight, {u1, v1}, colour});
  vertices_.push_back({topLeft, {u0, v0}, colour});
  vertices_.push_back({bottomRight, {u1, v1}, colour});
  vertices_.push_back({topRight, {u1, v0}, colour});
}

void TextOverlay::panel(float x, float y, float width, float height,
                        const glm::vec4& colour) {
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 0.0f;
  float v1 = 0.0f;
  cellUv(kSolidCell, u0, v0, u1, v1);
  // Sample the middle of the solid cell so no edge texel can be picked up.
  const float uMid = (u0 + u1) * 0.5f;
  const float vMid = (v0 + v1) * 0.5f;
  appendQuad(x, y, width, height, uMid, vMid, uMid, vMid, colour);
}

void TextOverlay::text(float x, float y, float scale, const glm::vec3& colour,
                       std::string_view value) {
  const glm::vec4 rgba{colour, 1.0f};
  const float glyphWidth = static_cast<float>(kGlyphWidth) * scale;
  const float glyphHeight = static_cast<float>(kGlyphHeight) * scale;

  float penX = x;
  float penY = y;
  for (const char raw : value) {
    if (raw == '\n') {
      penX = x;
      penY += lineHeight(scale);
      continue;
    }

    char c = raw;
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c < kFirstGlyph || c > kLastGlyph) c = ' ';

    if (c != ' ') {
      float u0 = 0.0f;
      float v0 = 0.0f;
      float u1 = 0.0f;
      float v1 = 0.0f;
      cellUv(c - kFirstGlyph, u0, v0, u1, v1);
      appendQuad(penX, penY, glyphWidth, glyphHeight, u0, v0, u1, v1, rgba);
    }
    penX += advance(scale);
  }
}

void TextOverlay::end() {
  if (vertices_.empty()) return;

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  if (vertices_.size() > bufferCapacity_) {
    // Only reached if a caller draws far more text than the HUD does; the
    // buffer is resized once and stays that size.
    bufferCapacity_ = vertices_.size();
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(bufferCapacity_ * sizeof(OverlayVertex)),
                 nullptr, GL_DYNAMIC_DRAW);
  }
  glBufferSubData(
      GL_ARRAY_BUFFER, 0,
      static_cast<GLsizeiptr>(vertices_.size() * sizeof(OverlayVertex)),
      vertices_.data());

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  shader_.use();
  shader_.set("uProjection", projection_);
  shader_.set("uFont", 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_);

  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_BLEND);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

}  // namespace rubik::render
