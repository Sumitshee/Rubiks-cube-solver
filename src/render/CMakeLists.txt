# ---------------------------------------------------------------------------
# rubik_render -- the OpenGL layer, and the only part of the project that knows
# graphics exists.
#
# Nothing below this line is reachable from rubik_core: the solver, the CLI and
# the benchmark harness all build and run without an OpenGL context, on a
# machine with no display at all. This target is what `RUBIK_BUILD_GUI=OFF`
# removes.
# ---------------------------------------------------------------------------

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# GLFW: window, context and input.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
  GIT_SHALLOW    TRUE
)

# GLM: vector, matrix and quaternion maths. Header-only.
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1
  GIT_SHALLOW    TRUE
)

# The function loader. glew-cmake is used rather than upstream GLEW or GLAD
# because it builds from plain CMake with no Python or code-generation step,
# which keeps a fresh Windows checkout to "configure and build".
set(glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ONLY_LIBS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  glew
  GIT_REPOSITORY https://github.com/Perlmint/glew-cmake.git
  GIT_TAG        glew-cmake-2.2.0
  GIT_SHALLOW    TRUE
)

# glew-cmake's own CMakeLists still declares a minimum of CMake 2.8, which
# CMake 4 refuses outright. This raises the floor for the dependency only --
# it is not a project-wide policy change, and it is restored immediately
# afterwards so nothing else inherits it.
set(RUBIK_SAVED_POLICY_MINIMUM "${CMAKE_POLICY_VERSION_MINIMUM}")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_MakeAvailable(glfw glm glew)

set(CMAKE_POLICY_VERSION_MINIMUM "${RUBIK_SAVED_POLICY_MINIMUM}")

find_package(OpenGL REQUIRED)

add_library(rubik_render STATIC
  GlContext.cpp
  Shader.cpp
  Mesh.cpp
  Camera.cpp
  CubeGeometry.cpp
  CubeRenderer.cpp
  TextOverlay.cpp
)

target_include_directories(rubik_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_link_libraries(rubik_render
  # rubik_ui, not rubik_core: the renderer needs CubeLayout's mapping from
  # grid slots to facelets, which is deliberately kept in the application
  # layer where it can be tested against the move tables without a context.
  PUBLIC  rubik_ui glfw libglew_static glm::glm OpenGL::GL
  PRIVATE rubik_warnings
)

# GLEW must be told it is being linked statically, or it declares its symbols
# dllimport and the link fails.
target_compile_definitions(rubik_render PUBLIC GLEW_STATIC GLEW_NO_GLU)

if(RUBIK_IPO_SUPPORTED)
  set_property(TARGET rubik_render PROPERTY
               INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
