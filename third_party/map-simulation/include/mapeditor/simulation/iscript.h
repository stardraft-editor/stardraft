// SPDX-License-Identifier: MIT
// Original bounded decoder for the SCPE animation bytecode file format.
// This is a decoder, not an implementation of engine opcode side effects.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <map>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace mapeditor::simulation::iscript {
struct Script {
  std::uint16_t id = 0, type = 0;
  std::vector<std::uint16_t> animations;
};
struct Instruction {
  std::uint32_t offset = 0, next = 0;
  std::uint8_t opcode = 0;
  std::vector<int> arguments;
};
struct SpriteStep {
  enum : unsigned {
    Frame = 1,
    X = 2,
    Y = 4,
    MainFrame = 8,
    MainDirection = 16
  };
  std::uint32_t next = 0;
  int wait = 0, span = 0;
  std::uint8_t terminal = 0;
  int frame = 0, x = 0, y = 0;
  unsigned flags = 0;
};
class Program {
public:
  explicit Program(std::span<const std::uint8_t> bytes)
      : bytes_(bytes.begin(), bytes.end()) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> reserved;
    if (bytes.size() < 8 || bytes.size() > 16u * 1024 * 1024)
      fail("Invalid IScript size");
    const auto table = word(0) | std::uint32_t(word(2)) << 16;
    auto cursor = table;
    for (;;) {
      auto id = word(cursor), at = word(cursor + 2);
      cursor += 4;
      if (id == 65535) break;
      if (!at || scripts_.contains(id)) fail("Invalid or duplicate script ID");
      require(at, 8);
      if (!std::equal(bytes_.begin() + at, bytes_.begin() + at + 4, "SCPE"))
        fail("Missing SCPE signature");
      Script script{id, word(at + 4), {}};
      const int count = animationCount(script.type);
      require(at, 8 + count * 2);
      reserved.emplace_back(at, at + 8 + count * 2);
      for (int n = 0; n < count; ++n) {
        auto entry = word(at + 8 + n * 2);
        if ((!n && !entry) || entry >= bytes_.size())
          fail("Invalid animation entry point");
        script.animations.push_back(entry);
      }
      scripts_.emplace(id, std::move(script));
    }
    reserved.emplace_back(0, 4);
    reserved.emplace_back(table, cursor);
    std::vector<std::uint32_t> pending;
    for (const auto &[id, script] : scripts_)
      for (auto entry : script.animations)
        if (entry) pending.push_back(entry);
    if (pending.empty()) fail("No IScript animations");
    while (!pending.empty()) {
      auto at = pending.back(); pending.pop_back();
      if (instructions_.contains(at)) continue;
      auto instruction = decode(at);
      for (auto [begin, end] : reserved)
        if (at < end && instruction.next > begin)
          fail("Instruction overlaps IScript metadata");
      auto next = instructions_.lower_bound(at);
      if ((next != instructions_.end() && instruction.next > next->first) ||
          (next != instructions_.begin() && std::prev(next)->second.next > at))
        fail("Overlapping IScript instructions");
      const int op = instruction.opcode;
      if (op == 7 || op == 30 || op == 53 || op == 57 || op == 58 ||
          op == 59 || op == 60 || op == 63)
        pending.push_back(instruction.arguments.back());
      if (op != 7 && op != 22 && op != 48 && op != 54)
        pending.push_back(instruction.next);
      instructions_.emplace(at, std::move(instruction));
    }
    indexInstructions();
  }
  Program(const Program &other)
      : bytes_(other.bytes_), scripts_(other.scripts_),
        instructions_(other.instructions_) {
    indexInstructions();
  }
  Program(Program &&) = default;
  Program &operator=(Program other) {
    bytes_.swap(other.bytes_);
    scripts_.swap(other.scripts_);
    instructions_.swap(other.instructions_);
    instructionIndex_.swap(other.instructionIndex_);
    return *this;
  }
  const std::map<std::uint16_t, Script> &scripts() const { return scripts_; }
  const std::map<std::uint32_t, Instruction> &instructions() const { return instructions_; }
  const Script &script(std::uint16_t id) const { return scripts_.at(id); }
  const Instruction &instruction(std::uint32_t offset) const {
    if (offset >= instructionIndex_.size() ||
        !instructionIndex_[offset].instruction)
      throw std::out_of_range("Unknown IScript instruction offset");
    return *instructionIndex_[offset].instruction;
  }
  // Compile short image-only blocks ending in wait/end. They cannot call the
  // world or branch on its state; all other blocks retain full VM dispatch.
  const SpriteStep *spriteStep(std::uint32_t offset) const {
    if (offset >= instructionIndex_.size())
      return nullptr;
    const auto &step = instructionIndex_[offset].sprite;
    return step.terminal ? &step : nullptr;
  }

