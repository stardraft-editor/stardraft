// SPDX-License-Identifier: MIT
#pragma once
#include "iscript.h"
#include "random.h"
#include <functional>
#include <optional>

namespace mapeditor::simulation::iscript {
// Value state makes world snapshots independent of address fixups. PC and
// return addresses use offsets in the original bytecode, not pointers into
// decoded data.
struct Cursor {
  std::uint16_t script = 0;
  std::uint32_t pc = 0, returnTo = 0;
  int animation = 0, wait = 0;
  bool ended = false;
};
enum class HostAction { Continue, Suspend, End };
struct AnimationHost {
  // Side effects (images, sound, unit motion, orders) belong to the world. A
  // missing handler is an error rather than an invisible unsupported operation.
  std::function<HostAction(const Instruction &)> apply;
  std::function<bool(const Instruction &)> condition;
  bool motionOnly = false;
};
class Animation {
public:
  Animation(const Program &program, Random &random)
      : program_(program), random_(random) {}
  bool select(Cursor &cursor, int animation) const {
    const auto &entries = program_.script(cursor.script).animations;
    if (animation < 0 || std::size_t(animation) >= entries.size() ||
        !entries[animation])
      return false;
    cursor.pc = entries[animation];
    cursor.returnTo = 0;
    cursor.animation = animation;
    cursor.wait = 0;
    cursor.ended = false;
    return true;
  }
  std::optional<int> averageWalkingSpeed(std::uint16_t script,
                                         unsigned frames = 32) const {
    if (!frames)
      throw std::invalid_argument("Empty animation speed window");
    Cursor cursor;
    cursor.script = script;
    if (!select(cursor, 11))
      return {};
    int displacement = 0;
    std::int64_t total = 0;
    AnimationHost host;
    host.motionOnly = true;
    host.condition = [](const auto &) { return false; };
    host.apply = [&](const auto &instruction) {
      if (instruction.opcode == 41)
        displacement = instruction.arguments.at(0) * 256;
      return HostAction::Continue;
    };
    for (unsigned frame = 0; frame < frames; ++frame) {
      displacement = 0;
      tick(cursor, host);
      if (cursor.ended)
        throw std::runtime_error(
            "Walking animation ended during speed analysis");
      total += displacement;
    }
    return int(total / frames);
  }
  void tick(Cursor &cursor, const AnimationHost &host,
            unsigned budget = 4096) const {
    if (cursor.ended)
      return;
    if (cursor.wait) {
      --cursor.wait;
      return;
    }
    for (unsigned step = 0; step < budget; ++step) {
      const auto &instruction = program_.instruction(cursor.pc);
      const auto &args = instruction.arguments;
      cursor.pc = instruction.next;
      auto dispatch = [&](const Instruction &event) {
        if (!host.apply)
          throw std::runtime_error("Missing animation side-effect host");
        auto action = host.apply(event);
        if (action == HostAction::End)
          cursor.ended = true;
        return action != HostAction::Continue;
      };
      switch (instruction.opcode) {
      case 5: // wait includes the frame executing the instruction
        cursor.wait = args[0] - 1;
        return;
      case 6:
        if (args[1] < args[0])
          throw std::runtime_error("Inverted animation wait range");
        cursor.wait =
            args[0] + int((random_.next() & 255) % (args[1] - args[0] + 1)) - 1;
        return;
      case 7:
        cursor.pc = args[0];
        break;
      case 22:
        cursor.ended = true;
        return;
      case 25:
      case 28: {
        if (host.motionOnly)
          break;
        if (args[0] <= 0)
          throw std::runtime_error("Empty animation sound selection");
        // Melee has a damage side effect as well as the chosen sound.
        if (instruction.opcode == 28) {
          Instruction damage{
              instruction.offset, instruction.next, instruction.opcode, {}};
          if (dispatch(damage))
            return;
        }
        auto sound = args[1 + random_.next() % args[0]];
        Instruction event{instruction.offset, instruction.next, 24, {sound}};
        if (dispatch(event))
          return;
        break;
      }
      case 26: {
        if (host.motionOnly)
          break;
        if (args[1] < args[0])
          throw std::runtime_error("Inverted animation sound range");
        Instruction event{
            instruction.offset,
            instruction.next,
            24,
            {args[0] + int(random_.next() % (args[1] - args[0] + 1))}};
        if (dispatch(event))
          return;
        break;
      }
      case 30:
        if ((random_.next() & 255) <= unsigned(args[0]))
          cursor.pc = args[1];
        break;
      case 34: {
        if (host.motionOnly)
          break;
        auto event = instruction;
        event.opcode = random_.next() % 4 == 1 ? 31 : 32;
        if (dispatch(event))
          return;
        break;
      }
      case 48:
        // Host switches to idle when there is no target; otherwise this opcode
        // remains at the current address and polls after eleven frames.
        if (!host.condition)
          throw std::runtime_error("Missing animation condition host");
        if (host.condition(instruction)) {
          cursor.pc = instruction.offset;
          cursor.wait = 10;
          return;
        }
        if (dispatch(instruction))
          return;
        break;
      case 53:
        cursor.returnTo = instruction.next;
        cursor.pc = args[0];
        break;
      case 54:
        if (!cursor.returnTo)
          throw std::runtime_error("Animation return without call");
        cursor.pc = cursor.returnTo;
        break;
      case 57:
      case 58:
      case 59:
      case 60:
      case 63:
        if (!host.condition)
          throw std::runtime_error("Missing animation condition host");
        if (host.condition(instruction))
          cursor.pc = args.back();
        break;
      default:
        if (dispatch(instruction))
          return;
        break;
      }
    }
    throw std::runtime_error("Animation instruction budget exhausted");
  }

private:
  const Program &program_;
  Random &random_;
};
} // namespace mapeditor::simulation::iscript
