// One mod terrain beside a game border that may be its source (a re-skin), piece by piece: a row for each rect of the
// terrain's template that has a mod piece: the mod's pair, the template's game pair of that rect, and the source
// border's pairs whose walkability is nearest the mod pair's (up to 3, the nearest first).
// The picture has no text; the .tsv has a line a row: template group, mod left, mod right, minitiles (of 32) whose
// walkability differs from the template pair's, then for each source pair its group and its differing minitiles.
// usage: cmbw_reskin_show <Cosmonarchy install> <era> <terrain name> <source era> <source ground> <source terrain> <out .png> <out .tsv>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/mods/terrain_rules.h"
#include "stardraft/previews.h"
#include "stardraft/sprites.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;
namespace tr = stardraft::terrainrules;

namespace {
// How many of a tile's 16 minitiles differ in walkability from another's (16: a tile is missing).
int differ(const Tileset& a, uint16_t ta, const Tileset& b, uint16_t tb) {
    const int ma = a.megatileFor(ta), mb = b.megatileFor(tb);
    if (ma <= 0 || mb <= 0 || size_t(ma) >= a.minitileFlags.size() || size_t(mb) >= b.minitileFlags.size()) return 16;
    int n = 0;
    for (size_t i = 0; i < 16; ++i) n += (a.minitileFlags[size_t(ma)][i] & 1) != (b.minitileFlags[size_t(mb)][i] & 1);
    return n;
}
void blit(Image& img, const Sprite& s, int x0, int y0) {
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x)
            if (x0 + x < img.width && y0 + y < img.height)
                for (int c = 0; c < 4; ++c) img.rgba[size_t(((y0 + y) * img.width + x0 + x) * 4 + c)] = s.rgba[size_t((y * s.width + x) * 4 + c)];
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 9) return std::fprintf(stderr, "usage: cmbw_reskin_show <install> <era> <terrain> <source era> <source ground> <source terrain> <out .png> <out .tsv>\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor ed(data), src(data);
    if (!ed.newMap(64, 64, uint16_t(std::atoi(argv[2])), 1, err) || !src.newMap(64, 64, uint16_t(std::atoi(argv[4])), 1, err))
        return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const Tileset& ts = ed.tileset();
    const Tileset& ss = src.tileset();
    const Tileset::ModTerrain* m = nullptr;
    for (const auto& t : ts.modTerrains)
        if (t.name == argv[3]) m = &t;
    if (!m) return std::fprintf(stderr, "no mod terrain named %s\n", argv[3]), 1;
    const auto [ground, terrain] = tr::templateOf(ts, *m);
    const std::vector<tr::TemplateRect> own = tr::templateRects(ts, ground, terrain);
    const std::vector<tr::TemplateRect> source = tr::templateRects(ss, uint16_t(std::atoi(argv[5])), uint16_t(std::atoi(argv[6])));
    std::printf("template: ground %u, terrain %u, %zu rects; source: %zu rects\n", unsigned(ground), unsigned(terrain), own.size(), source.size());
    struct Row {
        uint16_t group, l, r;
        int ownDiff;
        std::string role;
        std::vector<std::pair<int, const tr::TemplateRect*>> near;
    };
    std::vector<Row> rows;
    std::set<uint16_t> seen;
    for (const tr::TemplateRect& rect : own)
        for (const auto& p : m->pieces) {
            if (p[0] != rect.group || tr::nullHalf(p[1]) || tr::nullHalf(p[2]) || seen.count(p[0])) continue;
            seen.insert(p[0]);
            Row row{p[0], p[1], p[2], 0, rect.role, {}};
            row.ownDiff = differ(ts, p[1], ts, tr::gameTile(ts, p[0])) + differ(ts, p[2], ts, tr::gameTile(ts, uint32_t(p[0]) + 1));
            for (const tr::TemplateRect& s : source)
                row.near.push_back({differ(ts, p[1], ss, tr::gameTile(ss, s.group)) + differ(ts, p[2], ss, tr::gameTile(ss, uint32_t(s.group) + 1)), &s});
            std::stable_sort(row.near.begin(), row.near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            if (row.near.size() > 3) row.near.resize(3);
            rows.push_back(row);
        }
    Image img;
    img.width = 5 * 72, img.height = int(rows.size()) * 36;
    img.rgba.assign(size_t(img.width) * img.height * 4, 0);
    for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    FILE* f = std::fopen(argv[8], "w");
    int ownExact = 0, srcExact = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        const int y = int(i) * 36;
        blit(img, mapTilesPicture(ts, {r.l, r.r}, 2, 0, 0, 2, 1), 0, y);
        blit(img, mapTilesPicture(ts, {tr::gameTile(ts, r.group), tr::gameTile(ts, uint32_t(r.group) + 1)}, 2, 0, 0, 2, 1), 72, y);
        ownExact += r.ownDiff == 0, srcExact += !r.near.empty() && r.near[0].first == 0;
        if (f) std::fprintf(f, "%u\t%s\t%u\t%u\t%d", unsigned(r.group), r.role.c_str(), unsigned(r.l), unsigned(r.r), r.ownDiff);
        for (size_t k = 0; k < r.near.size(); ++k) {
            const tr::TemplateRect* s = r.near[k].second;
            blit(img, mapTilesPicture(ss, {tr::gameTile(ss, s->group), tr::gameTile(ss, uint32_t(s->group) + 1)}, 2, 0, 0, 2, 1), 144 + int(k) * 72, y);
            if (f) std::fprintf(f, "\t%u\t%s\t%d", unsigned(s->group), s->role.c_str(), r.near[k].first);
        }
        if (f) std::fprintf(f, "\n");
    }
    if (f) std::fclose(f);
    std::printf("%zu rects with a piece: the template's pair has the same walkability for %d, a source pair for %d\n", rows.size(), ownExact, srcExact);
    return writePng(argv[7], img) ? 0 : 1;
}
