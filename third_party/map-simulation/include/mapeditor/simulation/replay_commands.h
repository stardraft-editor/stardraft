// SPDX-License-Identifier: MIT
#pragma once
#include "replay.h"
#include <variant>

namespace mapeditor::simulation::replay {
// Classic command wire identifiers are protocol facts, not gameplay dispatch.
// Modern container families must not implicitly select this wire format.
enum class ClassicOpcode : std::uint8_t {
  Select = 0x09,
  AddSelection = 0x0a,
  RemoveSelection = 0x0b,
  Build = 0x0c,
  SharedVision = 0x0d,
  Alliances = 0x0e,
  Cheat = 0x12,
  ControlGroup = 0x13,
  RightClick = 0x14,
  Order = 0x15,
  CancelConstruction = 0x18,
  CancelMorph = 0x19,
  Stop = 0x1a,
  CarrierStop = 0x1b,
  ReaverStop = 0x1c,
  ReturnCargo = 0x1e,
  Train = 0x1f,
  CancelQueue = 0x20,
  Cloak = 0x21,
  Decloak = 0x22,
  Morph = 0x23,
  Unsiege = 0x25,
  Siege = 0x26,
  TrainFighter = 0x27,
  UnloadAll = 0x28,
  Unload = 0x29,
  Archon = 0x2a,
  Hold = 0x2b,
  Burrow = 0x2c,
  Unburrow = 0x2d,
  CancelNuke = 0x2e,
  LiftOff = 0x2f,
  Research = 0x30,
  CancelResearch = 0x31,
  Upgrade = 0x32,
  CancelUpgrade = 0x33,
  CancelAddon = 0x34,
  BuildingMorph = 0x35,
  Stim = 0x36,
  Leave = 0x57,
  Ping = 0x58,
  DarkArchon = 0x5a,
  Chat = 0x5c,
  ExtendedCheat = 0xd2
};
struct SelectionCommand {
  std::array<std::uint16_t, 12> units{};
  std::uint8_t count = 0;
};
struct BuildCommand {
  std::uint8_t order = 0;
  std::uint16_t tileX = 0, tileY = 0, unitType = 0;
};
struct TargetCommand {
  std::int16_t x = 0, y = 0;
  std::uint16_t unit = 0, unitType = 0;
  // RightClick has no explicit order byte; code distinguishes that case.
  std::uint8_t order = 0, queue = 0;
};
struct ControlGroupCommand {
  std::uint8_t operation = 0, group = 0;
};
struct ByteCommand {
  std::uint8_t value = 0;
};
struct WordCommand {
  std::uint16_t value = 0;
};
struct DwordCommand {
  std::uint32_t value = 0;
};
struct PositionCommand {
  std::int16_t x = 0, y = 0;
};
struct PingCommand {
  std::uint16_t x = 0, y = 0;
};
struct ChatCommand {
  std::array<std::uint8_t, 81> bytes{};
};
struct UnitValueCommand {
  std::uint8_t property = 0;
  std::uint16_t unit = 0;
  std::int32_t value = 0;
};
struct PlayerLevelCommand {
  std::uint8_t property = 0, player = 0, index = 0;
  std::int8_t value = 0;
};
struct PlayerValueCommand {
  std::uint8_t property = 0, player = 0;
  std::int32_t value = 0;
};
using ClassicPayload =
    std::variant<std::monostate, SelectionCommand, BuildCommand, TargetCommand,
                 ControlGroupCommand, ByteCommand, WordCommand, DwordCommand,
                 PositionCommand, PingCommand, ChatCommand, UnitValueCommand,
                 PlayerLevelCommand, PlayerValueCommand>;
struct ClassicCommand {
  std::uint8_t player = 0;
  ClassicOpcode code = ClassicOpcode::Select;
  std::size_t offset = 0, length = 0;
  ClassicPayload payload;
};
enum class CommandErrorCode {
  Truncated,
  Unsupported,
  SelectionLimit,
  CheatType
};
class CommandError : public std::runtime_error {
public:
  CommandError(CommandErrorCode, std::size_t offset, const char *message);
  CommandErrorCode code() const noexcept { return code_; }
  // Offset in the supplied frame payload, never a guessed resynchronization.
  std::size_t offset() const noexcept { return offset_; }

private:
  CommandErrorCode code_;
  std::size_t offset_;
};
// Non-owning, allocation-free decoder for a single classic frame payload.
// Parsing does not resolve player/unit IDs, normalize flags, or apply effects.
// A failed next() leaves position unchanged; callers cannot skip an unknown
// command by inventing a byte length. The source must outlive this object.
class ClassicCommands {
public:
  explicit ClassicCommands(std::span<const std::uint8_t> input)
      : input_(input) {}
  std::optional<ClassicCommand> next();
  std::size_t position() const noexcept { return position_; }
  std::span<const std::uint8_t> encoded(const ClassicCommand &) const;

private:
  std::span<const std::uint8_t> input_;
  std::size_t position_ = 0;
};
} // namespace mapeditor::simulation::replay
