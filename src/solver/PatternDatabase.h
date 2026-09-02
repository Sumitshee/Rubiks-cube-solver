#pragma once

#include "core/Cube.h"
#include "core/Error.h"
#include "core/Move.h"
#include "solver/NibbleArray.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <type_traits>
#include <string>

namespace rubik {

/// Progress report emitted once per BFS depth during generation.
struct PdbProgress {
  std::string name;
  int depth = 0;
  std::uint64_t statesAtDepth = 0;
  std::uint64_t statesFilled = 0;
  std::uint64_t statesTotal = 0;
  double elapsedSeconds = 0.0;
};

using PdbProgressCallback = std::function<void(const PdbProgress&)>;

/// A Korf-style pattern database: the exact solution length for an *abstraction*
/// of the cube, stored as a flat array indexed by that abstraction.
///
/// ## Why the values are admissible
///
/// This deserves stating precisely, because "it came from a pattern database"
/// is not an argument.
///
/// An abstraction here is a map phi from cube states to abstract states that
/// forgets information -- the corner database keeps only the eight corner
/// cubies and discards every edge; an edge database keeps six chosen edges and
/// discards the corners and the other six. Two properties make it a genuine
/// *relaxation* rather than merely a smaller problem:
///
///   1. **It is a homomorphism.** Every face turn m acts on abstract states in
///      a well-defined way: phi(apply(s, m)) depends only on phi(s) and m, never
///      on the information phi threw away. That is exactly what makes the
///      transition table well defined, and it is asserted by the tests.
///   2. **It preserves the goal.** phi(solved cube) is the abstract goal.
///
/// Together these give the admissibility argument. Take any real solution for a
/// state s, of length L. Applying that same move sequence in the abstract space
/// carries phi(s) to phi(solved) in L moves, by (1) and (2). So *some* abstract
/// solution of length L exists, and therefore the *shortest* abstract solution
/// is at most L. This database stores exactly that shortest abstract distance,
/// found by breadth-first search. Hence
///
///     PDB value  <=  true distance
///
/// for every state -- never an overestimate, which is what admissibility means.
/// Intuitively: discarding constraints can only make a problem easier.
///
/// ## Why several databases are combined with max, not sum
///
/// Summing two admissible heuristics is valid only when no single move is
/// counted by both -- that is, when the databases partition the moves between
/// them (Korf and Felner's *additive* pattern databases). On a Rubik's Cube a
/// single face turn moves four corners **and** four edges simultaneously, so a
/// move counted in the corner database is very often the same move counted in
/// an edge database. Adding them would double-count and could overestimate,
/// destroying admissibility. The same applies between the two edge databases:
/// one face turn moves four edges, which may be drawn from both groups.
///
/// So the combination is `max`, which is always safe: if no individual value
/// exceeds the true distance, neither does the largest of them.
template <typename Abstraction, typename Storage = NibbleArray>
class PatternDatabase {
 public:
  /// Marks an entry not yet reached by the BFS. Also the maximum storable
  /// value, which is why generation checks that no real distance reaches it.
  static constexpr std::uint8_t kUnvisited = 0x0F;

  explicit PatternDatabase(Abstraction abstraction)
      : abstraction_(std::move(abstraction)) {}

  // --- Lookup --------------------------------------------------------------

  /// The abstract distance for `cube`. Requires a generated or loaded database.
  [[nodiscard]] std::uint8_t lookup(const Cube& cube) const noexcept {
    return storage_.get(abstraction_.index(cube));
  }

  [[nodiscard]] std::uint8_t lookupIndex(std::uint32_t index) const noexcept {
    return storage_.get(index);
  }

  // --- State ---------------------------------------------------------------

  [[nodiscard]] bool ready() const noexcept { return ready_; }
  [[nodiscard]] std::uint32_t size() const noexcept { return abstraction_.size(); }
  [[nodiscard]] std::uint8_t maxDistance() const noexcept { return maxDistance_; }
  [[nodiscard]] std::size_t byteSize() const noexcept { return storage_.byteSize(); }
  [[nodiscard]] std::string name() const { return abstraction_.name(); }
  [[nodiscard]] const Abstraction& abstraction() const noexcept { return abstraction_; }

