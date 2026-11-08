// SPDX-License-Identifier: MIT
#include <algorithm>
#include <array>
#include <limits>
#include <mapeditor/simulation/regions.h>
#include <stdexcept>

namespace mapeditor::simulation {
namespace {
std::uint16_t word(std::span<const std::uint8_t> b, std::size_t p) {
  if (p > b.size() || b.size() - p < 2)
    throw std::runtime_error("Truncated terrain table");
  return std::uint16_t(b[p]) | std::uint16_t(b[p + 1]) << 8;
}
constexpr std::array<Point, 8> adjacent = {
    {{0, -1}, {-1, 0}, {1, 0}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}}};
constexpr std::array<Point, 4> cardinal = {{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
int area(RegionBounds b) { return (b.right - b.left) * (b.bottom - b.top); }
void uniqueAdd(std::vector<int> &to, int id) {
  if (std::find(to.begin(), to.end(), id) == to.end())
    to.push_back(id);
}
} // namespace
Terrain::Terrain(int width, int height, std::span<const std::uint16_t> tiles,
                 std::span<const std::uint8_t> cv5,
                 std::span<const std::uint8_t> vf4)
    : width_(width), height_(height) {
  if (width < 1 || height < 1 || width > 256 || height > 256 ||
      tiles.size() != std::size_t(width) * height || cv5.empty() ||
      cv5.size() % 52 || vf4.empty() || vf4.size() % 32)
    throw std::runtime_error("Invalid terrain dimensions or tables");
  tiles_.resize(tiles.size());
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      auto id = tiles[std::size_t(y) * width + x];
      auto group = std::size_t((id / 16) & 2047) * 52;
      auto mega = word(cv5, group + 20 + (id % 16) * 2) & 32767;
      TerrainTile tile;
      int walk = 0, middle = 0, high = 0, veryHigh = 0;
      tile.blocked = 0;
      for (int i = 0; i < 16; ++i) {
        auto flags = word(vf4, std::size_t(mega) * 32 + i * 2);
        tile.heights[i] = flags & 4 ? 2 : flags & 2 ? 1 : 0;
        walk += bool(flags & 1);
        middle += bool(flags & 2);
        high += bool(flags & 4);
        veryHigh += bool(flags & 8);
        if (!(flags & 1))
          tile.blocked |= std::uint16_t(1U << i);
      }
      tile.sightHeight = veryHigh              ? 3
                         : high >= 12          ? 2
                         : middle + high >= 12 ? 1
                                               : 0;
      tile.category = walk > 12 ? (high >= 12            ? 3
                                   : middle + high >= 12 ? 2
                                                         : 1)
                                : 0;
      tile.partial = walk > 0 && walk < 16;
      tile.buildable = walk == 16 && !(word(cv5, group + 2) & 128);
      tile.unbuildable = (word(cv5, group + 2) & 128) != 0;
      tile.creep = (word(cv5, group + 2) & 64) != 0;
      if (y == height - 1 || (y == height - 2 && (x < 5 || x >= width - 5))) {
        tile.category = 0;
        tile.partial = false;
        tile.buildable = false;
        tile.unbuildable = true;
        tile.creep = false;
        tile.blocked = 65535;
      }
      tiles_[std::size_t(y) * width + x] = tile;
    }
}
const TerrainTile &Terrain::tile(int x, int y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_)
    throw std::out_of_range("Terrain tile outside map");
  return tiles_[std::size_t(y) * width_ + x];
}
bool Terrain::walkable(int x, int y) const {
  if (x < 0 || y < 0 || x >= width_ * 4 || y >= height_ * 4)
    return false;
  return !(tile(x / 4, y / 4).blocked & (1U << ((y % 4) * 4 + x % 4)));
}

