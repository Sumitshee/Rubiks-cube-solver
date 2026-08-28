#include "bench/Microbench.h"

#include "core/Cube.h"
#include "solver/Coordinate.h"
#include "solver/korf/CornerAbstraction.h"
#include "solver/Combinatorics.h"
#include "solver/korf/EdgeAbstraction.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace rubik::bench {
namespace {

/// Accumulator the measured work feeds into, so nothing can be optimised away.
volatile std::uint64_t g_sink = 0;

/// A fixed pseudo-random move sequence. Reused by every measurement so they all
/// walk the same states, and cheap enough that generating it does not pollute
/// the timings.
const std::array<Move, 4096>& walkMoves() {
  static const auto moves = [] {
    std::array<Move, 4096> m{};
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    Move prev = Move::U;
    bool hasPrev = false;
    for (std::size_t i = 0; i < m.size();) {
      state = state * 6364136223846793005ull + 1442695040888963407ull;
      const Move candidate = static_cast<Move>((state >> 33) % kNumMoves);
      if (hasPrev && isRedundant(candidate, prev)) continue;
      m[i++] = candidate;
      prev = candidate;
      hasPrev = true;
    }
    return m;
  }();
  return moves;
}

/// Times `iterations` steps of the walk, performing `body` at each step.
///
/// The walk itself (apply + undo) is included, so subtracting the empty walk
/// isolates the body.
template <typename Body>
double timeWalk(std::uint64_t iterations, Body body) {
  const auto& moves = walkMoves();
  Cube cube;
  std::uint64_t local = 0;

  const auto start = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < iterations; ++i) {
    const Move m = moves[i & (moves.size() - 1)];
    cube.apply(m);
    local += body(cube, m);
    cube.undo(m);
  }
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  g_sink += local;
  return seconds;
}

/// Times a walk that only applies and undoes, with no body at all.
double timeBareWalk(std::uint64_t iterations) {
  const auto& moves = walkMoves();
  Cube cube;
  std::uint64_t local = 0;

  const auto start = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < iterations; ++i) {
    const Move m = moves[i & (moves.size() - 1)];
    cube.apply(m);
    // Touch the cube so the pair cannot be elided as dead.
    local += cube.cornerPerm()[0];
    cube.undo(m);
  }
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  g_sink += local;
  return seconds;
}

/// Times just the loop scaffolding, so even the walk can be isolated.
double timeEmptyLoop(std::uint64_t iterations) {
  const auto& moves = walkMoves();
  std::uint64_t local = 0;
  const auto start = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < iterations; ++i) {
    local += static_cast<std::uint64_t>(moves[i & (moves.size() - 1)]);
  }
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  g_sink += local;
  return seconds;
}

double nsPerOp(double seconds, std::uint64_t iterations) {
  return seconds * 1e9 / static_cast<double>(iterations);
}

}  // namespace