  /// FNV-1a over the raw storage. Used to verify a loaded file round-trips and
  /// to confirm generation is reproducible.
  [[nodiscard]] std::uint64_t checksum() const noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    const std::uint8_t* p = storage_.data();
    for (std::size_t i = 0; i < storage_.byteSize(); ++i) {
      hash ^= p[i];
      hash *= 1099511628211ull;
    }
    return hash;
  }

  // --- Generation ----------------------------------------------------------

  /// Fills the database by breadth-first search outward from the goal.
  ///
  /// Uses a **frontier-less** sweep: instead of keeping a queue of states to
  /// expand, each pass scans the distance array itself for entries equal to the
  /// current depth and expands those. Peak memory is therefore just the array --
  /// 42 MB for the corner database. A queue-based BFS would hold the frontier on
  /// top of that, and any scheme retaining parent pointers would keep the entire
  /// explored tree alive, which runs to gigabytes at this scale.
  ///
  /// The cost is re-scanning the array once per depth. With a maximum depth
  /// around 11 that is a dozen sequential passes, which is far cheaper than the
  /// random-access expansion work they bracket.
  ///
  /// Generation is deterministic: BFS visits states in a fixed order and no
  /// randomness is involved, so the same abstraction always yields the same
  /// bytes. `checksum()` makes that checkable.
  void generate(const PdbProgressCallback& onProgress = {}) {
    const std::uint32_t total = abstraction_.size();
    storage_.resize(total, kUnvisited);

    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
      return std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
    };

    storage_.set(abstraction_.goalIndex(), 0);
    std::uint64_t filled = 1;

    std::array<std::uint32_t, kNumMoves> successors{};

    for (int depth = 0;; ++depth) {
      const auto current = static_cast<std::uint8_t>(depth);
      const auto next = static_cast<std::uint8_t>(depth + 1);

      // Writing a distance equal to kUnvisited would make a filled entry
      // indistinguishable from an empty one, so refuse before writing rather
      // than after. Only an error if states actually remain at this depth --
      // a database whose true maximum is exactly kUnvisited-1 is fine.
      if (next >= kUnvisited) {
        bool statesRemain = false;
        for (std::uint32_t i = 0; i < total; ++i) {
          if (storage_.get(i) == current) { statesRemain = true; break; }
        }
        if (!statesRemain) break;
        throw DatabaseError(
            "pattern database '" + name() + "' needs a distance of " +
            std::to_string(depth + 1) +
            ", which does not fit in the chosen storage; use a wider element "
            "type");
      }

      std::uint64_t discovered = 0;

      for (std::uint32_t i = 0; i < total; ++i) {
        if (storage_.get(i) != current) continue;
        abstraction_.successors(i, successors.data());
        for (const std::uint32_t s : successors) {
          if (storage_.get(s) == kUnvisited) {
            storage_.set(s, next);
            ++discovered;
          }
        }
      }

      if (discovered == 0) break;
      filled += discovered;
      maxDistance_ = next;

      if (onProgress) {
        onProgress(PdbProgress{name(), depth + 1, discovered, filled,
                               static_cast<std::uint64_t>(total), elapsed()});
      }
    }

