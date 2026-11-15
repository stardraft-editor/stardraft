// The gates and the repair search for the mod's isometric rules, from the mod's maps (see cmbw_neighbor_check for the
// idea: the neighbouring tiles of a mod terrain on the maps, read through the rules as (rect, half) slots, must be
// pairs that the terrain's template border makes).
//   gates:  cmbw_neighbor_repair gates <Cosmonarchy install> <maps folder> <out .tsv> [rules .isom | -] [era]
//           a line a terrain: tileset, name, pairs on the maps, maps, legal pairs, and of the engine's painting (5
//           strokes on its ground): NULL tiles, tiles changed, pairs between the terrain's tiles, those the maps have
//   repair: cmbw_neighbor_repair repair <Cosmonarchy install> <maps folder> <out folder> [allow list | -] [era]
//           the border tiles of a terrain change slots where that makes more of the maps' pairs legal: two tiles swap, or
//           a tile joins another slot while its own keeps a tile (no move empties a slot). Fill tiles and the game's
//           tiles are fixed. Writes repaired.isom (with an allow list of "tileset<tab>name" lines: only those terrains
//           change) and moves.txt.
//   paint:  cmbw_neighbor_repair paint <Cosmonarchy install> <list: n<tab>tileset<tab>name> <out folder> <suffix> [rules .isom | -]
//           the engine's painting of each listed terrain: <n>-<suffix>.png
// Nothing is applied to the program: a rules file is for looking and measuring.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/map_file.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/mods/terrain_rules.h"
#include "stardraft/previews.h"
#include "stardraft/sprites.h"
#include "stardraft/terrain.h"
#include "stardraft/terrain_stamp.h"
#include "stardraft/tileset.h"

namespace fs = std::filesystem;
using namespace stardraft;
namespace tr = stardraft::terrainrules;

namespace {

const char* kEraName[8] = {"Badlands", "Platform", "Install", "AshWorld", "Jungle", "Desert", "Ice", "Twilight"};
const std::array<unsigned, 8> kStock = {1665, 1513, 1265, 1262, 1578, 1520, 1415, 1494};
const int kCw = 42, kCh = 28;

struct MapTiles {
    std::string name;
    int w = 0, h = 0;
    std::vector<uint16_t> tiles;
    bool stamp = false;  // one of the mod's brushes (a small block of tiles), not a map
};
struct Legal {
    std::unordered_set<uint32_t> pairs[2];
    std::unordered_set<uint32_t> slots;  // the slots the border's painting has (its rects, its two terrains' own)
};
uint32_t slot(uint16_t rect, int half) { return uint32_t(rect) << 1 | uint32_t(half); }
uint32_t pairKey(uint32_t a, uint32_t b) { return a << 13 | b; }
int eraOf(const std::string& name) {
    for (int k = 0; k < 8; ++k)
        if (name == kEraName[k]) return k;
    return 0;
}

Legal legalOf(GameData& data, int era, uint16_t ground, uint16_t terrain) {
    Legal out;
    std::string err;
    Editor ed(data);
    if (!ed.newMap(192, 192, uint16_t(era), 1, err)) return out;
    const int c = 96 * 32;
    ed.paintBrush(c, c, ground, Brush{Brush::Diamond, 30});
    std::mt19937 rng(uint32_t(era * 1000 + ground * 40 + terrain));
    for (int i = 0; i < 300; ++i) {
        const int x = c + int(rng() % 2200) - 1100, y = c + int(rng() % 1100) - 550;
        ed.paintBrush(x, y, i % 3 == 2 ? ground : terrain, Brush{Brush::Diamond, float(1 + rng() % 4)});
    }
    const TerrainLayer t = readTerrain(ed.chk(), "TILE");
    const int w = t.width, h = t.height;
    auto s = [&](size_t i) {
        const uint16_t g = uint16_t(t.tiles[i] >> 4);
        return slot(uint16_t(g & ~1u), g & 1);
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * w + x;
            out.slots.insert(s(i));
            if (x + 1 < w) out.pairs[0].insert(pairKey(s(i), s(i + 1)));
            if (y + 1 < h) out.pairs[1].insert(pairKey(s(i), s(i + size_t(w))));
        }
    return out;
}

std::set<uint16_t> edgeRectsOf(const Tileset& ts, const Tileset::ModTerrain& m) {
    std::set<uint16_t> out;
    const auto tpl = tr::templateOf(ts, m);
    for (const tr::TemplateRect& r : tr::templateRects(ts, tpl.first, tpl.second)) out.insert(r.group);
    return out;
}

// The engine's painting of a terrain (5 strokes on its ground): the tiles before and after the strokes.
bool paintLayers(GameData& data, int era, const std::string& name, TerrainLayer& before, TerrainLayer& after, Editor** keep = nullptr) {
    std::string err;
    static std::unique_ptr<Editor> ed;
    ed = std::make_unique<Editor>(data);
    if (!ed->newMap(128, 128, uint16_t(era), 1, err)) return false;
    const Tileset::ModTerrain* m = nullptr;
    for (const auto& t : ed->tileset().modTerrains)
        if (t.name == name) m = &t;
    if (!m) return false;
    const int cx = 2048, cy = 2048;
    ed->paintBrush(cx, cy, m->ground, Brush{Brush::Diamond, 16});
    before = readTerrain(ed->chk(), "TILE");
    const int strokes[5][3] = {{0, 0, 6}, {320, 96, 4}, {-448, -160, 1}, {-448, 160, 2}, {0, -288, 3}};
    for (const auto& s : strokes) ed->paintBrush(cx + s[0], cy + s[1], m->id, Brush{Brush::Diamond, float(s[2])});
    after = readTerrain(ed->chk(), "TILE");
    if (keep) *keep = ed.get();
    return before.tiles.size() == after.tiles.size();
}

// (only: a rect counts only when all its 6 points are `terrain` or `only` (0: any rect of the group))
std::string geomKey(const Tileset& ts, uint16_t terrain, uint16_t group, uint16_t only = 0) {
    const IsomTiles* tiles = IsomTiles::forTileset(ts);
    const IsomTerrainTable* table = IsomTerrainTable::forEra(ts.era);
    if (!tiles || !table) return {};
    std::vector<std::string> parts;
    for (const IsomTiles::Rect& rc : tiles->rectsOf(group)) {
        const auto L = table->corners(rc.left), R = table->corners(rc.right);
        const uint16_t pts[6] = {L[0], L[1], L[2], L[3], rc.parity ? R[1] : R[2], rc.parity ? R[2] : R[3]};
        bool other = false;
        for (int i = 0; i < 6; ++i) other = other || (only && pts[i] != terrain && pts[i] != only);
        if (other) continue;
        std::string k = std::to_string(rc.parity) + ":";
        for (int i = 0; i < 6; ++i) k += pts[i] == terrain ? '1' : '0';
        parts.push_back(k);
    }
    std::sort(parts.begin(), parts.end());
    parts.erase(std::unique(parts.begin(), parts.end()), parts.end());
    std::string out;
    for (const std::string& x : parts) out += (out.empty() ? "" : ",") + x;
    return out;
}

struct Node {
    uint16_t tile = 0;
    bool fixed = false;  // a fill tile or a game tile
    std::vector<uint32_t> slots;
    std::vector<std::array<int, 4>> links;  // {other node, dir, 1 when this node is the first (left / upper), uses}
    long long uses = 0;
};
// A terrain's tiles on the maps: the nodes (its own first), the pairs, the legal pairs of its template.
struct Study {
    std::vector<Node> nodes;
    size_t own = 0;
    std::map<std::array<int, 3>, int> pairs;  // {dir, node a, node b} -> uses
    std::set<uint16_t> edges;
    const Legal* legal = nullptr;
    long long uses = 0;
    int maps = 0;
    bool ok(const std::vector<uint32_t>& first, const std::vector<uint32_t>& second, int dir) const {
        for (uint32_t a : first)
            for (uint32_t b : second)
                if (legal->pairs[dir].count(pairKey(a, b))) return true;
        return false;
    }
    long long total() const {
        long long s = 0;
        for (const auto& [k, n] : pairs) s += ok(nodes[size_t(k[1])].slots, nodes[size_t(k[2])].slots, k[0]) ? n : 0;
        return s;
    }
    // The legal uses over the links of the nodes in `set` (each pair once).
    long long legalAround(const std::vector<int>& set) const {
        long long s = 0;
        for (size_t i = 0; i < set.size(); ++i)
            for (const auto& l : nodes[size_t(set[i])].links) {
                bool earlier = false;  // (the link to a node earlier in the set was counted from that node)
                for (size_t j = 0; j < i; ++j) earlier = earlier || set[j] == l[0];
                if (earlier || (l[0] == set[i] && !l[2])) continue;
                const auto& mine = nodes[size_t(set[i])].slots;
                const auto& other = nodes[size_t(l[0])].slots;
                s += (l[2] ? ok(mine, other, l[1]) : ok(other, mine, l[1])) ? l[3] : 0;
            }
        return s;
    }
};

bool study(const Tileset& ts, int e, const Tileset::ModTerrain& m, const std::vector<MapTiles>& maps, const Legal& legal, Study& s) {
    const auto tpl = tr::templateOf(ts, m);
    s.edges = edgeRectsOf(ts, m), s.legal = &legal;
    const bool gameGround = m.ground == tpl.first && m.groundTemplate == 0;
    std::vector<int> nodeOf(65536, -1);
    auto node = [&](uint16_t v) -> Node& {
        if (nodeOf[v] < 0) nodeOf[v] = int(s.nodes.size()), s.nodes.push_back({v, false, {}, {}, 0});
        return s.nodes[size_t(nodeOf[v])];
    };
    auto addSlot = [](Node& n, uint32_t k) {
        if (std::find(n.slots.begin(), n.slots.end(), k) == n.slots.end()) n.slots.push_back(k);
    };
    for (const auto& p : m.pieces)
        for (int half = 0; half < 2; ++half) {
            const uint16_t v = p[size_t(1 + half)];
            if (tr::nullHalf(v) || unsigned(v >> 4) < kStock[size_t(e)]) continue;
            Node& n = node(v);
            if (!s.edges.count(p[0])) n.fixed = true, addSlot(n, slot(p[0], 0)), addSlot(n, slot(p[0], 1));
            else addSlot(n, slot(p[0], half));
        }
    s.own = s.nodes.size();
    if (!s.own) return false;
    for (const MapTiles& mp : maps) {
        const int w = mp.w, h = mp.h;
        bool any = false;
        auto idx = [&](uint16_t v) {
            if (nodeOf[v] >= 0) return nodeOf[v];
            if (!gameGround || unsigned(v >> 4) >= kStock[size_t(e)] || (v >> 4) == 0) return -1;
            const uint16_t g = uint16_t(v >> 4);
            if (!legal.slots.count(slot(uint16_t(g & ~1u), g & 1))) return -1;  // (a game tile of another terrain: no evidence)
            Node& n = node(v);
            n.fixed = true, n.slots = {slot(uint16_t(g & ~1u), g & 1)};
            return nodeOf[v];
        };
        auto add = [&](size_t i, size_t j, int dir) {
            const bool ma = nodeOf[mp.tiles[i]] >= 0 && size_t(nodeOf[mp.tiles[i]]) < s.own;
            const bool mb = nodeOf[mp.tiles[j]] >= 0 && size_t(nodeOf[mp.tiles[j]]) < s.own;
            if (!ma && !mb) return;
            const int a = idx(mp.tiles[i]), b = idx(mp.tiles[j]);
            if (a < 0 || b < 0) return;
            ++s.pairs[{dir, a, b}], any = true;
        };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t i = size_t(y) * w + x;
                if (x + 1 < w) add(i, i + 1, 0);
                if (y + 1 < h) add(i, i + size_t(w), 1);
            }
        s.maps += any;
    }
    for (const auto& [k, n] : s.pairs) {
        s.nodes[size_t(k[1])].links.push_back({k[2], k[0], 1, n}), s.nodes[size_t(k[1])].uses += n;
        if (k[1] != k[2]) s.nodes[size_t(k[2])].links.push_back({k[1], k[0], 0, n}), s.nodes[size_t(k[2])].uses += n;
        s.uses += n;
    }
    return true;
}

std::array<std::vector<MapTiles>, 8> readMaps(const char* folder, int only) {
    std::array<std::vector<MapTiles>, 8> maps;
    std::error_code ec;
    std::string err;
    for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        if (p.extension() != ".scx" && p.extension() != ".scm") continue;
        Bytes raw;
        if (!readMapFile(p, raw, err)) continue;
        const Chk chk = Chk::parse(raw);
        const Section* es = chk.find("ERA ");
        if (!es || es->data.size() < 2) continue;
        const int e = (es->data[0] | es->data[1] << 8) & 7;
        if (only >= 0 && e != only) continue;
        const TerrainLayer t = readTerrain(chk, chk.has("TILE") ? "TILE" : "MTXM");
        if (t.width == 0 || t.height == 0 || t.tiles.size() < size_t(t.width) * t.height) continue;
        maps[size_t(e)].push_back({p.stem().string(), t.width, t.height, t.tiles});
    }
    return maps;
}


// The seam difference of two tiles' touching edges (the mean difference of the pixels there, divided by the tiles' own
// roughness at those edges), with the tiles' edges kept.
struct SeamScorer {
    const Tileset& ts;
    struct Prof {
        uint8_t e[4][96] = {};  // left column, right column, top row, bottom row
        float rough[4] = {};
    };
    std::map<int, Prof> profs;
    explicit SeamScorer(const Tileset& t) : ts(t) {}
    const Prof& prof(uint16_t tile) {
        const int mega = ts.megatileFor(tile);
        auto it = profs.find(mega);
        if (it != profs.end()) return it->second;
        Prof& pr = profs[mega];
        const Sprite sp = mapTilesPicture(ts, {tile}, 1, 0, 0, 1, 1);
        if (sp.width != 32 || sp.height != 32) return pr;
        auto px = [&](int x, int y, int c) { return sp.rgba[size_t((y * 32 + x) * 4 + c)]; };
        double r[4] = {};
        for (int i = 0; i < 32; ++i)
            for (int c = 0; c < 3; ++c) {
                pr.e[0][i * 3 + c] = px(0, i, c), pr.e[1][i * 3 + c] = px(31, i, c);
                pr.e[2][i * 3 + c] = px(i, 0, c), pr.e[3][i * 3 + c] = px(i, 31, c);
                r[0] += std::abs(px(0, i, c) - px(1, i, c)), r[1] += std::abs(px(31, i, c) - px(30, i, c));
                r[2] += std::abs(px(i, 0, c) - px(i, 1, c)), r[3] += std::abs(px(i, 31, c) - px(i, 30, c));
            }
        for (int k = 0; k < 4; ++k) pr.rough[k] = float(r[k] / 96.0);
        return pr;
    }
    // (dir 0: a left of b; 1: a over b)
    float seam(uint16_t a, uint16_t b, int dir) {
        const Prof& pa = prof(a);
        const Prof& pb = prof(b);
        const int ea = dir ? 3 : 1, eb = dir ? 2 : 0;
        double d = 0;
        for (int i = 0; i < 96; ++i) d += std::abs(int(pa.e[ea][i]) - int(pb.e[eb][i]));
        return float(d / 96.0) / (2.0f + 0.5f * (pa.rough[ea] + pb.rough[eb]));
    }
    // A tile among its four neighbours (0: none): the mean seam, or -1 with fewer than 2 neighbours.
    float among(uint16_t tile, const uint16_t n[4]) {
        float sum = 0;
        int sides = 0;
        if (n[0] >> 4) sum += seam(n[0], tile, 0), ++sides;
        if (n[1] >> 4) sum += seam(tile, n[1], 0), ++sides;
        if (n[2] >> 4) sum += seam(n[2], tile, 1), ++sides;
        if (n[3] >> 4) sum += seam(tile, n[3], 1), ++sides;
        return sides >= 2 ? sum / float(sides) : -1.0f;
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) return std::fprintf(stderr, "usage: cmbw_neighbor_repair gates|repair|paint <install> ... (see the source)\n"), 2;
    const std::string mode = argv[1];
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    if (const char* r = std::getenv("RULES")) cmbw::useTerrainOverride(r);  // (a rules file over the program's, for any mode)

