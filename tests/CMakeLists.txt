include(FetchContent)

# GoogleTest is fetched rather than expected on the system so that a fresh
# Windows machine needs nothing beyond CMake and a compiler.
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
  GIT_SHALLOW    TRUE
)

# Link against the same CRT as the rest of the build (MSVC default is static).
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(rubik_tests
  test_move.cpp
  test_cube.cpp
  test_facelets.cpp
  test_nibble_array.cpp
  test_combinatorics.cpp
  test_coordinate.cpp
  test_move_table.cpp
  test_two_phase_solver.cpp
  test_pattern_database.cpp
  test_optimal_solver.cpp
  test_cube_layout.cpp
  test_cube_editor.cpp
  test_cube_controller.cpp
  test_cube_playback.cpp
  test_solver_service.cpp
)

target_include_directories(rubik_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(rubik_tests
  PRIVATE rubik_core rubik_ui rubik_warnings GTest::gtest_main
)

# The pattern-database tests read the generated databases from the source tree.
# They skip with an explanation when the files are absent rather than spending a
# minute regenerating them inside a unit test.
target_compile_definitions(rubik_tests
  PRIVATE RUBIK_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)

include(GoogleTest)
gtest_discover_tests(rubik_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES TIMEOUT 300
)
