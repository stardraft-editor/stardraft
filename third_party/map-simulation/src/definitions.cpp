// SPDX-License-Identifier: MIT
#include <map>
#include <mapeditor/simulation/definitions.h>
namespace mapeditor::simulation {
int Definitions::acquisitionRange(int type) const {
  const auto &body = units.at(type);
  const auto &armament = body.turret >= 0 && body.turret < int(units.size())
                             ? units[body.turret] : body;
  auto reach = [&](int index) {
    return index >= 0 && index < int(weapons.size())
               ? int(weapons[index].maximumRange / 32) : 0;
  };
  const std::array<int, 3> coverage{armament.acquisitionRange,
                                   reach(armament.groundWeapon),
                                   reach(armament.airWeapon)};
  return *std::max_element(coverage.begin(), coverage.end());
}
namespace {
std::uint32_t value(std::span<const std::uint8_t> bytes, std::size_t at,
                    int width) {
  if (at > bytes.size() || bytes.size() - at < std::size_t(width))
    throw std::runtime_error("Truncated graphics metadata");
  std::uint32_t result = 0;
  for (int i = 0; i < width; ++i)
    result |= std::uint32_t(bytes[at + i]) << (i * 8);
  return result;
}
} // namespace
OffsetTable::OffsetTable(std::span<const std::uint8_t> bytes) {
  auto count = value(bytes, 0, 4), entries = value(bytes, 4, 4);
  if (bytes.size() < 8 || count > (bytes.size() - 8) / 4 ||
      entries > bytes.size() / 2)
    throw std::runtime_error("Invalid graphics offset table");
  values_.resize(count);
  for (std::size_t frame = 0; frame < count; ++frame) {
    auto start = value(bytes, 8 + frame * 4, 4);
    if (start > bytes.size() || entries > (bytes.size() - start) / 2)
      throw std::runtime_error("Truncated graphics offsets");
    auto &points = values_[frame];
    points.reserve(entries);
    for (std::size_t i = 0; i < entries; ++i) {
      int x = bytes[start + i * 2], y = bytes[start + i * 2 + 1];
      points.push_back({x < 128 ? x : x - 256, y < 128 ? y : y - 256});
    }
  }
}
Point OffsetTable::at(int frame, int index, bool flip) const {
  auto point = values_.at(frame).at(index);
  if (flip)
    point.x = -point.x;
  return point;
}
Definitions::Definitions(const Read &read)
    : units(dat::units(read("arr/units.dat"))),
      weapons(dat::weapons(read("arr/weapons.dat"))),
      orders(dat::orders(read("arr/orders.dat"))),
      flingies(dat::flingies(read("arr/flingy.dat"))),
      sprites(dat::sprites(read("arr/sprites.dat"))),
      images(dat::images(read("arr/images.dat"))),
      program(read("scripts/iscript.bin")) {
  auto names = read("arr/images.tbl");
  auto count = value(names, 0, 2);
  if (count > (names.size() - 2) / 2)
    throw std::runtime_error("Invalid image filename table");
  auto filename = [&](unsigned index) {
    if (!index || index > count)
      throw std::out_of_range("Invalid graphics filename index");
    std::size_t at = value(names, index * 2, 2);
    std::string name = "unit/";
    for (;; ++at) {
      if (at >= names.size())
        throw std::runtime_error("Unterminated graphics filename");
      if (!names[at])
        return name;
      name.push_back(char(names[at]));
    }
  };
  std::map<unsigned, int> frameCounts;
  std::map<unsigned, std::shared_ptr<const OffsetTable>> loaded;
  // LO opcode indices put attack/damage/special first and shield last.
  constexpr int order[]{1, 2, 3, 4, 5, 0};
  for (std::size_t id = 0; id < images.size(); ++id) {
    const auto &image = images[id];
    if (image.grp) {
      auto found = frameCounts.find(image.grp);
      if (found == frameCounts.end()) {
        auto bytes = read(filename(image.grp));
        auto frames = value(bytes, 0, 2);
        if (bytes.size() < 6 || frames > (bytes.size() - 6) / 8)
          throw std::runtime_error("Invalid GRP frame index");
        found = frameCounts.emplace(image.grp, int(frames)).first;
      }
      frames_[id] = found->second;
    }
    for (int kind = 0; kind < 6; ++kind) {
      auto index = image.offsets[order[kind]];
      if (!index)
        continue;
      auto found = loaded.find(index);
      if (found == loaded.end())
        found =
            loaded
                .emplace(index,
                         std::make_shared<OffsetTable>(read(filename(index))))
                .first;
      offsets_[id][kind] = found->second;
    }
  }
}
Point Definitions::offset(int image, int frame, int kind, int index,
                          bool flip) const {
  const auto &table = offsets_.at(image).at(kind);
  if (!table)
    throw std::runtime_error("Missing image offset table");
  return table->at(frame, index, flip);
}
SpriteResources Definitions::resources() const {
  return {images, program, [this](int image) { return frameCount(image); },
          [this](int image, int frame, int kind, int index, bool flip) {
            return offset(image, frame, kind, index, flip);
          }};
}
} // namespace mapeditor::simulation
