#pragma once

#include "render/Gl.h"

#include <string>

namespace rubik::render {

/// Owns the GLFW window and the OpenGL context.
///
/// RAII throughout: the constructor initialises GLFW, creates the window and
/// loads the function pointers; the destructor tears all three down in reverse.
/// Nothing else in the project may create a context, so there is exactly one
/// place where graphics initialisation can fail, and exactly one error message
/// to write.
class GlContext {
 public:
  /// Throws `Error` with a specific message when the window or context cannot
  /// be created -- a missing driver, no display, or an OpenGL version below
  /// 3.3. Failing here is expected on a headless machine, and is why the solver
  /// never depends on this target.
  GlContext(int width, int height, const std::string& title);
  ~GlContext();

  GlContext(const GlContext&) = delete;
  GlContext& operator=(const GlContext&) = delete;

  [[nodiscard]] bool shouldClose() const;
  void requestClose();
  void swapBuffers();

  /// Turns vsync off so the loop runs as fast as it can. Only useful for
  /// measuring what a frame actually costs -- with vsync on, every frame
  /// reads as one refresh interval whatever the renderer is doing.
  void setVsync(bool enabled);
  void pollEvents();

  [[nodiscard]] GLFWwindow* window() const noexcept { return window_; }

  /// Framebuffer size in pixels, which is not the window size on a scaled
  /// display.
  void framebufferSize(int& width, int& height) const;

  /// Seconds since the context was created.
  [[nodiscard]] double time() const;

  /// A human-readable description of the driver, for the console banner.
  [[nodiscard]] std::string describe() const;

 private:
  GLFWwindow* window_ = nullptr;
};

}  // namespace rubik::render
