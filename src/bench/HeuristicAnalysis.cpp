#include "bench/HeuristicAnalysis.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>

namespace rubik::bench {
namespace {

double pearson(const std::vector<double>& x, const std::vector<double>& y) {
  if (x.size() < 2 || x.size() != y.size()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const double n = static_cast<double>(x.size());
  const double meanX = std::accumulate(x.begin(), x.end(), 0.0) / n;
  const double meanY = std::accumulate(y.begin(), y.end(), 0.0) / n;

  double covariance = 0.0;
  double varianceX = 0.0;
  double varianceY = 0.0;
  for (std::size_t i = 0; i < x.size(); ++i) {
    const double dx = x[i] - meanX;
    const double dy = y[i] - meanY;
    covariance += dx * dy;
    varianceX += dx * dx;
    varianceY += dy * dy;
  }
  if (varianceX <= 0.0 || varianceY <= 0.0) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return covariance / std::sqrt(varianceX * varianceY);
}

}  // namespace

std::vector<Distribution> analyseHeuristics(
    const std::vector<Cube>& states, const std::vector<int>& trueDistances,
    const std::vector<NamedHeuristic>& heuristics) {
  std::vector<Distribution> results;
  results.reserve(heuristics.size());

  for (const auto& [name, heuristic] : heuristics) {
    Distribution d;
    d.name = name;
    d.histogram.assign(32, 0);

    std::vector<int> values;
    values.reserve(states.size());
    std::vector<double> pairedHeuristic;
    std::vector<double> pairedTrue;
    std::map<int, std::vector<int>> byTrue;
    double shortfallSum = 0.0;
    int shortfallCount = 0;

    for (std::size_t i = 0; i < states.size(); ++i) {
      const int h = heuristic(states[i]);
      values.push_back(h);
      if (h >= 0 && h < static_cast<int>(d.histogram.size())) d.histogram[static_cast<std::size_t>(h)]++;

      const int truth = i < trueDistances.size() ? trueDistances[i] : -1;
      if (truth >= 0) {
        pairedHeuristic.push_back(h);
        pairedTrue.push_back(truth);
        byTrue[truth].push_back(h);
        shortfallSum += truth - h;
        ++shortfallCount;
      }
    }

    if (!values.empty()) {
      d.mean = std::accumulate(values.begin(), values.end(), 0.0) /
               static_cast<double>(values.size());
      std::vector<int> sorted = values;
      std::sort(sorted.begin(), sorted.end());
      const std::size_t mid = sorted.size() / 2;
      d.median = sorted.size() % 2 == 1
                     ? sorted[mid]
                     : (sorted[mid - 1] + sorted[mid]) / 2.0;
      d.minimum = sorted.front();
      d.maximum = sorted.back();
    }

    d.correlation = pearson(pairedHeuristic, pairedTrue);
    for (const auto& [truth, hs] : byTrue) {
      d.meanByTrueDistance[truth] =
          std::accumulate(hs.begin(), hs.end(), 0.0) / static_cast<double>(hs.size());
    }
    if (shortfallCount > 0) {
      d.meanShortfall = shortfallSum / shortfallCount;
    }

    results.push_back(std::move(d));
  }
  return results;
}

std::string formatDistributions(const std::vector<Distribution>& distributions) {
  std::ostringstream out;

  out << std::left << std::setw(26) << "heuristic" << std::right << std::setw(9)
      << "mean" << std::setw(9) << "median" << std::setw(6) << "min"
      << std::setw(6) << "max" << std::setw(9) << "corr" << std::setw(12)
      << "shortfall" << "\n";
  out << std::string(77, '-') << "\n";
  for (const Distribution& d : distributions) {
    out << std::left << std::setw(26) << d.name << std::right << std::fixed
        << std::setprecision(3) << std::setw(9) << d.mean << std::setw(9)
        << d.median << std::setw(6) << d.minimum << std::setw(6) << d.maximum;
    if (std::isnan(d.correlation)) {
      out << std::setw(9) << "n/a";
    } else {
      out << std::setw(9) << std::setprecision(3) << d.correlation;
    }
    out << std::setw(12) << std::setprecision(2) << d.meanShortfall << "\n";
  }

  // Histograms.
  out << "\nDistribution (percentage of states at each heuristic value)\n";
  int widest = 0;
  for (const Distribution& d : distributions) widest = std::max(widest, d.maximum);

  out << std::left << std::setw(26) << "heuristic" << std::right;
  for (int v = 0; v <= widest; ++v) out << std::setw(7) << v;
  out << "\n" << std::string(static_cast<std::size_t>(26 + 7 * (widest + 1)), '-') << "\n";

  for (const Distribution& d : distributions) {
    const int total = std::accumulate(d.histogram.begin(), d.histogram.end(), 0);
    out << std::left << std::setw(26) << d.name << std::right << std::fixed
        << std::setprecision(1);
    for (int v = 0; v <= widest; ++v) {
      const double share =
          total > 0 ? 100.0 * d.histogram[static_cast<std::size_t>(v)] / total : 0.0;
      if (share == 0.0) {
        out << std::setw(7) << "-";
      } else {
        out << std::setw(7) << share;
      }
    }
    out << "\n";
  }

  // Mean heuristic against true distance, where known.
  bool anyTruth = false;
  for (const Distribution& d : distributions) {
    if (!d.meanByTrueDistance.empty()) anyTruth = true;
  }
  if (anyTruth) {
    std::vector<int> depths;
    for (const Distribution& d : distributions) {
      for (const auto& [truth, mean] : d.meanByTrueDistance) {
        (void)mean;
        if (std::find(depths.begin(), depths.end(), truth) == depths.end()) {
          depths.push_back(truth);
        }
      }
    }
    std::sort(depths.begin(), depths.end());

    out << "\nMean heuristic by true optimal distance\n";
    out << std::left << std::setw(26) << "heuristic" << std::right;
    for (const int depth : depths) out << std::setw(8) << depth;
    out << "\n" << std::string(26 + 8 * depths.size(), '-') << "\n";
    for (const Distribution& d : distributions) {
      out << std::left << std::setw(26) << d.name << std::right << std::fixed
          << std::setprecision(2);
      for (const int depth : depths) {
        const auto it = d.meanByTrueDistance.find(depth);
        if (it == d.meanByTrueDistance.end()) {
          out << std::setw(8) << "-";
        } else {
          out << std::setw(8) << it->second;
        }
      }
      out << "\n";
    }
  }

  return out.str();
}

}  // namespace rubik::bench
