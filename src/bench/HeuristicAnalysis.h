#pragma once

#include "core/Cube.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace rubik::bench {

/// A heuristic to profile, paired with a name for the report.
using NamedHeuristic = std::pair<std::string, std::function<int(const Cube&)>>;

/// Summary statistics for one heuristic over a state set.
struct Distribution {
  std::string name;
  double mean = 0.0;
  double median = 0.0;
  int minimum = 0;
  int maximum = 0;
  /// Count of states at each heuristic value.
  std::vector<int> histogram;

  /// Pearson correlation with the true distance, over the states where it is
  /// known. NaN when fewer than two such states exist.
  double correlation = 0.0;
  /// Mean heuristic value grouped by true distance.
  std::map<int, double> meanByTrueDistance;
  /// How far below the true distance the heuristic sits, on average. This is
  /// the quantity that actually governs how much of the tree must be searched
  /// blind.
  double meanShortfall = 0.0;
};

/// Measures each heuristic across `states`.
///
/// `trueDistances` runs parallel to `states`; entries below zero mark a state
/// whose optimal distance is not known, which is normal for deep states that
/// cannot be solved optimally in reasonable time. Those states still contribute
/// to the distribution, just not to the correlation.
[[nodiscard]] std::vector<Distribution> analyseHeuristics(
    const std::vector<Cube>& states, const std::vector<int>& trueDistances,
    const std::vector<NamedHeuristic>& heuristics);

/// Formats the distributions as a report.
[[nodiscard]] std::string formatDistributions(
    const std::vector<Distribution>& distributions);

}  // namespace rubik::bench