    // Every abstract state must be reachable: the abstraction is a quotient of
    // a group acting transitively on it. A gap means the index space and the
    // transition function disagree, which would silently produce a wrong
    // heuristic.
    if (filled != total) {
      throw DatabaseError("pattern database '" + name() + "' filled only " +
                          std::to_string(filled) + " of " +
                          std::to_string(total) +
                          " states; the abstraction is inconsistent");
    }
    ready_ = true;
  }

  // --- Persistence ---------------------------------------------------------

  /// Writes the database, then renames into place so an interrupted write
  /// cannot leave a half-finished file that a later run would trust.
  void save(const std::string& path) const {
    if (!ready_) throw DatabaseError("refusing to save an ungenerated database");

    const std::string temp = path + ".tmp";
    {
      std::ofstream out(temp, std::ios::binary | std::ios::trunc);
      if (!out) throw DatabaseError("cannot open '" + temp + "' for writing");

      writeHeader(out);
      out.write(reinterpret_cast<const char*>(storage_.data()),
                static_cast<std::streamsize>(storage_.byteSize()));
      if (!out) throw DatabaseError("failed while writing '" + temp + "'");
    }

    std::remove(path.c_str());
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
      throw DatabaseError("cannot move '" + temp + "' into place at '" + path + "'");
    }
  }

  /// Loads a database. Returns false if the file simply is not there (a normal
  /// first run); throws `DatabaseError` if it exists but is unusable, since
  /// silently regenerating over a corrupt file would hide a real problem.
  bool load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    Header header{};
    in.read(reinterpret_cast<char*>(&header.magic), sizeof(header.magic));
    readScalar(in, header.version);
    readScalar(in, header.stateCount);
    readScalar(in, header.byteCount);
    readScalar(in, header.checksum);
    readScalar(in, header.maxDistance);
    readScalar(in, header.bitsPerEntry);
    if (!in) throw DatabaseError("'" + path + "' is truncated (header)");

    if (std::string(header.magic, sizeof(header.magic)) != kMagic) {
      throw DatabaseError("'" + path + "' is not a pattern database file");
    }
    if (header.version != kVersion) {
      throw DatabaseError("'" + path + "' has format version " +
                          std::to_string(header.version) + ", expected " +
                          std::to_string(kVersion));
    }
    if (header.stateCount != abstraction_.size()) {
      throw DatabaseError("'" + path + "' holds " +
                          std::to_string(header.stateCount) + " states but '" +
                          name() + "' expects " +
                          std::to_string(abstraction_.size()));
    }
    if (header.bitsPerEntry != bitsPerEntry()) {
      throw DatabaseError("'" + path + "' uses " +
                          std::to_string(header.bitsPerEntry) +
                          " bits per entry, this build expects " +
                          std::to_string(bitsPerEntry()));
    }

    storage_.resize(abstraction_.size(), kUnvisited);
    if (header.byteCount != storage_.byteSize()) {
      throw DatabaseError("'" + path + "' declares " +
                          std::to_string(header.byteCount) +
                          " bytes of data, expected " +
                          std::to_string(storage_.byteSize()));
    }

    in.read(reinterpret_cast<char*>(storage_.data()),
            static_cast<std::streamsize>(storage_.byteSize()));
    if (static_cast<std::size_t>(in.gcount()) != storage_.byteSize()) {
      throw DatabaseError("'" + path + "' is truncated (data)");
    }

    const std::uint64_t actual = checksum();
    if (actual != header.checksum) {
      throw DatabaseError("'" + path + "' is corrupt: checksum " +
                          std::to_string(actual) + " does not match the stored " +
                          std::to_string(header.checksum));
    }

    maxDistance_ = header.maxDistance;
    ready_ = true;
    return true;
  }

  /// Loads if present, otherwise generates and saves. The usual entry point.
  /// Returns true when the database was generated rather than loaded.
  bool loadOrGenerate(const std::string& path,
                      const PdbProgressCallback& onProgress = {}) {
    if (load(path)) return false;
    generate(onProgress);
    save(path);
    return true;
  }

 private:
  static constexpr const char* kMagic = "RUBIKPDB";
  static constexpr std::uint32_t kVersion = 1;

  struct Header {
    char magic[8];
    std::uint32_t version;
    std::uint64_t stateCount;
    std::uint64_t byteCount;
    std::uint64_t checksum;
    std::uint8_t maxDistance;
    std::uint8_t bitsPerEntry;
  };

  [[nodiscard]] static std::uint8_t bitsPerEntry() noexcept {
    // Distinguishes a nibble-packed file from a byte-per-entry one so the two
    // can never be confused for each other.
    return std::is_same<Storage, NibbleArray>::value ? 4 : 8;
  }

  template <typename T>
  static void readScalar(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
  }

  template <typename T>
  static void writeScalar(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
  }

  void writeHeader(std::ofstream& out) const {
    // Fields are written individually rather than as a struct, so padding
    // differences between compilers cannot change the file layout.
    out.write(kMagic, 8);
    writeScalar(out, kVersion);
    writeScalar(out, static_cast<std::uint64_t>(abstraction_.size()));
    writeScalar(out, static_cast<std::uint64_t>(storage_.byteSize()));
    writeScalar(out, checksum());
    writeScalar(out, maxDistance_);
    writeScalar(out, bitsPerEntry());
  }

  Abstraction abstraction_;
  Storage storage_;
  std::uint8_t maxDistance_ = 0;
  bool ready_ = false;
};

}  // namespace rubik
