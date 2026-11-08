// SPDX-License-Identifier: MIT
#include <algorithm>
#include <limits>
#include <mapeditor/simulation/scenario.h>
#include <mapeditor/simulation/world.h>
#include <string>

namespace mapeditor::simulation {
namespace {
std::uint16_t u16(std::span<const std::uint8_t> b, std::size_t at) {
  return std::uint16_t(b[at] | (unsigned(b[at + 1]) << 8));
}
std::uint32_t u32(std::span<const std::uint8_t> b, std::size_t at) {
  return u16(b, at) | (std::uint32_t(u16(b, at + 2)) << 16);
}
} // namespace
ScenarioError::ScenarioError(ScenarioErrorCode code, std::size_t offset,
                             const char *message)
    : std::runtime_error(std::string(message) + " at scenario byte " +
                         std::to_string(offset)),
      code_(code), offset_(offset) {}
bool ScenarioChunk::is(std::string_view name) const noexcept {
  return name.size() == tag.size() &&
         std::equal(tag.begin(), tag.end(), name.begin(),
                    [](std::uint8_t a, char b) {
                      return a == static_cast<std::uint8_t>(b);
                    });
}
Scenario Scenario::read(std::span<const std::uint8_t> input,
                        const ScenarioLimits &limits) {
  if (input.size() > limits.bytes)
    throw ScenarioError(ScenarioErrorCode::Limit, 0, "Scenario input limit");
  Scenario result;
  result.terrainLimit_ = limits.terrainTiles;
  result.recordLimit_ = limits.records;
  result.bytes_.assign(input.begin(), input.end());
  std::size_t at = 0;
  while (at < input.size()) {
    if (input.size() - at < 8)
      throw ScenarioError(ScenarioErrorCode::Truncated, at,
                          "Truncated scenario chunk header");
    auto size = u32(input, at + 4);
    if (size > input.size() - at - 8)
      break;
    if (result.chunks_.size() >= limits.chunks)
      throw ScenarioError(ScenarioErrorCode::Limit, at,
                          "Scenario chunk count limit");
    ScenarioChunk chunk;
    std::copy_n(input.begin() + at, 4, chunk.tag.begin());
    chunk.offset = at + 8;
    chunk.size = size;
    result.chunks_.push_back(chunk);
    at += 8 + std::size_t(size);
  }
  result.tail_ = at;
  return result;
}
std::span<const std::uint8_t>
Scenario::payload(const ScenarioChunk &chunk) const {
  if (chunk.offset > bytes_.size() || chunk.size > bytes_.size() - chunk.offset)
    throw std::out_of_range("Scenario chunk outside owned bytes");
  return std::span(bytes_).subspan(chunk.offset, chunk.size);
}
std::span<const std::uint8_t> Scenario::tail() const noexcept {
  return std::span(bytes_).subspan(tail_);
}
ScenarioTerrain Scenario::terrain() const {
  ScenarioTerrain result;
  bool version = false, dimensions = false, era = false, mtxm = false;
  for (const auto &chunk : chunks_) {
    auto data = payload(chunk);
    auto require = [&](std::size_t size) {
      if (data.size() < size)
        throw ScenarioError(ScenarioErrorCode::Field, chunk.offset,
                            "Short scenario terrain field");
    };
    if (chunk.is("VER ")) {
      require(2);
      result.version = u16(data, 0);
      version = true;
    } else if (chunk.is("DIM ")) {
      require(4);
      result.width = u16(data, 0);
      result.height = u16(data, 2);
      dimensions = true;
    } else if (chunk.is("ERA ")) {
      require(2);
      result.tileset = u16(data, 0) % 8;
      era = true;
    }
  }
  if (!version || !dimensions || !era)
    throw ScenarioError(ScenarioErrorCode::Missing, 0,
                        "Scenario terrain metadata is missing");
  if (!result.width || !result.height)
    throw ScenarioError(ScenarioErrorCode::Terrain, 0,
                        "Zero scenario dimensions");
  auto count = std::size_t(result.width) * result.height;
  if (count > terrainLimit_ ||
      count > std::numeric_limits<std::size_t>::max() / 2)
    throw ScenarioError(ScenarioErrorCode::Limit, 0,
                        "Scenario terrain tile limit");
  std::vector<std::uint8_t> raster(count * 2);
  for (const auto &chunk : chunks_) {
    if (!chunk.is("MTXM"))
      continue;
    mtxm = true;
    auto data = payload(chunk);
    // Observed engine behavior distinguishes a single dangling byte at the
    // raster boundary from a longer ignored suffix. Keep this compatibility
    // edge explicit rather than silently accepting the malformed partial word.
    if (data.size() == raster.size() + 1)
      throw ScenarioError(ScenarioErrorCode::Terrain,
                          chunk.offset + raster.size(),
                          "Single-byte MTXM overrun");
    std::copy_n(data.begin(), std::min(data.size(), raster.size()),
                raster.begin());
  }
  if (!mtxm)
    throw ScenarioError(ScenarioErrorCode::Missing, 0,
                        "Scenario MTXM is missing");
  result.tiles.resize(count);
  for (std::size_t i = 0; i < count; ++i)
    result.tiles[i] = u16(raster, i * 2);
  return result;
}
std::shared_ptr<const WorldMap>
buildScenarioTerrain(const ScenarioTerrain &scenario,
                     std::span<const std::uint8_t> cv5,
                     std::span<const std::uint8_t> vf4) {
  auto geometryTiles = scenario.tiles;
  for (auto &tile : geometryTiles)
    if (std::size_t((tile / 16) & 2047) >= cv5.size() / 52)
      tile = 0;
  return std::make_shared<const WorldMap>(
      Terrain(scenario.width, scenario.height, geometryTiles, cv5, vf4),
      scenario.tileset);
}
std::vector<ScenarioUnit> Scenario::units() const {
  std::vector<ScenarioUnit> result;
  for (const auto &chunk : chunks_) {
    if (!chunk.is("UNIT"))
      continue;
    auto data = payload(chunk);
    if (data.size() % 36)
      throw ScenarioError(ScenarioErrorCode::Field,
                          chunk.offset + data.size() / 36 * 36,
                          "Partial UNIT record");
    if (data.size() / 36 > recordLimit_ - result.size())
      throw ScenarioError(ScenarioErrorCode::Limit, chunk.offset,
                          "Scenario unit record limit");
    for (std::size_t at = 0; at < data.size(); at += 36) {
      auto bytes = data.subspan(at, 36);
      ScenarioUnit unit;
      std::copy(bytes.begin(), bytes.end(), unit.raw.begin());
      unit.offset = chunk.offset + at;
      unit.serial = u32(bytes, 0);
      unit.x = u16(bytes, 4);
      unit.y = u16(bytes, 6);
      unit.type = u16(bytes, 8);
      unit.relation = u16(bytes, 10);
      unit.validState = u16(bytes, 12);
      unit.validProperties = u16(bytes, 14);
      unit.owner = bytes[16];
      unit.health = bytes[17];
      unit.shields = bytes[18];
      unit.energy = bytes[19];
      unit.resources = u32(bytes, 20);
      unit.hangar = u16(bytes, 24);
      unit.state = u16(bytes, 26);
      unit.relatedSerial = u32(bytes, 32);
      result.push_back(unit);
    }
  }
  return result;
}
std::vector<ScenarioSprite> Scenario::sprites() const {
  std::vector<ScenarioSprite> result;
  for (const auto &chunk : chunks_) {
    if (!chunk.is("THG2"))
      continue;
    auto data = payload(chunk);
    if (data.size() % 10)
      throw ScenarioError(ScenarioErrorCode::Field,
                          chunk.offset + data.size() / 10 * 10,
                          "Partial THG2 record");
    if (data.size() / 10 > recordLimit_ - result.size())
      throw ScenarioError(ScenarioErrorCode::Limit, chunk.offset,
                          "Scenario sprite record limit");
    for (std::size_t at = 0; at < data.size(); at += 10) {
      auto bytes = data.subspan(at, 10);
      ScenarioSprite sprite;
      std::copy(bytes.begin(), bytes.end(), sprite.raw.begin());
      sprite.offset = chunk.offset + at;
      sprite.type = u16(bytes, 0);
      sprite.x = u16(bytes, 2);
      sprite.y = u16(bytes, 4);
      sprite.owner = bytes[6];
      sprite.flags = u16(bytes, 8);
      result.push_back(sprite);
    }
  }
  return result;
}
} // namespace mapeditor::simulation
