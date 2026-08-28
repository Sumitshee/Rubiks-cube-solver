#include "render/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace rubik::render {
namespace {

constexpr float kDefaultDistance = 9.0f;
constexpr float kMinDistance = 4.5f;
constexpr float kMaxDistance = 20.0f;
constexpr float kRadiansPerPixel = 0.008f;

}  // namespace

Camera::Camera() { reset(); }

void Camera::reset() {
  distance_ = kDefaultDistance;
  // A three-quarter view: turned right and tipped down, so three faces are
  // visible at once and the cube reads as a solid rather than a square.
  const glm::quat yaw = glm::angleAxis(glm::radians(-35.0f), glm::vec3(0, 1, 0));
  const glm::quat pitch = glm::angleAxis(glm::radians(25.0f), glm::vec3(1, 0, 0));
  orientation_ = glm::normalize(pitch * yaw);
}

void Camera::orbit(float deltaX, float deltaY) {
  // Rotate about the *world* up and the *camera's* right. Using world up keeps
  // horizontal drags feeling level however far the cube has been tipped, which
  // is what people expect from an orbit control.
  const glm::quat aroundUp =
      glm::angleAxis(deltaX * kRadiansPerPixel, glm::vec3(0, 1, 0));

  const glm::vec3 cameraRight =
      glm::normalize(glm::conjugate(orientation_) * glm::vec3(1, 0, 0));
  const glm::quat aroundRight =
      glm::angleAxis(deltaY * kRadiansPerPixel, cameraRight);

  orientation_ = glm::normalize(orientation_ * aroundRight * aroundUp);
}

void Camera::zoom(float delta) {
  distance_ = std::clamp(distance_ - delta * 0.6f, kMinDistance, kMaxDistance);
}

glm::vec3 Camera::eyePosition() const {
  // The camera sits on the +z axis of its own frame, looking at the origin.
  return glm::conjugate(orientation_) * glm::vec3(0.0f, 0.0f, distance_);
}

glm::mat4 Camera::viewMatrix() const {
  const glm::mat4 rotation = glm::mat4_cast(orientation_);
  const glm::mat4 translation =
      glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -distance_));
  return translation * rotation;
}

glm::mat4 Camera::projectionMatrix(float aspectRatio) const {
  return glm::perspective(glm::radians(38.0f), std::max(aspectRatio, 0.01f),
                          0.1f, 100.0f);
}

}  // namespace rubik::render
