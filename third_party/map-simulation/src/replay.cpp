// SPDX-License-Identifier: MIT
#include <algorithm>
#include <mapeditor/simulation/replay.h>
#include <string>

namespace mapeditor::simulation::replay {
namespace {
constexpr auto crcTable() {
  std::array<std::uint32_t, 256> table{};
  // IEEE CRC-32, reflected polynomial. Generated from the polynomial rather
  // than importing any engine's implementation or lookup table.
  for (std::uint32_t i = 0; i < table.size(); ++i) {
    auto n = i;
    for (int bit = 0; bit < 8; ++bit)
      n = (n >> 1) ^ ((n & 1) ? 0xedb88320u : 0u);
    table[i] = n;
  }
  return table;
}
constexpr auto crcLookup = crcTable();
std::uint32_t checksum(std::span<const std::uint8_t> data) {
  std::uint32_t crc = 0xffffffffu;
  for (auto b : data)
    crc = (crc >> 8) ^ crcLookup[(crc ^ b) & 255];
  return crc;
}
std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t at) {
  return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at + 1]) << 8) |
         (std::uint32_t(bytes[at + 2]) << 16) |
         (std::uint32_t(bytes[at + 3]) << 24);
}
bool zlibHeader(std::span<const std::uint8_t> bytes) {
  return bytes.size() >= 2 && (bytes[0] & 15) == 8 && (bytes[0] >> 4) <= 7 &&
         ((unsigned(bytes[0]) << 8) | bytes[1]) % 31 == 0;
}
class Reader {
public:
  Reader(std::span<const std::uint8_t> bytes, const Inflater &inflate,
         const Limits &limits)
      : bytes(bytes), inflate(inflate), limits(limits) {
    if (bytes.size() > limits.inputBytes)
      fail(ErrorCode::Limit, "Replay input limit");
  }
  [[noreturn]] void fail(ErrorCode code, const char *message) const {
    throw Error(code, at, message);
  }
  std::span<const std::uint8_t> take(std::size_t count) {
    if (count > bytes.size() - at)
      fail(ErrorCode::Truncated, "Truncated replay");
    auto part = bytes.subspan(at, count);
    at += count;
    return part;
  }
  std::uint32_t u32() { return word(take(4), 0); }
  void budget(std::size_t count) {
    if (count > limits.sectionBytes || count > limits.retainedBytes - retained)
      fail(ErrorCode::Limit, "Replay retained byte limit");
    retained += count;
  }
  Bytes retain(std::size_t count) {
    budget(count);
    auto source = take(count);
    return Bytes(source.begin(), source.end());
  }
  Bytes section(std::size_t size, Format format) {
    budget(size);
    if (size == 0 && format != Format::Legacy)
      return {};
    auto crc = u32();
    auto count = u32();
    if (count != size / 8192 + (size % 8192 != 0))
      fail(ErrorCode::ChunkCount, "Wrong replay chunk count");
    // Each chunk needs a size word before any allocation can be useful.
    if (count > (bytes.size() - at) / 4)
      fail(ErrorCode::Truncated, "Truncated replay chunk table");
    Bytes decoded(size);
    std::size_t done = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
      auto stored = u32();
      auto payload = take(stored);
      auto wanted = std::min<std::size_t>(8192, size - done);
      bool compressed = false;
      if (format == Format::Legacy) {
        if (stored > wanted)
          fail(ErrorCode::ChunkSize, "Oversized legacy replay chunk");
        compressed = stored != wanted;
      } else {
        // Four-byte scalar sections are raw even if their value happens to
        // start with an RFC1950 header. A zlib stream needs more than four
        // bytes.
        compressed = payload.size() > 4 && zlibHeader(payload);
      }
      auto output = std::span(decoded).subspan(done, wanted);
      if (compressed) {
        if (!inflate)
          fail(ErrorCode::MissingInflater, "Replay needs a compression codec");
        std::size_t written;
        try {
          written = inflate(format == Format::Legacy ? Compression::PkwareDcl
                                                     : Compression::Zlib,
                            payload, output);
        } catch (...) {
          fail(ErrorCode::Compression, "Replay decompression failed");
        }
        if (written != wanted)
          fail(ErrorCode::ChunkSize, "Wrong decoded replay chunk size");
      } else {
        if (stored != wanted)
          fail(ErrorCode::ChunkSize, "Wrong raw replay chunk size");
        std::copy(payload.begin(), payload.end(), output.begin());
      }
      done += wanted;
    }
    if (checksum(decoded) != crc)
      fail(ErrorCode::Checksum, "Replay section checksum mismatch");
    return decoded;
  }
  std::span<const std::uint8_t> bytes;
  const Inflater &inflate;
  const Limits &limits;
  std::size_t at = 0, retained = 0;
};
template <std::size_t N>
void copyField(std::array<std::uint8_t, N> &out,
               std::span<const std::uint8_t> input, std::size_t offset) {
  std::copy_n(input.begin() + offset, N, out.begin());
}
Metadata decodeMetadata(std::span<const std::uint8_t> h) {
  Metadata m;
  auto u16 = [&](std::size_t at) {
    return std::uint16_t(h[at] | (unsigned(h[at + 1]) << 8));
  };
  m.engine = h[0];
  m.frames = word(h, 1);
  m.seed = word(h, 8);
  copyField(m.creator, h, 24);
  m.flags = word(h, 48);
  m.width = u16(52);
  m.height = u16(54);
  m.activePlayers = h[56];
  m.slots = h[57];
  m.speed = h[58];
  m.state = h[59];
  m.gameType = u16(60);
  m.gameSubtype = u16(62);
  m.tileset = u16(68);
  copyField(m.title, h, 72);
  copyField(m.mapName, h, 97);
  auto &o = m.options;
  o.victory = h[137];
  o.resources = h[138];
  o.standardStats = h[139];
  o.fog = h[140];
  o.initialUnits = h[141];
  o.fixedPositions = h[142];
  o.restrictions = h[143];
  o.allies = h[144];
  o.teams = h[145];
  o.cheats = h[146];
  o.tournament = h[147];
  o.victoryValue = word(h, 148);
  o.startingMinerals = word(h, 152);
  o.startingGas = word(h, 156);
  for (std::size_t i = 0; i < m.players.size(); ++i) {
    auto offset = 161 + 36 * i;
    auto &p = m.players[i];
    p.slot = word(h, offset);
    p.id = word(h, offset + 4);
    p.controller = h[offset + 8];
    p.race = h[offset + 9];
    p.force = h[offset + 10];
    copyField(p.name, h, offset + 11);
  }
  for (std::size_t i = 0; i < m.colors.size(); ++i)
    m.colors[i] = word(h, 593 + 4 * i);
  copyField(m.createMelee, h, 625);
  return m;
}
} // namespace
Error::Error(ErrorCode code, std::size_t offset, const char *message)
    : std::runtime_error(std::string(message) + " at byte " +
                         std::to_string(offset)),
      code_(code), offset_(offset) {}

