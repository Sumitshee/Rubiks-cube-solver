#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rubik {

/// A dense array of 4-bit unsigned values.
///
/// Pattern database entries are distances to the solved state, and every
/// database used here has a maximum distance below 15, so each entry fits in a
/// nibble. Halving the footprint matters twice over: the corner database drops
/// from 84 MB to 42 MB, and -- far more importantly -- the search's random
/// access pattern means the working set is effectively bounded by cache and
/// page behaviour rather than bandwidth. Twice as many entries per cache line
/// is a direct reduction in misses.
///
/// The trade is two extra ALU ops (a shift and a mask) per lookup, which is
/// nothing next to the cost of the cache miss they are hiding.
///
/// Access is unchecked in release builds by design: `get` sits in the hottest
/// loop in the program. Use `size()` to bounds-check at the call site when the
/// index is not already known to be in range.
class NibbleArray {
 public:
  NibbleArray() = default;

  /// Creates `size` entries, each initialised to `fill` (only the low nibble
  /// of `fill` is used).
  explicit NibbleArray(std::size_t size, std::uint8_t fill = 0x0F)
      : size_(size), data_((size + 1) / 2, static_cast<std::uint8_t>((fill & 0x0F) * 0x11)) {}

  [[nodiscard]] std::uint8_t get(std::size_t pos) const noexcept {
    const std::uint8_t byte = data_[pos >> 1];
    // Even indices live in the low nibble, odd indices in the high nibble.
    // Branchless: shift by 4 when the low bit is set.
    const unsigned shift = static_cast<unsigned>((pos & 1U) << 2U);
    return static_cast<std::uint8_t>((static_cast<unsigned>(byte) >> shift) & 0x0FU);
  }

  void set(std::size_t pos, std::uint8_t value) noexcept {
    std::uint8_t& byte = data_[pos >> 1];
    const unsigned shift = static_cast<unsigned>((pos & 1U) << 2U);
    byte = static_cast<std::uint8_t>((byte & ~(0x0FU << shift)) |
                                     ((value & 0x0FU) << shift));
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  /// Bytes actually occupied, i.e. ceil(size / 2).
  [[nodiscard]] std::size_t byteSize() const noexcept { return data_.size(); }

  [[nodiscard]] const std::uint8_t* data() const noexcept { return data_.data(); }
  [[nodiscard]] std::uint8_t* data() noexcept { return data_.data(); }

  void fill(std::uint8_t value) noexcept {
    std::fill(data_.begin(), data_.end(),
              static_cast<std::uint8_t>((value & 0x0F) * 0x11));
  }

  void resize(std::size_t size, std::uint8_t fillValue = 0x0F) {
    size_ = size;
    data_.assign((size + 1) / 2,
                 static_cast<std::uint8_t>((fillValue & 0x0F) * 0x11));
  }

 private:
  std::size_t size_ = 0;
  std::vector<std::uint8_t> data_;
};

}  // namespace rubik
