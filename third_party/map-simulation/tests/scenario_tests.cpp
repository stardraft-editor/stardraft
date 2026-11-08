// SPDX-License-Identifier: MIT
#include <algorithm>
#include <iostream>
#include <mapeditor/simulation/scenario.h>
#include <mapeditor/simulation/world.h>
#include <source_location>
#include <string>
using namespace mapeditor::simulation;
using Bytes = std::vector<std::uint8_t>;
namespace {
void check(bool value,
           std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("Scenario test failed at line " +
                             std::to_string(where.line()));
}
void u32(Bytes &b, std::uint32_t n) {
  for (int i = 0; i < 4; ++i)
    b.push_back(std::uint8_t(n >> (8 * i)));
}
void chunk(Bytes &b, std::string_view name, Bytes data) {
  check(name.size() == 4);
  b.insert(b.end(), name.begin(), name.end());
  u32(b, std::uint32_t(data.size()));
  b.insert(b.end(), data.begin(), data.end());
}
template <class F> void rejects(F f, ScenarioErrorCode code) {
  try {
    f();
  } catch (const ScenarioError &e) {
    check(e.code() == code);
    return;
  }
  throw std::runtime_error("Invalid scenario accepted");
}
Bytes base() {
  Bytes b;
  chunk(b, "VER ", {205, 0, 99});
  chunk(b, "ERA ", {0x3f, 0x12});
  chunk(b, "DIM ", {2, 0, 2, 0});
  return b;
}
} // namespace
int main() {
  try {
    // Independent sequential byte records exercise every field, unknown-byte
    // retention, repeated chunks, offsets, truncation and aggregate limits.
    Bytes recordBytes, unitBytes(36), spriteBytes(10);
    for (int i = 0; i < 36; ++i)
      unitBytes[i] = std::uint8_t(i + 1);
    for (int i = 0; i < 10; ++i)
      spriteBytes[i] = std::uint8_t(i + 41);
    chunk(recordBytes, "UNIT", unitBytes);
    chunk(recordBytes, "THG2", spriteBytes);
    chunk(recordBytes, "UNIT", unitBytes);
    chunk(recordBytes, "THG2", spriteBytes);
    auto records = Scenario::read(recordBytes);
    auto units = records.units();
    auto sprites = records.sprites();
    check(units.size() == 2 && units[0].offset == 8 && units[1].offset == 70 &&
          units[0].serial == 0x04030201 && units[0].x == 0x0605 &&
          units[0].y == 0x0807 && units[0].type == 0x0a09 &&
          units[0].relation == 0x0c0b && units[0].validState == 0x0e0d &&
          units[0].validProperties == 0x100f && units[0].owner == 17 &&
          units[0].health == 18 && units[0].shields == 19 &&
          units[0].energy == 20 && units[0].resources == 0x18171615 &&
          units[0].hangar == 0x1a19 && units[0].state == 0x1c1b &&
          units[0].relatedSerial == 0x24232221 &&
          std::ranges::equal(units[0].raw, unitBytes));
    check(sprites.size() == 2 && sprites[0].offset == 52 &&
          sprites[1].offset == 114 && sprites[0].type == 0x2a29 &&
          sprites[0].x == 0x2c2b && sprites[0].y == 0x2e2d &&
          sprites[0].owner == 47 && sprites[0].flags == 0x3231 &&
          std::ranges::equal(sprites[0].raw, spriteBytes));
    ScenarioLimits recordLimits;
    recordLimits.records = 1;
    auto limitedRecords = Scenario::read(recordBytes, recordLimits);
    rejects([&] { limitedRecords.units(); }, ScenarioErrorCode::Limit);
    rejects([&] { limitedRecords.sprites(); }, ScenarioErrorCode::Limit);
    for (int n = 1; n < 36; ++n) {
      Bytes partial;
      chunk(partial, "UNIT", Bytes(n));
      rejects([&] { Scenario::read(partial).units(); },
              ScenarioErrorCode::Field);
    }
    for (int n = 1; n < 10; ++n) {
      Bytes partial;
      chunk(partial, "THG2", Bytes(n));
      rejects([&] { Scenario::read(partial).sprites(); },
              ScenarioErrorCode::Field);
    }
    auto bytes = base();
    chunk(bytes, "MTXM", {0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0xf0, 0xde});
    chunk(bytes, "MTXM", {0x11, 0x22, 0x33});
    chunk(bytes, std::string_view("\xff\0\x80z", 4), {});
    auto original = bytes;
    auto scenario = Scenario::read(bytes);
    std::fill(bytes.begin(), bytes.end(), 0);
    check(std::ranges::equal(scenario.bytes(), original) &&
          scenario.tail().empty());
    check(scenario.chunks().size() == 6 &&
          scenario.chunks().back().tag[0] == 255 &&
          scenario.chunks().back().is(std::string_view("\xff\0\x80z", 4)) &&
          !scenario.chunks().back().is("BAD"));
    auto terrain = scenario.terrain();
    check(terrain.width == 2 && terrain.height == 2 && terrain.version == 205 &&
          terrain.tileset == 7 &&
          terrain.tiles ==
              std::vector<std::uint16_t>{0x2211, 0x5633, 0x9abc, 0xdef0});
    auto copied = scenario;
    scenario = {};
    check(copied.terrain().tiles == terrain.tiles);
    auto zero = base();
    chunk(zero, "MTXM", {});
    check(Scenario::read(zero).terrain().tiles ==
          std::vector<std::uint16_t>(4, 0));
    auto reordered = Bytes{};
    chunk(reordered, "DIM ", {8, 0, 8, 0});
    chunk(reordered, "MTXM", {1, 2, 3, 4, 5, 6, 7, 8});
    chunk(reordered, "VER ", {59, 0});
    chunk(reordered, "ERA ", {0, 0});
    chunk(reordered, "DIM ", {2, 0, 2, 0});
    chunk(reordered, "ERA ", {10, 0});
    auto t = Scenario::read(reordered).terrain();
    check(t.width == 2 && t.height == 2 && t.version == 59 && t.tileset == 2 &&
          t.tiles ==
              std::vector<std::uint16_t>{0x0201, 0x0403, 0x0605, 0x0807});
    auto padded = base();
    chunk(padded, "MTXM", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    check(Scenario::read(padded).terrain().tiles == t.tiles);
    auto oneByteOverrun = base();
    chunk(oneByteOverrun, "MTXM", Bytes(9));
    rejects([&] { Scenario::read(oneByteOverrun).terrain(); },
            ScenarioErrorCode::Terrain);
    auto prefix = original.size();
    original.insert(original.end(), {'J', 'U', 'N', 'K'});
    u32(original, 0xffffffff);
    chunk(original, "ERA ", {0, 0});
    auto protectedMap = Scenario::read(original);
    check(protectedMap.tailOffset() == prefix &&
          protectedMap.tail().size() == original.size() - prefix &&
          protectedMap.terrain().tileset == 7 &&
          std::ranges::equal(protectedMap.bytes(), original));
    for (int n = 1; n < 8; ++n) {
      auto shortHeader = zero;
      shortHeader.resize(shortHeader.size() + n, 42);
      rejects([&] { Scenario::read(shortHeader); },
              ScenarioErrorCode::Truncated);
    }
    for (std::string_view tag : {"VER ", "DIM ", "ERA "}) {
      auto shortField = zero;
      chunk(shortField, tag, {0});
      rejects([&] { Scenario::read(shortField).terrain(); },
              ScenarioErrorCode::Field);
    }
    rejects([&] { Scenario::read(base()).terrain(); },
            ScenarioErrorCode::Missing);
    rejects([&] { Scenario::read({}).terrain(); }, ScenarioErrorCode::Missing);
    auto noDimensions = base();
    chunk(noDimensions, "DIM ", {0, 0, 1, 0});
    chunk(noDimensions, "MTXM", {});
    rejects([&] { Scenario::read(noDimensions).terrain(); },
            ScenarioErrorCode::Terrain);
    ScenarioLimits limits;
    limits.bytes = zero.size() - 1;
    rejects([&] { Scenario::read(zero, limits); }, ScenarioErrorCode::Limit);
    limits = {};
    limits.chunks = 3;
    rejects([&] { Scenario::read(zero, limits); }, ScenarioErrorCode::Limit);
    limits = {};
    limits.terrainTiles = 3;
    rejects([&] { Scenario::read(zero, limits).terrain(); },
            ScenarioErrorCode::Limit);
    limits.terrainTiles = 4;
    check(Scenario::read(zero, limits).terrain().tiles.size() == 4);
    bool caught = false;
    try {
      copied.payload({{}, std::size_t(-1), 1});
    } catch (const std::out_of_range &) {
      caught = true;
    }
    check(caught);
    ScenarioTerrain geometry;
    geometry.width = geometry.height = 16;
    geometry.tileset = 2;
    geometry.tiles.assign(256, 16);
    geometry.tiles[0] = 0x7fff;
    Bytes cv5(104), vf4(64);
    cv5[52 + 20] = 1;
    for (int i = 0; i < 16; ++i)
      vf4[32 + i * 2] = 1;
    auto map = buildScenarioTerrain(geometry, cv5, vf4);
    check(geometry.tiles[0] == 0x7fff && map->tileset == 2 &&
          !map->terrain.walkable(0, 0) && map->terrain.walkable(4, 0) &&
          !map->regions.entries().empty() && !map->contours.edges()[0].empty());
    std::fill(cv5.begin(), cv5.end(), 0);
    std::fill(vf4.begin(), vf4.end(), 0);
    check(map->terrain.walkable(4, 0));
    std::cout << "PASS: bounded owned scenario chunks, protected tails, "
                 "duplicate terrain overlays and metadata\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
