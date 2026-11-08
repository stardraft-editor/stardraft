// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace mapeditor::simulation {
struct ScenarioLimits {
  std::size_t bytes = 128 * 1024 * 1024;
  std::size_t chunks = 1024 * 1024;
  std::size_t terrainTiles = 256 * 256;
  std::size_t records = 1024 * 1024;
};
enum class ScenarioErrorCode { Limit, Truncated, Missing, Field, Terrain };
class ScenarioError : public std::runtime_error {
public:
  ScenarioError(ScenarioErrorCode, std::size_t offset, const char *message);
  ScenarioErrorCode code() const noexcept { return code_; }
  std::size_t offset() const noexcept { return offset_; }

private:
  ScenarioErrorCode code_;
  std::size_t offset_;
};
struct ScenarioChunk {
  std::array<std::uint8_t, 4> tag{};
  std::size_t offset = 0, size = 0;
  bool is(std::string_view name) const noexcept;
};
struct ScenarioTerrain {
  std::uint16_t version = 0, width = 0, height = 0;
  int tileset = 0;
  std::vector<std::uint16_t> tiles;
};
struct ScenarioUnit {
  std::array<std::uint8_t, 36> raw{};
  std::size_t offset = 0;
  std::uint32_t serial = 0, resources = 0, relatedSerial = 0;
  std::uint16_t x = 0, y = 0, type = 0, relation = 0, validState = 0,
                validProperties = 0, hangar = 0, state = 0;
  std::uint8_t owner = 0, health = 0, shields = 0, energy = 0;
};
struct ScenarioSprite {
  std::array<std::uint8_t, 10> raw{};
  std::size_t offset = 0;
  std::uint16_t type = 0, x = 0, y = 0, flags = 0;
  std::uint8_t owner = 0;
};
struct WorldMap;
// Construct native geometry from caller-owned terrain tables. Raw tile values
// remain intact; absent groups use the engine's group-zero geometry fallback.
std::shared_ptr<const WorldMap>
buildScenarioTerrain(const ScenarioTerrain &, std::span<const std::uint8_t> cv5,
                     std::span<const std::uint8_t> vf4);
// Read-only engine input, separate from the editor's strict, editable Document.
// Keeps physical chunk order, duplicates, unknown bytes and an oversized-chunk
// tail. A short chunk header is an error; an advertised payload beyond EOF ends
// scanning without discarding the original bytes.
class Scenario {
public:
  static Scenario read(std::span<const std::uint8_t>,
                       const ScenarioLimits & = {});
  std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }
  std::span<const ScenarioChunk> chunks() const noexcept { return chunks_; }
  std::span<const std::uint8_t> payload(const ScenarioChunk &) const;
  std::span<const std::uint8_t> tail() const noexcept;
  std::size_t tailOffset() const noexcept { return tail_; }
  // Last scalar chunks provide dimensions/version/era; repeated MTXM chunks
  // overlay byte prefixes, including partial words. No game assets are read.
  ScenarioTerrain terrain() const;
  // All repeated record chunks retain physical order. Partial records fail;
  // invalid IDs/flags are preserved for the match rules to interpret.
  std::vector<ScenarioUnit> units() const;
  std::vector<ScenarioSprite> sprites() const;

private:
  std::vector<std::uint8_t> bytes_;
  std::vector<ScenarioChunk> chunks_;
  std::size_t tail_ = 0, terrainLimit_ = 0;
  std::size_t recordLimit_ = 0;
};
} // namespace mapeditor::simulation
