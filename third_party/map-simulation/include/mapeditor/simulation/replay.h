// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace mapeditor::simulation::replay {
using Bytes = std::vector<std::uint8_t>;
// Container families do not identify the game's exact rules patch.
enum class Format { Legacy, Modern18, Modern121 };
enum class Compression { PkwareDcl, Zlib };
// Decode only the supplied input, write only inside output, and return the
// number of bytes written. Throw on invalid streams. Legacy DCL packet padding
// is allowed, matching the classic reference decoder; zlib streams must end
// exactly at the encoded boundary. Keeping codecs at this boundary leaves the
// simulation library independent of any particular compression dependency.
using Inflater = std::function<std::size_t(
    Compression, std::span<const std::uint8_t>, std::span<std::uint8_t>)>;
struct Limits {
  std::size_t inputBytes = 512 * 1024 * 1024;
  std::size_t sectionBytes = 128 * 1024 * 1024;
  // Includes retained opaque extension/trailer bytes as well as decoded data.
  std::size_t retainedBytes = 512 * 1024 * 1024;
  std::size_t frameBlocks = 4 * 1024 * 1024;
  std::size_t extensions = 1024;
};
enum class ErrorCode {
  Truncated,
  Limit,
  Identifier,
  ChunkCount,
  ChunkSize,
  MissingInflater,
  Compression,
  Checksum,
  Boundary,
  Timeline
};
class Error : public std::runtime_error {
public:
  Error(ErrorCode code, std::size_t offset, const char *message);
  ErrorCode code() const noexcept { return code_; }
  // Physical input offset, except frame block errors (Timeline and the block
  // count Limit) which index decoded commands.
  std::size_t offset() const noexcept { return offset_; }

private:
  ErrorCode code_;
  std::size_t offset_;
};
struct Player {
  std::uint32_t slot = 0;
  std::uint32_t id = 0;
  std::uint8_t controller = 0, race = 0, force = 0;
  // Original bytes: no assumption about text encoding or NUL termination.
  std::array<std::uint8_t, 25> name{};
};
struct GameOptions {
  std::uint8_t victory = 0, resources = 0, standardStats = 0, fog = 0,
               initialUnits = 0, fixedPositions = 0, restrictions = 0,
               allies = 0, teams = 0, cheats = 0, tournament = 0;
  std::uint32_t victoryValue = 0, startingMinerals = 0, startingGas = 0;
};
struct Metadata {
  std::uint8_t engine = 0;
  std::uint32_t frames = 0, seed = 0, flags = 0;
  std::uint16_t width = 0, height = 0, gameType = 0, gameSubtype = 0,
                tileset = 0;
  std::uint8_t activePlayers = 0, slots = 0, speed = 0, state = 0;
  std::array<std::uint8_t, 24> creator{};
  std::array<std::uint8_t, 25> title{};
  std::array<std::uint8_t, 32> mapName{};
  std::array<Player, 12> players{};
  std::array<std::uint32_t, 8> colors{};
  std::array<std::uint8_t, 8> createMelee{};
  GameOptions options;
};
struct FrameBlock {
  std::uint32_t frame = 0;
  std::size_t payloadOffset = 0;
  std::uint8_t payloadSize = 0;
};
struct Extension {
  std::array<std::uint8_t, 4> tag{};
  Bytes encoded;
};
class Replay {
public:
  static Replay read(std::span<const std::uint8_t> input,
                     const Inflater &inflate = {}, const Limits &limits = {},
                     std::optional<Format> hint = std::nullopt);
  Format format() const noexcept { return format_; }
  const Metadata &metadata() const noexcept { return metadata_; }
  std::span<const std::uint8_t> header() const noexcept { return header_; }
  std::span<const std::uint8_t> commands() const noexcept { return commands_; }
  // Scenario bytes are opaque here; protected CHK tails are not container
  // errors.
  std::span<const std::uint8_t> map() const noexcept { return map_; }
  std::span<const std::uint8_t> modernNames() const noexcept { return names_; }
  std::span<const std::uint8_t> trailer() const noexcept { return trailer_; }
  const std::vector<Extension> &extensions() const noexcept {
    return extensions_;
  }
  std::span<const FrameBlock> blocks() const noexcept { return blocks_; }
  std::span<const FrameBlock> blocksAt(std::uint32_t frame) const noexcept;
  std::span<const std::uint8_t> payload(const FrameBlock &block) const;

private:
  Format format_ = Format::Legacy;
  Metadata metadata_;
  Bytes header_, commands_, map_, names_, trailer_;
  std::vector<Extension> extensions_;
  std::vector<FrameBlock> blocks_;
};
} // namespace mapeditor::simulation::replay
