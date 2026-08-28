# ---------------------------------------------------------------------------
# rubik_core -- cube representation and move algebra.
#
# This target has no dependency on the solver, the renderer, or any third-party
# library. Keeping it free-standing is what allows the solver to be tested and
# benchmarked without a graphics context.
# ---------------------------------------------------------------------------

add_library(rubik_core STATIC
  core/Cube.cpp
  core/Facelets.cpp
  core/CubeValidation.cpp
  core/Move.cpp
  solver/Coordinate.cpp
  solver/MoveTable.cpp
  solver/TwoPhaseSolver.cpp
  solver/TwoPhaseTables.cpp
  solver/korf/OptimalSolver.cpp
  bench/Benchmark.cpp
  bench/HeuristicAnalysis.cpp
  bench/Microbench.cpp
  bench/ProcessMemory.cpp
)

target_include_directories(rubik_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(rubik_core
  PRIVATE rubik_warnings
  PUBLIC  rubik_options
)

if(RUBIK_IPO_SUPPORTED)
  set_property(TARGET rubik_core PROPERTY
               INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

# ---------------------------------------------------------------------------
# rubik_ui -- the application layer.
#
# Sits between the viewer and the solver: it owns the logical cube, the move
# queue, animation timing, solution playback and background solving. It has no
# OpenGL dependency at all, which is exactly why the interesting integration
# behaviour -- that a move animation commits the right state, that playback ends
# solved, that a background solve does not race -- can be unit tested without a
# window.
# ---------------------------------------------------------------------------

find_package(Threads REQUIRED)

add_library(rubik_ui STATIC
  ui/CubeLayout.cpp
  ui/CubeEditor.cpp
  ui/CubeController.cpp
  ui/SolverService.cpp
)

target_include_directories(rubik_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(rubik_ui
  PUBLIC  rubik_core Threads::Threads
  PRIVATE rubik_warnings
)

if(RUBIK_IPO_SUPPORTED)
  set_property(TARGET rubik_ui PROPERTY
               INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

# ---------------------------------------------------------------------------
# rubiks_solver -- command-line front end.
# ---------------------------------------------------------------------------

add_executable(rubiks_solver
  app/cli_main.cpp
)

target_link_libraries(rubiks_solver
  PRIVATE rubik_core rubik_warnings
)

if(RUBIK_IPO_SUPPORTED)
  set_property(TARGET rubiks_solver PROPERTY
               INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

# ---------------------------------------------------------------------------
# rubiks_bench -- the reproducible benchmark harness.
#
# A separate executable rather than another flag on rubiks_solver: it carries a
# large option surface of its own, and keeping it apart means the solver binary
# does not grow a benchmarking dependency.
# ---------------------------------------------------------------------------

add_executable(rubiks_bench
  app/bench_main.cpp
)

target_link_libraries(rubiks_bench
  PRIVATE rubik_core rubik_warnings
)

if(RUBIK_IPO_SUPPORTED)
  set_property(TARGET rubiks_bench PROPERTY
               INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

# ---------------------------------------------------------------------------
# rubiks_gui -- the 3D viewer. Optional, and the only target that needs OpenGL.
# ---------------------------------------------------------------------------

if(RUBIK_BUILD_GUI)
  add_subdirectory(render)

  add_executable(rubiks_gui WIN32
    app/gui_main.cpp
  )

  target_link_libraries(rubiks_gui
    PRIVATE rubik_ui rubik_render rubik_warnings
  )

  # WIN32 above suppresses the console window for a normal launch; keep a
  # console entry point so the viewer can still be started from a terminal and
  # print diagnostics.
  if(MSVC)
    set_target_properties(rubiks_gui PROPERTIES
                          LINK_FLAGS "/ENTRY:mainCRTStartup")
  endif()

  if(RUBIK_IPO_SUPPORTED)
    set_property(TARGET rubiks_gui PROPERTY
                 INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  endif()
endif()

# Windows process-memory reporting lives in bench/ProcessMemory.cpp.
if(WIN32)
  # psapi: process memory counters. advapi32: the registry read for the CPU name.
  target_link_libraries(rubik_core PUBLIC psapi advapi32)
endif()
