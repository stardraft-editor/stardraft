// SPDX-License-Identifier: MIT
#include <algorithm>
#include <bit>
#include <mapeditor/simulation/replay_commands.h>
#include <string>

namespace mapeditor::simulation::replay {
namespace {
class Fields {
public:
  std::span<const std::uint8_t> input;
  std::size_t at;
  std::span<const std::uint8_t> take(std::size_t n) {
    if (n > input.size() - at)
      throw CommandError(CommandErrorCode::Truncated, at,
                         "Truncated classic command");
    auto result = input.subspan(at, n);
    at += n;
    return result;
  }
  std::uint8_t byte() { return take(1)[0]; }
  std::uint16_t word() {
    auto b = take(2);
    return std::uint16_t(b[0] | (unsigned(b[1]) << 8));
  }
  std::uint32_t dword() {
    auto low = word();
    return low | (std::uint32_t(word()) << 16);
  }
  std::int16_t signedWord() { return std::bit_cast<std::int16_t>(word()); }
  std::int32_t signedDword() { return std::bit_cast<std::int32_t>(dword()); }
};
} // namespace
CommandError::CommandError(CommandErrorCode code, std::size_t offset,
                           const char *message)
    : std::runtime_error(std::string(message) + " at frame payload byte " +
                         std::to_string(offset)),
      code_(code), offset_(offset) {}

std::optional<ClassicCommand> ClassicCommands::next() {
  if (position_ == input_.size())
    return std::nullopt;
  Fields f{input_, position_};
  ClassicCommand c;
  c.offset = position_;
  c.player = f.byte();
  c.code = static_cast<ClassicOpcode>(f.byte());
  using enum ClassicOpcode;
  switch (c.code) {
  case Select:
  case AddSelection:
  case RemoveSelection: {
    SelectionCommand selection;
    selection.count = f.byte();
    if (selection.count > selection.units.size())
      throw CommandError(CommandErrorCode::SelectionLimit, f.at - 1,
                         "Classic selection exceeds twelve units");
    for (std::size_t i = 0; i < selection.count; ++i)
      selection.units[i] = f.word();
    c.payload = selection;
    break;
  }
  case Build: {
    BuildCommand build;
    build.order = f.byte();
    build.tileX = f.word();
    build.tileY = f.word();
    build.unitType = f.word();
    c.payload = build;
    break;
  }
  case RightClick:
  case Order: {
    TargetCommand target;
    target.x = f.signedWord();
    target.y = f.signedWord();
    target.unit = f.word();
    target.unitType = f.word();
    if (c.code == Order)
      target.order = f.byte();
    target.queue = f.byte();
    c.payload = target;
    break;
  }
  case ControlGroup: {
    auto operation = f.byte();
    c.payload = ControlGroupCommand{operation, f.byte()};
    break;
  }
  case SharedVision:
  case Train:
  case CancelQueue:
  case Morph:
  case Unload:
  case BuildingMorph:
    c.payload = WordCommand{f.word()};
    break;
  case Alliances:
  case Cheat:
    c.payload = DwordCommand{f.dword()};
    break;
  case Stop:
  case ReturnCargo:
  case Cloak:
  case Decloak:
  case Unsiege:
  case Siege:
  case UnloadAll:
  case Hold:
  case Burrow:
  case Unburrow:
  case Research:
  case Upgrade:
  case Leave:
    c.payload = ByteCommand{f.byte()};
    break;
  case LiftOff: {
    auto x = f.signedWord();
    c.payload = PositionCommand{x, f.signedWord()};
    break;
  }
  case Ping: {
    auto x = f.word();
    c.payload = PingCommand{x, f.word()};
    break;
  }
  case Chat: {
    ChatCommand chat;
    auto data = f.take(chat.bytes.size());
    std::copy(data.begin(), data.end(), chat.bytes.begin());
    c.payload = chat;
    break;
  }
  case CancelConstruction:
  case CancelMorph:
  case CarrierStop:
  case ReaverStop:
  case TrainFighter:
  case Archon:
  case CancelNuke:
  case CancelResearch:
  case CancelUpgrade:
  case CancelAddon:
  case Stim:
  case DarkArchon:
    break;
  case ExtendedCheat: {
    auto domain = f.byte();
    if (domain > 1)
      throw CommandError(CommandErrorCode::CheatType, f.at - 1,
                         "Unknown extended cheat domain");
    auto property = f.byte();
    if (domain == 0 && property <= 2) {
      auto unit = f.word();
      c.payload = UnitValueCommand{property, unit, f.signedDword()};
    } else if (domain == 1 && property <= 3) {
      auto player = f.byte();
      if (property < 2) {
        auto index = f.byte();
        c.payload = PlayerLevelCommand{property, player, index,
                                       std::bit_cast<std::int8_t>(f.byte())};
      } else
        c.payload = PlayerValueCommand{property, player, f.signedDword()};
    } else
      throw CommandError(CommandErrorCode::CheatType, f.at - 1,
                         "Unknown extended cheat layout");
    break;
  }
  default:
    throw CommandError(CommandErrorCode::Unsupported, c.offset + 1,
                       "Unknown classic command opcode");
  }
  c.length = f.at - position_;
  position_ = f.at;
  return c;
}
std::span<const std::uint8_t>
ClassicCommands::encoded(const ClassicCommand &c) const {
  if (c.offset > input_.size() || c.length > input_.size() - c.offset)
    throw std::out_of_range("Classic command outside frame payload");
  return input_.subspan(c.offset, c.length);
}
} // namespace mapeditor::simulation::replay
