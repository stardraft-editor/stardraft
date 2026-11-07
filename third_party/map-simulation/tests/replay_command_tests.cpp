// SPDX-License-Identifier: MIT
#include <algorithm>
#include <mapeditor/simulation/replay_commands.h>
#include <mapeditor/simulation/replay_identity.h>
#include <source_location>
#include <string>
using namespace mapeditor::simulation::replay;
using mapeditor::simulation::UnitHandle;
namespace {
void check(bool value,
           std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("Classic command test failed at line " +
                             std::to_string(where.line()));
}
template <class F> void rejects(F f, CommandErrorCode code) {
  try {
    f();
  } catch (const CommandError &e) {
    check(e.code() == code);
    return;
  }
  throw std::runtime_error("Invalid classic command accepted");
}
void append(Bytes &b, std::initializer_list<std::uint8_t> data) {
  b.insert(b.end(), data);
}
} // namespace
void testReplayCommands() {
  Metadata metadata;
  const std::array<std::uint32_t, 12> ids{
      257, 1, 0xffffffffu, 128, 255, 1, 0, 0x12340000u, 7, 0x12345678u, 7, 254};
  for (std::size_t i = 0; i < ids.size(); ++i) {
    metadata.players[i].id = ids[i];
    metadata.players[i].slot = std::uint32_t(255 - i);
  }
  const ClassicActors actors(metadata);
  metadata.players[1].id = 2;
  check(actors.ids() == ids);
  std::array<int, 256> expected;
  expected.fill(-1);
  expected[0] = 6;
  expected[1] = 1;
  expected[7] = 8;
  expected[128] = 3;
  expected[254] = 11;
  expected[255] = 4;
  for (int actor = 0; actor < 256; ++actor) {
    auto found = actors.resolve(std::uint8_t(actor));
    check(found.value_or(-1) == expected[actor]);
  }
  for (std::uint32_t raw = 0; raw < 65536; ++raw) {
    ClassicUnitId id{std::uint16_t(raw)};
    check(id.slot() == int(raw & 0x7ff) - 1 &&
          id.generationTag() == (raw >> 11));
    if (id.empty())
      check((raw & 0x7ff) == 0);
    else {
      auto handle =
          UnitHandle{id.slot(), std::uint32_t(id.generationTag()) + 64};
      check(ClassicUnitId::fromHandle(handle).raw() == raw);
    }
  }
  check(ClassicUnitId::fromHandle({}).empty());
  check(ClassicUnitId::fromHandle({2046, 31}).raw() == 65535);
  for (int invalid : {-2, 2047}) {
    bool caught = false;
    try {
      ClassicUnitId::fromHandle({invalid, 1});
    } catch (const std::out_of_range &) {
      caught = true;
    }
    check(caught);
  }
  // Golden wire records are specified independently of the decoder. Each begins
  // with the recorded player ID and wire opcode, followed by its payload.
  const std::vector<Bytes> records = {
      {7, 0x09, 2, 0x34, 0x12, 0xcd, 0xab},
      {7, 0x0a, 0},
      {7, 0x0b, 1, 0xff, 0xff},
      {7, 0x0c, 0x19, 0x34, 0x12, 0xcd, 0xab, 0xfe, 0x00},
      {7, 0x0d, 0x34, 0x12},
      {7, 0x0e, 0x98, 0xba, 0xdc, 0xfe},
      {7, 0x12, 0x98, 0xba, 0xdc, 0xfe},
      {7, 0x13, 2, 10},
      {7, 0x14, 0xff, 0xff, 0, 0x80, 0x78, 0x56, 0xfe, 0, 2},
      {7, 0x15, 0xff, 0xff, 0, 0x80, 0x78, 0x56, 0xfe, 0, 0x7b, 2},
      {7, 0x18},
      {7, 0x19},
      {7, 0x1a, 255},
      {7, 0x1b},
      {7, 0x1c},
      {7, 0x1e, 255},
      {7, 0x1f, 0x34, 0x12},
      {7, 0x20, 0xff, 0xff},
      {7, 0x21, 255},
      {7, 0x22, 255},
      {7, 0x23, 0x34, 0x12},
      {7, 0x25, 255},
      {7, 0x26, 255},
      {7, 0x27},
      {7, 0x28, 255},
      {7, 0x29, 0x34, 0x12},
      {7, 0x2a},
      {7, 0x2b, 255},
      {7, 0x2c, 255},
      {7, 0x2d, 255},
      {7, 0x2e},
      {7, 0x2f, 0, 0x80, 0xff, 0x7f},
      {7, 0x30, 255},
      {7, 0x31},
      {7, 0x32, 255},
      {7, 0x33},
      {7, 0x34},
      {7, 0x35, 0x34, 0x12},
      {7, 0x36},
      {7, 0x57, 255},
      {7, 0x58, 0xff, 0xff, 0x34, 0x12},
      {7, 0x5a},
      {7, 0xd2, 0, 0, 0x34, 0x12, 0, 0, 0, 0x80},
      {7, 0xd2, 0, 1, 0x34, 0x12, 0xff, 0xff, 0xff, 0xff},
      {7, 0xd2, 0, 2, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12},
      {7, 0xd2, 1, 0, 11, 60, 0x80},
      {7, 0xd2, 1, 1, 11, 43, 0xff},
      {7, 0xd2, 1, 2, 11, 0, 0, 0, 0x80},
      {7, 0xd2, 1, 3, 11, 0xff, 0xff, 0xff, 0xff}};
  auto fixtures = records;
  Bytes chat{7, 0x5c};
  for (int i = 0; i < 81; ++i)
    chat.push_back(std::uint8_t(i));
  fixtures.push_back(chat);
  Bytes twelve{7, 0x09, 12};
  for (int i = 0; i < 12; ++i)
    append(twelve, {std::uint8_t(i), 255});
  fixtures.push_back(twelve);
  Bytes combined;
  for (const auto &bytes : fixtures) {
    ClassicCommands reader(bytes);
    auto command = reader.next();
    check(command && command->player == 7 &&
          std::uint8_t(command->code) == bytes[1] && command->offset == 0 &&
          command->length == bytes.size() &&
          std::ranges::equal(reader.encoded(*command), bytes) &&
          reader.position() == bytes.size() && !reader.next());
    for (std::size_t n = 1; n < bytes.size(); ++n) {
      ClassicCommands partial(std::span(bytes).first(n));
      rejects([&] { partial.next(); }, CommandErrorCode::Truncated);
      check(partial.position() == 0);
    }
    combined.insert(combined.end(), bytes.begin(), bytes.end());
  }
  ClassicCommands mixed(combined);
  std::size_t position = 0;
  for (const auto &bytes : fixtures) {
    auto command = mixed.next();
    check(command && command->offset == position &&
          command->length == bytes.size() &&
          std::ranges::equal(mixed.encoded(*command), bytes));
    position += bytes.size();
  }
  check(!mixed.next());
  auto decode = [](const Bytes &bytes) {
    return *ClassicCommands(bytes).next();
  };
  const auto target = std::get<TargetCommand>(decode(records[9]).payload);
  check(target.x == -1 && target.y == -32768 && target.unit == 0x5678 &&
        target.unitType == 254 && target.order == 123 && target.queue == 2);
  const auto click = std::get<TargetCommand>(decode(records[8]).payload);
  check(click.order == 0 && click.queue == 2 && click.x == -1 &&
        click.y == -32768);
  const auto build = std::get<BuildCommand>(decode(records[3]).payload);
  check(build.order == 25 && build.tileX == 0x1234 && build.tileY == 0xabcd &&
        build.unitType == 254);
  const auto selection = std::get<SelectionCommand>(decode(records[0]).payload);
  check(selection.count == 2 && selection.units[0] == 0x1234 &&
        selection.units[1] == 0xabcd);
  check(std::get<SelectionCommand>(decode(twelve).payload).units[11] == 0xff0b);
  check(std::get<SelectionCommand>(decode(records[1]).payload).count == 0);
  check(std::get<WordCommand>(decode(records[4]).payload).value == 0x1234);
  check(std::get<DwordCommand>(decode(records[5]).payload).value == 0xfedcba98);
  const auto group = std::get<ControlGroupCommand>(decode(records[7]).payload);
  check(group.operation == 2 && group.group == 10);
  check(std::get<ByteCommand>(decode(records[12]).payload).value == 255);
  const auto pos = std::get<PositionCommand>(decode(records[31]).payload);
  check(pos.x == -32768 && pos.y == 32767);
  const auto ping = std::get<PingCommand>(decode(records[40]).payload);
  check(ping.x == 65535 && ping.y == 0x1234);
  check(std::get<ChatCommand>(decode(chat).payload).bytes[80] == 80);
  check(std::get<UnitValueCommand>(decode(records[42]).payload).value ==
        (-2147483647 - 1));
  check(std::get<UnitValueCommand>(decode(records[43]).payload).property == 1);
  check(std::get<UnitValueCommand>(decode(records[44]).payload).value ==
        0x12345678);
  const auto level = std::get<PlayerLevelCommand>(decode(records[45]).payload);
  check(level.property == 0 && level.player == 11 && level.index == 60 &&
        level.value == -128);
  check(std::get<PlayerValueCommand>(decode(records[48]).payload).value == -1);
  const Bytes badSelection{255, 0x09, 13};
  ClassicCommands bad(badSelection);
  rejects([&] { bad.next(); }, CommandErrorCode::SelectionLimit);
  check(bad.position() == 0);
  const Bytes unknown{0, 0xff, 7, 0x18};
  ClassicCommands unsupported(unknown);
  rejects([&] { unsupported.next(); }, CommandErrorCode::Unsupported);
  check(unsupported.position() == 0);
  rejects([&] { unsupported.next(); }, CommandErrorCode::Unsupported);
  for (const auto &bytes : {Bytes{0, 0xd2, 2}, Bytes{0, 0xd2, 2, 0},
                            Bytes{0, 0xd2, 0, 3}, Bytes{0, 0xd2, 1, 4}}) {
    ClassicCommands invalid(bytes);
    rejects([&] { invalid.next(); }, CommandErrorCode::CheatType);
    check(invalid.position() == 0);
  }
  const Bytes tail{0, 0x18, 0, 0xff};
  ClassicCommands partial(tail);
  check(partial.next()->length == 2);
  rejects([&] { partial.next(); }, CommandErrorCode::Unsupported);
  check(partial.position() == 2);
  ClassicCommands empty({});
  check(!empty.next());
  bool caught = false;
  try {
    empty.encoded({0, ClassicOpcode::Select, std::size_t(-1), 2, {}});
  } catch (const std::out_of_range &) {
    caught = true;
  }
  check(caught);
}
