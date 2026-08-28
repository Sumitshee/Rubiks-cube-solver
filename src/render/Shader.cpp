#include "render/Shader.h"

#include "core/Error.h"

#include <glm/gtc/type_ptr.hpp>

#include <utility>
#include <vector>

namespace rubik::render {
namespace {

GLuint compile(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok == GL_FALSE) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<std::size_t>(length > 0 ? length : 1));
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    glDeleteShader(shader);
    throw Error(std::string(type == GL_VERTEX_SHADER ? "vertex" : "fragment") +
                " shader failed to compile: " + log.data());
  }
  return shader;
}

}  // namespace

Shader::Shader(const char* vertexSource, const char* fragmentSource) {
  const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource);
  GLuint fragment = 0;
  try {
    fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
  } catch (...) {
    glDeleteShader(vertex);
    throw;
  }

  program_ = glCreateProgram();
  glAttachShader(program_, vertex);
  glAttachShader(program_, fragment);
  glLinkProgram(program_);

  // The shader objects are no longer needed once linked, whatever the outcome.
  glDetachShader(program_, vertex);
  glDetachShader(program_, fragment);
  glDeleteShader(vertex);
  glDeleteShader(fragment);

  GLint ok = GL_FALSE;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (ok == GL_FALSE) {
    GLint length = 0;
    glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<std::size_t>(length > 0 ? length : 1));
    glGetProgramInfoLog(program_, length, nullptr, log.data());
    glDeleteProgram(program_);
    program_ = 0;
    throw Error(std::string("shader program failed to link: ") + log.data());
  }
}

Shader::~Shader() {
  if (program_ != 0) glDeleteProgram(program_);
}

Shader::Shader(Shader&& other) noexcept
    : program_(other.program_), locations_(std::move(other.locations_)) {
  other.program_ = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
  if (this != &other) {
    if (program_ != 0) glDeleteProgram(program_);
    program_ = other.program_;
    locations_ = std::move(other.locations_);
    other.program_ = 0;
  }
  return *this;
}

void Shader::use() const { glUseProgram(program_); }

GLint Shader::location(const char* name) {
  const auto found = locations_.find(name);
  if (found != locations_.end()) return found->second;
  const GLint value = glGetUniformLocation(program_, name);
  locations_.emplace(name, value);
  return value;
}

void Shader::set(const char* name, int value) { glUniform1i(location(name), value); }

void Shader::set(const char* name, float value) {
  glUniform1f(location(name), value);
}

void Shader::set(const char* name, const glm::vec2& value) {
  glUniform2fv(location(name), 1, glm::value_ptr(value));
}

void Shader::set(const char* name, const glm::vec3& value) {
  glUniform3fv(location(name), 1, glm::value_ptr(value));
}

void Shader::set(const char* name, const glm::vec4& value) {
  glUniform4fv(location(name), 1, glm::value_ptr(value));
}

void Shader::set(const char* name, const glm::mat3& value) {
  glUniformMatrix3fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::set(const char* name, const glm::mat4& value) {
  glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setArray(const char* name, const glm::vec3* values, int count) {
  glUniform3fv(location(name), count, glm::value_ptr(values[0]));
}

}  // namespace rubik::render
