#pragma once

#include <cstddef>

namespace rubik::bench {

/// Process memory statistics, in bytes.
///
/// The only platform-specific code in the project, isolated here behind a
/// portable interface. On platforms without an implementation both values are
/// reported as zero rather than guessed, so a benchmark row shows "n/a" instead
/// of a fabricated number.
struct MemoryUsage {
  std::size_t currentBytes = 0;
  std::size_t peakBytes = 0;

  [[nodiscard]] bool available() const noexcept { return peakBytes != 0; }
};

[[nodiscard]] MemoryUsage processMemory() noexcept;

}  // namespace rubik::bench
