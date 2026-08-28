#include "bench/ProcessMemory.h"

#if defined(_WIN32)
// clang-format off
// windows.h defines min/max macros that break std::min and std::max, and pulls
// in a great deal that is not needed here.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

namespace rubik::bench {

MemoryUsage processMemory() noexcept {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
    return MemoryUsage{counters.WorkingSetSize, counters.PeakWorkingSetSize};
  }
  return {};
#else
  // No portable equivalent. Reporting zero is honest; guessing would not be.
  return {};
#endif
}

}  // namespace rubik::bench
