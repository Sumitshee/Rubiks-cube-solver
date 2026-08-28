#pragma once

/// Single place that gets the OpenGL include order right.
///
/// GLEW must come before anything that pulls in `gl.h`, GLFW included, or it
/// errors out. Every other file in this directory includes this header instead
/// of the two individually, so the ordering cannot be got wrong by accident.

// clang-format off
#include <GL/glew.h>
#include <GLFW/glfw3.h>
// clang-format on
