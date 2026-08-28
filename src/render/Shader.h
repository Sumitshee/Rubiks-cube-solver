#pragma once

#include "render/Gl.h"

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

namespace rubik::render {

/// A linked shader program, owned.
///
/// Shaders are compiled once at start-up from strings built into the binary --
/// there are two of them and they are short, so shipping separate files would
/// only add a way for the program to fail at run time on a machine where the
/// working directory is not what it expected.
///
/// Uniform locations are looked up once and cached, so setting a uniform per
/// draw does not cost a driver query.
class Shader {
 public:
  /// Throws `Error` carrying the compiler or linker log on failure.
  Shader(const char* vertexSource, const char* fragmentSource);
  ~Shader();

  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;
  Shader(Shader&& other) noexcept;
  Shader& operator=(Shader&& other) noexcept;

  void use() const;

  void set(const char* name, int value);
  void set(const char* name, float value);
  void set(const char* name, const glm::vec2& value);
  void set(const char* name, const glm::vec3& value);
  void set(const char* name, const glm::vec4& value);
  void set(const char* name, const glm::mat3& value);
  void set(const char* name, const glm::mat4& value);
  /// For `uniform vec3 name[count]`.
  void setArray(const char* name, const glm::vec3* values, int count);

 private:
  [[nodiscard]] GLint location(const char* name);

  GLuint program_ = 0;
  std::unordered_map<std::string, GLint> locations_;
};

}  // namespace rubik::render