    if (mode == "paint") {
        if (argc < 6) return 2;
        if (argc > 6 && std::string(argv[6]) != "-") cmbw::useTerrainOverride(argv[6]);
        std::ifstream in(argv[3]);
        std::string line;
        int done = 0;
        while (std::getline(in, line)) {
            std::stringstream ss(line);
            std::string n, era, name;
            std::getline(ss, n, '\t'), std::getline(ss, era, '\t'), std::getline(ss, name, '\t');
            if (const char* rich = std::getenv("PAINT_RICH"); rich && std::atoi(rich)) {
                const int e = eraOf(era);
                Editor red(data);
                const Tileset::ModTerrain* pm = nullptr;
                const bool game = name.rfind("game:", 0) == 0;  // (game:<ground>:<terrain>: a game border itself, isometric terrain ids)
                if (!red.newMap(192, 192, uint16_t(e), 1, err)) continue;
                if (!game)
                    for (const auto& t : red.tileset().modTerrains)
                        if (t.name == name) pm = &t;
                if (!pm && !game) continue;
                const int c = 96 * 32;
                uint16_t ground = 0, id = 0;
                if (game) {
                    const size_t b = name.find(':', 5);
                    ground = uint16_t(std::atoi(name.c_str() + 5)), id = uint16_t(std::atoi(name.c_str() + b + 1));
                } else {
                    ground = pm->ground, id = pm->id;
                    // (PAINT_TEMPLATE=1: the same strokes with the terrain's template, the game border, to compare)
                    if (const char* t = std::getenv("PAINT_TEMPLATE"); t && std::atoi(t)) {
                        const auto tpl = tr::templateOf(red.tileset(), *pm);
                        ground = tpl.first, id = tpl.second;
                    }
                }
                red.paintBrush(c, c, ground, Brush{Brush::Diamond, 30});
                const TerrainLayer rb = readTerrain(red.chk(), "TILE");
                // (PAINT_SEED: the same strokes for every terrain, in any tileset)
                std::mt19937 rng(std::getenv("PAINT_SEED") ? uint32_t(std::atoi(std::getenv("PAINT_SEED"))) : uint32_t(e * 977 + (pm ? pm->id : id)));
                for (int i = 0; i < 160; ++i) {
                    const int x = c + int(rng() % 2200) - 1100, y = c + int(rng() % 1100) - 550;
                    // (PAINT_THIN=1: brushes of 0.5 and 1 only, the thin lines and single spots)
                    const bool thin = std::getenv("PAINT_THIN") && std::atoi(std::getenv("PAINT_THIN"));
                    red.paintBrush(x, y, i % 3 == 2 ? ground : id, Brush{Brush::Diamond, thin ? 0.5f * float(1 + rng() % 2) : float(1 + rng() % 4)});
                }
                const TerrainLayer ra = readTerrain(red.chk(), "TILE");
                int nulls = 0;
                for (size_t i = 0; i < ra.tiles.size() && i < rb.tiles.size(); ++i) nulls += ra.tiles[i] != rb.tiles[i] && (ra.tiles[i] >> 4) == 0;
                TerrainLayer part;
                const int pw = 84, ph = 48;
                part.width = uint16_t(pw), part.height = uint16_t(ph);
                for (int y = 0; y < ph; ++y)
                    for (int x = 0; x < pw; ++x) part.tiles.push_back(ra.tiles[size_t(96 - ph / 2 + y) * ra.width + 96 - pw / 2 + x]);
                // (the picture's tile values beside it: a line a row)
                if (FILE* tf = std::fopen((std::string(argv[4]) + "/" + n + "-" + argv[5] + ".txt").c_str(), "w")) {
                    for (int y = 0; y < ph; ++y)
                        for (int x = 0; x < pw; ++x) std::fprintf(tf, "%u%c", unsigned(part.tiles[size_t(y) * pw + x]), x + 1 < pw ? ' ' : '\n');
                    std::fclose(tf);
                }
                std::printf("%s\t%s\t%s\tNULL tiles %d\n", n.c_str(), era.c_str(), name.c_str(), nulls);
                done += writePng((std::string(argv[4]) + "/" + n + "-" + argv[5] + ".png").c_str(), renderTerrain(red.tileset(), part));
                continue;
            }
            TerrainLayer before, after;
            Editor* ed = nullptr;
            if (!paintLayers(data, eraOf(era), name, before, after, &ed)) continue;
            TerrainLayer part;
            part.width = uint16_t(kCw), part.height = uint16_t(kCh);
            for (int y = 0; y < kCh; ++y)
                for (int x = 0; x < kCw; ++x) part.tiles.push_back(after.tiles[size_t(64 - 15 + y) * after.width + 64 - 20 + x]);
            done += writePng((std::string(argv[4]) + "/" + n + "-" + argv[5] + ".png").c_str(), renderTerrain(ed->tileset(), part));
        }
        return std::printf("painted %d\n", done), 0;
    }

