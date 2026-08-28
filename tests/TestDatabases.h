#pragma once

#include "solver/MoveTable.h"
#include "solver/korf/KorfHeuristic.h"

#include <memory>
#include <string>

/// Shared access to the generated pattern databases for the test binary.
///
/// The databases are large -- 82.65 MB for the three core ones, 326 MB with the
/// seven-edge database -- and several test files need them. Loading them per
/// file cost the suite roughly 900 MB of peak working set on a machine with
/// 7.75 GB of RAM, because four separate function-local singletons each held
/// their own copy. These accessors give the whole binary two instances at most.
///
/// Two, not one, on purpose. Most of the optimal-solver tests are written
/// against the *three-database* configuration and check things like
/// `bestAvailableMode` returning `MaxOfThree`; collapsing everything onto a
/// single instance that happens to have the seven-edge database loaded would
/// quietly change what those tests exercise.
namespace rubik::testdb {

/// The coordinate transition tables. Static storage duration, so anything that
/// holds a reference to them (`KorfHeuristic` does) stays valid for the run.
inline const MoveTables& moveTables() {
  static const MoveTables instance;
  return instance;
}

/// Corner + edge A + edge B. Null when the files have not been generated.
inline std::shared_ptr<const korf::KorfHeuristic> threeDatabaseHeuristic() {
  static const std::shared_ptr<const korf::KorfHeuristic> instance = [] {
    auto h = std::make_shared<korf::KorfHeuristic>(moveTables());
    const std::string dir = RUBIK_DATA_DIR;
    if (!h->corners().load(dir + "/corner.db") ||
        !h->edgesA().load(dir + "/edge_a.db") ||
        !h->edgesB().load(dir + "/edge_b.db")) {
      return std::shared_ptr<const korf::KorfHeuristic>{};
    }
    return std::shared_ptr<const korf::KorfHeuristic>(std::move(h));
  }();
  return instance;
}

/// The same three plus the optional seven-edge database. Null when any of the
/// four is missing -- including when only the seven-edge one is absent, which
/// is the normal case on a machine that has not opted into the extra 243.6 MB.
inline std::shared_ptr<const korf::KorfHeuristic> sevenEdgeHeuristic() {
  static const std::shared_ptr<const korf::KorfHeuristic> instance = [] {
    auto h = std::make_shared<korf::KorfHeuristic>(moveTables());
    const std::string dir = RUBIK_DATA_DIR;
    if (!h->corners().load(dir + "/corner.db") ||
        !h->edgesA().load(dir + "/edge_a.db") ||
        !h->edgesB().load(dir + "/edge_b.db") || !h->loadSeven(dir)) {
      return std::shared_ptr<const korf::KorfHeuristic>{};
    }
    return std::shared_ptr<const korf::KorfHeuristic>(std::move(h));
  }();
  return instance;
}

/// The strongest configuration available, for callers that only need *a*
/// working heuristic rather than a particular one.
inline std::shared_ptr<const korf::KorfHeuristic> bestHeuristic() {
  if (auto seven = sevenEdgeHeuristic()) return seven;
  return threeDatabaseHeuristic();
}

}  // namespace rubik::testdb
