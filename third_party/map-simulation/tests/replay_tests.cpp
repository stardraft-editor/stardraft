// SPDX-License-Identifier: MIT
#include <algorithm>
#include <iostream>
#include <mapeditor/simulation/replay.h>
#include <string>
using namespace mapeditor::simulation::replay;
namespace {
void check(bool value) {
  if (!value)
    throw std::runtime_error("Replay test failed");
}
void u32(Bytes &b, std::uint32_t n) {
  for (int i = 0; i < 4; ++i)
    b.push_back(std::uint8_t(n >> (8 * i)));
}
void put(Bytes &b, std::size_t at, std::uint32_t n) {
  for (int i = 0; i < 4; ++i)
    b.at(at + i) = std::uint8_t(n >> (8 * i));
}
// Independent bit-at-a-time IEEE CRC avoids sharing the reader's table logic.
std::uint32_t crc(std::span<const std::uint8_t> b) {
  std::uint32_t n = 0xffffffffu;
  for (auto v : b) {
    n ^= v;
    for (int j = 0; j < 8; ++j)
      n = (n & 1) ? (n >> 1) ^ 0xedb88320u : n >> 1;
  }
  return n;
}
Bytes scalar(std::uint32_t n) {
  Bytes b;
  u32(b, n);
  return b;
}
void append(Bytes &b, std::span<const std::uint8_t> s) {
  b.insert(b.end(), s.begin(), s.end());
}
void section(Bytes &b, const Bytes &decoded, Format format) {
  if (decoded.empty() && format != Format::Legacy)
    return;
  u32(b, crc(decoded));
  u32(b, std::uint32_t((decoded.size() + 8191) / 8192));
  for (std::size_t i = 0; i < decoded.size(); i += 8192) {
    auto n = std::min<std::size_t>(8192, decoded.size() - i);
    u32(b, std::uint32_t(n));
    append(b, std::span(decoded).subspan(i, n));
  }
}
Bytes frame(std::uint32_t n, Bytes payload) {
  auto b = scalar(n);
  b.push_back(std::uint8_t(payload.size()));
  append(b, payload);
  return b;
}
struct Fixture {
  Bytes bytes, header = Bytes(633), commands, map, names = Bytes(1152);
  std::size_t headerStart = 0, commandsStart = 0, mapLengthStart = 0,
              extensionsStart = 0;
  Fixture(Format format, Bytes stream = {}, Bytes scenario = {})
      : commands(std::move(stream)), map(std::move(scenario)) {
    header[0] = 1;
    put(header, 1, 100);
    put(header, 8, 0x12345678);
    put(header, 48, 0x87654321);
    header[52] = 128;
    header[54] = 96;
    header[56] = 2;
    header[57] = 8;
    header[58] = 6;
    header[59] = 1;
    header[60] = 2;
    header[62] = 3;
    header[68] = 7;
    for (int i = 0; i < 11; ++i)
      header[137 + i] = std::uint8_t(31 + i);
    put(header, 148, 0x1234abcd);
    put(header, 152, 0xfedc9876);
    put(header, 156, 0x76543210);
    for (int i = 0; i < 12; ++i) {
      auto at = 161 + 36 * i;
      put(header, at, 0x10000000u + i);
      put(header, at + 4, 0x20000000u + i);
      header[at + 8] = std::uint8_t(i);
      header[at + 9] = std::uint8_t(i % 3);
      header[at + 10] = std::uint8_t(i % 4);
      std::fill_n(header.begin() + at + 11, 25, std::uint8_t(128 + i));
    }
    for (int i = 0; i < 8; ++i) {
      put(header, 593 + 4 * i, 0xabcdef00u + i);
      header[625 + i] = std::uint8_t(8 + i);
    }
    std::fill_n(header.begin() + 24, 24, 0xf0);
    std::fill_n(header.begin() + 72, 25, 0xf1);
    std::fill_n(header.begin() + 97, 32, 0xf2);
    for (std::size_t i = 0; i < names.size(); ++i)
      names[i] = std::uint8_t(i);
    section(bytes,
            format == Format::Modern121 ? Bytes{'s', 'e', 'R', 'S'}
                                        : Bytes{'r', 'e', 'R', 'S'},
            Format::Legacy);
    if (format == Format::Modern121)
      u32(bytes, 0);
    headerStart = bytes.size();
    section(bytes, header, format);
    section(bytes, scalar(std::uint32_t(commands.size())), format);
    commandsStart = bytes.size();
    section(bytes, commands, format);
    mapLengthStart = bytes.size();
    section(bytes, scalar(std::uint32_t(map.size())), format);
    section(bytes, map, format);
    if (format != Format::Legacy)
      section(bytes, names, format);
    extensionsStart = bytes.size();
    if (format == Format::Modern121)
      put(bytes, 16, std::uint32_t(bytes.size()));
  }
};
template <class F> void rejects(F f, ErrorCode expected) {
  try {
    f();
  } catch (const Error &e) {
    check(e.code() == expected);
    return;
  }
  throw std::runtime_error("Replay accepted invalid data");
}
void metadataAndTimeline() {
  Bytes stream = frame(0, {1, 2});
  append(stream, frame(7, {}));
  append(stream, frame(7, {255, 0, 128}));
  append(stream, frame(100, {99}));
  // Deliberately malformed/protected map bytes are preserved, not parsed as
  // CHK.
  Bytes map(16385, 42);
  for (auto format : {Format::Legacy, Format::Modern18, Format::Modern121}) {
    Fixture f(format, stream, map);
    auto r = Replay::read(f.bytes, {}, {}, format);
    check(r.format() == format && std::ranges::equal(r.header(), f.header) &&
          std::ranges::equal(r.commands(), stream) &&
          std::ranges::equal(r.map(), map));
    const auto &m = r.metadata();
    check(m.engine == 1 && m.frames == 100 && m.seed == 0x12345678 &&
          m.flags == 0x87654321 && m.width == 128 && m.height == 96 &&
          m.activePlayers == 2 && m.slots == 8 && m.speed == 6 &&
          m.state == 1 && m.gameType == 2 && m.gameSubtype == 3 &&
          m.tileset == 7 && m.creator.back() == 0xf0 &&
          m.title.back() == 0xf1 && m.mapName.back() == 0xf2);
    const auto &o = m.options;
    check(o.victory == 31 && o.resources == 32 && o.standardStats == 33 &&
          o.fog == 34 && o.initialUnits == 35 && o.fixedPositions == 36 &&
          o.restrictions == 37 && o.allies == 38 && o.teams == 39 &&
          o.cheats == 40 && o.tournament == 41 &&
          o.victoryValue == 0x1234abcd && o.startingMinerals == 0xfedc9876 &&
          o.startingGas == 0x76543210);
    for (int i = 0; i < 12; ++i) {
      const auto &p = m.players[i];
      check(p.slot == 0x10000000u + i && p.id == 0x20000000u + i &&
            p.controller == i && p.race == i % 3 && p.force == i % 4 &&
            p.name.front() == 128 + i && p.name.back() == 128 + i);
    }
    for (int i = 0; i < 8; ++i)
      check(m.colors[i] == 0xabcdef00u + i && m.createMelee[i] == 8 + i);
    check(r.blocks().size() == 4 && r.blocksAt(7).size() == 2 &&
          r.blocksAt(8).empty() && r.blocksAt(101).empty() &&
          r.blocksAt(0).size() == 1 && r.blocksAt(100).size() == 1);
    check(r.payload(r.blocksAt(7)[0]).empty());
    check(std::ranges::equal(r.payload(r.blocksAt(7)[1]), Bytes{255, 0, 128}));
    check((format == Format::Legacy && r.modernNames().empty()) ||
          std::ranges::equal(r.modernNames(), f.names));
    // Owning data survives destruction/mutation of the caller's bytes.
    std::fill(f.bytes.begin(), f.bytes.end(), 0);
    check(std::ranges::equal(r.commands(), stream));
    auto copy = r;
    r = {};
    check(std::ranges::equal(copy.payload(copy.blocksAt(7)[1]),
                             Bytes{255, 0, 128}));
  }
}
void invalidContainers() {
  Fixture f(Format::Modern121, frame(100, {1}), Bytes(8193, 42));
  // Every truncated prefix fails, including exact main-section boundaries.
  for (std::size_t i = 0; i < f.bytes.size(); ++i) {
    bool caught = false;
    try {
      Replay::read(std::span(f.bytes).first(i));
    } catch (const Error &) {
      caught = true;
    }
    check(caught);
  }
  auto bad = f.bytes;
  bad[f.headerStart + 12] ^= 1;
  rejects([&] { Replay::read(bad); }, ErrorCode::Checksum);
  bad = f.bytes;
  put(bad, f.headerStart + 4, 0xffffffff);
  rejects([&] { Replay::read(bad); }, ErrorCode::ChunkCount);
  bad = f.bytes;
  put(bad, f.headerStart + 8, 632);
  rejects([&] { Replay::read(bad); }, ErrorCode::ChunkSize);
  bad = f.bytes;
  put(bad, 16, 1);
  rejects([&] { Replay::read(bad); }, ErrorCode::Boundary);
  rejects([&] { Replay::read(f.bytes, {}, {}, Format::Legacy); },
          ErrorCode::Identifier);
  Bytes unknown;
  section(unknown, {'b', 'a', 'd', '!'}, Format::Legacy);
  rejects([&] { Replay::read(unknown); }, ErrorCode::Identifier);
  // Claim a huge section using a correctly checksummed length packet.
  bad.assign(f.bytes.begin(), f.bytes.begin() + f.mapLengthStart);
  section(bad, scalar(0xffffffff), Format::Modern121);
  rejects([&] { Replay::read(bad); }, ErrorCode::Limit);
  for (auto format : {Format::Legacy, Format::Modern121}) {
    Fixture empty(format);
    auto r = Replay::read(empty.bytes);
    check(r.blocks().empty() && r.map().empty() && r.blocksAt(0).empty());
  }
  Fixture legacy(Format::Legacy);
  bad = legacy.bytes;
  put(bad, legacy.headerStart + 8, 634);
  rejects([&] { Replay::read(bad); }, ErrorCode::ChunkSize);
  bad = legacy.bytes;
  bad[legacy.commandsStart] = 0;
  rejects([&] { Replay::read(bad); }, ErrorCode::Checksum);
}
void timelineAndLimits() {
  for (auto stream :
       {Bytes{0}, Bytes{0, 0, 0, 0}, Bytes{0, 0, 0, 0, 1}, frame(101, {}), [] {
          auto b = frame(10, {});
          append(b, frame(9, {}));
          return b;
        }()}) {
    Fixture f(Format::Legacy, stream);
    rejects([&] { Replay::read(f.bytes); }, ErrorCode::Timeline);
  }
  Fixture f(Format::Modern121, frame(1, {}));
  Limits limits;
  limits.inputBytes = f.bytes.size() - 1;
  rejects([&] { Replay::read(f.bytes, {}, limits); }, ErrorCode::Limit);
  limits = {};
  limits.sectionBytes = 632;
  rejects([&] { Replay::read(f.bytes, {}, limits); }, ErrorCode::Limit);
  limits = {};
  limits.retainedBytes = 4 + 633 + 4 + 5 + 4 + 1151;
  rejects([&] { Replay::read(f.bytes, {}, limits); }, ErrorCode::Limit);
  ++limits.retainedBytes;
  Replay::read(f.bytes, {}, limits);
  limits = {};
  limits.frameBlocks = 0;
  rejects([&] { Replay::read(f.bytes, {}, limits); }, ErrorCode::Limit);
  auto r = Replay::read(f.bytes);
  bool caught = false;
  try {
    r.payload({0, std::size_t(-1), 255});
  } catch (const std::out_of_range &) {
    caught = true;
  }
  check(caught);
}
void extensionsAndTrailer() {
  Fixture f(Format::Modern121);
  for (int i = 0; i < 2; ++i) {
    // Repeated unknown binary tags and arbitrary encoded bytes stay ordered.
    append(f.bytes, Bytes{0, 255, 'X', 'Y'});
    u32(f.bytes, 3);
    append(f.bytes, Bytes{std::uint8_t(i), 128, 254});
  }
  auto r = Replay::read(f.bytes);
  check(r.extensions().size() == 2 && r.extensions()[0].tag[1] == 255 &&
        r.extensions()[0].encoded[0] == 0 && r.extensions()[1].encoded[0] == 1);
  Limits limits;
  limits.extensions = 1;
  rejects([&] { Replay::read(f.bytes, {}, limits); }, ErrorCode::Limit);
  for (std::size_t i = 1; i < 11; ++i) {
    auto bytes = std::span(f.bytes).first(f.extensionsStart + i);
    rejects([&] { Replay::read(bytes); }, ErrorCode::Truncated);
  }
  Fixture classic(Format::Legacy);
  append(classic.bytes, {f.bytes.data() + f.extensionsStart, 22});
  auto legacy = Replay::read(classic.bytes);
  check(legacy.trailer().size() == 22 && legacy.extensions().empty());
}
void compressionBoundary() {
  // A raw scalar can begin with a valid zlib header: it remains a scalar.
  // Here the command byte length is 0x9c78 (40056).
  Bytes scalarCollision;
  for (int i = 0; i < 5007; ++i)
    append(scalarCollision, frame(0, {0, 0, 0}));
  check(scalarCollision.size() == 0x9c78);
  Fixture scalarFixture(Format::Modern121, scalarCollision);
  check(Replay::read(scalarFixture.bytes).blocks().size() == 5007);
  // Synthetic codec markers test callback dispatch and fixed output bounds;
  // these are not claimed to be valid compressed game recordings.
  for (auto format : {Format::Legacy, Format::Modern18, Format::Modern121}) {
    Fixture f(format, frame(1, {3}));
    Bytes marker = format == Format::Legacy ? Bytes{0, 6, 42}
                                            : Bytes{0x78, 0x9c, 42, 0, 0};
    Bytes bytes(f.bytes.begin(), f.bytes.begin() + f.headerStart + 8);
    u32(bytes, std::uint32_t(marker.size()));
    append(bytes, marker);
    append(bytes, std::span(f.bytes).subspan(f.headerStart + 12 + 633));
    if (format == Format::Modern121)
      put(bytes, 16, std::uint32_t(bytes.size()));
    rejects([&] { Replay::read(bytes); }, ErrorCode::MissingInflater);
    int calls = 0;
    auto inflater = [&](Compression c, std::span<const std::uint8_t> in,
                        std::span<std::uint8_t> out) {
      ++calls;
      check(c == (format == Format::Legacy ? Compression::PkwareDcl
                                           : Compression::Zlib));
      check(std::ranges::equal(in, marker) && out.size() == 633);
      std::copy(f.header.begin(), f.header.end(), out.begin());
      return out.size();
    };
    auto r = Replay::read(bytes, inflater);
    check(r.format() == format && calls == 1 && r.metadata().frames == 100);
    rejects(
        [&] {
          Replay::read(bytes,
                       [](auto, auto, auto) -> std::size_t { return 632; });
        },
        ErrorCode::ChunkSize);
    rejects(
        [&] {
          Replay::read(bytes, [](auto, auto, auto) -> std::size_t {
            throw std::runtime_error("bad compression");
          });
        },
        ErrorCode::Compression);
  }
  // Known standard CRC check value, complemented for the wire convention.
  const Bytes digits{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  check(crc(digits) == 0x340bc6d9);
}
} // namespace
void testReplayCommands();
void testReplaySelection();
int main() {
  try {
    metadataAndTimeline();
    invalidContainers();
    timelineAndLimits();
    extensionsAndTrailer();
    compressionBoundary();
    testReplayCommands();
    testReplaySelection();
    std::cout
        << "PASS: replay containers, metadata, frame indexing, ownership, "
           "opaque data, truncation, checksums, limits and codec boundary\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
