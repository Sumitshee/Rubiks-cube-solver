#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace rubik::render {

/// An orbit camera that keeps its orientation as a quaternion.
///
/// Dragging composes an incremental rotation onto the current orientation
/// rather than accumulating yaw and pitch angles. That matters here because the
/// user can orbit continuously in any direction: with Euler angles, tipping the
/// cube past vertical collapses two axes onto one (gimbal lock) and the drag
/// direction suddenly changes meaning. Composing quaternions has no such
/// degenerate orientation, and drags stay predictable however the cube is
/// turned.
class Camera {
 public:
  Camera();

  /// Orbits by a mouse delta in pixels.
  void orbit(float deltaX, float deltaY);
  /// Zooms by a scroll delta. Distance is clamped to sensible bounds.
  void zoom(float delta);
  /// Returns to the default three-quarter view.
  void reset();

  [[nodiscard]] glm::mat4 viewMatrix() const;
  [[nodiscard]] glm::mat4 projectionMatrix(float aspectRatio) const;
  [[nodiscard]] glm::vec3 eyePosition() const;

  [[nodiscard]] float distance() const noexcept { return distance_; }

 private:
  glm::quat orientation_{};
  float distance_ = 0.0f;
};

}  // namespace rubik::render
