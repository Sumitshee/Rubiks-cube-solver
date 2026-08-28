#include "render/GlContext.h"

#include "core/Error.h"

#include <sstream>

namespace rubik::render {
namespace {

int g_initCount = 0;

void errorCallback(int code, const char* description) {
  // GLFW reports asynchronously; the constructor turns a failed creation into
  // an exception, so this only needs to make the reason visible.
  std::fprintf(stderr, "GLFW error %d: %s\n", code, description ? description : "");
}

}  // namespace

GlContext::GlContext(int width, int height, const std::string& title) {
  if (g_initCount == 0) {
    glfwSetErrorCallback(errorCallback);
    if (glfwInit() != GLFW_TRUE) {
      throw Error(
          "could not initialise GLFW; this machine may have no display or no "
          "OpenGL driver. The solver and benchmark tools do not need one -- use "
          "rubiks_solver or rubiks_bench instead.");
    }
  }
  ++g_initCount;

  // OpenGL 3.3 core: enough for everything here, and available essentially
  // everywhere. Asking for more would exclude older integrated GPUs for no gain.
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 4);  // multisampling, for clean cubie edges
#if defined(__APPLE__)
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

  window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
  if (window_ == nullptr) {
    if (--g_initCount == 0) glfwTerminate();
    throw Error(
        "could not create an OpenGL 3.3 window. The driver may be too old, or "
        "there may be no display attached.");
  }

  glfwMakeContextCurrent(window_);
  glfwSwapInterval(1);  // vsync: the scene is static most of the time

  glewExperimental = GL_TRUE;
  const GLenum status = glewInit();
  if (status != GLEW_OK) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
    if (--g_initCount == 0) glfwTerminate();
    throw Error("could not load OpenGL function pointers (GLEW error " +
                std::to_string(static_cast<int>(status)) + ")");
  }
  // glewInit leaves a harmless GL_INVALID_ENUM behind on core profiles.
  (void)glGetError();

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glEnable(GL_MULTISAMPLE);
}

GlContext::~GlContext() {
  if (window_ != nullptr) glfwDestroyWindow(window_);
  if (--g_initCount == 0) glfwTerminate();
}

bool GlContext::shouldClose() const {
  return glfwWindowShouldClose(window_) == GLFW_TRUE;
}

void GlContext::requestClose() { glfwSetWindowShouldClose(window_, GLFW_TRUE); }

void GlContext::swapBuffers() { glfwSwapBuffers(window_); }

void GlContext::setVsync(bool enabled) { glfwSwapInterval(enabled ? 1 : 0); }

void GlContext::pollEvents() { glfwPollEvents(); }

void GlContext::framebufferSize(int& width, int& height) const {
  glfwGetFramebufferSize(window_, &width, &height);
}

double GlContext::time() const { return glfwGetTime(); }

std::string GlContext::describe() const {
  const auto text = [](GLenum name) {
    const GLubyte* value = glGetString(name);
    return value != nullptr ? reinterpret_cast<const char*>(value) : "unknown";
  };
  std::ostringstream out;
  out << text(GL_RENDERER) << " | OpenGL " << text(GL_VERSION);
  return out.str();
}

}  // namespace rubik::render