    // look: cmbw_neighbor_repair look <install> <list: n<tab>tileset<tab>name> <out folder> <suffix> [rules .isom | -]
    // The picture check: the 160-stroke painting of each listed terrain against the same strokes with its template (the
    // game's own border). Each 8x8 block of both pictures is ground (0), terrain (1), border art (2: far from both) or
    // NULL (3), by the nearest block of a painting of only the ground or only the terrain, in the same tileset. A line a
    // terrain: the share of the blocks that either painting changed where both agree, the blocks where the game has
    // terrain and the mod has ground ("missing"), the other way ("extra"), the border art blocks of both, NULL blocks,
    // the mean seam of the changed tiles (mod, game). <n>-<suffix>-look.png: per block, white both ground, grey both
    // terrain, dark grey both border art, red game terrain / mod not, blue mod terrain / game not, orange border art in
    // one only, black NULL.
    if (mode == "look") {
        if (argc < 6) return 2;
        if (argc > 6 && std::string(argv[6]) != "-") cmbw::useTerrainOverride(argv[6]);
        std::ifstream in(argv[3]);
        std::string line;
        const int c = 96 * 32, pw = 84, ph = 48, x0 = 96 - pw / 2, y0 = 96 - ph / 2, bw = pw * 4, bh = ph * 4;
        using Feat = std::array<float, 48>;
        auto feat = [](const Image& im, int bx, int by, Feat& f) {
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx)
                    for (int ch = 0; ch < 3; ++ch) {
                        int s = 0;
                        for (int dy = 0; dy < 2; ++dy)
                            for (int dx = 0; dx < 2; ++dx)
                                s += im.rgba[size_t(((by * 8 + sy * 2 + dy) * im.width + bx * 8 + sx * 2 + dx) * 4 + ch)];
                        f[size_t((sy * 4 + sx) * 3 + ch)] = float(s) / 4.0f;
                    }
        };
        auto dist = [](const Feat& a, const Feat& b) {
            float d = 0;
            for (size_t i = 0; i < 48; ++i) d += std::abs(a[i] - b[i]);
            return d / 48.0f;
        };
        auto part = [&](const TerrainLayer& t, int x, int y, int w, int h) {
            TerrainLayer p;
            p.width = uint16_t(w), p.height = uint16_t(h);
            for (int yy = 0; yy < h; ++yy)
                for (int xx = 0; xx < w; ++xx) p.tiles.push_back(t.tiles[size_t(y + yy) * t.width + x + xx]);
            return p;
        };
        struct Look {
            TerrainLayer before, after;  // (the picture's part)
            std::vector<int8_t> cls;     // per block
            float seam = 0;
            float sep = 0;  // how far the terrain's blocks are from the ground's (median nearest distance / the samples' spread)
        };
        // The painting with these two terrains, and its blocks' classes.
        auto paint = [&](int e, uint32_t seed, uint16_t ground, uint16_t id, Look& L) {
            Editor ed(data);
            if (!ed.newMap(192, 192, uint16_t(e), 1, err)) return false;
            const Tileset& ts = ed.tileset();
            ed.paintBrush(c, c, ground, Brush{Brush::Diamond, 30});
            const TerrainLayer rb = readTerrain(ed.chk(), "TILE");
            std::mt19937 rng(seed);
            for (int i = 0; i < 160; ++i) {
                const int x = c + int(rng() % 2200) - 1100, y = c + int(rng() % 1100) - 550;
                ed.paintBrush(x, y, i % 3 == 2 ? ground : id, Brush{Brush::Diamond, float(1 + rng() % 4)});
            }
            const TerrainLayer ra = readTerrain(ed.chk(), "TILE");
            L.before = part(rb, x0, y0, pw, ph), L.after = part(ra, x0, y0, pw, ph);
            // The samples: the middle of a painting of only the ground, then of only the terrain.
            std::vector<Feat> sample[2];
            Editor se(data);
            if (!se.newMap(192, 192, uint16_t(e), 1, err)) return false;
            for (int k = 0; k < 2; ++k) {
                se.paintBrush(c, c, k ? id : ground, Brush{Brush::Diamond, 30});
                const TerrainLayer sl = part(readTerrain(se.chk(), "TILE"), 96 - 8, 96 - 4, 16, 8);
                const Image im = renderTerrain(ts, sl);
                std::set<std::array<int, 48>> seen;
                for (int by = 0; by < 32; ++by)
                    for (int bx = 0; bx < 64; ++bx) {
                        Feat f;
                        feat(im, bx, by, f);
                        std::array<int, 48> q;
                        for (size_t i = 0; i < 48; ++i) q[i] = int(f[i]) / 6;
                        if (seen.insert(q).second) sample[k].push_back(f);
                    }
            }
            // (border art: farther from both than 2.5 times the samples' own spread, at least 12)
            std::vector<float> self;
            for (int k = 0; k < 2; ++k)
                for (size_t i = 0; i < sample[k].size(); i += 3) {
                    float best = 1e9f;
                    for (size_t j = 0; j < sample[k].size(); ++j)
                        if (j != i) best = std::min(best, dist(sample[k][i], sample[k][j]));
                    if (best < 1e8f) self.push_back(best);
                }
            std::sort(self.begin(), self.end());
            const float far = std::max(12.0f, 2.5f * (self.empty() ? 0.0f : self[self.size() / 2]));
            {
                std::vector<float> cross;
                for (const Feat& t : sample[1]) {
                    float best = 1e9f;
                    for (const Feat& g : sample[0]) best = std::min(best, dist(t, g));
                    cross.push_back(best);
                }
                std::sort(cross.begin(), cross.end());
                const float spread = self.empty() ? 1.0f : std::max(1.0f, self[self.size() / 2]);
                L.sep = cross.empty() ? 0.0f : cross[cross.size() / 2] / spread;
            }
            const Image im = renderTerrain(ts, L.after);
            L.cls.assign(size_t(bw * bh), 0);
            for (int by = 0; by < bh; ++by)
                for (int bx = 0; bx < bw; ++bx) {
                    int8_t& out = L.cls[size_t(by * bw + bx)];
                    if ((L.after.tiles[size_t(by / 4) * pw + bx / 4] >> 4) == 0) { out = 3; continue; }
                    Feat f;
                    feat(im, bx, by, f);
                    float d[2] = {1e9f, 1e9f};
                    for (int k = 0; k < 2; ++k)
                        for (const Feat& s : sample[k]) d[k] = std::min(d[k], dist(f, s));
                    out = std::min(d[0], d[1]) > far ? 2 : d[1] < d[0] ? 1 : 0;  // (a tie: ground)
                }
            SeamScorer sc(ts);
            double sum = 0;
            int n = 0;
            for (int y = 1; y + 1 < ph; ++y)
                for (int x = 1; x + 1 < pw; ++x) {
                    const size_t i = size_t(y) * pw + x;
                    if (L.after.tiles[i] == L.before.tiles[i]) continue;
                    const uint16_t nb[4] = {L.after.tiles[i - 1], L.after.tiles[i + 1], L.after.tiles[i - size_t(pw)], L.after.tiles[i + size_t(pw)]};
                    if (const float s = sc.among(L.after.tiles[i], nb); s >= 0) sum += s, ++n;
                }
            L.seam = n ? float(sum / n) : 0.0f;
            return true;
        };
        int done = 0;
        while (std::getline(in, line)) {
            std::stringstream ss(line);
            std::string n, era, name;
            std::getline(ss, n, '\t'), std::getline(ss, era, '\t'), std::getline(ss, name, '\t');
            const int e = eraOf(era);
            Editor red(data);
            const Tileset::ModTerrain* pm = nullptr;
            if (red.newMap(64, 64, uint16_t(e), 1, err))
                for (const auto& t : red.tileset().modTerrains)
                    if (t.name == name) pm = &t;
            if (!pm) continue;
            auto tpl = tr::templateOf(red.tileset(), *pm);
            // (a list line can name the reference: <ground id><tab><terrain id>, the game border the terrain is a reskin
            // of, when that is not its template)
            {
                std::string rg, rt;
                if (std::getline(ss, rg, '\t') && std::getline(ss, rt, '\t') && std::atoi(rg.c_str()) > 0 && std::atoi(rt.c_str()) > 0)
                    tpl = {uint16_t(std::atoi(rg.c_str())), uint16_t(std::atoi(rt.c_str()))};
            }
            const uint32_t seed = uint32_t(e * 977 + pm->id);
            Look mod, game;
            if (!paint(e, seed, pm->ground, pm->id, mod) || !paint(e, seed, tpl.first, tpl.second, game)) continue;
            int counted = 0, agree = 0, missing = 0, extra = 0, artMod = 0, artGame = 0, nulls = 0;
            Image pic;
            pic.width = bw * 4, pic.height = bh * 4;
            pic.rgba.assign(size_t(pic.width * pic.height * 4), 255);
            for (int by = 0; by < bh; ++by)
                for (int bx = 0; bx < bw; ++bx) {
                    const size_t t = size_t(by / 4) * pw + bx / 4, b = size_t(by * bw + bx);
                    const bool changed = mod.after.tiles[t] != mod.before.tiles[t] || game.after.tiles[t] != game.before.tiles[t];
                    const int a = mod.cls[b], g = game.cls[b];
                    uint8_t col[3] = {255, 255, 255};
                    if (a == 3) col[0] = col[1] = col[2] = 0;
                    else if (a == g) col[0] = col[1] = col[2] = uint8_t(a == 0 ? 255 : a == 1 ? 170 : 90);
                    else if (g == 1 && a == 0) col[0] = 220, col[1] = col[2] = 40;
                    else if (a == 1 && g == 0) col[2] = 220, col[0] = col[1] = 40;
                    else col[0] = 240, col[1] = 150, col[2] = 30;
                    for (int py = 0; py < 4; ++py)
                        for (int px = 0; px < 4; ++px)
                            for (int ch = 0; ch < 3; ++ch) pic.rgba[size_t(((by * 4 + py) * pic.width + bx * 4 + px) * 4 + ch)] = col[ch];
                    if (!changed) continue;
                    ++counted, agree += a == g, missing += g == 1 && a == 0, extra += a == 1 && g == 0;
                    artMod += a == 2, artGame += g == 2, nulls += a == 3;
                }
            const auto pct = [&](int x) { return counted ? 100.0 * x / counted : 0.0; };
            std::printf("%s\t%s\t%s\tagree %.1f\tmissing %.1f\textra %.1f\tart %.1f/%.1f\tNULL %.1f\tseam %.2f/%.2f\tsep %.1f/%.1f\n", n.c_str(),
                        era.c_str(), name.c_str(), pct(agree), pct(missing), pct(extra), pct(artMod), pct(artGame), pct(nulls), mod.seam,
                        game.seam, mod.sep, game.sep);
            std::fflush(stdout);
            done += writePng((std::string(argv[4]) + "/" + n + "-" + argv[5] + "-look.png").c_str(), pic);
        }
        return std::printf("looked %d\n", done), 0;
    }

    // holes: cmbw_neighbor_repair holes <install> <list: n<tab>tileset<tab>name> [rules .isom | -]
    // The wide NULL check: HOLES_SETS (default 12) paintings of each listed terrain, 120 strokes each of brush sizes 1 to
    // 8 (a third of them the ground), and the same strokes with its template (the game border). A hole: a tile the
    // strokes changed that is NULL (group 0) or has no megatile. Each hole's slot is the game border's tile there: a rect
    // of the template and its half (left 0, right 1), or "out" with the game's group (a rect outside the template, which
    // borrow and seams do not fill). A line a terrain: n, tileset, name, holes, paintings, then slot=count, most first.
    if (mode == "holes") {
        if (argc < 4) return 2;
        if (argc > 4 && std::string(argv[4]) != "-") cmbw::useTerrainOverride(argv[4]);
        const int sets = std::getenv("HOLES_SETS") ? std::max(1, std::atoi(std::getenv("HOLES_SETS"))) : 12;
        std::ifstream in(argv[3]);
        std::string line;
        while (std::getline(in, line)) {
            std::stringstream ss(line);
            std::string n, era, name;
            std::getline(ss, n, '\t'), std::getline(ss, era, '\t'), std::getline(ss, name, '\t');
            const int e = eraOf(era);
            Editor red(data);
            const Tileset::ModTerrain* pm = nullptr;
            if (red.newMap(192, 192, uint16_t(e), 1, err))
                for (const auto& t : red.tileset().modTerrains)
                    if (t.name == name) pm = &t;
            if (!pm) continue;
            const Tileset::ModTerrain m = *pm;
            const auto tpl = tr::templateOf(red.tileset(), m);
            std::set<uint16_t> rects;
            for (const tr::TemplateRect& r : tr::templateRects(red.tileset(), tpl.first, tpl.second)) rects.insert(r.group);
            // (a terrain whose fill is the NULL tile on purpose, as the mod's own rows have it, such as Twilight's Chasm:
            // its black inside, which the mod's maps also draw with the NULL tile; these are no holes)
            bool nullFill = false;
            for (const auto& pc : m.pieces)
                if (!rects.count(pc[0]) && (pc[1] >> 4) == 0 && (pc[2] >> 4) == 0) nullFill = true;
            const int c = 96 * 32;
            auto paintSet =[&](int s, uint16_t ground, uint16_t id, TerrainLayer& before, TerrainLayer& after, const Tileset** ts,
                                std::unique_ptr<Editor>& ed) {
                ed = std::make_unique<Editor>(data);
                if (!ed->newMap(192, 192, uint16_t(e), 1, err)) return false;
                ed->paintBrush(c, c, ground, Brush{Brush::Diamond, 40});
                before = readTerrain(ed->chk(), "TILE");
                std::mt19937 rng(uint32_t(e * 977 + m.id) * 7919u + uint32_t(s) * 104729u);
                for (int i = 0; i < 120; ++i) {
                    const int x = c + int(rng() % 2400) - 1200, y = c + int(rng() % 1200) - 600;
                    ed->paintBrush(x, y, i % 3 == 2 ? ground : id, Brush{Brush::Diamond, float(1 + rng() % 8)});
                }
                after = readTerrain(ed->chk(), "TILE");
                *ts = &ed->tileset();
                return before.tiles.size() == after.tiles.size();
            };
            int holes = 0, done = 0;
            std::map<std::string, int> slots;
            for (int s = 0; s < sets; ++s) {
                TerrainLayer mb, ma, gb, ga;
                const Tileset *mts = nullptr, *gts = nullptr;
                std::unique_ptr<Editor> me, ge;
                if (!paintSet(s, m.ground, m.id, mb, ma, &mts, me) || !paintSet(s, tpl.first, tpl.second, gb, ga, &gts, ge)) continue;
                ++done;
                for (size_t i = 0; i < ma.tiles.size() && i < ga.tiles.size(); ++i) {
                    const uint16_t v = ma.tiles[i];
                    if (v == mb.tiles[i] || ((v >> 4) != 0 && mts->megatileFor(v) > 0)) continue;
                    if (nullFill && (v >> 4) == 0) {
                        const uint16_t gg = uint16_t(ga.tiles[i] >> 4);
                        if (!rects.count(gg) && !(gg > 0 && rects.count(uint16_t(gg - 1)))) continue;  // (inside: the fill)
                    }
                    ++holes;
                    if (std::getenv("HOLES_DEBUG") && holes <= 5) std::printf("  hole tile %u (group %u, megatile %u)\n", unsigned(v), unsigned(v >> 4), unsigned(mts->megatileFor(v)));
                    const uint16_t g = uint16_t(ga.tiles[i] >> 4);
                    std::string k = rects.count(g) ? std::to_string(g) + ":0" : g > 0 && rects.count(uint16_t(g - 1)) ? std::to_string(g - 1) + ":1"
                                                                                                                   : "out" + std::to_string(g);
                    ++slots[k];
                }
            }
            std::vector<std::pair<int, std::string>> order;
            for (const auto& [k, v] : slots) order.push_back({-v, k});
            std::sort(order.begin(), order.end());
            std::printf("%s\t%s\t%s\tholes %d\tpaintings %d\t", n.c_str(), era.c_str(), name.c_str(), holes, done);
            for (size_t i = 0; i < order.size(); ++i) std::printf("%s%s=%d", i ? " " : "", order[i].second.c_str(), -order[i].first);
            std::printf("\n");
            std::fflush(stdout);
        }
        return 0;
    }

    // retemplate: cmbw_neighbor_repair retemplate <install> <out .isom> <era> <terrain name> <ground> <terrain> [rules .isom | -]
    // The archetype sandbox's mapping, done by geometry: the terrain gets another template (game terrain <terrain> on
    // game ground <ground>), and each rect of the new template takes the tiles of the old template's rect with the same
    // corner shapes (geomKey) and cliff row (else the same corner shapes in any row); a rect with none stays NULL. The
    // rules file holds that terrain only. Prints the rects matched, matched in another row, and left NULL.
    if (mode == "retemplate") {
        if (argc < 9) return 2;
        if (argc > 9 && std::string(argv[9]) != "-") cmbw::useTerrainOverride(argv[9]);
        const int e = std::atoi(argv[4]);
        Editor ed(data);
        if (!ed.newMap(64, 64, uint16_t(e), 1, err)) return 1;
        const Tileset& ts = ed.tileset();
        const Tileset::ModTerrain* pm = nullptr;
        for (const auto& t : ts.modTerrains)
            if (t.name == argv[5] && !t.variantOf) pm = &t;
        if (!pm) return std::fprintf(stderr, "no %s\n", argv[5]), 1;
        Tileset::ModTerrain m = *pm;
        const uint16_t ng = uint16_t(std::atoi(argv[6])), nt = uint16_t(std::atoi(argv[7]));
        const auto old = tr::templateOf(ts, m);
        auto keyOf = [&](uint16_t g, uint16_t t, const tr::TemplateRect& r) { return geomKey(ts, t, r.group, g); };
        std::multimap<std::string, std::pair<uint16_t, int>> from;  // (corner shapes -> old rect, its row)
        for (const auto& r : tr::templateRects(ts, old.first, old.second)) from.insert({keyOf(old.first, old.second, r), {r.group, r.row}});
        std::vector<std::array<uint16_t, 3>> pieces;
        int same = 0, other = 0, none = 0;
        for (const auto& r : tr::templateRects(ts, ng, nt)) {
            const std::string k = keyOf(ng, nt, r);
            int src = -1;
            for (auto [a, b] = from.equal_range(k); a != b; ++a)
                if (a->second.second == r.row) src = a->second.first;
            const bool sameRow = src >= 0;
            if (src < 0)
                if (auto it = from.find(k); it != from.end()) src = it->second.first;
            bool any = false;
            if (src >= 0)
                for (const auto& p : m.pieces)
                    if (p[0] == src && !tr::nullHalf(p[1]) && !tr::nullHalf(p[2])) pieces.push_back({r.group, p[1], p[2]}), any = true;
            if (!any) pieces.push_back({r.group, 0, 1}), ++none;
            else ++(sameRow ? same : other);
        }
        // (the fill and the pieces outside the old template stay: they are the terrain's inside)
        std::set<uint16_t> oldRects;
        for (const auto& r : tr::templateRects(ts, old.first, old.second)) oldRects.insert(r.group);
        for (const auto& p : m.pieces)
            if (!oldRects.count(p[0])) pieces.push_back(p);
        m.pieces = pieces, m.standIn = nt, m.groundTemplate = m.ground == ng ? 0 : ng, m.setPieces = 0, m.gapPieces = 0;
        tr::normalize(ts, m);
        tr::Rules rules;
        tr::TilesetRules tsr = tr::rulesOf(ts);
        tsr.terrains.clear();
        tr::Terrain x;
        x.t = m;
        tsr.terrains.push_back(x);
        rules.tilesets.push_back(tsr);
        std::printf("%s\t%s\tmatched %d\tother row %d\tNULL %d\n", argv[4], argv[5], same, other, none);
        return tr::save(argv[3], rules, err) ? 0 : (std::fprintf(stderr, "save: %s\n", err.c_str()), 1);
    }

    // templates: cmbw_neighbor_repair templates <install> <out .tsv> [rules .isom | -]
    // Every mod terrain's template (the game border its rects come from): tileset, name, template ground, template terrain,
    // the template's rect count, how many of them have a complete piece.
    if (mode == "templates") {
        if (argc > 4 && std::string(argv[4]) != "-") cmbw::useTerrainOverride(argv[4]);
        FILE* out = std::fopen(argv[3], "w");
        if (!out) return 1;
        for (int e = 0; e < 8; ++e) {
            Editor ed(data);
            if (!ed.newMap(64, 64, uint16_t(e), 1, err)) continue;
            const Tileset& ts = ed.tileset();
            for (const Tileset::ModTerrain& m : ts.modTerrains) {
                if (m.variantOf) continue;
                const auto tpl = tr::templateOf(ts, m);
                const auto [done, total] = tr::completeness(ts, m);
                std::fprintf(out, "%s\t%s\t%u\t%u\t%d\t%d\n", kEraName[e], m.name.c_str(), unsigned(tpl.first), unsigned(tpl.second), total, done);
            }
        }
        std::fclose(out);
        return 0;
    }

    // borders: cmbw_neighbor_repair borders <install> <out .tsv> -
    // Every game border of every tileset (game terrain b on game ground a, ids 1 to 31 that have rects with both): tileset,
    // a, b, family, rect count, the rects' roles sorted (two borders with the same roles tile the same way).
    if (mode == "borders") {
        FILE* out = std::fopen(argv[3], "w");
        if (!out) return 1;
        for (int e = 0; e < 8; ++e) {
            Editor ed(data);
            if (!ed.newMap(64, 64, uint16_t(e), 1, err)) continue;
            const Tileset& ts = ed.tileset();
            for (uint16_t a = 1; a < 32; ++a)
                for (uint16_t b = 1; b < 32; ++b) {
                    if (a == b) continue;
                    const std::vector<tr::TemplateRect> rects = tr::templateRects(ts, a, b);
                    if (rects.empty()) continue;
                    std::vector<std::string> roles;
                    for (const auto& r : rects) roles.push_back(r.role);
                    std::sort(roles.begin(), roles.end());
                    std::string sig;
                    for (const auto& r : roles) sig += (sig.empty() ? "" : "|") + r;
                    // (the geometry: each rect's corner points that are the terrain (geomKey) and its cliff row; names
                    // aside, two borders with the same geometry tile the same way)
                    std::vector<std::string> geo;
                    for (const auto& r : rects) geo.push_back(geomKey(ts, b, r.group, a) + "/" + std::to_string(r.row));
                    std::sort(geo.begin(), geo.end());
                    std::string gsig;
                    for (const auto& g : geo) gsig += (gsig.empty() ? "" : "|") + g;
                    std::fprintf(out, "%s\t%u\t%u\t%s\t%zu\t%s\t%s\n", kEraName[e], unsigned(a), unsigned(b), tr::familyName(tr::familyOf(rects, ts)), rects.size(),
                                 sig.c_str(), gsig.c_str());
                }
        }
        std::fclose(out);
        return 0;
    }

    if (mode == "gates") {
        if (argc > 5 && std::string(argv[5]) != "-") cmbw::useTerrainOverride(argv[5]);
        const int only = argc > 6 ? std::atoi(argv[6]) : -1;
        const auto maps = readMaps(argv[3], only);
        FILE* out = std::fopen(argv[4], "w");
        if (!out) return 1;
        for (int e = 0; e < 8; ++e) {
            if (only >= 0 && e != only) continue;
            Editor ed(data);
            if (!ed.newMap(64, 64, uint16_t(e), 1, err)) continue;
            const Tileset& ts = ed.tileset();
            const std::vector<Tileset::ModTerrain> terrains = ts.modTerrains;
            std::map<std::pair<uint16_t, uint16_t>, Legal> legalCache;
            std::vector<MapTiles> sheetMaps;
            if (const char* so = std::getenv("SHEET_ONLY"); so && std::atoi(so)) {
                MapTiles mt{"sheet", 16, int(ts.groups.size()) - int(kStock[size_t(e)]), {}, true};
                for (size_t g = kStock[size_t(e)]; g < ts.groups.size(); ++g)
                    for (unsigned k = 0; k < 16; ++k) mt.tiles.push_back(uint16_t(g * 16 + k));
                sheetMaps.push_back(mt);
            }
            const std::vector<MapTiles>& eraMaps = sheetMaps.empty() ? maps[size_t(e)] : sheetMaps;
            for (const Tileset::ModTerrain& m : terrains) {
                const auto tpl = tr::templateOf(ts, m);
                long long uses = 0, legalUses = 0;
                int onMaps = 0;
                std::set<uint64_t> mapPairs;  // (dir, tile, tile) between the terrain's own tiles
                std::vector<uint8_t> mine(65536, 0);
                if (tpl.first != tpl.second && !edgeRectsOf(ts, m).empty()) {
                    if (!legalCache.count(tpl)) legalCache[tpl] = legalOf(data, e, tpl.first, tpl.second);
                    Study s;
                    if (study(ts, e, m, eraMaps, legalCache[tpl], s)) {
                        uses = s.uses, legalUses = s.total(), onMaps = s.maps;
                        for (size_t i = 0; i < s.own; ++i) mine[s.nodes[i].tile] = 1;
                        for (const auto& [k, n] : s.pairs)
                            if (size_t(k[1]) < s.own && size_t(k[2]) < s.own)
                                mapPairs.insert(uint64_t(k[0]) << 32 | uint64_t(s.nodes[size_t(k[1])].tile) << 16 | s.nodes[size_t(k[2])].tile);
                    }
                }
                int nulls = 0, changed = 0;
                std::set<uint64_t> painted;
                TerrainLayer before, after;
                if (paintLayers(data, e, m.name, before, after)) {
                    const int w = after.width, h = after.height;
                    for (size_t i = 0; i < after.tiles.size(); ++i)
                        if (after.tiles[i] != before.tiles[i]) ++changed, nulls += (after.tiles[i] >> 4) == 0;
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x) {
                            const size_t i = size_t(y) * w + x;
                            if (!mine[after.tiles[i]]) continue;
                            if (x + 1 < w && mine[after.tiles[i + 1]]) painted.insert(uint64_t(after.tiles[i]) << 16 | after.tiles[i + 1]);
                            if (y + 1 < h && mine[after.tiles[i + size_t(w)]]) painted.insert(1ull << 32 | uint64_t(after.tiles[i]) << 16 | after.tiles[i + size_t(w)]);
                        }
                }
                int seen = 0;
                for (uint64_t k : painted) seen += int(mapPairs.count(k));
                std::fprintf(out, "%s\t%s\t%lld\t%d\t%lld\t%d\t%d\t%zu\t%d\n", kEraName[e], m.name.c_str(), uses, onMaps, legalUses, nulls, changed, painted.size(), seen);
                std::fflush(out);
            }
        }
        std::fclose(out);
        return 0;
    }

    // grid: cmbw_neighbor_repair grid <install> <era> <tiles .txt> <out .png>: rows of tile values as a picture (0: blank)
    if (mode == "grid") {
        if (argc < 6) return 2;
        Editor ed(data);
        if (!ed.newMap(64, 64, uint16_t(std::atoi(argv[3])), 1, err)) return 1;
        std::ifstream in(argv[4]);
        std::string line;
        std::vector<std::vector<uint16_t>> rowsIn;
        size_t wide = 0;
        while (std::getline(in, line)) {
            std::stringstream ss(line);
            std::vector<uint16_t> row;
            unsigned v;
            while (ss >> v) row.push_back(uint16_t(v));
            wide = std::max(wide, row.size());
            rowsIn.push_back(row);
        }
        TerrainLayer t;
        t.width = uint16_t(wide), t.height = uint16_t(rowsIn.size());
        for (const auto& row : rowsIn)
            for (size_t x = 0; x < wide; ++x) t.tiles.push_back(x < row.size() ? row[x] : 0);
        return writePng(argv[5], renderTerrain(ed.tileset(), t)) ? 0 : 1;
    }

    // borrow: cmbw_neighbor_repair borrow <install> <out folder> [era]: a rules file where every NULL half of a border
    // rect has the game's own tile of that rect (a marker: the painting then shows which NULL slots the engine reaches),
    // and borrowed.txt ("tileset, terrain, rect, half").
    // reach: cmbw_neighbor_repair reach <install> <borrowed.txt> <out reach.txt> <borrow.isom>: each terrain painted with
    // many random strokes; how many tiles of each borrowed slot the painting has.
    if (mode == "borrow") {
        const std::string outDir = argv[3];
        const int only = argc > 4 ? std::atoi(argv[4]) : -1;
        tr::Rules rules;
        FILE* out = std::fopen((outDir + "/borrowed.txt").c_str(), "w");
        for (int e = 0; e < 8; ++e) {
            if (only >= 0 && e != only) continue;
            Editor ed(data);
            if (!ed.newMap(64, 64, uint16_t(e), 1, err)) continue;
            const Tileset& ts = ed.tileset();
            tr::TilesetRules tsRules = tr::rulesOf(ts);
            for (tr::Terrain& terrain : tsRules.terrains) {
                Tileset::ModTerrain& m = terrain.t;
                bool changed = false;
                for (uint16_t rect : edgeRectsOf(ts, m))
                    for (size_t half = 0; half < 2; ++half) {
                        bool have = false;
                        for (const auto& p : m.pieces) have = have || (p[0] == rect && !tr::nullHalf(p[1 + half]));
                        uint16_t game = tr::gameTile(ts, uint32_t(rect) + half);
                        // (BORROW_ALL=1: a slot the game has no tile for gets its group's first value as the marker, so
                        // that seams searches it too; the marker never stays: seams puts NULL back where it finds no tile)
                        if (!game && !have && std::getenv("BORROW_ALL") && std::atoi(std::getenv("BORROW_ALL")) && size_t(rect) + half < ts.groups.size())
                            game = uint16_t((uint32_t(rect) + half) << 4);
                        if (have || !game) continue;
                        bool placed = false;
                        for (auto& p : m.pieces)
                            if (!placed && p[0] == rect && tr::nullHalf(p[1 + half])) p[1 + half] = game, placed = true;
                        if (!placed) {
                            std::array<uint16_t, 3> p = {rect, 0, 0};
                            p[1 + half] = game;
                            m.pieces.insert(m.pieces.begin(), p);
                        }
                        changed = true;
                        if (out) std::fprintf(out, "%s\t%s\t%u\t%zu\n", kEraName[e], m.name.c_str(), unsigned(rect), half);
                    }
                if (changed) tr::normalize(ts, m);
            }
            rules.tilesets.push_back(tsRules);
        }
        if (out) std::fclose(out);
        return tr::save(outDir + "/borrow.isom", rules, err) ? 0 : (std::fprintf(stderr, "save: %s\n", err.c_str()), 1);
    }
    if (mode == "reach") {
        if (argc < 6) return 2;
        cmbw::useTerrainOverride(argv[5]);
        std::map<std::pair<std::string, std::string>, std::set<uint32_t>> borrowed;
        {
            std::ifstream in(argv[3]);
            std::string line;
            while (std::getline(in, line)) {
                std::stringstream ss(line);
                std::string era, name, rect, half;
                std::getline(ss, era, '\t'), std::getline(ss, name, '\t'), std::getline(ss, rect, '\t'), std::getline(ss, half, '\t');
                borrowed[{era, name}].insert(slot(uint16_t(std::atoi(rect.c_str())), std::atoi(half.c_str())));
            }
        }
        FILE* out = std::fopen(argv[4], "w");
        for (const auto& [key, slots] : borrowed) {
            const int e = eraOf(key.first);
            Editor ed(data);
            if (!ed.newMap(192, 192, uint16_t(e), 1, err)) continue;
            const Tileset::ModTerrain* m = nullptr;
            for (const auto& t : ed.tileset().modTerrains)
                if (t.name == key.second) m = &t;
            if (!m) continue;
            const int c = 96 * 32;
            ed.paintBrush(c, c, m->ground, Brush{Brush::Diamond, 30});
            const TerrainLayer before = readTerrain(ed.chk(), "TILE");
            std::mt19937 rng(uint32_t(e * 977 + m->id));
            for (int i = 0; i < 160; ++i) {
                const int x = c + int(rng() % 2200) - 1100, y = c + int(rng() % 1100) - 550;
                ed.paintBrush(x, y, i % 3 == 2 ? m->ground : m->id, Brush{Brush::Diamond, float(1 + rng() % 4)});
            }
            const TerrainLayer after = readTerrain(ed.chk(), "TILE");
            std::map<uint32_t, int> count;
            for (size_t i = 0; i < after.tiles.size() && i < before.tiles.size(); ++i) {
                const uint16_t v = after.tiles[i];
                if (v == before.tiles[i] || unsigned(v >> 4) >= kStock[size_t(e)]) continue;
                const uint16_t g = uint16_t(v >> 4);
                const uint32_t k = slot(uint16_t(g & ~1u), g & 1);
                if (slots.count(k)) ++count[k];
            }
            for (uint32_t k : slots)
                if (out) std::fprintf(out, "%s\t%s\t%u\t%u\t%d\n", key.first.c_str(), key.second.c_str(), unsigned(k >> 1), unsigned(k & 1), count[k]);
        }
        if (out) std::fclose(out);
        return 0;
    }

    // seams: RULES=<borrow.isom> cmbw_neighbor_repair seams <install> <borrowed.txt> <out folder>
    // For each NULL rect that the engine paints (the borrow file marks them with the game's tiles): the places of the
    // marker in a painting of many strokes give its neighbours; each mod tile that no rule uses, of a group near the
    // terrain's own, gets the mean seam difference (pixels on the touching edges, divided by the tiles' roughness) against
    // those neighbours, 2 sides or more a place. The terrain's own border tiles, scored the same way where they stand,
    // give the limit (their median), and a self-test: how often a known tile is the best or among the 5 best for its own
    // places. A rect takes its best piece when its score is at most that median. Writes seams.txt and seamfill.isom
    // (the markers gone: the piece, or NULL again).
    if (mode == "seams") {
        const std::string outDir = argv[4];
        std::map<std::pair<std::string, std::string>, std::set<uint32_t>> borrowed;
        {
            std::ifstream in(argv[3]);
            std::string line;
            while (std::getline(in, line)) {
                std::stringstream ss(line);
                std::string era, name, rect, half;
                std::getline(ss, era, '\t'), std::getline(ss, name, '\t'), std::getline(ss, rect, '\t'), std::getline(ss, half, '\t');
                borrowed[{era, name}].insert(slot(uint16_t(std::atoi(rect.c_str())), std::atoi(half.c_str())));
            }
        }
        std::set<std::string> forced;
        if (const char* ff = std::getenv("SEAMS_FORCE")) {
            std::ifstream in(ff);
            for (std::string line; std::getline(in, line);) forced.insert(line);
        }
        tr::Rules rules;
        FILE* out = std::fopen((outDir + "/seams.txt").c_str(), "w");
        int filled = 0, open = 0;
        for (int e = 0; e < 8; ++e) {
            bool any = false;
            for (const auto& [key, v] : borrowed) any = any || key.first == kEraName[e];
            if (!any) continue;
            Editor ed0(data);
            if (!ed0.newMap(64, 64, uint16_t(e), 1, err)) continue;
            const Tileset& ts = ed0.tileset();
            tr::TilesetRules tsRules = tr::rulesOf(ts);
            std::vector<uint8_t> used(65536, 0);
            for (const auto& t : tsRules.terrains)
                for (const auto& pc : t.t.pieces) used[pc[1]] = used[pc[2]] = 1;
            // A tile's four edges (left column, right column, top row, bottom row) and the roughness at each.
            struct Prof {
                uint8_t e[4][96] = {};
                float rough[4] = {};
            };
            std::map<int, Prof> profs;
            auto profOf = [&](uint16_t tile) -> const Prof& {
                const int mega = ts.megatileFor(tile);
                auto it = profs.find(mega);
                if (it != profs.end()) return it->second;
                Prof& pr = profs[mega];
                const Sprite sp = mapTilesPicture(ts, {tile}, 1, 0, 0, 1, 1);
                if (sp.width != 32 || sp.height != 32) return pr;
                auto px = [&](int x, int y, int c) { return sp.rgba[size_t((y * 32 + x) * 4 + c)]; };
                double r[4] = {};
                for (int i = 0; i < 32; ++i)
                    for (int c = 0; c < 3; ++c) {
                        pr.e[0][i * 3 + c] = px(0, i, c), pr.e[1][i * 3 + c] = px(31, i, c);
                        pr.e[2][i * 3 + c] = px(i, 0, c), pr.e[3][i * 3 + c] = px(i, 31, c);
                        r[0] += std::abs(px(0, i, c) - px(1, i, c)), r[1] += std::abs(px(31, i, c) - px(30, i, c));
                        r[2] += std::abs(px(i, 0, c) - px(i, 1, c)), r[3] += std::abs(px(i, 31, c) - px(i, 30, c));
                    }
                for (int k = 0; k < 4; ++k) pr.rough[k] = float(r[k] / 96.0);
                return pr;
            };
            // (dir 0: a left of b; 1: a over b)
            auto seam = [&](uint16_t a, uint16_t b, int dir) {
                const Prof& pa = profOf(a);
                const Prof& pb = profOf(b);
                const int ea = dir ? 3 : 1, eb = dir ? 2 : 0;
                double d = 0;
                for (int i = 0; i < 96; ++i) d += std::abs(int(pa.e[ea][i]) - int(pb.e[eb][i]));
                return float(d / 96.0) / (2.0f + 0.5f * (pa.rough[ea] + pb.rough[eb]));
            };
            struct Ctx {
                uint16_t n[4];  // the tiles at the left, the right, above and below (0: not known)
            };
            auto ctxScore = [&](uint16_t tile, const Ctx& c) {
                float sum = 0;
                int sides = 0;
                if (c.n[0]) sum += seam(c.n[0], tile, 0), ++sides;
                if (c.n[1]) sum += seam(tile, c.n[1], 0), ++sides;
                if (c.n[2]) sum += seam(c.n[2], tile, 1), ++sides;
                if (c.n[3]) sum += seam(tile, c.n[3], 1), ++sides;
                return sides >= 2 ? sum / float(sides) : -1.0f;
            };
            auto avgScore = [&](uint16_t tile, const std::vector<Ctx>& cs) {
                float sum = 0;
                int n = 0;
                for (const Ctx& c : cs)
                    if (const float v = ctxScore(tile, c); v >= 0) sum += v, ++n;
                return n ? sum / float(n) : -1.0f;
            };
            for (tr::Terrain& terrain : tsRules.terrains) {
                Tileset::ModTerrain& m = terrain.t;
                const auto bit = borrowed.find({kEraName[e], m.name});
                if (bit == borrowed.end()) continue;
                const std::set<uint32_t>& gaps = bit->second;
                const std::set<uint16_t> edges = edgeRectsOf(ts, m);
                std::map<uint16_t, std::string> role;
                {
                    const auto tpl = tr::templateOf(ts, m);
                    for (const tr::TemplateRect& r : tr::templateRects(ts, tpl.first, tpl.second)) role[r.group] = r.role;
                }
                std::set<uint16_t> ownBorder;
                unsigned minG = 65535, maxG = 0;
                int next1 = 0, next16 = 0;
                for (const auto& pc : m.pieces) {
                    for (size_t half = 0; half < 2; ++half) {
                        const uint16_t v = pc[1 + half];
                        if (tr::nullHalf(v) || unsigned(v >> 4) < kStock[size_t(e)]) continue;
                        minG = std::min(minG, unsigned(v >> 4)), maxG = std::max(maxG, unsigned(v >> 4));
                        if (edges.count(pc[0])) ownBorder.insert(v);
                    }
                    if (edges.count(pc[0]) && unsigned(pc[1] >> 4) >= kStock[size_t(e)] && unsigned(pc[2] >> 4) >= kStock[size_t(e)])
                        next1 += pc[2] == pc[1] + 1, next16 += pc[2] == pc[1] + 16;
                }
                const uint16_t step = next16 > next1 ? 16 : 1;
                // The painting: many strokes; the markers' and the own border tiles' neighbours.
                std::map<uint32_t, std::vector<Ctx>> gapCtx;
                std::map<uint16_t, std::vector<Ctx>> ownCtx;
                {
                    Editor ed(data);
                    const Tileset::ModTerrain* pm = nullptr;
                    if (ed.newMap(192, 192, uint16_t(e), 1, err))
                        for (const auto& t : ed.tileset().modTerrains)
                            if (t.name == m.name) pm = &t;
                    if (pm) {
                        const int c = 96 * 32;
                        ed.paintBrush(c, c, pm->ground, Brush{Brush::Diamond, 30});
                        const TerrainLayer before = readTerrain(ed.chk(), "TILE");
                        std::mt19937 rng(uint32_t(e * 977 + pm->id));
                        // (SEAMS_RICH=1: 480 strokes of brush sizes 1 to 8, as the wide NULL check (holes) paints, so
                        // that the slots only bigger brushes reach have places too)
                        const bool rich = std::getenv("SEAMS_RICH") && std::atoi(std::getenv("SEAMS_RICH"));
                        for (int i = 0; i < (rich ? 480 : 160); ++i) {
                            const int x = c + int(rng() % 2200) - 1100, y = c + int(rng() % 1100) - 550;
                            ed.paintBrush(x, y, i % 3 == 2 ? pm->ground : pm->id, Brush{Brush::Diamond, float(1 + rng() % (rich ? 8 : 4))});
                        }
                        const TerrainLayer after = readTerrain(ed.chk(), "TILE");
                        const int w = after.width, h = after.height;
                        auto markerAt = [&](int x, int y) {
                            const size_t i = size_t(y) * w + x;
                            const uint16_t v = after.tiles[i];
                            if (v == before.tiles[i] || unsigned(v >> 4) >= kStock[size_t(e)]) return 0u;
                            const uint16_t g = uint16_t(v >> 4);
                            const uint32_t k = slot(uint16_t(g & ~1u), g & 1);
                            return gaps.count(k) ? k + 1 : 0u;
                        };
                        for (int y = 1; y + 1 < h; ++y)
                            for (int x = 1; x + 1 < w; ++x) {
                                const uint16_t v = after.tiles[size_t(y) * w + x];
                                const uint32_t mk = markerAt(x, y);
                                if (!mk && !ownBorder.count(v)) continue;
                                const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
                                Ctx c{};
                                for (int k = 0; k < 4; ++k) {
                                    const uint16_t n = after.tiles[size_t(ny[k]) * w + nx[k]];
                                    c.n[k] = (n >> 4) != 0 && !markerAt(nx[k], ny[k]) ? n : 0;
                                }
                                if (mk) {
                                    if (gapCtx[mk - 1].size() < 12) gapCtx[mk - 1].push_back(c);
                                } else if (ownCtx[v].size() < 6) {
                                    ownCtx[v].push_back(c);
                                }
                            }
                    }
                }
                // The markers go: NULL again, until a piece is taken below.
                for (auto& pc : m.pieces)
                    for (size_t half = 0; half < 2; ++half)
                        if (gaps.count(slot(pc[0], int(half))) && unsigned(pc[1 + half] >> 4) < kStock[size_t(e)]) pc[1 + half] = 0;
                // The tiles to choose from: no rule uses them, a group within 40 of the terrain's own.
                std::vector<uint16_t> pool;
                if (maxG >= minG)
                    for (unsigned g = minG > 40 + kStock[size_t(e)] ? minG - 40 : kStock[size_t(e)]; g <= maxG + 40 && g < ts.groups.size(); ++g)
                        for (unsigned k = 0; k < 16; ++k)
                            if (const uint16_t v = uint16_t(g << 4 | k); !used[v] && ts.megatileFor(v) > 0) pool.push_back(v);
                std::set<uint16_t> poolSet(pool.begin(), pool.end());
                // The own tiles' scores where they stand: the limit, and the self-test.
                std::vector<float> ownScores;
                int tested = 0, top1 = 0, top5 = 0;
                for (const auto& [t, cs] : ownCtx) {
                    const float mine = avgScore(t, cs);
                    if (mine < 0) continue;
                    ownScores.push_back(mine);
                    if (tested >= 60) continue;
                    ++tested;
                    int better = 0;
                    for (uint16_t c : ownBorder)
                        if (c != t && ts.megatileFor(c) != ts.megatileFor(t))
                            if (const float v = avgScore(c, cs); v >= 0 && v < mine) ++better;
                    for (uint16_t c : pool)
                        if (const float v = avgScore(c, cs); v >= 0 && v < mine) ++better;
                    top1 += better == 0, top5 += better < 5;
                }
                std::sort(ownScores.begin(), ownScores.end());
                const float limit = ownScores.empty() ? 0.0f : ownScores[ownScores.size() / 2];
                std::set<uint16_t> gapRects;
                for (uint32_t g : gaps)
                    if (gapCtx.count(g)) gapRects.insert(uint16_t(g >> 1));
                if (out)
                    std::fprintf(out, "# %s\t%s\tgap rects %zu\tpool %zu\town median %.2f\tself-test: best %d, in 5 best %d of %d\n", kEraName[e], m.name.c_str(),
                                 gapRects.size(), pool.size(), limit, top1, top5, tested);
                // Each gap rect's 3 best pieces; then the longest run of rects whose pieces' tiles rise with the rect order (one of
                // the 3 a rect, a score of at most 1.5 times the limit). A rect takes its best piece when its score is at
                // most the limit ("seam"), or its piece of a run of 3 rects or more ("run").
                struct RectCands {
                    uint16_t rect;
                    bool h0, h1;
                    std::vector<std::pair<float, uint16_t>> top;  // (score, left tile)
                };
                std::vector<RectCands> rc;
                for (uint16_t rect : gapRects) {
                    RectCands r{rect, gapCtx.count(slot(rect, 0)) > 0, gapCtx.count(slot(rect, 1)) > 0, {}};
                    std::vector<std::pair<float, uint16_t>> ranked;
                    for (uint16_t left : pool) {
                        const uint16_t right = uint16_t(left + step);
                        if (step == 1 && (left & 1)) continue;
                        if (r.h0 && r.h1 && !poolSet.count(right)) continue;
                        const float a = r.h0 ? avgScore(left, gapCtx[slot(rect, 0)]) : -1, b = r.h1 ? avgScore(right, gapCtx[slot(rect, 1)]) : -1;
                        if ((r.h0 && a < 0) || (r.h1 && b < 0)) continue;
                        ranked.push_back({r.h0 && r.h1 ? (a + b) / 2 : r.h0 ? a : b, left});
                    }
                    std::sort(ranked.begin(), ranked.end());
                    // (the same picture in another group, a copy, is one candidate: the first)
                    for (const auto& c : ranked) {
                        bool copy = false;
                        for (const auto& t : r.top) copy = copy || ts.megatileFor(t.second) == ts.megatileFor(c.second);
                        if (!copy) r.top.push_back(c);
                        if (r.top.size() == 3) break;
                    }
                    rc.push_back(r);
                }
                struct Dp {
                    int len = 0, prevRect = -1, prevCand = -1;
                    float sum = 0;
                };
                std::vector<std::array<Dp, 3>> dp(rc.size());
                int bestI = -1, bestK = -1;
                for (size_t a = 0; a < rc.size(); ++a)
                    for (size_t k = 0; k < rc[a].top.size(); ++k) {
                        if (rc[a].top[k].first > 1.5f * limit) continue;
                        Dp d{1, -1, -1, rc[a].top[k].first};
                        for (size_t b = 0; b < a; ++b)
                            for (size_t q = 0; q < rc[b].top.size(); ++q) {
                                const Dp& o = dp[b][q];
                                if (!o.len || rc[b].top[q].second >= rc[a].top[k].second) continue;
                                const float sum = o.sum + rc[a].top[k].first;
                                if (o.len + 1 > d.len || (o.len + 1 == d.len && sum < d.sum)) d = {o.len + 1, int(b), int(q), sum};
                            }
                        dp[a][k] = d;
                        if (bestI < 0 || d.len > dp[size_t(bestI)][size_t(bestK)].len ||
                            (d.len == dp[size_t(bestI)][size_t(bestK)].len && d.sum < dp[size_t(bestI)][size_t(bestK)].sum))
                            bestI = int(a), bestK = int(k);
                    }
                std::map<uint16_t, int> runPick;  // rect -> which of its candidates
                if (bestI >= 0 && dp[size_t(bestI)][size_t(bestK)].len >= 3)
                    for (int a = bestI, k = bestK; a >= 0;) {
                        runPick[rc[size_t(a)].rect] = k;
                        const Dp d = dp[size_t(a)][size_t(k)];
                        a = d.prevRect, k = d.prevCand;
                    }
                for (const RectCands& r : rc) {
                    int pick = -1;
                    const char* why = "open";
                    if (runPick.count(r.rect)) pick = runPick[r.rect], why = "run";
                    else if (!r.top.empty() && limit > 0 && r.top[0].first <= limit) pick = 0, why = "seam";
                    else if (!r.top.empty() && forced.count(m.name + "\t" + std::to_string(r.rect))) pick = 0, why = "forced";
                    if (out) {
                        std::fprintf(out, "%s\t%s\t%u\t%s\t%s\t%d", kEraName[e], m.name.c_str(), unsigned(r.rect), role[r.rect].c_str(), why, pick);
                        for (const auto& c : r.top) std::fprintf(out, "\t%u,%u:%.2f", unsigned(c.second), unsigned(c.second + step), c.first);
                        std::fprintf(out, "\n");
                    }
                    if (pick < 0) {
                        ++open;
                        continue;
                    }
                    ++filled;
                    for (size_t half = 0; half < 2; ++half) {
                        if (!(half ? r.h1 : r.h0)) continue;
                        const uint16_t tile = uint16_t(r.top[size_t(pick)].second + (half ? step : 0));  // (twins of a set take the same tiles)
                        bool placed = false;
                        for (auto& pc : m.pieces)
                            if (!placed && pc[0] == r.rect && tr::nullHalf(pc[1 + half])) pc[1 + half] = tile, placed = true;
                        if (!placed) {
                            std::array<uint16_t, 3> pc = {r.rect, 0, 0};
                            pc[1 + half] = tile;
                            m.pieces.insert(m.pieces.begin(), pc);
                        }
                    }
                }
                tr::normalize(ts, m);
            }
            rules.tilesets.push_back(tsRules);
        }
        if (out) std::fclose(out);
        if (!tr::save(outDir + "/seamfill.isom", rules, err)) return std::fprintf(stderr, "save: %s\n", err.c_str()), 1;
        return std::printf("NULL rects: %d filled by seams, %d open\n", filled, open), 0;
    }

    // suspects: RULES=<fill .isom> cmbw_neighbor_repair suspects <install> <seams.txt> <out folder>
    // The pieces that the seams mode put in, checked in a painting of many strokes with every neighbour in place: a
    // piece whose mean seam difference against its neighbours is above what 9 of 10 of the terrain's other border tiles
    // have where they stand is a suspect. Writes suspects.txt and pruned.isom (the suspects NULL again).
    if (mode == "suspects") {
        const std::string outDir = argv[4];
        struct Pick {
            uint16_t rect, left, right;
        };
        std::map<std::pair<std::string, std::string>, std::vector<Pick>> picks;
        {
            std::ifstream in(argv[3]);
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty() || line[0] == '#') continue;
                std::vector<std::string> f;
                std::stringstream ss(line);
                for (std::string x; std::getline(ss, x, '\t');) f.push_back(x);
                if (f.size() < 7) continue;
                const int pick = std::atoi(f[5].c_str());
                if (pick < 0 || size_t(6 + pick) >= f.size()) continue;
                unsigned l = 0, r = 0;
                if (std::sscanf(f[size_t(6 + pick)].c_str(), "%u,%u", &l, &r) != 2) continue;
                picks[{f[0], f[1]}].push_back({uint16_t(std::atoi(f[2].c_str())), uint16_t(l), uint16_t(r)});
            }
        }
        tr::Rules rules;
        FILE* out = std::fopen((outDir + "/suspects.txt").c_str(), "w");
        int suspects = 0, kept = 0;
        for (int e = 0; e < 8; ++e) {
            bool any = false;
            for (const auto& [key, v] : picks) any = any || key.first == kEraName[e];
            if (!any) continue;
            Editor ed0(data);
            if (!ed0.newMap(64, 64, uint16_t(e), 1, err)) continue;
            const Tileset& ts = ed0.tileset();
            tr::TilesetRules tsRules = tr::rulesOf(ts);
            struct Prof {
                uint8_t e[4][96] = {};
                float rough[4] = {};
            };
            std::map<int, Prof> profs;
            auto profOf = [&](uint16_t tile) -> const Prof& {
                const int mega = ts.megatileFor(tile);
                auto it = profs.find(mega);
                if (it != profs.end()) return it->second;
                Prof& pr = profs[mega];
                const Sprite sp = mapTilesPicture(ts, {tile}, 1, 0, 0, 1, 1);
                if (sp.width != 32 || sp.height != 32) return pr;
                auto px = [&](int x, int y, int c) { return sp.rgba[size_t((y * 32 + x) * 4 + c)]; };
                double r[4] = {};
                for (int i = 0; i < 32; ++i)
                    for (int c = 0; c < 3; ++c) {
                        pr.e[0][i * 3 + c] = px(0, i, c), pr.e[1][i * 3 + c] = px(31, i, c);
                        pr.e[2][i * 3 + c] = px(i, 0, c), pr.e[3][i * 3 + c] = px(i, 31, c);
                        r[0] += std::abs(px(0, i, c) - px(1, i, c)), r[1] += std::abs(px(31, i, c) - px(30, i, c));
                        r[2] += std::abs(px(i, 0, c) - px(i, 1, c)), r[3] += std::abs(px(i, 31, c) - px(i, 30, c));
                    }
                for (int k = 0; k < 4; ++k) pr.rough[k] = float(r[k] / 96.0);
                return pr;
            };
            auto seam = [&](uint16_t a, uint16_t b, int dir) {
                const Prof& pa = profOf(a);
                const Prof& pb = profOf(b);
                const int ea = dir ? 3 : 1, eb = dir ? 2 : 0;
                double d = 0;
                for (int i = 0; i < 96; ++i) d += std::abs(int(pa.e[ea][i]) - int(pb.e[eb][i]));
                return float(d / 96.0) / (2.0f + 0.5f * (pa.rough[ea] + pb.rough[eb]));
            };
            for (tr::Terrain& terrain : tsRules.terrains) {
                Tileset::ModTerrain& m = terrain.t;
                const auto pit = picks.find({kEraName[e], m.name});
                if (pit == picks.end()) continue;
                const std::set<uint16_t> edges = edgeRectsOf(ts, m);
                std::set<uint16_t> border;
                for (const auto& pc : m.pieces)
                    for (size_t half = 0; half < 2; ++half)
                        if (edges.count(pc[0]) && !tr::nullHalf(pc[1 + half]) && unsigned(pc[1 + half] >> 4) >= kStock[size_t(e)]) border.insert(pc[1 + half]);
                std::map<uint16_t, std::pair<double, int>> score;  // tile -> (sum of its places' mean seams, places)
                Editor ed(data);
                const Tileset::ModTerrain* pm = nullptr;
                if (ed.newMap(192, 192, uint16_t(e), 1, err))
                    for (const auto& t : ed.tileset().modTerrains)
                        if (t.name == m.name) pm = &t;
                if (!pm) continue;
                const int c = 96 * 32;
                ed.paintBrush(c, c, pm->ground, Brush{Brush::Diamond, 30});
                std::mt19937 rng(uint32_t(e * 977 + pm->id));
                for (int i = 0; i < 160; ++i) {
                    const int x = c + int(rng() % 2200) - 1100, y = c + int(rng() % 1100) - 550;
                    ed.paintBrush(x, y, i % 3 == 2 ? pm->ground : pm->id, Brush{Brush::Diamond, float(1 + rng() % 4)});
                }
                const TerrainLayer after = readTerrain(ed.chk(), "TILE");
                const int w = after.width, h = after.height;
                for (int y = 1; y + 1 < h; ++y)
                    for (int x = 1; x + 1 < w; ++x) {
                        const uint16_t v = after.tiles[size_t(y) * w + x];
                        if (!border.count(v)) continue;
                        const uint16_t n[4] = {after.tiles[size_t(y) * w + x - 1], after.tiles[size_t(y) * w + x + 1], after.tiles[size_t(y - 1) * w + x],
                                               after.tiles[size_t(y + 1) * w + x]};
                        float sum = 0;
                        int sides = 0;
                        if (n[0] >> 4) sum += seam(n[0], v, 0), ++sides;
                        if (n[1] >> 4) sum += seam(v, n[1], 0), ++sides;
                        if (n[2] >> 4) sum += seam(n[2], v, 1), ++sides;
                        if (n[3] >> 4) sum += seam(v, n[3], 1), ++sides;
                        if (sides >= 2) score[v].first += sum / float(sides), ++score[v].second;
                    }
                std::set<uint16_t> picked;
                for (const Pick& pk : pit->second) picked.insert(pk.left), picked.insert(pk.right);
                std::vector<float> others;
                for (const auto& [t, sc] : score)
                    if (!picked.count(t) && sc.second) others.push_back(float(sc.first / sc.second));
                std::sort(others.begin(), others.end());
                const float limit = others.empty() ? 0.0f : others[std::min(others.size() - 1, others.size() * 9 / 10)];
                for (const Pick& pk : pit->second) {
                    double sum = 0;
                    int n = 0;
                    for (uint16_t t : {pk.left, pk.right})
                        if (score.count(t)) sum += score[t].first, n += score[t].second;
                    const float mean = n ? float(sum / n) : 0.0f;
                    const bool bad = n > 0 && limit > 0 && mean > 1.15f * limit;  // (clearly above: 1 in 10 good tiles is over the limit itself)
                    if (out)
                        std::fprintf(out, "%s\t%s\t%u\t%u\t%u\t%.2f\t%.2f\t%d\t%s\n", kEraName[e], m.name.c_str(), unsigned(pk.rect), unsigned(pk.left), unsigned(pk.right),
                                     mean, limit, n, bad ? "suspect" : n ? "ok" : "not painted");
                    if (!bad) {
                        ++kept;
                        continue;
                    }
                    ++suspects;
                    for (auto& pc : m.pieces)
                        if (pc[0] == pk.rect)
                            for (size_t half = 0; half < 2; ++half)
                                if (pc[1 + half] == pk.left || pc[1 + half] == pk.right) pc[1 + half] = 0;
                }
                tr::normalize(ts, m);
            }
            rules.tilesets.push_back(tsRules);
        }
        if (out) std::fclose(out);
        if (!tr::save(outDir + "/pruned.isom", rules, err)) return std::fprintf(stderr, "save: %s\n", err.c_str()), 1;
        return std::printf("pieces checked in the painting: %d kept, %d suspects\n", kept, suspects), 0;
    }

    // pieces: [RULES=<.isom>] cmbw_neighbor_repair pieces <install> <era> <out .tsv>
    // Every piece of every mod terrain of the tileset: terrain, rect, role ("fill" for a rect that is not of the border),
    // left tile, right tile.
    if (mode == "pieces") {
        const int e = std::atoi(argv[3]);
        Editor ed(data);
        if (!ed.newMap(64, 64, uint16_t(e), 1, err)) return 1;
        const Tileset& ts = ed.tileset();
        FILE* out = std::fopen(argv[4], "w");
        if (!out) return 1;
        for (const Tileset::ModTerrain& m : ts.modTerrains) {
            std::map<uint16_t, std::string> role;
            const auto tpl = tr::templateOf(ts, m);
            for (const tr::TemplateRect& r : tr::templateRects(ts, tpl.first, tpl.second)) role[r.group] = r.role;
            for (const auto& pc : m.pieces)
                std::fprintf(out, "%s\t%u\t%s\t%u\t%u\n", m.name.c_str(), unsigned(pc[0]), role.count(pc[0]) ? role[pc[0]].c_str() : "fill", unsigned(pc[1]),
                             unsigned(pc[2]));
        }
        std::fclose(out);
        return 0;
    }

    // audit: [RULES=<.isom>] cmbw_neighbor_repair audit <install> <era> <out .tsv>
    // Every border rect of every mod terrain of the tileset, checked in a painting of many strokes: the mean seam
    // difference of its tiles against their neighbours where they stand, how many places, and that score divided by the
    // terrain's median over its rects (1: as the terrain's usual piece; 1.3 and more stands out).
    if (mode == "audit") {
        const int e = std::atoi(argv[3]);
        Editor ed0(data);
        if (!ed0.newMap(64, 64, uint16_t(e), 1, err)) return 1;
        const Tileset& ts = ed0.tileset();
        SeamScorer scorer(ts);
        FILE* out = std::fopen(argv[4], "w");
        if (!out) return 1;
        // With an out .isom: a rect's variant (one of its pieces) goes when a tile of it stands out in the painting (1.3 times
        // the terrain's median seam or more) and the rect keeps another piece that does not (under 1.15).
        tr::TilesetRules tsRules = tr::rulesOf(ts);
        int dropped = 0;
        for (tr::Terrain& terrainRules : tsRules.terrains) {
            Tileset::ModTerrain& m = terrainRules.t;
            const auto tpl = tr::templateOf(ts, m);
            std::map<uint16_t, std::string> role;
            for (const tr::TemplateRect& r : tr::templateRects(ts, tpl.first, tpl.second)) role[r.group] = r.role;
            std::map<uint16_t, std::vector<uint16_t>> rectsOf;  // tile -> its border rects
            for (const auto& pc : m.pieces)
                for (size_t half = 0; half < 2; ++half)
                    if (role.count(pc[0]) && !tr::nullHalf(pc[1 + half]) && unsigned(pc[1 + half] >> 4) >= kStock[size_t(e)]) rectsOf[pc[1 + half]].push_back(pc[0]);
            if (rectsOf.empty()) continue;
            Editor ed(data);
            const Tileset::ModTerrain* pm = nullptr;
            if (ed.newMap(192, 192, uint16_t(e), 1, err))
                for (const auto& t : ed.tileset().modTerrains)
                    if (t.name == m.name) pm = &t;
            if (!pm) continue;
            const int c = 96 * 32;
            ed.paintBrush(c, c, pm->ground, Brush{Brush::Diamond, 30});
            std::mt19937 rng(uint32_t(e * 977 + pm->id));
            for (int i = 0; i < 160; ++i) {
                const int x = c + int(rng() % 2200) - 1100, y = c + int(rng() % 1100) - 550;
                ed.paintBrush(x, y, i % 3 == 2 ? pm->ground : pm->id, Brush{Brush::Diamond, float(1 + rng() % 4)});
            }
            const TerrainLayer after = readTerrain(ed.chk(), "TILE");
            const int w = after.width, h = after.height;
            std::map<uint16_t, std::pair<double, int>> score;  // rect -> (sum, places)
            std::map<uint16_t, std::pair<double, int>> tileScore;  // tile (a variant of a rect's piece) -> the same
            for (int y = 1; y + 1 < h; ++y)
                for (int x = 1; x + 1 < w; ++x) {
                    const uint16_t v = after.tiles[size_t(y) * w + x];
                    const auto it = rectsOf.find(v);
                    if (it == rectsOf.end()) continue;
                    const uint16_t n[4] = {after.tiles[size_t(y) * w + x - 1], after.tiles[size_t(y) * w + x + 1], after.tiles[size_t(y - 1) * w + x],
                                           after.tiles[size_t(y + 1) * w + x]};
                    const float sc = scorer.among(v, n);
                    if (sc < 0) continue;
                    for (uint16_t rect : it->second) score[rect].first += sc, ++score[rect].second;
                    tileScore[v].first += sc, ++tileScore[v].second;
                }
            std::vector<float> means;
            for (const auto& [rect, sc] : score) means.push_back(float(sc.first / sc.second));
            if (means.empty()) continue;
            std::sort(means.begin(), means.end());
            const float median = means[means.size() / 2];
            if (argc > 5 && median > 0) {
                auto ratio = [&](uint16_t tile) {
                    const auto it = tileScore.find(tile);
                    return it == tileScore.end() || !it->second.second ? -1.0f : float(it->second.first / it->second.second) / median;
                };
                auto pieceRatio = [&](const std::array<uint16_t, 3>& pc) { return std::max(ratio(pc[1]), ratio(pc[2])); };
                std::map<uint16_t, int> good;  // rect -> its pieces that are painted and do not stand out
                for (const auto& pc : m.pieces)
                    if (role.count(pc[0]) && pieceRatio(pc) >= 0 && pieceRatio(pc) < 1.15f) ++good[pc[0]];
                for (size_t i = m.pieces.size(); i-- > 0;) {
                    const auto pc = m.pieces[i];
                    if (!role.count(pc[0]) || pieceRatio(pc) < 1.3f || !good[pc[0]]) continue;
                    std::fprintf(out, "D\t%s\t%u\t%s\t%u\t%u\t%.2f\n", m.name.c_str(), unsigned(pc[0]), role[pc[0]].c_str(), unsigned(pc[1]), unsigned(pc[2]), pieceRatio(pc));
                    m.pieces.erase(m.pieces.begin() + long(i)), ++dropped;
                }
                tr::normalize(ts, m);
            }
            for (const auto& [rect, sc] : score) {
                const float mean = float(sc.first / sc.second);
                std::fprintf(out, "%s\t%u\t%s\t%.2f\t%d\t%.2f\n", m.name.c_str(), unsigned(rect), role[rect].c_str(), mean, sc.second, median > 0 ? mean / median : 0.0f);
            }
            // (each tile too, "T" lines: a rect's variants one by one)
            for (const auto& [tile, sc] : tileScore) {
                const float mean = float(sc.first / sc.second);
                std::fprintf(out, "T\t%s\t%u\t%u\t%s\t%.2f\t%d\t%.2f\n", m.name.c_str(), unsigned(tile), unsigned(rectsOf[tile][0]), role[rectsOf[tile][0]].c_str(), mean,
                             sc.second, median > 0 ? mean / median : 0.0f);
            }
        }
        std::fclose(out);
        if (argc > 5) {
            tr::Rules rules;
            rules.tilesets.push_back(tsRules);
            if (!tr::save(argv[5], rules, err)) return std::fprintf(stderr, "save: %s\n", err.c_str()), 1;
            std::printf("variants dropped: %d\n", dropped);
        }
        return 0;
    }

    // clear: [RULES=<.isom>] cmbw_neighbor_repair clear <install> <era> <list of "terrain<tab>rect" lines> <out .isom>
    // The listed rects of the listed terrains lose their pieces (NULL): for a new pick by the seams mode.
    if (mode == "clear") {
        if (argc < 6) return 2;
        const int e = std::atoi(argv[3]);
        std::set<std::string> list;
        {
            std::ifstream in(argv[4]);
            for (std::string line; std::getline(in, line);) list.insert(line);
        }
        Editor ed(data);
        if (!ed.newMap(64, 64, uint16_t(e), 1, err)) return 1;
        const Tileset& ts = ed.tileset();
        tr::TilesetRules tsRules = tr::rulesOf(ts);
        int cleared = 0;
        for (tr::Terrain& terrain : tsRules.terrains) {
            Tileset::ModTerrain& m = terrain.t;
            bool changed = false;
            for (auto& pc : m.pieces)
                if (list.count(m.name + "\t" + std::to_string(pc[0])) && (!tr::nullHalf(pc[1]) || !tr::nullHalf(pc[2]))) {
                    std::printf("%s\trect %u\t%u\t%u\n", m.name.c_str(), unsigned(pc[0]), unsigned(pc[1]), unsigned(pc[2]));
                    pc[1] = pc[2] = 0, changed = true, ++cleared;
                }
            if (changed) tr::normalize(ts, m);
        }
        tr::Rules rules;
        rules.tilesets.push_back(tsRules);
        if (!tr::save(argv[5], rules, err)) return std::fprintf(stderr, "save: %s\n", err.c_str()), 1;
        return std::printf("pieces cleared: %d\n", cleared), 0;
    }

    // sheetstats: [RULES=<.isom>] cmbw_neighbor_repair sheetstats <install> <era> <out .tsv>
    // Is the mod's tile sheet (its groups as rows of 16) laid out as the world is? For each terrain, the pairs of its own
    // border tiles that touch in the sheet, by kind (side by side inside one piece; side by side across two pieces;
    // one over the other in consecutive groups), and how many of them the template allows; and a baseline: random pairs
    // of the terrain's own tiles, side by side and stacked.
    if (mode == "sheetstats") {
        const int e = std::atoi(argv[3]);
        Editor ed(data);
        if (!ed.newMap(64, 64, uint16_t(e), 1, err)) return 1;
        const Tileset& ts = ed.tileset();
        FILE* out = std::fopen(argv[4], "w");
        if (!out) return 1;
        MapTiles sheet{"sheet", 16, int(ts.groups.size()) - int(kStock[size_t(e)]), {}, true};
        for (size_t g = kStock[size_t(e)]; g < ts.groups.size(); ++g)
            for (unsigned k = 0; k < 16; ++k) sheet.tiles.push_back(uint16_t(g * 16 + k));
        const std::vector<MapTiles> sheets = {sheet};
        std::map<std::pair<uint16_t, uint16_t>, Legal> legalCache;
        std::mt19937 rng(7);
        std::fprintf(out, "terrain\tin-piece\tin-piece legal\tacross\tacross legal\tstacked\tstacked legal\trandom h legal\trandom v legal\n");
        for (const Tileset::ModTerrain& m : ts.modTerrains) {
            const auto tpl = tr::templateOf(ts, m);
            if (tpl.first == tpl.second || edgeRectsOf(ts, m).empty()) continue;
            if (!legalCache.count(tpl)) legalCache[tpl] = legalOf(data, e, tpl.first, tpl.second);
            Study st;
            if (!study(ts, e, m, sheets, legalCache[tpl], st)) continue;
            long long n[3] = {}, ok[3] = {};
            for (const auto& [k, c] : st.pairs) {
                const Node& a = st.nodes[size_t(k[1])];
                const Node& b = st.nodes[size_t(k[2])];
                if (a.fixed || b.fixed) continue;  // (border tiles only)
                const int kind = k[0] == 1 ? 2 : (b.tile == a.tile + 1 && !(a.tile & 1)) ? 0 : 1;
                ++n[kind], ok[kind] += st.ok(a.slots, b.slots, k[0]);
            }
            std::vector<int> border;
            for (size_t i = 0; i < st.own; ++i)
                if (!st.nodes[i].fixed) border.push_back(int(i));
            long long rn[2] = {}, rok[2] = {};
            for (int t = 0; t < 4000 && border.size() > 1; ++t) {
                const Node& a = st.nodes[size_t(border[rng() % border.size()])];
                const Node& b = st.nodes[size_t(border[rng() % border.size()])];
                if (&a == &b) continue;
                const int dir = t & 1;
                ++rn[dir], rok[dir] += st.ok(a.slots, b.slots, dir);
            }
            // (SHEET_DETAIL=<terrain name>: its stacked pairs in the sheet, legal or not, with their rects)
            if (const char* dn = std::getenv("SHEET_DETAIL"); dn && m.name == dn) {
                std::map<uint16_t, std::string> role;
                for (const tr::TemplateRect& r : tr::templateRects(ts, tpl.first, tpl.second)) role[r.group] = r.role;
                auto slotText = [&](const Node& nd) {
                    std::string t;
                    for (uint32_t sl : nd.slots) t += (t.empty() ? "" : "|") + std::to_string(sl >> 1) + (sl & 1 ? "R " : "L ") + role[uint16_t(sl >> 1)];
                    return t;
                };
                for (const auto& [k, c] : st.pairs) {
                    if (k[0] != 1) continue;
                    const Node& na = st.nodes[size_t(k[1])];
                    const Node& nb = st.nodes[size_t(k[2])];
                    if (na.fixed || nb.fixed) continue;
                    std::printf("%s\tgroup %u col %u -> %u\t%s\tover\t%s\n", st.ok(na.slots, nb.slots, 1) ? "legal  " : "ILLEGAL", unsigned(na.tile >> 4),
                                unsigned(na.tile & 15), unsigned(nb.tile >> 4), slotText(na).c_str(), slotText(nb).c_str());
                }
            }
            auto pct = [](long long a, long long d) { return d ? 100.0 * double(a) / double(d) : -1.0; };
            std::fprintf(out, "%s\t%lld\t%.0f\t%lld\t%.0f\t%lld\t%.0f\t%.0f\t%.0f\n", m.name.c_str(), n[0], pct(ok[0], n[0]), n[1], pct(ok[1], n[1]), n[2], pct(ok[2], n[2]),
                         pct(rok[0], rn[0]), pct(rok[1], rn[1]));
        }
        std::fclose(out);
        return 0;
    }

    // layoutdump: [RULES=<.isom>] cmbw_neighbor_repair layoutdump <install> <out .tsv>
    // Every border piece of every mod terrain of every tileset, with its template: era, terrain, family, the template's
    // rect count, its game ground and terrain, the rect, its role, the left and right tile.
    if (mode == "layoutdump") {
        FILE* out = std::fopen(argv[3], "w");
        if (!out) return 1;
        for (int e = 0; e < 8; ++e) {
            Editor ed(data);
            if (!ed.newMap(64, 64, uint16_t(e), 1, err)) continue;
            const Tileset& ts = ed.tileset();
            for (const Tileset::ModTerrain& m : ts.modTerrains) {
                const auto tpl = tr::templateOf(ts, m);
                const std::vector<tr::TemplateRect> rects = tr::templateRects(ts, tpl.first, tpl.second);
                if (rects.empty()) continue;
                const char* fam = tr::familyName(tr::familyOf(rects, ts));
                std::map<uint16_t, std::string> role;
                for (const tr::TemplateRect& r : rects) role[r.group] = r.role;
                for (const auto& pc : m.pieces) {
                    if (!role.count(pc[0])) continue;
                    std::fprintf(out, "%s\t%s\t%s\t%zu\t%u\t%u\t%u\t%s\t%u\t%u\n", kEraName[e], m.name.c_str(), fam, rects.size(), unsigned(tpl.first),
                                 unsigned(tpl.second), unsigned(pc[0]), role[pc[0]].c_str(), unsigned(pc[1]), unsigned(pc[2]));
                }
            }
        }
        std::fclose(out);
        return 0;
    }

    // apply: [RULES=<.isom>] cmbw_neighbor_repair apply <install> <era> <list .tsv> <out .isom>
    // The list's lines (era, terrain, rect, role, left, right, ...) replace the pieces of those rects: each listed rect
    // gets exactly the listed pieces whose two tiles have a picture.
    // take: cmbw_neighbor_repair take <install> <out .isom> <base .isom | -> [<from .isom> <era> <terrain name | *>]...
    // a rules file: the base's terrains, then each named terrain (*: all of the era's) of a from file in place of the
    // one with its id (or added). For the loop: a candidate's one terrain into the current rules.
    if (mode == "take") {
        tr::Rules out;
        if (std::string(argv[4]) != "-" && !tr::load(argv[4], out, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        for (int a = 5; a + 2 < argc; a += 3) {
            tr::Rules from;
            if (!tr::load(argv[a], from, err)) return std::fprintf(stderr, "%s: %s\n", argv[a], err.c_str()), 1;
            const int e = eraOf(argv[a + 1]);
            const std::string name = argv[a + 2];
            int taken = 0;
            for (const tr::TilesetRules& ft : from.tilesets) {
                if (ft.era != e) continue;
                tr::TilesetRules* ot = nullptr;
                for (tr::TilesetRules& t : out.tilesets)
                    if (t.era == ft.era) ot = &t;
                if (!ot) {
                    out.tilesets.push_back({ft.era, ft.groups, ft.fingerprint, {}});
                    ot = &out.tilesets.back();
                }
                if (ot->fingerprint != ft.fingerprint) return std::fprintf(stderr, "%s: made for other groups\n", argv[a]), 1;
                for (const tr::Terrain& t : ft.terrains) {
                    if (name != "*" && t.t.name != name) continue;
                    bool put = false;
                    for (tr::Terrain& o : ot->terrains)
                        if (o.t.id == t.t.id) o = t, put = true;
                    if (!put) ot->terrains.push_back(t);
                    ++taken;
                }
            }
            if (!taken) return std::fprintf(stderr, "%s: no %s in %s\n", argv[a], name.c_str(), argv[a + 1]), 1;
        }
        if (!tr::save(argv[3], out, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        return 0;
    }

    if (mode == "apply") {
        if (argc < 6) return 2;
        const int e = std::atoi(argv[3]);
        std::map<std::pair<std::string, uint16_t>, std::vector<std::array<uint16_t, 2>>> want;
        {
            std::ifstream in(argv[4]);
            for (std::string line; std::getline(in, line);) {
                std::vector<std::string> f;
                std::stringstream ss(line);
                for (std::string x; std::getline(ss, x, '\t');) f.push_back(x);
                if (f.size() < 6 || f[0] != kEraName[e]) continue;
                want[{f[1], uint16_t(std::atoi(f[2].c_str()))}].push_back({uint16_t(std::atoi(f[4].c_str())), uint16_t(std::atoi(f[5].c_str()))});
            }
        }
        Editor ed(data);
        if (!ed.newMap(64, 64, uint16_t(e), 1, err)) return 1;
        const Tileset& ts = ed.tileset();
        tr::TilesetRules tsRules = tr::rulesOf(ts);
        int rects = 0, pieces = 0, skipped = 0;
        for (tr::Terrain& terrain : tsRules.terrains) {
            Tileset::ModTerrain& m = terrain.t;
            bool changed = false;
            for (const auto& [key, list] : want) {
                if (key.first != m.name) continue;
                std::vector<std::array<uint16_t, 3>> keep;
                for (const auto& lr : list) {
                    if (ts.megatileFor(lr[0]) <= 0 || ts.megatileFor(lr[1]) <= 0) {
                        ++skipped;
                        continue;
                    }
                    keep.push_back({key.second, lr[0], lr[1]});
                }
                if (keep.empty()) continue;
                m.pieces.erase(std::remove_if(m.pieces.begin(), m.pieces.end(), [&](const std::array<uint16_t, 3>& pc) { return pc[0] == key.second; }),
                               m.pieces.end());
                m.pieces.insert(m.pieces.begin(), keep.begin(), keep.end());
                ++rects, pieces += int(keep.size()), changed = true;
            }
            if (changed) tr::normalize(ts, m);
        }
        tr::Rules rules;
        rules.tilesets.push_back(tsRules);
        if (!tr::save(argv[5], rules, err)) return std::fprintf(stderr, "save: %s\n", err.c_str()), 1;
        return std::printf("rects set: %d (%d pieces), tiles with no picture skipped: %d\n", rects, pieces, skipped), 0;
    }

    // copies: cmbw_neighbor_repair copies <install> <source era> <ground> <terrain> <out .tsv>
    // The game's own border of one tileset (ground, terrain: isometric terrain ids, e.g. Badlands 1 2 = Dirt / High
    // Dirt) as the source: every tile of its rects (each variant, both halves). Then every mod tile of every tileset
    // that is a copy of one of them: the same picture after a recolour (gray pictures, normalized: correlation 0.97 or
    // more over 32 x 32). Lines: era, mod tile, source tile, rect, role, half, variant, correlation, mean colour
    // difference (0: the same colours).
    if (mode == "copies") {
        if (argc < 7) return 2;
        const int se = std::atoi(argv[3]);
        const uint16_t ground = uint16_t(std::atoi(argv[4])), terrain = uint16_t(std::atoi(argv[5]));
        FILE* out = std::fopen(argv[6], "w");
        if (!out) return 1;
        struct Pic {
            float g[1024];  // gray, zero mean, unit variance
            float s[64];    // 8 x 8, the same
            float rgb[3];   // mean colour
            bool ok = false;
        };
        auto picOf = [](const Tileset& ts, uint16_t tile, Pic& p) {
            p.ok = false;
            if (ts.megatileFor(tile) <= 0) return;
            const Sprite sp = mapTilesPicture(ts, {tile}, 1, 0, 0, 1, 1);
            if (sp.width != 32 || sp.height != 32) return;
            double sum = 0, sq = 0, c[3] = {};
            for (int i = 0; i < 1024; ++i) {
                const float v = 0.299f * sp.rgba[size_t(i) * 4] + 0.587f * sp.rgba[size_t(i) * 4 + 1] + 0.114f * sp.rgba[size_t(i) * 4 + 2];
                p.g[i] = v, sum += v, sq += double(v) * v;
                for (int k = 0; k < 3; ++k) c[k] += sp.rgba[size_t(i) * 4 + size_t(k)];
            }
            const double mean = sum / 1024, sd = std::sqrt(std::max(0.0, sq / 1024 - mean * mean));
            if (sd < 4) return;  // (a flat tile: no picture to match)
            for (int i = 0; i < 1024; ++i) p.g[i] = float((p.g[i] - mean) / sd);
            for (int k = 0; k < 3; ++k) p.rgb[k] = float(c[k] / 1024);
            double s2 = 0, ss = 0;
            for (int by = 0; by < 8; ++by)
                for (int bx = 0; bx < 8; ++bx) {
                    float a = 0;
                    for (int y = 0; y < 4; ++y)
                        for (int x = 0; x < 4; ++x) a += p.g[(by * 4 + y) * 32 + bx * 4 + x];
                    p.s[by * 8 + bx] = a / 16, s2 += a / 16;
                }
            const double m2 = s2 / 64;
            for (float& v : p.s) v = float(v - m2), ss += double(v) * v;
            const float n2 = float(std::sqrt(ss / 64));
            if (n2 > 1e-6f)
                for (float& v : p.s) v /= n2;
            p.ok = true;
        };
        // The source.
        Editor src(data);
        if (!src.newMap(64, 64, uint16_t(se), 1, err)) return 1;
        const Tileset& sts = src.tileset();
        struct Src {
            uint16_t tile, rect;
            std::string role;
            int half, variant;
            Pic pic;
        };
        std::vector<Src> srcs;
        for (const tr::TemplateRect& r : tr::templateRects(sts, ground, terrain))
            for (int half = 0; half < 2; ++half)
                for (int k = 0; k < 16; ++k) {
                    Src sr{uint16_t((r.group + half) * 16 + k), r.group, r.role, half, k, {}};
                    picOf(sts, sr.tile, sr.pic);
                    if (sr.pic.ok) srcs.push_back(std::move(sr));
                }
        std::printf("source tiles with a picture: %zu\n", srcs.size());
        long long found = 0;
        for (int e = 0; e < 8; ++e) {
            Editor ed(data);
            if (!ed.newMap(64, 64, uint16_t(e), 1, err)) continue;
            const Tileset& ts = ed.tileset();
            int n = 0;
            Pic p;
            for (size_t g = kStock[size_t(e)]; g < ts.groups.size(); ++g)
                for (unsigned k = 0; k < 16; ++k) {
                    const uint16_t tile = uint16_t(g * 16 + k);
                    picOf(ts, tile, p);
                    if (!p.ok) continue;
                    int best = -1;
                    float bestC = 0;
                    for (size_t i = 0; i < srcs.size(); ++i) {
                        float c = 0;
                        for (int j = 0; j < 64; ++j) c += p.s[j] * srcs[i].pic.s[j];
                        if (c / 64 < 0.9f) continue;
                        float f = 0;
                        for (int j = 0; j < 1024; ++j) f += p.g[j] * srcs[i].pic.g[j];
                        f /= 1024;
                        if (f > bestC) bestC = f, best = int(i);
                    }
                    if (best < 0 || bestC < 0.97f) continue;
                    const Src& sr = srcs[size_t(best)];
                    const float dc = (std::abs(p.rgb[0] - sr.pic.rgb[0]) + std::abs(p.rgb[1] - sr.pic.rgb[1]) + std::abs(p.rgb[2] - sr.pic.rgb[2])) / 3;
                    std::fprintf(out, "%s\t%u\t%u\t%u\t%s\t%d\t%d\t%.3f\t%.1f\n", kEraName[e], unsigned(tile), unsigned(sr.tile), unsigned(sr.rect), sr.role.c_str(),
                                 sr.half, sr.variant, bestC, dc);
                    ++n, ++found;
                }
            std::printf("%s: %d mod tiles are copies\n", kEraName[e], n);
            std::fflush(stdout);
        }
        std::fclose(out);
        return 0;
    }

    // rebase: cmbw_neighbor_repair rebase <install> <era> <terrain> <stand-in id> <copies-all.tsv> <first group> <last group> <out .isom> [keep]
    // A mod terrain made again from copies only: its stand-in (and so its template) set to the game terrain whose border
    // its tiles copy, and its border pieces built from the copies in its groups: a piece is two tiles side by side (t, t+1)
    // whose copies' slots (the best source's and the tied ones') share one rect of the new template, t as its left half
    // and t + 1 as its right. Two or more such rects: ambiguous, left out (reported). A rect with no piece: NULL, or with
    // "keep" its pieces of today. Its fill pieces stay.
    if (mode == "rebase") {
        if (argc < 10) return 2;
        const int e = std::atoi(argv[3]);
        const std::string name = argv[4];
        const uint16_t standIn = uint16_t(std::atoi(argv[5]));
        const int g0 = std::atoi(argv[7]), g1 = std::atoi(argv[8]);
        const bool keep = argc > 10 && std::string(argv[10]) == "keep";
        // copies: tile -> slots ("source#role:half"), of the source borders (the main one, or REBASE_SRC: one or more,
        // ";" between them: borders of one archetype), scores 0.8 or more
        std::map<uint16_t, std::set<std::string>> slots;
        std::string mainSource;
        std::vector<std::string> mains;
        {
            std::vector<std::vector<std::string>> rows;
            std::map<std::string, int> sources;
            std::ifstream in(argv[6]);
            for (std::string line; std::getline(in, line);) {
                std::vector<std::string> f;
                std::stringstream ss(line);
                for (std::string x; std::getline(ss, x, '\t');) f.push_back(x);
                if (f.size() < 16 || f[0] != kEraName[e]) continue;
                const int tile = std::atoi(f[1].c_str());
                if ((tile >> 4) < g0 || (tile >> 4) > g1 || std::atof(f[10].c_str()) < 0.8) continue;
                ++sources[f[2] + "|" + f[3] + "|" + f[4]];
                rows.push_back(f);
            }
            std::string main;
            int best = 0;
            for (const auto& [k, n] : sources)
                if (n > best) best = n, main = k;
            mains = {main};
            if (const char* src = std::getenv("REBASE_SRC")) {
                mains.clear();
                std::stringstream ss(src);
                for (std::string x; std::getline(ss, x, ';');)
                    if (!x.empty()) mains.push_back(x);
            }
            mainSource = mains.empty() ? main : mains[0];
            for (const std::string& m : mains) std::printf("  source border: %s (%d of %zu copies)\n", m.c_str(), sources[m], rows.size());
            for (const auto& f : rows) {
                const std::string src = f[2] + "|" + f[3] + "|" + f[4];
                if (std::find(mains.begin(), mains.end(), src) == mains.end()) continue;
                auto& sl = slots[uint16_t(std::atoi(f[1].c_str()))];
                sl.insert(src + "#" + f[6] + ":" + f[7]);
                std::stringstream ts(f[15]);
                for (std::string x; std::getline(ts, x, ',');)
                    if (!x.empty()) sl.insert(src + "#" + x);
            }
        }
        Editor ed(data);
        if (!ed.newMap(64, 64, uint16_t(e), 1, err)) return 1;
        const Tileset& ts = ed.tileset();
        tr::TilesetRules tsRules = tr::rulesOf(ts);
        bool found = false;
        for (tr::Terrain& terrain : tsRules.terrains) {
            Tileset::ModTerrain& m = terrain.t;
            if (m.name != name) continue;
            found = true;
            const auto oldTpl = tr::templateOf(ts, m);
            const std::set<uint16_t> oldEdges = edgeRectsOf(ts, m);
            const std::vector<std::array<uint16_t, 3>> oldPieces = m.pieces;
            std::map<std::string, std::vector<uint16_t>> oldRectsOfShape;  // (keep: today's rects by shape)
            for (uint16_t g : oldEdges) oldRectsOfShape[geomKey(ts, oldTpl.second, g)].push_back(g);
            std::printf("%s: stand-in %u ground %u ground template %u, template %u/%u (%zu rects)\n", m.name.c_str(), unsigned(m.standIn), unsigned(m.ground),
                        unsigned(m.groundTemplate), unsigned(oldTpl.first), unsigned(oldTpl.second), oldEdges.size());
            m.standIn = standIn, m.groundTemplate = 0;  // (the template: the stand-in's border with the ground itself)
            if (const char* gt = std::getenv("REBASE_GT")) m.groundTemplate = uint16_t(std::atoi(gt));  // (or with this game terrain as the ground)
            const auto tpl = tr::templateOf(ts, m);
            const std::vector<tr::TemplateRect> rects = tr::templateRects(ts, tpl.first, tpl.second);
            std::printf("  new template %u/%u: %zu rects\n", unsigned(tpl.first), unsigned(tpl.second), rects.size());
            // The source border's roles as shapes, and the template's rects' shapes.
            const char* geomEnv = std::getenv("REBASE_GEOM");
            const IsomTerrainTable* tt = IsomTerrainTable::forEra(uint16_t(e));
            const std::string tplName = std::string(kEraName[e]) + "|" + tt->terrainName(tpl.first) + "|" + tt->terrainName(tpl.second);
            const bool byGeom = geomEnv ? std::atoi(geomEnv) != 0 : mains.size() > 1 || tplName != mainSource;
            std::printf("  matching by %s\n", byGeom ? "shape (the template is another border)" : "role (the template is the source border)");
            std::map<std::string, std::string> keyOfSourceRole;
            std::map<uint16_t, std::string> keyOfRect;
            if (byGeom) {
                for (const std::string& source : mains) {  // (each source border's roles as shapes)
                    std::vector<std::string> parts;
                    std::stringstream ms(source);
                    for (std::string x; std::getline(ms, x, '|');) parts.push_back(x);
                    int se = -1;
                    for (int k = 0; k < 8; ++k)
                        if (parts.size() == 3 && parts[0] == kEraName[k]) se = k;
                    const IsomTerrainTable* st = se >= 0 ? IsomTerrainTable::forEra(uint16_t(se)) : nullptr;
                    uint16_t sg = 0, sterr = 0;
                    for (uint16_t i = 1; st && i <= st->terrainCount(); ++i) {
                        if (parts[1] == st->terrainName(i)) sg = i;
                        if (parts[2] == st->terrainName(i)) sterr = i;
                    }
                    Editor sed(data);
                    if (se >= 0 && sg && sterr && sed.newMap(64, 64, uint16_t(se), 1, err))
                        for (const tr::TemplateRect& r : tr::templateRects(sed.tileset(), sg, sterr))  // (the pair's own shapes and the cliff row)
                            keyOfSourceRole[source + "#" + r.role] = geomKey(sed.tileset(), sterr, r.group, sg) + "/" + std::to_string(r.row);
                }
                for (const tr::TemplateRect& r : rects) keyOfRect[r.group] = geomKey(ts, tpl.second, r.group, tpl.first) + "/" + std::to_string(r.row);
                std::set<std::string> sk, tk;
                for (const auto& [r, k] : keyOfSourceRole) sk.insert(k);
                for (const auto& [g, k] : keyOfRect) tk.insert(k);
                int common = 0;
                for (const std::string& k : sk) common += int(tk.count(k));
                std::printf("  shapes: source %zu, template %zu, in both %d\n", sk.size(), tk.size(), common);
                // the copies' slots as shapes
                for (auto& [t, sl] : slots) {
                    std::set<std::string> next;
                    for (const std::string& x : sl) {
                        const size_t c = x.rfind(':');
                        const auto it = keyOfSourceRole.find(x.substr(0, c));
                        if (it != keyOfSourceRole.end()) next.insert(it->second + x.substr(c));
                    }
                    sl = next;
                }
            } else {  // (by role: the template is the one source border; the slots without their source)
                for (auto& [t, sl] : slots) {
                    std::set<std::string> next;
                    for (const std::string& x : sl) next.insert(x.substr(x.find('#') + 1));
                    sl = next;
                }
            }
            auto slotOf = [&](const tr::TemplateRect& r, int half) { return (byGeom ? keyOfRect[r.group] : r.role) + ":" + std::to_string(half); };
            std::vector<std::array<uint16_t, 3>> pieces;
            int ambiguous = 0;
            std::set<uint16_t> lefts;
            for (const auto& [t, sl] : slots) lefts.insert(uint16_t(t & ~1u));
            std::map<uint16_t, std::set<uint16_t>> fitOf;  // left tile -> rects its pair fits
            std::map<uint16_t, std::map<uint16_t, int>> groupVotes;  // group -> rect -> pairs that fit it
            for (uint16_t t : lefts) {
                const auto il = slots.find(t), ir = slots.find(uint16_t(t + 1));
                std::set<uint16_t> fit;
                for (const tr::TemplateRect& r : rects)
                    if ((il == slots.end() || il->second.count(slotOf(r, 0))) && (ir == slots.end() || ir->second.count(slotOf(r, 1)))) fit.insert(r.group);
                fitOf[t] = fit;
                for (uint16_t g : fit) ++groupVotes[uint16_t(t >> 4)][g];
            }
            for (uint16_t t : lefts) {
                std::set<uint16_t> fit = fitOf[t];
                if (fit.size() > 1) {  // (the group's vote)
                    const auto& v = groupVotes[uint16_t(t >> 4)];
                    int best = 0, second = 0;
                    uint16_t bestRect = 0;
                    for (uint16_t g : fit) {
                        const int n = v.count(g) ? v.at(g) : 0;
                        if (n > best) second = best, best = n, bestRect = g;
                        else if (n > second) second = n;
                    }
                    if (best > second) fit = {bestRect};
                }
                if (fit.size() == 1) pieces.push_back({*fit.begin(), t, uint16_t(t + 1)});
                else if (fit.size() > 1 && std::getenv("REBASE_TIES_ALL")) {  // (the game itself has this picture in each of these rects)
                    for (uint16_t g : fit) pieces.push_back({g, t, uint16_t(t + 1)});
                } else if (fit.size() > 1) {
                    ++ambiguous;
                    std::printf("  ambiguous %u,%u:", unsigned(t), unsigned(t + 1));
                    for (uint16_t g : fit)
                        for (const tr::TemplateRect& r : rects)
                            if (r.group == g) std::printf(" %u %s;", unsigned(g), r.role.c_str());
                    std::printf("\n");
                }
            }
            // The new pieces: the template's rects from the copies; today's fill pieces (rects outside both templates) stay.
            std::vector<std::array<uint16_t, 3>> next;
            std::set<uint16_t> newRects;
            for (const tr::TemplateRect& r : rects) newRects.insert(r.group);
            std::set<uint16_t> filled;
            for (const auto& pc : pieces) next.push_back(pc), filled.insert(pc[0]);
            for (const auto& pc : m.pieces) {
                if (!newRects.count(pc[0]) && !oldEdges.count(pc[0])) next.push_back(pc);  // (fill)
                else if (keep && newRects.count(pc[0]) && !filled.count(pc[0]) && oldTpl == tpl) next.push_back(pc);
            }
            // keep, another template: a rect with no copy takes today's pieces of the rect of the same shape
            if (keep && oldTpl != tpl)
                for (const tr::TemplateRect& r : rects) {
                    if (filled.count(r.group)) continue;
                    const auto it = oldRectsOfShape.find(geomKey(ts, tpl.second, r.group));
                    if (it == oldRectsOfShape.end() || it->second.size() != 1) continue;
                    int n = 0;
                    for (const auto& pc : oldPieces)
                        if (pc[0] == it->second[0] && !tr::nullHalf(pc[1]) && !tr::nullHalf(pc[2])) next.push_back({r.group, pc[1], pc[2]}), ++n;
                    if (n) std::printf("  rect %u %s: %d pieces of today's rect %u (same shape)\n", unsigned(r.group), r.role.c_str(), n, unsigned(it->second[0]));
                }
            m.pieces = next;
            tr::normalize(ts, m);
            std::printf("  pieces from copies: %zu in %zu of %zu rects; ambiguous pairs left out: %d\n", pieces.size(), filled.size(), rects.size(), ambiguous);
            for (const tr::TemplateRect& r : rects)
                if (!filled.count(r.group)) std::printf("  no copy for rect %u %s\n", unsigned(r.group), r.role.c_str());
            std::set<uint16_t> withPieces;  // (the rects with a piece at the end: copies, or kept tiles)
            for (const auto& pc : m.pieces)
                if (!tr::nullHalf(pc[1]) && !tr::nullHalf(pc[2])) withPieces.insert(pc[0]);
            int empty = 0;
            for (const tr::TemplateRect& r : rects) empty += withPieces.count(r.group) ? 0 : 1;
            std::printf("  rects empty at the end: %d of %zu\n", empty, rects.size());
        }
        if (!found) return std::fprintf(stderr, "no terrain %s\n", name.c_str()), 1;
        tr::Rules rules;
        rules.tilesets.push_back(tsRules);
        if (!tr::save(argv[9], rules, err)) return std::fprintf(stderr, "save: %s\n", err.c_str()), 1;
        return 0;
    }

    // gaps: cmbw_neighbor_repair gaps <install> <maps folder> <out folder> [era]
    // For each NULL slot of a terrain's border (a rect half with no tile): the mod tiles no rule uses that the maps have
    // where that slot fits (3 or 4 of the place's neighbours are tiles with known slots, one at least the terrain's own,
    // and the slot is legal beside each of them). A vote is shared between the slots that fit a place. A slot takes its
    // top tile when it has 3 votes or more and 60 % of the slot's votes. Writes gaps.txt and gapfill.isom.
    if (mode == "gaps") {
        const std::string outDir = argv[4];
        const int only = argc > 5 ? std::atoi(argv[5]) : -1;
        auto maps = readMaps(argv[3], only);
        // The mod's brushes (SCMDraft brush files) as more places to look: a brush is a block of tiles that go together.
        // BRUSHES=0 leaves them out.
        int brushes = 0;
        if (const char* b = std::getenv("BRUSHES"); !b || std::atoi(b))
            for (int e = 0; e < 8; ++e) {
                if (only >= 0 && e != only) continue;
                for (const TerrainStamp& st : cmbw::brushStamps(argv[2], uint16_t(e))) {
                    MapTiles mt{st.name, st.width, st.height, st.tiles, true};
                    for (size_t i = 0; i < mt.tiles.size() && i < st.mask.size(); ++i)
                        if (!st.mask[i]) mt.tiles[i] = 0;
                    if (mt.w > 0 && mt.h > 0 && mt.tiles.size() >= size_t(mt.w) * mt.h) maps[size_t(e)].push_back(mt), ++brushes;
                }
            }
        std::printf("brushes: %d\n", brushes);
        long long brushVotes = 0;
        // (the reach file of the reach mode: "tileset, terrain, rect, half, tiles painted"; only those slots are gaps)
        std::set<std::string> reached;
        const bool haveReach = argc > 6;
        if (haveReach) {
            std::ifstream in(argv[6]);
            std::string line;
            while (std::getline(in, line)) {
                const size_t tab = line.rfind('\t');
                if (tab != std::string::npos && std::atoi(line.c_str() + tab + 1) > 0) reached.insert(line.substr(0, tab));
            }
        }
        tr::Rules rules;
        FILE* out = std::fopen((outDir + "/gaps.txt").c_str(), "w");
        int filled = 0, open = 0;
        for (int e = 0; e < 8; ++e) {
            if (only >= 0 && e != only) continue;
            Editor ed(data);
            if (!ed.newMap(64, 64, uint16_t(e), 1, err)) continue;
            const Tileset& ts = ed.tileset();
            tr::TilesetRules tsRules = tr::rulesOf(ts);
            std::vector<uint8_t> used(65536, 0);  // the tiles some terrain's rules use
            for (const auto& t : tsRules.terrains)
                for (const auto& p : t.t.pieces) used[p[1]] = used[p[2]] = 1;
            {
                // The brushes' mod tiles: how many no rule uses (a brush tile that no terrain has).
                std::set<uint16_t> inBrush, inMaps;
                for (const MapTiles& mp : maps[size_t(e)])
                    for (uint16_t v : mp.tiles)
                        if (unsigned(v >> 4) >= kStock[size_t(e)]) (mp.stamp ? inBrush : inMaps).insert(v);
                size_t brushFree = 0, mapFree = 0;
                for (uint16_t v : inBrush) brushFree += !used[v];
                for (uint16_t v : inMaps) mapFree += !used[v];
                std::printf("%s: mod tiles in brushes %zu (no rule uses %zu), on maps %zu (no rule uses %zu)\n", kEraName[e], inBrush.size(), brushFree, inMaps.size(),
                            mapFree);
            }
            std::map<std::pair<uint16_t, uint16_t>, Legal> legalCache;
            for (tr::Terrain& terrain : tsRules.terrains) {
                Tileset::ModTerrain& m = terrain.t;
                const auto tpl = tr::templateOf(ts, m);
                const std::vector<tr::TemplateRect> rects = tr::templateRects(ts, tpl.first, tpl.second);
                if (tpl.first == tpl.second || rects.empty()) continue;
                if (!legalCache.count(tpl)) legalCache[tpl] = legalOf(data, e, tpl.first, tpl.second);
                const Legal& legal = legalCache[tpl];
                Study s;
                if (!study(ts, e, m, maps[size_t(e)], legal, s)) continue;
                const bool gameGround = m.ground == tpl.first && m.groundTemplate == 0;
                std::vector<const std::vector<uint32_t>*> slotsOf(65536, nullptr);
                std::vector<uint8_t> ownTile(65536, 0);
                for (size_t i = 0; i < s.nodes.size(); ++i) slotsOf[s.nodes[i].tile] = &s.nodes[i].slots, ownTile[s.nodes[i].tile] = i < s.own;
                std::set<uint32_t> all, gapSet;
                std::map<uint16_t, std::string> role;
                for (size_t i = 0; i < s.own; ++i)
                    for (uint32_t k : s.nodes[i].slots) all.insert(k);
                for (const tr::TemplateRect& r : rects) {
                    role[r.group] = r.role;
                    for (int half = 0; half < 2; ++half) {
                        all.insert(slot(r.group, half));
                        bool have = false;
                        for (const auto& p : m.pieces) have = have || (p[0] == r.group && !tr::nullHalf(p[size_t(1 + half)]));
                        if (!have && (!haveReach || reached.count(std::string(kEraName[e]) + "\t" + m.name + "\t" + std::to_string(r.group) + "\t" +
                                                                      std::to_string(half))))
                            gapSet.insert(slot(r.group, half));
                    }
                }
                if (gapSet.empty()) continue;
                std::map<uint32_t, std::map<uint16_t, double>> votes;
                for (const MapTiles& mp : maps[size_t(e)]) {
                    const int w = mp.w, h = mp.h;
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x) {
                            const uint16_t v = mp.tiles[size_t(y) * w + x];
                            if (unsigned(v >> 4) < kStock[size_t(e)] || slotsOf[v]) continue;
                            // The neighbours: left, right, up, down; {dir, 1 when the neighbour is the first of the pair}.
                            const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1}, nd[4] = {0, 0, 1, 1}, nf[4] = {1, 0, 1, 0};
                            std::vector<uint32_t> known[4];
                            int knownN = 0, ownN = 0;
                            for (int k = 0; k < 4; ++k) {
                                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h) continue;
                                const uint16_t n = mp.tiles[size_t(ny[k]) * w + nx[k]];
                                if (slotsOf[n]) {
                                    known[k] = *slotsOf[n], ownN += ownTile[n];
                                } else if (gameGround && unsigned(n >> 4) < kStock[size_t(e)] && (n >> 4) != 0) {
                                    const uint16_t g = uint16_t(n >> 4);
                                    const uint32_t sl = slot(uint16_t(g & ~1u), g & 1);
                                    if (legal.slots.count(sl)) known[k] = {sl};
                                }
                                knownN += !known[k].empty();
                            }
                            // (a brush is small: one known neighbour of the terrain's is enough there, when few slots fit)
                            if ((mp.stamp ? knownN < 1 : knownN < 3) || ownN < 1) continue;
                            std::vector<uint32_t> fits;
                            for (uint32_t c : all) {
                                bool ok = true;
                                for (int k = 0; k < 4 && ok; ++k)
                                    if (!known[k].empty()) ok = nf[k] ? s.ok(known[k], {c}, nd[k]) : s.ok({c}, known[k], nd[k]);
                                if (ok) fits.push_back(c);
                            }
                            if (mp.stamp && fits.size() > 4) continue;
                            for (uint32_t c : fits)
                                if (gapSet.count(c)) votes[c][v] += 1.0 / double(fits.size()), brushVotes += mp.stamp;
                        }
                }
                // The terrain's piece layout: the right tile is the next subtile (1) or the same subtile of the next group (16).
                int next1 = 0, next16 = 0;
                for (const auto& p : m.pieces)
                    if (!tr::nullHalf(p[1]) && !tr::nullHalf(p[2]) && s.edges.count(p[0])) next1 += p[2] == p[1] + 1, next16 += p[2] == p[1] + 16;
                const uint16_t step = next16 > next1 ? 16 : 1;
                // Each gap rect: its candidate pieces by their left tile (a right half's candidate votes for its left partner).
                std::map<uint16_t, std::map<uint16_t, double>> leftVotes;
                for (uint32_t g : gapSet)
                    for (const auto& [tile, n] : votes[g]) {
                        const uint16_t left = (g & 1) ? uint16_t(tile - step) : tile;
                        if (used[left] || used[uint16_t(left + step)] || ts.megatileFor(left) <= 0 || ts.megatileFor(uint16_t(left + step)) <= 0) continue;
                        if (step == 1 && (left & 1)) continue;
                        leftVotes[uint16_t(g >> 1)][left] += n;
                    }
                struct Cand {
                    uint16_t rect, left;
                    double n;
                };
                std::vector<Cand> tops;
                std::set<uint16_t> gapRects;
                for (uint32_t g : gapSet) gapRects.insert(uint16_t(g >> 1));
                for (uint16_t rect : gapRects) {
                    Cand c{rect, 0, 0};
                    for (const auto& [left, n] : leftVotes[rect])
                        if (n > c.n) c.left = left, c.n = n;
                    if (c.left) tops.push_back(c);
                }
                // The longest run of rects whose tiles do not go down as the rects go up.
                std::vector<int> len(tops.size(), 1), prev(tops.size(), -1);
                int bestEnd = -1;
                for (size_t i = 0; i < tops.size(); ++i) {
                    for (size_t j = 0; j < i; ++j)
                        if (tops[j].left <= tops[i].left && len[j] + 1 > len[i]) len[i] = len[j] + 1, prev[i] = int(j);
                    if (bestEnd < 0 || len[i] > len[size_t(bestEnd)]) bestEnd = int(i);
                }
                std::set<uint16_t> inRun;
                if (bestEnd >= 0 && len[size_t(bestEnd)] >= 3)
                    for (int i = bestEnd; i >= 0; i = prev[size_t(i)]) inRun.insert(tops[size_t(i)].rect);
                bool changed = false;
                for (uint16_t rect : gapRects) {
                    const Cand* c = nullptr;
                    for (const Cand& t : tops)
                        if (t.rect == rect) c = &t;
                    const char* why = !c ? "no tile on the maps" : inRun.count(rect) ? "run" : c->n >= 3 ? "votes" : "not kept";
                    const bool take = c && (inRun.count(rect) || c->n >= 3);
                    if (out)
                        std::fprintf(out, "%s\t%s\t%u\t%s\t%u\t%u\t%.1f\t%s\n", kEraName[e], m.name.c_str(), unsigned(rect), role[rect].c_str(), unsigned(c ? c->left : 0),
                                     unsigned(c ? c->left + step : 0), c ? c->n : 0.0, why);
                    if (!take) {
                        ++open;
                        continue;
                    }
                    ++filled, changed = true;
                    for (size_t half = 0; half < 2; ++half) {
                        if (!gapSet.count(slot(rect, int(half)))) continue;
                        const uint16_t tile = uint16_t(c->left + (half ? step : 0));
                        bool placed = false;
                        for (auto& p : m.pieces)
                            if (!placed && p[0] == rect && tr::nullHalf(p[1 + half])) p[1 + half] = tile, placed = true;
                        if (!placed) {
                            std::array<uint16_t, 3> p = {rect, 0, 0};
                            p[1 + half] = tile;
                            m.pieces.insert(m.pieces.begin(), p);
                        }
                    }
                }
                if (changed) tr::normalize(ts, m);
            }
            rules.tilesets.push_back(tsRules);
        }
        if (out) std::fclose(out);
        if (!tr::save(outDir + "/gapfill.isom", rules, err)) return std::fprintf(stderr, "save: %s\n", err.c_str()), 1;
        return std::printf("NULL rects: %d filled, %d open; votes from brushes: %lld\n", filled, open, brushVotes), 0;
    }

    // repair
    const std::string outDir = argv[4];
    std::set<std::string> allow;
    bool allowAll = true;
    if (argc > 5 && std::string(argv[5]) != "-") {
        allowAll = false;
        std::ifstream in(argv[5]);
        std::string line;
        while (std::getline(in, line)) allow.insert(line);
    }
    const int only = argc > 6 ? std::atoi(argv[6]) : -1;
    const auto maps = readMaps(argv[3], only);
    tr::Rules rules;
    FILE* movesOut = std::fopen((outDir + "/moves.txt").c_str(), "w");
    long long allUses = 0, allBefore = 0, allAfter = 0;
    int allMoves = 0, changedTerrains = 0;
    for (int e = 0; e < 8; ++e) {
        if (only >= 0 && e != only) continue;
        Editor ed(data);
        if (!ed.newMap(64, 64, uint16_t(e), 1, err)) continue;
        const Tileset& ts = ed.tileset();
        tr::TilesetRules tsRules = tr::rulesOf(ts);
        std::map<std::pair<uint16_t, uint16_t>, Legal> legalCache;
        for (tr::Terrain& terrain : tsRules.terrains) {
            Tileset::ModTerrain& m = terrain.t;
            const auto tpl = tr::templateOf(ts, m);
            if (tpl.first == tpl.second || edgeRectsOf(ts, m).empty()) continue;
            if (!allowAll && !allow.count(std::string(kEraName[e]) + "\t" + m.name)) continue;
            if (!legalCache.count(tpl)) legalCache[tpl] = legalOf(data, e, tpl.first, tpl.second);
            Study s;
            if (!study(ts, e, m, maps[size_t(e)], legalCache[tpl], s) || s.uses < 300) continue;
            const long long before = s.total();
            // How many tiles stand in each slot (a move never empties one).
            std::map<uint32_t, int> inSlot;
            for (size_t i = 0; i < s.own; ++i)
                if (!s.nodes[i].fixed)
                    for (uint32_t k : s.nodes[i].slots) ++inSlot[k];
            std::vector<uint32_t> candidates;
            for (uint16_t rect : s.edges) candidates.push_back(slot(rect, 0)), candidates.push_back(slot(rect, 1));
            std::vector<int> movable;
            for (size_t i = 0; i < s.own; ++i)
                if (!s.nodes[i].fixed && s.nodes[i].uses >= 10) movable.push_back(int(i));
            std::set<uint16_t> movedTiles;
            int moves = 0;
            for (int round = 0; round < 10; ++round) {
                bool changed = false;
                for (int a : movable) {
                    Node& na = s.nodes[size_t(a)];
                    const long long curA = s.legalAround({a});
                    if (curA == na.uses) continue;
                    long long bestGain = 0;
                    int bestSwap = -1;
                    uint32_t bestSlot = 0;
                    const std::vector<uint32_t> slotsA = na.slots;
                    // Two tiles swap their slots.
                    for (int b : movable) {
                        if (b == a) continue;
                        Node& nb = s.nodes[size_t(b)];
                        if (nb.slots == slotsA) continue;
                        const long long cur = s.legalAround({a, b});
                        const std::vector<uint32_t> slotsB = nb.slots;
                        na.slots = slotsB, nb.slots = slotsA;
                        const long long now = s.legalAround({a, b});
                        na.slots = slotsA, nb.slots = slotsB;
                        const long long need = std::max<long long>(5, (na.uses + nb.uses) / 20);
                        if (now - cur >= need && now - cur > bestGain) bestGain = now - cur, bestSwap = b, bestSlot = 0;
                    }
                    // The tile joins another slot (its own slots each keep a tile).
                    bool mayLeave = true;
                    for (uint32_t k : slotsA) mayLeave = mayLeave && inSlot[k] > 1;
                    if (mayLeave)
                        for (uint32_t c : candidates) {
                            na.slots = {c};
                            const long long now = s.legalAround({a});
                            na.slots = slotsA;
                            const long long need = std::max<long long>(5, na.uses / 10);
                            if (now - curA >= need && now - curA > bestGain) bestGain = now - curA, bestSwap = -1, bestSlot = c;
                        }
                    if (bestGain <= 0) continue;
                    if (bestSwap >= 0) {
                        Node& nb = s.nodes[size_t(bestSwap)];
                        std::swap(na.slots, nb.slots);
                        movedTiles.insert(na.tile), movedTiles.insert(nb.tile);
                    } else {
                        for (uint32_t k : slotsA) --inSlot[k];
                        na.slots = {bestSlot}, ++inSlot[bestSlot];
                        movedTiles.insert(na.tile);
                    }
                    ++moves, changed = true;
                }
                if (!changed) break;
            }
            const long long after = s.total();
            allUses += s.uses, allBefore += before, allAfter += after, allMoves += moves, changedTerrains += moves > 0;
            if (movesOut)
                std::fprintf(movesOut, "%s\t%s\t%lld\t%.1f\t%.1f\t%d\n", kEraName[e], m.name.c_str(), s.uses, 100.0 * double(before) / double(s.uses),
                             100.0 * double(after) / double(s.uses), moves);
            if (movedTiles.empty()) continue;
            // The pieces: each moved tile leaves its old border pieces (that half NULL) and goes into its new slots: a
            // piece of the rect with that half free, else a new piece.
            for (auto& p : m.pieces)
                for (size_t half = 0; half < 2; ++half)
                    if (movedTiles.count(p[1 + half]) && s.edges.count(p[0])) p[1 + half] = 0;
            for (size_t i = 0; i < s.own; ++i) {
                const Node& n = s.nodes[i];
                if (!movedTiles.count(n.tile)) continue;
                for (uint32_t k : n.slots) {
                    const uint16_t rect = uint16_t(k >> 1);
                    const size_t half = k & 1;
                    bool placed = false;
                    for (auto& p : m.pieces)
                        if (!placed && p[0] == rect && tr::nullHalf(p[1 + half]) && !tr::nullHalf(p[2 - half])) p[1 + half] = n.tile, placed = true;
                    for (auto& p : m.pieces)
                        if (!placed && p[0] == rect && tr::nullHalf(p[1]) && tr::nullHalf(p[2])) p[1 + half] = n.tile, placed = true;
                    if (!placed) {
                        std::array<uint16_t, 3> p = {rect, 0, 0};
                        p[1 + half] = n.tile;
                        m.pieces.insert(m.pieces.begin(), p);
                    }
                }
            }
            tr::normalize(ts, m);
        }
        rules.tilesets.push_back(tsRules);
    }
    if (movesOut) std::fclose(movesOut);
    if (!tr::save(outDir + "/repaired.isom", rules, err)) return std::fprintf(stderr, "save: %s\n", err.c_str()), 1;
    std::printf("legal before %.1f%%, after %.1f%%; %d moves in %d terrains\n", allUses ? 100.0 * double(allBefore) / double(allUses) : 0.0,
                allUses ? 100.0 * double(allAfter) / double(allUses) : 0.0, allMoves, changedTerrains);
    return 0;
}
