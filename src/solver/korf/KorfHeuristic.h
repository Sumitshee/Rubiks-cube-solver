#pragma once

#include "core/Cube.h"
#include "solver/MoveTable.h"
#include "solver/NibbleArray.h"
#include "solver/PatternDatabase.h"
#include "solver/korf/CornerAbstraction.h"
#include "solver/korf/EdgeAbstraction.h"

#include <algorithm>
#include <string>

namespace rubik::korf {

using CornerDatabase = PatternDatabase<CornerAbstraction, NibbleArray>;
using EdgeDatabase = PatternDatabase<EdgeAbstraction, NibbleArray>;
using EdgeDatabase7 = PatternDatabase<EdgeAbstraction7, NibbleArray>;

/// Korf's heuristic: the maximum of one corner and two edge pattern databases.
///
/// **Why max and not sum.** Each database is admissible on its own (see
/// PatternDatabase for that argument). Summing them would require that no
/// single move be counted twice -- the condition for Korf and Felner's additive
/// databases. It does not hold here: one face turn moves four corners *and*
/// four edges at once, and those four edges may be drawn from both edge groups.
/// A move can therefore be counted by all three databases simultaneously, so
/// the sum could exceed the true distance and would not be admissible. The
/// maximum always is: if no individual value exceeds the true distance, neither
/// does the largest.
///
/// Getting an additive version would mean redefining the databases so each move
/// is charged to exactly one of them, which is a different design and not what
/// Korf's 1997 paper does.
class KorfHeuristic {
 public:
  explicit KorfHeuristic(const MoveTables& tables)
      : corners_(CornerAbstraction(tables)),
        edgesA_(makeEdgeGroupA()),
        edgesB_(makeEdgeGroupB()),
        edges7_(makeEdgeGroup7()) {}

  /// A lower bound on the number of moves needed to solve `cube`.
  [[nodiscard]] std::uint8_t estimate(const Cube& cube) const noexcept {
    return std::max({corners_.lookup(cube), edgesA_.lookup(cube),
                     edgesB_.lookup(cube)});
  }

  /// As `estimateAtLeast`, but for a caller that already knows the corner
  /// database index.
  ///
  /// Recomputing that index from the cube costs a Lehmer rank over eight
  /// corners plus a base-3 orientation pack -- measured at roughly 19 ns, on
  /// every generated node. A search that maintains the two corner coordinates
  /// through its move tables gets the index for two array reads instead, which
  /// is why this overload exists.
  [[nodiscard]] std::uint8_t estimateAtLeast(const Cube& cube,
                                             std::uint32_t cornerIndex,
                                             std::uint8_t budget) const noexcept {
    std::uint8_t best = corners_.lookupIndex(cornerIndex);
    if (best > budget) return best;
    const std::uint8_t a = edgesA_.lookup(cube);
    if (a > budget) return a;
    best = std::max(best, a);
    const std::uint8_t b = edgesB_.lookup(cube);
    return std::max(best, b);
  }

  /// The optional seven-edge database, present only when it has been generated.
  ///
  /// Adding it to the `max` is admissible for exactly the same reason the other
  /// three are: it is a homomorphic abstraction that preserves the goal, so its
  /// exact distance cannot exceed the real one. Overlapping the six-edge groups
  /// is harmless under `max` -- overlap would only be a problem for a *sum*.
  [[nodiscard]] bool hasSevenEdge() const noexcept { return edges7_.ready(); }
  [[nodiscard]] EdgeDatabase7& edges7() noexcept { return edges7_; }
  [[nodiscard]] const EdgeDatabase7& edges7() const noexcept { return edges7_; }

  [[nodiscard]] std::uint8_t estimateWithSeven(const Cube& cube) const noexcept {
    return std::max(estimate(cube), edges7_.lookup(cube));
  }

  [[nodiscard]] std::uint8_t estimateAtLeastWithSeven(
      const Cube& cube, std::uint32_t cornerIndex,
      std::uint8_t budget) const noexcept {
    std::uint8_t best = corners_.lookupIndex(cornerIndex);
    if (best > budget) return best;
    const std::uint8_t seven = edges7_.lookup(cube);
    if (seven > budget) return seven;
    best = std::max(best, seven);
    const std::uint8_t a = edgesA_.lookup(cube);
    if (a > budget) return a;
    best = std::max(best, a);
    const std::uint8_t b = edgesB_.lookup(cube);
    return std::max(best, b);
  }

  /// Stops as soon as any database already proves the bound cannot be met.
  ///
  /// The search only ever asks "is the estimate greater than this budget?", so
  /// once one database answers yes there is no reason to touch the others --
  /// and each untouched lookup is a cache miss avoided. Returns a value that is
  /// still a valid lower bound, though not necessarily the maximum.
  [[nodiscard]] std::uint8_t estimateAtLeast(const Cube& cube,
                                             std::uint8_t budget) const noexcept {
    std::uint8_t best = corners_.lookup(cube);
    if (best > budget) return best;
    const std::uint8_t a = edgesA_.lookup(cube);
    if (a > budget) return a;
    best = std::max(best, a);
    const std::uint8_t b = edgesB_.lookup(cube);
    return std::max(best, b);
  }

  [[nodiscard]] bool ready() const noexcept {
    return corners_.ready() && edgesA_.ready() && edgesB_.ready();
  }

  [[nodiscard]] std::size_t byteSize() const noexcept {
    return corners_.byteSize() + edgesA_.byteSize() + edgesB_.byteSize() +
           edges7_.byteSize();
  }

  [[nodiscard]] CornerDatabase& corners() noexcept { return corners_; }
  [[nodiscard]] EdgeDatabase& edgesA() noexcept { return edgesA_; }
  [[nodiscard]] EdgeDatabase& edgesB() noexcept { return edgesB_; }
  [[nodiscard]] const CornerDatabase& corners() const noexcept { return corners_; }
  [[nodiscard]] const EdgeDatabase& edgesA() const noexcept { return edgesA_; }
  [[nodiscard]] const EdgeDatabase& edgesB() const noexcept { return edgesB_; }

  /// Loads the three core databases from `directory`, generating and saving any
  /// that are missing. Returns true if anything had to be generated.
  bool loadOrGenerate(const std::string& directory,
                      const PdbProgressCallback& onProgress = {}) {
    bool generated = false;
    generated |= corners_.loadOrGenerate(directory + "/corner.db", onProgress);
    generated |= edgesA_.loadOrGenerate(directory + "/edge_a.db", onProgress);
    generated |= edgesB_.loadOrGenerate(directory + "/edge_b.db", onProgress);
    return generated;
  }

  /// Loads the seven-edge database if it is on disk. Returns false when absent,
  /// which is not an error -- it is optional, and the solver falls back to the
  /// three-database heuristic.
  bool loadSeven(const std::string& directory) {
    return edges7_.load(directory + "/edge_7.db");
  }

  /// Loads or builds the seven-edge database. Kept separate from
  /// `loadOrGenerate` because it is a deliberate opt-in: 243.6 MB on disk and
  /// close to four minutes to build.
  bool loadOrGenerateSeven(const std::string& directory,
                           const PdbProgressCallback& onProgress = {}) {
    return edges7_.loadOrGenerate(directory + "/edge_7.db", onProgress);
  }

 private:
  CornerDatabase corners_;
  EdgeDatabase edgesA_;
  EdgeDatabase edgesB_;
  EdgeDatabase7 edges7_;
};

}  // namespace rubik::korf