Replay Replay::read(std::span<const std::uint8_t> input,
                    const Inflater &inflate, const Limits &limits,
                    std::optional<Format> hint) {
  Reader r(input, inflate, limits);
  Replay result;
  // Identifiers themselves are stored as one uncompressed four-byte chunk.
  auto identifier = r.section(4, Format::Legacy);
  const Bytes classic{'r', 'e', 'R', 'S'}, modern{'s', 'e', 'R', 'S'};
  if (identifier == modern)
    result.format_ = Format::Modern121;
  else if (identifier == classic) {
    // The first header chunk distinguishes normal modern18 producers from DCL.
    // A caller can disambiguate an all-raw old-modern recording with a hint.
    result.format_ =
        input.size() - r.at >= 14 && zlibHeader(input.subspan(r.at + 12))
            ? Format::Modern18
            : Format::Legacy;
  } else
    r.fail(ErrorCode::Identifier, "Unknown replay identifier");
  if (hint) {
    if ((*hint == Format::Modern121) != (identifier == modern))
      r.fail(ErrorCode::Identifier,
             "Replay format hint conflicts with identifier");
    result.format_ = *hint;
  }
  std::optional<std::uint32_t> mainEnd;
  if (result.format_ == Format::Modern121)
    mainEnd = r.u32();
  result.header_ = r.section(633, result.format_);
  result.metadata_ = decodeMetadata(result.header_);
  auto commandSize = r.section(4, result.format_);
  result.commands_ = r.section(word(commandSize, 0), result.format_);
  auto mapSize = r.section(4, result.format_);
  result.map_ = r.section(word(mapSize, 0), result.format_);
  if (result.format_ != Format::Legacy)
    result.names_ = r.section(1152, result.format_);
  if (mainEnd && *mainEnd != r.at)
    r.fail(ErrorCode::Boundary, "Wrong replay main-section boundary");
  if (result.format_ == Format::Legacy)
    result.trailer_ = r.retain(input.size() - r.at);
  else {
    while (r.at < input.size()) {
      if (result.extensions_.size() >= limits.extensions)
        r.fail(ErrorCode::Limit, "Replay extension count limit");
      Extension e;
      copyField(e.tag, r.take(4), 0);
      auto size = r.u32();
      e.encoded = r.retain(size);
      result.extensions_.push_back(std::move(e));
    }
  }
  std::size_t at = 0;
  std::uint32_t previous = 0;
  while (at < result.commands_.size()) {
    auto fail = [&](ErrorCode code, const char *message) {
      throw Error(code, at, message);
    };
    if (result.blocks_.size() >= limits.frameBlocks)
      fail(ErrorCode::Limit, "Replay frame block count limit");
    if (result.commands_.size() - at < 5)
      fail(ErrorCode::Timeline, "Truncated replay frame header");
    auto frame = word(result.commands_, at);
    auto size = result.commands_[at + 4];
    if (size > result.commands_.size() - at - 5)
      fail(ErrorCode::Timeline, "Truncated replay frame payload");
    if (frame < previous || frame > result.metadata_.frames)
      fail(ErrorCode::Timeline,
           "Replay frame is out of order or past game end");
    result.blocks_.push_back({frame, at + 5, size});
    previous = frame;
    at += 5 + size;
  }
  return result;
}
std::span<const FrameBlock>
Replay::blocksAt(std::uint32_t frame) const noexcept {
  auto first = std::lower_bound(
      blocks_.begin(), blocks_.end(), frame,
      [](const FrameBlock &b, std::uint32_t f) { return b.frame < f; });
  auto last = std::upper_bound(
      first, blocks_.end(), frame,
      [](std::uint32_t f, const FrameBlock &b) { return f < b.frame; });
  return std::span(blocks_).subspan(std::size_t(first - blocks_.begin()),
                                    std::size_t(last - first));
}
std::span<const std::uint8_t> Replay::payload(const FrameBlock &block) const {
  if (block.payloadOffset > commands_.size() ||
      block.payloadSize > commands_.size() - block.payloadOffset)
    throw std::out_of_range("Replay frame payload outside command stream");
  return std::span(commands_).subspan(block.payloadOffset, block.payloadSize);
}
} // namespace mapeditor::simulation::replay
