#include "render/Mesh.h"


namespace rubik::render {

Mesh::Mesh(const std::vector<Vertex>& vertices)
    : count_(vertices.size()) {
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
               vertices.data(), GL_STATIC_DRAW);

  const auto stride = static_cast<GLsizei>(sizeof(Vertex));
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<void*>(offsetof(Vertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<void*>(offsetof(Vertex, normal)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<void*>(offsetof(Vertex, face)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<void*>(offsetof(Vertex, sticker)));

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Mesh::release() noexcept {
  if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
  if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
  vbo_ = 0;
  vao_ = 0;
  count_ = 0;
}

Mesh::~Mesh() { release(); }

Mesh::Mesh(Mesh&& other) noexcept
    : vao_(other.vao_), vbo_(other.vbo_), count_(other.count_) {
  other.vao_ = 0;
  other.vbo_ = 0;
  other.count_ = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
  if (this != &other) {
    release();
    vao_ = other.vao_;
    vbo_ = other.vbo_;
    count_ = other.count_;
    other.vao_ = 0;
    other.vbo_ = 0;
    other.count_ = 0;
  }
  return *this;
}

void Mesh::draw() const {
  if (count_ == 0) return;
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count_));
  glBindVertexArray(0);
}

}  // namespace rubik::render
