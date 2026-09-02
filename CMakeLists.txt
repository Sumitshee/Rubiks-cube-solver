cmake_minimum_required(VERSION 3.20)

project(RubiksCubeSolver
        VERSION 0.1.0
        DESCRIPTION "Rubik's Cube solver with IDA* search and 3D visualisation"
        LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Standard and global settings
# ---------------------------------------------------------------------------

# C++17 rather than C++20: nothing in this project needs a C++20 feature, and
# staying at 17 keeps the code buildable by older toolchains alongside MSVC.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Executables land in build/bin for single-config generators and in
# build/bin/<Config> for multi-config ones (Visual Studio, Ninja Multi-Config).
# Keeping the configurations apart matters: sharing one directory means a Debug
# build silently overwrites the Release binary you were about to benchmark.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Default to Release for single-config generators. Multi-config generators
# (Visual Studio, Ninja Multi-Config) pick the config at build time instead.
get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _is_multi_config AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
               Debug Release RelWithDebInfo MinSizeRel)
endif()

option(RUBIK_BUILD_TESTS "Build the automated test suite" ON)

# The 3D viewer. Turning it off skips the GLFW/GLM/GLEW downloads entirely,
# which is what a headless or CI machine wants: the solver, the CLI and the
# benchmark harness never reference OpenGL.
option(RUBIK_BUILD_GUI "Build the OpenGL viewer (downloads GLFW, GLM, GLEW)" ON)
option(RUBIK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Off by default and deliberately so. Enabling it targets the building machine's
# instruction set, which produces a binary that may crash elsewhere with an
# illegal instruction -- unacceptable for something whose whole point is
# comparable benchmark numbers. Provided as an option purely so the effect can
# be measured; see the Optimisation section of the README for the result.
option(RUBIK_NATIVE_ARCH "Target this machine's instruction set (not portable)" OFF)

# AddressSanitizer. MSVC ships this one and only this one -- there is no
# ThreadSanitizer, MemorySanitizer or UndefinedBehaviorSanitizer for MSVC -- so
# this is the whole of the sanitizer story on the project's primary toolchain.
# It is incompatible with link-time optimisation and with the /RTC checks the
# Debug configuration enables, both of which are turned off below when it is on.
# Build it as RelWithDebInfo:
#   cmake -S . -B build-asan -DRUBIK_SANITIZE_ADDRESS=ON -DRUBIK_BUILD_GUI=OFF
#   cmake --build build-asan --config RelWithDebInfo
option(RUBIK_SANITIZE_ADDRESS "Build with AddressSanitizer (MSVC /fsanitize=address)" OFF)

# ---------------------------------------------------------------------------
# Warning configuration, applied through an interface target
# ---------------------------------------------------------------------------

add_library(rubik_warnings INTERFACE)

if(MSVC)
  target_compile_options(rubik_warnings INTERFACE
    /W4                # high warning level
    /permissive-       # strict standard conformance
    /Zc:__cplusplus    # report the real __cplusplus value
    /utf-8             # source and execution charset
    /MP                # parallel compilation
  )
  if(RUBIK_WARNINGS_AS_ERRORS)
    target_compile_options(rubik_warnings INTERFACE /WX)
  endif()
  # Suppress the MSVC "unsafe" CRT warnings; the code uses standard C++.
  target_compile_definitions(rubik_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)

  if(RUBIK_SANITIZE_ADDRESS)
    # Applied to every target rather than through rubik_warnings: GoogleTest
    # has to be built the same way, or its container annotations disagree with
    # ours and the test binary refuses to link.
    add_compile_options(/fsanitize=address)
    # ASan cannot coexist with the runtime checks Debug turns on, nor with
    # incremental linking.
    foreach(flags CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELWITHDEBINFO)
      string(REGEX REPLACE "/RTC[1csu]*" "" ${flags} "${${flags}}")
      set(${flags} "${${flags}}" CACHE STRING "" FORCE)
    endforeach()
    add_link_options(/INCREMENTAL:NO)
  endif()
else()
  target_compile_options(rubik_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Woverloaded-virtual
  )
  if(RUBIK_WARNINGS_AS_ERRORS)
    target_compile_options(rubik_warnings INTERFACE -Werror)
  endif()
endif()

# ---------------------------------------------------------------------------
# Optimisation for the search hot path
# ---------------------------------------------------------------------------
#
# Deliberately NOT using -march=native / -Ofast. -Ofast enables -ffast-math,
# which is unnecessary here since the solver is entirely integer work, and
# -march=native produces binaries that will not run on other machines --
# unacceptable for something whose benchmark numbers are meant to be comparable
# across machines. Interprocedural optimisation is enabled instead, because the
# hot loop crosses translation units (search -> cube -> heuristic).

add_library(rubik_options INTERFACE)
if(MSVC)
  target_compile_options(rubik_options INTERFACE
    $<$<CONFIG:Release>:/O2 /Oi /Ot>
    $<$<CONFIG:RelWithDebInfo>:/O2 /Oi>
  )
  if(RUBIK_NATIVE_ARCH)
    target_compile_options(rubik_options INTERFACE /arch:AVX2)
  endif()
else()
  target_compile_options(rubik_options INTERFACE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O2>
  )
  if(RUBIK_NATIVE_ARCH)
    target_compile_options(rubik_options INTERFACE -march=native)
  endif()
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT RUBIK_IPO_SUPPORTED OUTPUT RUBIK_IPO_ERROR)
if(RUBIK_SANITIZE_ADDRESS)
  # /GL and /fsanitize=address cannot be combined.
  set(RUBIK_IPO_SUPPORTED FALSE)
endif()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------

add_subdirectory(src)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(RUBIK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Configuration summary
# ---------------------------------------------------------------------------

message(STATUS "")
message(STATUS "RubiksCubeSolver ${PROJECT_VERSION}")
message(STATUS "  Compiler        : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  C++ standard    : ${CMAKE_CXX_STANDARD}")
if(_is_multi_config)
  message(STATUS "  Configurations  : ${CMAKE_CONFIGURATION_TYPES} (choose with --config)")
else()
  message(STATUS "  Build type      : ${CMAKE_BUILD_TYPE}")
endif()
message(STATUS "  Pointer size    : ${CMAKE_SIZEOF_VOID_P} bytes")
message(STATUS "  LTO available   : ${RUBIK_IPO_SUPPORTED}")
message(STATUS "  AddressSanitizer: ${RUBIK_SANITIZE_ADDRESS}")
message(STATUS "  Tests           : ${RUBIK_BUILD_TESTS}")
message(STATUS "  GUI viewer      : ${RUBIK_BUILD_GUI}")
message(STATUS "")

# A 32-bit build cannot host the pattern databases (they need well over 2 GB of
# address space once loaded). Fail loudly rather than at run time.
if(CMAKE_SIZEOF_VOID_P LESS 8)
  message(FATAL_ERROR
    "A 64-bit toolchain is required. The Korf pattern databases exceed the "
    "2 GB address space a 32-bit process gets on Windows. Configure with a "
    "64-bit generator, e.g. -A x64 for Visual Studio.")
endif()
