#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rubik {

/// One byte per entry, with the same interface as `NibbleArray`.
///
/// Exists so the two storage layouts can be swapped as a template parameter and
/// measured against each other rather than assumed. Nibble packing halves the
/// footprint at the cost of a shift and a mask per lookup; which one wins is a
/// question about cache behaviour, not about instruction count, and the answer
/// depends on the database size relative to the machine's cache.
class ByteArray {
 public:
  ByteArray() = default;

  explicit ByteArray(std::size_t size, std::uint8_t fill = 0x0F)
      : data_(size, fill) {}

  [[nodiscard]] std::uint8_t get(std::size_t pos) const noexcept {
    return data_[pos];
  }

  void set(std::size_t pos, std::uint8_t value) noexcept {
    data_[pos] = value;
  }

  [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
  [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
  [[nodiscard]] std::size_t byteSize() const noexcept { return data_.size(); }

  [[nodiscard]] const std::uint8_t* data() const noexcept { return data_.data(); }
  [[nodiscard]] std::uint8_t* data() noexcept { return data_.data(); }

  void fill(std::uint8_t value) noexcept {
    std::fill(data_.begin(), data_.end(), value);
  }

  void resize(std::size_t size, std::uint8_t fillValue = 0x0F) {
    data_.assign(size, fillValue);
  }

 private:
  std::vector<std::uint8_t> data_;
};

}  // namespace rubik