std::vector<MicroResult> runMicrobenchmarks(const MoveTables& tables,
                                            const korf::KorfHeuristic* heuristic,
                                            std::uint64_t iterations) {
  std::vector<MicroResult> results;

  const korf::CornerAbstraction cornerAbstraction(tables);
  const korf::EdgeAbstraction edgeA = korf::makeEdgeGroupA();

  // Warm up: fault in the databases and settle clock speeds.
  (void)timeBareWalk(iterations / 10);

  const double emptySeconds = timeEmptyLoop(iterations);
  const double bareSeconds = timeBareWalk(iterations);

  results.push_back({"loop scaffolding only", nsPerOp(emptySeconds, iterations),
                     iterations, 0.0, "measurement floor, subtracted below"});

  results.push_back({"Cube::apply + Cube::undo",
                     nsPerOp(bareSeconds - emptySeconds, iterations), iterations,
                     1.0, "one make/unmake pair per generated node"});

  const auto measure = [&](const std::string& name, double perNode,
                           const std::string& note, auto body) {
    const double seconds = timeWalk(iterations, body);
    results.push_back({name, nsPerOp(seconds - bareSeconds, iterations),
                       iterations, perNode, note});
  };

  measure("isRedundant", 1.0, "pruning check per candidate move",
          [](const Cube&, Move m) -> std::uint64_t {
            return isRedundant(m, Move::R) ? 1u : 0u;
          });

  measure("coord::cornerPermutation", 0.0, "part of the corner index",
          [](const Cube& c, Move) -> std::uint64_t {
            return coord::cornerPermutation(c);
          });

  measure("coord::cornerOrientation", 0.0, "part of the corner index",
          [](const Cube& c, Move) -> std::uint64_t {
            return coord::cornerOrientation(c);
          });

  measure("CornerAbstraction::index", 1.0, "recomputed from scratch per lookup",
          [&](const Cube& c, Move) -> std::uint64_t {
            return cornerAbstraction.index(c);
          });

  measure("EdgeAbstraction::index", 1.4,
          "inverse permutation plus partial rank",
          [&](const Cube& c, Move) -> std::uint64_t { return edgeA.index(c); });

  // Splits the edge index into its two halves. Making the index incremental
  // would remove only the inversion; the rank would still have to be paid, so
  // this measures the ceiling on that optimisation before attempting it.
  measure("  ... rank half only", 0.0,
          "encodePartialPermutation(12,6) on known positions",
          [](const Cube& c, Move) -> std::uint64_t {
            std::array<std::uint8_t, 6> positions{};
            for (std::size_t j = 0; j < 6; ++j) positions[j] = c.edgePerm()[j];
            return encodePartialPermutation(positions.data(), kNumEdges, 6);
          });

  if (heuristic != nullptr && heuristic->ready()) {
    measure("corner PDB lookup (index + read)", 1.0, "always consulted",
            [&](const Cube& c, Move) -> std::uint64_t {
              return heuristic->corners().lookup(c);
            });

    measure("edge A PDB lookup (index + read)", 0.7, "consulted when under budget",
            [&](const Cube& c, Move) -> std::uint64_t {
              return heuristic->edgesA().lookup(c);
            });

    measure("KorfHeuristic::estimate (full max)", 0.0,
            "used only for the root and when ordering",
            [&](const Cube& c, Move) -> std::uint64_t {
              return heuristic->estimate(c);
            });

    // A budget of 0 is the common case deep in the search: the first database
    // usually already exceeds it, so the other two are skipped.
    measure("KorfHeuristic::estimateAtLeast(0)", 1.0,
            "the search's actual heuristic call",
            [&](const Cube& c, Move) -> std::uint64_t {
              return heuristic->estimateAtLeast(c, 0);
            });

    measure("KorfHeuristic::estimateAtLeast(12)", 0.0,
            "loose budget: all three databases are read",
            [&](const Cube& c, Move) -> std::uint64_t {
              return heuristic->estimateAtLeast(c, 12);
            });
  }

  return results;
}

std::string formatMicroResults(const std::vector<MicroResult>& results) {
  std::ostringstream out;
  out << std::left << std::setw(38) << "primitive" << std::right << std::setw(11)
      << "ns/op" << std::setw(9) << "per node" << std::setw(12) << "ns/node"
      << "   note\n";
  out << std::string(112, '-') << "\n";

  double predicted = 0.0;
  for (const MicroResult& r : results) {
    const double perNode = r.nanosecondsPerOp * r.perGeneratedNode;
    predicted += perNode;
    out << std::left << std::setw(38) << r.name << std::right << std::fixed
        << std::setprecision(2) << std::setw(11) << r.nanosecondsPerOp
        << std::setw(9) << std::setprecision(1) << r.perGeneratedNode
        << std::setw(12) << std::setprecision(2)
        << (r.perGeneratedNode > 0.0 ? perNode : 0.0) << "   " << r.note << "\n";
  }
  out << std::string(112, '-') << "\n";
  out << "Predicted cost per generated node: " << std::fixed
      << std::setprecision(2) << predicted << " ns\n";
  return out.str();
}

}  // namespace rubik::bench