Regions::Regions(const Terrain &terrain)
    : width_(terrain.width()), height_(terrain.height()),
      tiles_(std::size_t(width_) * height_, -1) {
  auto index = [&](int x, int y) { return y * width_ + x; };
  auto inside = [&](int x, int y) {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
  };
  auto label = [&](int x, int y) {
    return inside(x, y) ? tiles_[index(x, y)] : -2;
  };
  auto available = [&](int x, int y, int category) {
    return inside(x, y) && label(x, y) == -1 &&
           terrain.tile(x, y).category == category;
  };
  std::vector<RegionBounds> growth;
  auto addRegion = [&](RegionBounds box, bool reserved) {
    int id = int(regions_.size());
    if (id >= 5000)
      throw std::runtime_error("Terrain has too many regions");
    Region r;
    r.category = terrain.tile(box.left, box.top).category;
    r.reserved = reserved;
    r.center = {((box.left + box.right) / 2) * 32 + 16,
                ((box.top + box.bottom) / 2) * 32 + 16};
    r.bounds = box;
    regions_.push_back(r);
    growth.push_back(box);
    for (int y = box.top; y < box.bottom; ++y)
      for (int x = box.left; x < box.right; ++x)
        tiles_[index(x, y)] = id;
    return id;
  };
  addRegion({0, height_ - 1, width_, height_}, true);
  auto seedAfter = [&](int start) {
    int count = width_ * height_;
    start %= count;
    for (int step = 0; step < count; ++step) {
      int candidate = (start + step) % count;
      if (tiles_[candidate] < 0)
        return candidate;
    }
    return -1;
  };
  auto seedBox = [&](int seed) {
    int x = seed % width_, y = seed / width_,
        category = terrain.tile(x, y).category;
    int maxWidth = std::min(8, width_ - x),
        maxHeight = std::min(7, height_ - y);
    int w = 1, h = 1;
    bool right = true, down = true, corner = true;
    while ((right || down) && w < maxWidth && h < maxHeight) {
      if (right)
        for (int j = 0; j < h; ++j)
          if (!available(x + w, y + j, category)) {
            right = false;
            break;
          }
      if (down)
        for (int i = 0; i < w; ++i)
          if (!available(x + i, y + h, category)) {
            down = false;
            break;
          }
      corner = corner && available(x + w, y + h, category);
      bool growY = down;
      bool growX = right && (corner || !down);
      h += growY;
      w += growX;
    }
    w = std::min(w, h * 3);
    h = std::min(h, w * 3);
    return RegionBounds{x, y, x + w, y + h};
  };
  auto grow = [&](int id) {
    auto &box = growth[id];
    box = {std::max(0, box.left - 1), std::max(0, box.top - 1),
           std::min(width_, box.right + 1), std::min(height_, box.bottom + 1)};
    for (int pass = 0; pass < 2; ++pass)
      for (int y = box.top; y < box.bottom; ++y)
        for (int x = box.left; x < box.right; ++x) {
          if (!available(x, y, regions_[id].category))
            continue;
          for (auto d : cardinal)
            if (label(x + d.x, y + d.y) == id) {
              tiles_[index(x, y)] = id;
              break;
            }
        }
  };
  int cursor = 0, previous = 56;
  bool expanded = false;
  for (;;) {
    int seed = seedAfter(cursor);
    if (seed < 0)
      break;
    auto best = seedBox(seed);
    if (area(best) < previous) {
      auto candidate = best;
      for (int attempt = 0; attempt < 25; ++attempt) {
        int next = seedAfter(candidate.top * width_ + candidate.right);
        if (next < 0)
          break;
        candidate = seedBox(next);
        if (area(candidate) > area(best)) {
          best = candidate;
          if (area(candidate) >= previous)
            break;
        }
      }
    }
    previous = area(best);
    cursor = best.top * width_ + best.right;
    int id = addRegion(best, false);
    grow(id);
    if (previous <= 6 && !expanded) {
      expanded = true;
      for (int n = 1; n < int(regions_.size()); ++n)
        grow(n);
    }
  }
  auto walkSquare = [&](int x, int y) {
    return terrain.walkable(x, y) && terrain.walkable(x + 1, y) &&
           terrain.walkable(x, y + 1) && terrain.walkable(x + 1, y + 1);
  };
  auto diagonalOpen = [&](int x, int y, Point d) {
    int cornerX = (x + (d.x > 0)) * 4, cornerY = (y + (d.y > 0)) * 4;
    bool nw = walkSquare(cornerX - 2, cornerY - 2),
         ne = walkSquare(cornerX, cornerY - 2),
         sw = walkSquare(cornerX - 2, cornerY),
         se = walkSquare(cornerX, cornerY);
    return d.x == d.y ? nw && se && (ne || sw) : ne && sw && (nw || se);
  };
  auto refresh = [&]() {
    for (auto &r : regions_) {
      r.tileCount = 0;
      r.bounds = {width_, height_, 0, 0};
      r.walking.clear();
      r.blocked.clear();
      r.group = r.reserved ? 16384 : 0;
    }
    for (int y = 0; y < height_; ++y)
      for (int x = 0; x < width_; ++x) {
        auto &r = regions_[label(x, y)];
        ++r.tileCount;
        r.bounds.left = std::min(r.bounds.left, x);
        r.bounds.right = std::max(r.bounds.right, x + 1);
        r.bounds.top = std::min(r.bounds.top, y);
        r.bounds.bottom = std::max(r.bounds.bottom, y + 1);
      }
    for (int id = 0; id < int(regions_.size()); ++id) {
      auto &r = regions_[id];
      auto b = r.bounds;
      for (int y = b.top; y < b.bottom; ++y)
        for (int x = b.left; x < b.right; ++x)
          if (label(x, y) == id)
            for (auto d : adjacent) {
              int neighbor = label(x + d.x, y + d.y);
              if (neighbor < 0 || neighbor == id)
                continue;
              auto &n = regions_[neighbor];
              if (d.x && d.y && r.walkable() && n.walkable() &&
                  !diagonalOpen(x, y, d))
                continue;
              uniqueAdd(n.walkable() ? r.walking : r.blocked, neighbor);
            }
      auto zero = std::find(r.blocked.begin(), r.blocked.end(), 0);
      if (zero != r.blocked.end() && zero + 1 != r.blocked.end())
        std::iter_swap(zero, r.blocked.end() - 1);
    }
    int group = 0;
    for (int first = 0; first < int(regions_.size()); ++first)
      if (!regions_[first].group && regions_[first].tileCount) {
        ++group;
        regions_[first].group = group;
        std::vector<int> stack{first};
        while (!stack.empty()) {
          int n = stack.back();
          stack.pop_back();
          auto &r = regions_[n];
          for (int neighbor : r.walkable() ? r.walking : r.blocked)
            if (!regions_[neighbor].group) {
              regions_[neighbor].group = group;
              stack.push_back(neighbor);
            }
        }
      }
  };
  refresh();
  for (int threshold = 6;; threshold += 2) {
    for (int id = int(regions_.size()) - 1; id >= 0; --id) {
      auto &r = regions_[id];
      if (!r.tileCount || r.tileCount >= threshold || r.reserved)
        continue;
      int chosen = -1;
      auto consider = [&](int n) {
        auto &candidate = regions_[n];
        if (candidate.tileCount && !candidate.reserved &&
            candidate.category == r.category &&
            (chosen < 0 || candidate.tileCount < regions_[chosen].tileCount))
          chosen = n;
      };
      for (int n : r.walking)
        consider(n);
      for (int n : r.blocked)
        consider(n);
      if (chosen < 0)
        continue;
      auto &into = regions_[chosen];
      auto b = r.bounds;
      for (int y = b.top; y < b.bottom; ++y)
        for (int x = b.left; x < b.right; ++x)
          if (label(x, y) == id)
            tiles_[index(x, y)] = chosen;
      into.tileCount += r.tileCount;
      r.tileCount = 0;
      into.bounds = {std::min(b.left, into.bounds.left),
                     std::min(b.top, into.bounds.top),
                     std::max(b.right, into.bounds.right),
                     std::max(b.bottom, into.bounds.bottom)};
    }
    if (std::count_if(regions_.begin(), regions_.end(),
                      [](const auto &r) { return r.tileCount > 0; }) < 2500)
      break;
    if (threshold > width_ * height_)
      throw std::runtime_error("Region compaction cannot converge");
  }
  std::vector<int> remap(regions_.size());
  std::vector<Region> compact;
  for (int id = 0; id < int(regions_.size()); ++id)
    if (regions_[id].tileCount) {
      remap[id] = int(compact.size());
      compact.push_back(std::move(regions_[id]));
    }
  for (auto &id : tiles_)
    id = remap[id];
  regions_ = std::move(compact);
  refresh();
  struct Split {
    bool active = false;
    int walk = -1, blocked = -1;
    std::uint16_t mask = 0;
  };
  std::vector<Split> splits(tiles_.size());
  auto edgeWalk = [&](int x, int y, int side, bool neighbor) {
    int mx = x * 4, my = y * 4, count = 0;
    for (int n = 0; n < (side < 4 ? 4 : 1); ++n) {
      int px = mx, py = my;
      switch (side) {
      case 0:
        px += n;
        py += neighbor ? -1 : 0;
        break;
      case 1:
        px += neighbor ? -1 : 0;
        py += n;
        break;
      case 2:
        px += neighbor ? 4 : 3;
        py += n;
        break;
      case 3:
        px += n;
        py += neighbor ? 4 : 3;
        break;
      case 4:
        px += neighbor ? -1 : 0;
        py += neighbor ? -1 : 0;
        break;
      case 5:
        px += neighbor ? 4 : 3;
        py += neighbor ? -1 : 0;
        break;
      case 6:
        px += neighbor ? -1 : 0;
        py += neighbor ? 4 : 3;
        break;
      case 7:
        px += neighbor ? 4 : 3;
        py += neighbor ? 4 : 3;
        break;
      }
      count += terrain.walkable(px, py);
    }
    return count;
  };
  for (int y = 0; y < height_; ++y)
    for (int x = 0; x < width_; ++x) {
      int at = index(x, y), base = tiles_[at];
      auto t = terrain.tile(x, y);
      if (!t.partial)
        continue;
      bool baseWalk = regions_[base].walkable();
      int foreign = -1, best = 0;
      for (int side = baseWalk ? 7 : 3; side >= 0; --side) {
        int weight = edgeWalk(x, y, side, false);
        if (baseWalk)
          weight = (side < 4 ? 4 : 1) - weight;
        if (weight <= best)
          continue;
        auto d = adjacent[side];
        int nx = x + d.x, ny = y + d.y;
        if (!inside(nx, ny))
          continue;
        int ni = index(nx, ny), neighbor = tiles_[ni];
        auto split = splits[ni];
        if (!split.active) {
          if (neighbor == base || regions_[neighbor].walkable() == baseWalk)
            continue;
        } else {
          auto contact = edgeWalk(x, y, side, true);
          if (baseWalk ? contact == (side < 4 ? 4 : 1) : contact == 0)
            continue;
          neighbor = baseWalk ? split.blocked : split.walk;
        }
        best = weight;
        foreign = neighbor;
      }
      Split split;
      split.active = true;
      split.mask = t.blocked;
      if (foreign < 0 || foreign == base) {
        foreign = base;
        split.mask = baseWalk ? 0 : 65535;
      } else if (!baseWalk && !regions_[foreign].walkable())
        split.mask = 65535;
      split.walk = baseWalk ? base : foreign;
      split.blocked = baseWalk ? foreign : base;
      splits[at] = split;
    }
  preferred_ = tiles_;
  for (std::size_t i = 0; i < splits.size(); ++i)
    if (splits[i].active)
      preferred_[i] = splits[i].walk;
  cells_.resize(std::size_t(width_) * 4 * height_ * 4);
  for (int y = 0; y < height_ * 4; ++y)
    for (int x = 0; x < width_ * 4; ++x) {
      int at = index(x / 4, y / 4), id = tiles_[at];
      auto s = splits[at];
      if (s.active)
        id = s.mask & (1U << ((y % 4) * 4 + x % 4)) ? s.blocked : s.walk;
      cells_[std::size_t(y) * width_ * 4 + x] = id;
    }
}
int Regions::regionAt(Point p) const {
  if (p.x < 0 || p.y < 0 || p.x >= width_ * 32 || p.y >= height_ * 32)
    return -1;
  return cells_[std::size_t(p.y / 8) * width_ * 4 + p.x / 8];
}
int Regions::regionAtPreferWalkable(Point p) const {
  if (p.x < 0 || p.y < 0 || p.x >= width_ * 32 || p.y >= height_ * 32)
    return -1;
  return preferred_[std::size_t(p.y / 32) * width_ + p.x / 32];
}
} // namespace mapeditor::simulation