private:
  std::vector<std::uint8_t> bytes_;
  std::map<std::uint16_t, Script> scripts_;
  std::map<std::uint32_t, Instruction> instructions_;
  struct IndexedInstruction {
    const Instruction *instruction = nullptr;
    SpriteStep sprite;
  };
  std::vector<IndexedInstruction> instructionIndex_;
  void indexInstructions() {
    instructionIndex_.assign(
        instructions_.empty() ? 0 : instructions_.rbegin()->first + 1, {});
    for (const auto &[offset, instruction] : instructions_)
      instructionIndex_[offset].instruction = &instruction;
    for (const auto &[offset, instruction] : instructions_) {
      const auto *current = &instruction;
      SpriteStep step;
      for (int jumps = 0; jumps < 32; ++jumps) {
        if (current->opcode == 5 || current->opcode == 6 ||
            current->opcode == 22) {
          step.terminal = current->opcode;
          step.next = current->next;
          if (step.terminal != 22)
            step.wait = current->arguments[0] - 1;
          if (step.terminal == 6)
            step.span = current->arguments[1] - current->arguments[0] + 1;
          instructionIndex_[offset].sprite = step;
          break;
        }
        const auto &a = current->arguments;
        switch (current->opcode) {
        case 0:
          step.flags =
              (step.flags | SpriteStep::Frame) & ~SpriteStep::MainFrame;
          step.frame = a[0];
          break;
        case 2:
          step.flags |= SpriteStep::X;
          step.x = a[0];
          break;
        case 3:
          step.flags |= SpriteStep::Y;
          step.y = a[0];
          break;
        case 4:
          step.flags |= SpriteStep::X | SpriteStep::Y;
          step.x = a[0];
          step.y = a[1];
          break;
        case 29:
          step.flags |= SpriteStep::MainFrame | SpriteStep::MainDirection;
          break;
        case 43:
          step.flags =
              (step.flags | SpriteStep::Frame | SpriteStep::MainDirection) &
              ~SpriteStep::MainFrame;
          step.frame = a[0];
          break;
        case 7:
          current = &this->instruction(std::uint32_t(a[0]));
          continue;
        default:
          current = nullptr;
          break;
        }
        if (!current)
          break;
        current = &this->instruction(current->next);
      }
    }
  }
  [[noreturn]] static void fail(const char *why) { throw std::runtime_error(why); }
  void require(std::size_t at, std::size_t count) const {
    if (at > bytes_.size() || count > bytes_.size() - at)
      fail("Truncated IScript data");
  }
  std::uint16_t word(std::size_t at) const {
    require(at, 2);
    return bytes_[at] | std::uint16_t(bytes_[at + 1]) << 8;
  }
  static int animationCount(int type) {
    switch (type) {
    case 0: case 1: return 2;
    case 2: return 4;
    case 12: case 13: return 14;
    case 14: case 15: return 16;
    case 20: case 21: return 22;
    case 23: return 24;
    case 24: return 26;
    case 26: case 27: case 28: case 29: return 28;
    default: fail("Unsupported IScript header type");
    }
  }
  Instruction decode(std::uint32_t at) const {
    static constexpr std::array<std::string_view, 69> formats = {
      "w", "w", "s", "s", "ss", "b", "bb", "w", "wss", "wss", "w", "w", "",
      "wss", "wss", "wss", "wss", "wss", "w", "wss", "wss", "wb", "", "b", "w",
      "v", "ww", "", "v", "", "bw", "b", "b", "", "b", "b", "b", "b", "", "",
      "b", "b", "", "b", "b", "", "", "", "", "b", "", "", "b", "w", "", "w",
      "b", "w", "ww", "www", "www", "ss", "", "w", "w", "b", "wss", "", ""};
    require(at, 1);
    Instruction result{at, at + 1, bytes_[at], {}};
    if (result.opcode >= formats.size()) fail("Unknown IScript opcode");
    auto format = formats[result.opcode];
    for (char type : format) {
      if (type == 'w') {
        result.arguments.push_back(word(result.next)); result.next += 2;
      } else if (type == 'v') {
        require(result.next, 1);
        auto count = bytes_[result.next++];
        result.arguments.push_back(count);
        require(result.next, count * 2);
        for (int n = 0; n < count; ++n) {
          result.arguments.push_back(word(result.next)); result.next += 2;
        }
      } else {
        require(result.next, 1);
        auto value = bytes_[result.next++];
        result.arguments.push_back(type == 's' && value >= 128 ? int(value) - 256 : value);
      }
    }
    return result;
  }
};
} // namespace mapeditor::simulation::iscript
