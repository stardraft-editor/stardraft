// Direct terrain artwork reflection adapted from Map Studio (MIT); see NOTICE.md.
#include "stardraft/terrain_mirror.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace stardraft {
namespace {
using TilePixels = std::array<uint8_t, 3072>;
bool tilePixels(const Tileset& ts, uint16_t tile, bool mirror, TilePixels& pixels) {
    const int mega = ts.megatileFor(tile);
    if (mega < 0 || size_t(mega) >= ts.megatiles.size()) return false;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const int sx = mirror ? 31 - x : x;
            const auto mini = ts.megatiles[size_t(mega)][size_t(y / 8 * 4 + sx / 8)];
            if ((mini >> 1) >= ts.minitiles.size()) return false;
            const int mx = (mini & 1) ? 7 - sx % 8 : sx % 8;
            const auto color = ts.palette[ts.minitiles[mini >> 1][size_t(y % 8 * 8 + mx)]];
            const size_t p = size_t(y * 32 + x) * 3;
            pixels[p] = color.r, pixels[p + 1] = color.g, pixels[p + 2] = color.b;
        }
    return true;
}
uint64_t pixelHash(const TilePixels& pixels) {
    uint64_t h = 14695981039346656037ull;
    for (auto c : pixels) h = (h ^ c) * 1099511628211ull;
    return h;
}
std::array<uint8_t, 192> signature(const TilePixels& pixels) {
    std::array<uint8_t, 192> out{};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            for (int c = 0; c < 3; ++c) {
                unsigned sum = 0;
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx) sum += pixels[size_t(((y * 4 + sy) * 32 + x * 4 + sx) * 3 + c)];
                out[size_t((y * 8 + x) * 3 + c)] = uint8_t(sum / 16);
            }
    return out;
}
template <size_t N> int distance(const std::array<uint8_t, N>& a, const std::array<uint8_t, N>& b) {
    int score = 0;
    for (size_t i = 0; i < N; ++i) score += std::abs(int(a[i]) - b[i]);
    return score;
}
} // namespace

MirroredTile TileMirrorMatcher::match(uint16_t tile) {
    if (auto it = cache_.find(tile); it != cache_.end()) return it->second;
    TilePixels reflected;
    if (!tilePixels(ts_, tile, true, reflected)) return {tile, false, false};
    if (!ready_) {
        std::set<std::pair<int, bool>> seen;
        for (size_t group = 0; group < ts_.groups.size() && group < 4096; ++group)
            for (int sub = 0; sub < 16; ++sub) {
                const auto value = uint16_t(group * 16 + sub);
                if (value == 0xffff) continue; // clipboard transparency sentinel
                const auto& g = ts_.groups[group];
                const auto key = std::make_pair(ts_.megatileFor(value), g.type == 1);
                if (key.first <= 0 || !seen.insert(key).second) continue;
                TilePixels pixels;
                if (tilePixels(ts_, value, false, pixels)) candidates_.push_back({value, pixelHash(pixels), signature(pixels)});
            }
        ready_ = true;
    }
    const auto hash = pixelHash(reflected);
    const auto small = signature(reflected);
    std::vector<std::pair<int, uint16_t>> shortlist;
    for (const auto& c : candidates_) {
        // This is direct artwork reflection, not an ISOM or pathing operation.
        // Opposite cliff artwork can have different CV5/VF4 metadata. Requiring
        // mirrored flags excludes the correct caps and splits small formations.
        const auto* sourceGroup = ts_.groupFor(tile);
        const auto* candidateGroup = ts_.groupFor(c.tile);
        if (!sourceGroup || !candidateGroup || (sourceGroup->type != 1 && candidateGroup->type == 1)) continue;
        if (c.hash == hash) {
            TilePixels pixels;
            if (tilePixels(ts_, c.tile, false, pixels) && pixels == reflected) return cache_[tile] = {c.tile, true, true};
        }
        shortlist.push_back({distance(small, c.signature), c.tile});
    }
    const size_t keep = std::min<size_t>(24, shortlist.size());
    std::partial_sort(shortlist.begin(), shortlist.begin() + keep, shortlist.end());
    MirroredTile result{tile, false, false};
    int best = std::numeric_limits<int>::max();
    for (size_t i = 0; i < keep; ++i) {
        TilePixels pixels;
        if (!tilePixels(ts_, shortlist[i].second, false, pixels)) continue;
        const int score = distance(reflected, pixels);
        if (score < best) best = score, result = {shortlist[i].second, score == 0, true};
    }
    return cache_[tile] = result;
}
} // namespace stardraft
