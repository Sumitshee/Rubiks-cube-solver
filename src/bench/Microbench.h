#pragma once

#include "solver/MoveTable.h"
#include "solver/korf/KorfHeuristic.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rubik::bench {

/// One measured primitive.
struct MicroResult {
  std::string name;
  double nanosecondsPerOp = 0.0;
  std::uint64_t operations = 0;
  /// How many times the search performs this per generated node, so the
  /// measured unit cost can be turned into a share of total runtime.
  double perGeneratedNode = 0.0;
  std::string note;
};

/// Measures the cost of each primitive the IDA* hot loop uses.
///
/// ## Why this rather than a sampling profiler
///
/// Two obstacles rule the usual Windows tools out here. ETW-based collection
/// (`wpr.exe`, and the Windows Performance Toolkit) needs an elevated shell,
/// and the Visual Studio collector present in Build Tools writes `.diagsession`
/// files that only the full IDE can open. More fundamentally, the release build
/// uses link-time optimisation and every primitive below is a small inline
/// function, so a sampler would attribute essentially all self time to
/// `Search::dfs` and tell us nothing about the split.
///
/// So the components are measured *differentially* instead: a pseudo-random
/// walk over the cube is timed with and without the operation under test, and
/// the difference is that operation's cost. The walk mirrors how the search
/// actually touches memory -- successive states differ by a single move -- which
/// matters, because a uniformly random access pattern would overstate cache
/// misses. Multiplying each unit cost by how often the search performs it gives
/// a predicted total that can be checked against the measured wall time.
[[nodiscard]] std::vector<MicroResult> runMicrobenchmarks(
    const MoveTables& tables, const korf::KorfHeuristic* heuristic,
    std::uint64_t iterations = 20'000'000);

/// Formats the results as a table, including each primitive's predicted share
/// of the per-node cost.
[[nodiscard]] std::string formatMicroResults(const std::vector<MicroResult>& results);

}  // namespace rubik::bench
