#pragma once

#include "render/Gl.h"
#include "render/Shader.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace rubik::render {

/// A pixel-space overlay for the heads-up display: flat panels and text.
///
/// ## Why a built-in font
///
/// The alternative is a font library and a font file, which means one more
/// dependency and one more thing that can be missing at run time on somebody
/// else's machine. The HUD needs a few dozen characters of monospaced text, so
/// the font here is a 5x7 bitmap compiled into the binary and expanded into a
/// small single-channel texture at start-up. It costs 320 bytes of source data
/// and cannot fail to load.
///
/// ## Batching
///
/// Every panel and every string between `begin` and `end` is appended to one
/// vertex array and drawn with a single call. Unlike the cube, this geometry
/// genuinely does change each frame -- the frame time and the solver status are
/// different every time -- so it lives in a buffer allocated once at start-up
/// and refilled with `glBufferSubData`. The staging vector is a member and is
/// reserved up front, so a frame still performs no allocation.
class TextOverlay {
 public:
  TextOverlay();
  ~TextOverlay();

  TextOverlay(const TextOverlay&) = delete;
  TextOverlay& operator=(const TextOverlay&) = delete;

  /// Starts a batch. Coordinates are framebuffer pixels with the origin at the
  /// top left, which is how a HUD is naturally laid out.
  void begin(int framebufferWidth, int framebufferHeight);

  /// A filled rectangle, typically a translucent backing so text stays legible
  /// over a white cube face.
  void panel(float x, float y, float width, float height,
             const glm::vec4& colour);

  /// Draws `text` with its top-left corner at (x, y). Understands newlines.
  /// Lower-case is drawn using the upper-case glyphs; anything outside the
  /// font's range is drawn as a space.
  void text(float x, float y, float scale, const glm::vec3& colour,
            std::string_view value);

  /// Ends the batch and issues the draw.
  void end();

  /// Layout helpers, so callers can size panels around their text.
  [[nodiscard]] static float advance(float scale) noexcept;
  [[nodiscard]] static float lineHeight(float scale) noexcept;
  [[nodiscard]] static float widthOf(std::string_view value, float scale) noexcept;

 private:
  struct OverlayVertex {
    glm::vec2 position;
    glm::vec2 uv;
    glm::vec4 colour;
  };

  void appendQuad(float x, float y, float width, float height, float u0,
                  float v0, float u1, float v1, const glm::vec4& colour);
  void buildFontTexture();

  Shader shader_;
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint texture_ = 0;
  std::size_t bufferCapacity_ = 0;

  std::vector<OverlayVertex> vertices_;
  glm::mat4 projection_{1.0f};
  int width_ = 0;
  int height_ = 0;
};

}  // namespace rubik::render
