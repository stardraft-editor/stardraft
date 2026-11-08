// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <stdexcept>
namespace mapeditor::simulation {
// Brood War's observed 32-bit linear congruential sequence. Overflow is defined
// unsigned arithmetic; output is the upper 15 bits of each new state.
class Random {
public:
  explicit Random(std::uint32_t seed = 42) : state_(seed) {}
  std::uint32_t state() const { return state_; }
  std::uint32_t next() {
    state_ = state_ * 22695477U + 1U;
    return (state_ >> 16) & 32767U;
  }
  int between(int first, int last) {
    if (first > last)
      throw std::invalid_argument("Invalid random range");
    auto width = std::uint64_t(std::int64_t(last) - first) + 1;
    return int(std::int64_t(first) + next() % width);
  }
  int scaled(int first, int last) {
    if (first > last)
      throw std::invalid_argument("Invalid scaled random range");
    auto width = std::uint64_t(std::int64_t(last) - first) + 1;
    return int(std::int64_t(first) + ((next() * width) >> 15));
  }

private:
  std::uint32_t state_;
};
} // namespace mapeditor::simulation
