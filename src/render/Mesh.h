#pragma once

#include "render/Gl.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace rubik::render {

/// One vertex of the cubie mesh.
///
/// `face` says which of the six directions this vertex belongs to, and
/// `sticker` whether it is part of the coloured tile or the black body. The
/// fragment shader uses the pair to pick a colour, which is what lets all 27
/// cubies share a single immutable mesh: only a uniform changes between draws,
/// never the geometry.
struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  float face = 0.0f;
  float sticker = 0.0f;
};

/// A vertex array and its buffer, owned.
///
/// Uploaded once at start-up. Nothing here is rebuilt per frame -- the cube
/// changes by transform and colour, never by geometry.
class Mesh {
 public:
  Mesh() = default;
  explicit Mesh(const std::vector<Vertex>& vertices);
  ~Mesh();

  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;
  Mesh(Mesh&& other) noexcept;
  Mesh& operator=(Mesh&& other) noexcept;

  void draw() const;
  [[nodiscard]] std::size_t vertexCount() const noexcept { return count_; }

 private:
  void release() noexcept;

  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  std::size_t count_ = 0;
};

}  // namespace rubik::render
