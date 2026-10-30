// A mod's isometric terrain rules as a file (include/stardraft/mods/terrain_rules.h).
#include "stardraft/mods/terrain_rules.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

#include "stardraft/isom.h"
#include "stardraft/isom_core.h"

namespace stardraft::terrainrules {

namespace {

struct Out {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { u8(uint8_t(v)), u8(uint8_t(v >> 8)); }
    void u32(uint32_t v) { u16(uint16_t(v)), u16(uint16_t(v >> 16)); }
    void u64(uint64_t v) { u32(uint32_t(v)), u32(uint32_t(v >> 32)); }
    void str(const std::string& s) {
        u16(uint16_t(std::min<size_t>(s.size(), 0xFFFF)));
        b.insert(b.end(), s.begin(), s.begin() + std::min<size_t>(s.size(), 0xFFFF));
    }
};
struct In {
    const std::vector<uint8_t>& b;
    size_t at = 0;
    bool ok = true;
    bool need(size_t n) { return ok = ok && at + n <= b.size(); }
    uint8_t u8() { return need(1) ? b[at++] : 0; }
    uint16_t u16() { const uint16_t lo = u8(); return uint16_t(lo | u8() << 8); }
    uint32_t u32() { const uint32_t lo = u16(); return lo | uint32_t(u16()) << 16; }
    uint64_t u64() { const uint64_t lo = u32(); return lo | uint64_t(u32()) << 32; }
    std::string str() {
        const uint16_t n = u16();
        if (!need(n)) return {};
        std::string s(b.begin() + long(at), b.begin() + long(at + n));
        at += n;
        return s;
    }
};

// The groups of a game border (ground a, terrain b): the ones the game uses for rects with the two (as the built-in
// rows find their gaps, src/mods/cmbw.cpp blockPieces).
std::vector<uint16_t> borderGroups(const Tileset& t, uint16_t a, uint16_t b) {
    std::vector<uint16_t> out;
    const IsomTiles* tiles = IsomTiles::forTileset(t);
    if (!tiles || !a || !b) return out;
    const std::vector<std::array<uint16_t, 2>> terrains = tileGroupTerrains(t);
    for (size_t g = 0; g + 1 < terrains.size(); ++g)
        if (terrains[g][1] && std::minmax(terrains[g][0], terrains[g][1]) == std::minmax(a, b) && !tiles->rectsOf(uint16_t(g)).empty()) out.push_back(uint16_t(g));
    return out;
}

}  // namespace

const char* familyName(Family f) {
    switch (f) {
        case Family::Patch: return "patch";
        case Family::Blend: return "blend";
        case Family::Pavement: return "pavement";
        case Family::Wall: return "wall";
        case Family::Cliff: return "cliff";
        default: return "other";
    }
}

std::vector<TemplateRect> templateRects(const Tileset& t, uint16_t ground, uint16_t terrain) {
    std::vector<TemplateRect> out;
    const IsomTiles* tiles = IsomTiles::forTileset(t);
    const IsomTerrainTable* table = IsomTerrainTable::forEra(t.era);
    if (!tiles || !table) return out;
    // (a rect is 2 x 1 tiles between two diamonds, L and R: even parity, L up and left of R; odd, L down and left of R.
    // Its 6 corner points (the two diamonds' corners, two of them shared), from the rect's middle, in pixels)
    static const float kEven[6][2] = {{-96, -16}, {-32, -48}, {32, -16}, {-32, 16}, {96, 16}, {32, 48}};  // L left, top, right, bottom; R right, bottom
    static const float kOdd[6][2] = {{-96, 16}, {-32, -16}, {32, 16}, {-32, 48}, {32, -48}, {96, -16}};   // L left, top, right, bottom; R top, right
    static const char* kDirs[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    for (uint16_t g : borderGroups(t, ground, terrain)) {
        TemplateRect r;
        r.group = g;
        float weight[6] = {}, n = 0;
        for (const IsomTiles::Rect& rc : tiles->rectsOf(g)) {
            const auto L = table->corners(rc.left), R = table->corners(rc.right);
            const uint16_t pts[6] = {L[0], L[1], L[2], L[3], rc.parity ? R[1] : R[2], rc.parity ? R[2] : R[3]};
            for (int i = 0; i < 6; ++i) weight[i] += pts[i] == terrain ? 1.0f : 0.0f;
            n += 1;
        }
        if (n > 0) {
            float cx = 0, cy = 0, fill = 0;
            const bool odd = tiles->rectsOf(g).front().parity != 0;
            for (int i = 0; i < 6; ++i) {
                const float w = weight[i] / n;
                fill += w, cx += w * (odd ? kOdd[i][0] : kEven[i][0]), cy += w * (odd ? kOdd[i][1] : kEven[i][1]);
            }
            r.fill = fill;
            if (fill > 0.01f && fill < 5.99f) {
                const float ang = std::atan2(cx, -cy);  // (0 = N, clockwise)
                r.dir = int(std::lround(ang / (3.14159265f / 4))) & 7;
            }
        }
        const uint16_t top = tiles->stackTop(g), bottom = tiles->stackBottom(g);
        r.row = top && bottom ? 2 : bottom ? 1 : top ? 3 : 0;  // (a bottom link: a row under it, so this is the top)
        std::string role = r.fill <= 0.01f ? "outside" : r.fill >= 5.99f ? "inside" : r.fill < 2.5f ? "corner" : r.fill <= 3.5f ? "edge" : "inner corner";
        if (r.dir >= 0) role += std::string(" ") + kDirs[r.dir];
        if (r.row) role += r.row == 1 ? " top" : r.row == 2 ? " middle" : " bottom";
        r.role = role;
        out.push_back(std::move(r));
    }
    std::map<std::string, int> seen;  // (two rects with one role: numbered)
    for (const TemplateRect& r : out) ++seen[r.role];
    std::map<std::string, int> nth;
    for (TemplateRect& r : out)
        if (seen[r.role] > 1) r.role += " " + std::to_string(++nth[r.role]);
    return out;
}

Family familyOf(const std::vector<TemplateRect>& rects, const Tileset& t) {
    if (rects.empty()) return Family::Other;
    const IsomTiles* tiles = IsomTiles::forTileset(t);
    std::set<uint16_t> ids;
    for (const TemplateRect& r : rects)
        if (tiles) {
            if (tiles->stackTop(r.group)) ids.insert(tiles->stackTop(r.group));
            if (tiles->stackBottom(r.group)) ids.insert(tiles->stackBottom(r.group));
        }
    const size_t n = rects.size();
    if (n <= 16) return Family::Patch;
    if (n <= 28) return ids.size() <= 1 ? Family::Pavement : Family::Blend;
    if (n <= 40) return Family::Wall;
    return Family::Cliff;
}

std::pair<uint16_t, uint16_t> templateOf(const Tileset& t, const Tileset::ModTerrain& m) {
    uint16_t ground = m.groundTemplate ? m.groundTemplate : m.ground;
    for (int n = 0; n < 16 && ground >= Tileset::kModTerrainIds; ++n)  // (a mod ground with no template: its stand-in)
        if (const Tileset::ModTerrain* g = t.modTerrain(ground)) ground = g->groundTemplate ? g->groundTemplate : g->standIn;
        else break;
    return {ground, m.standIn};
}

PieceState stateOf(const std::array<uint16_t, 3>& p) {
    const bool l = nullHalf(p[1]), r = nullHalf(p[2]);
    if (l && r) return PieceState::Null;
    if (l || r) return PieceState::Partial;
    return (p[1] >> 4) == p[0] && (p[2] >> 4) == p[0] + 1 ? PieceState::Borrowed : PieceState::Own;
}

uint16_t gameTile(const Tileset& t, uint32_t group) {
    if (group >= t.groups.size()) return 0;
    for (uint16_t k = 0; k < 16; ++k)
        if (t.megatileFor(uint16_t(group << 4 | k)) > 0) return uint16_t(group << 4 | k);
    return 0;
}

void normalize(const Tileset& t, Tileset::ModTerrain& m) {
    const auto [ground, terrain] = templateOf(t, m);
    const std::vector<uint16_t> groups = borderGroups(t, ground, terrain);
    const std::set<uint16_t> in(groups.begin(), groups.end());
    std::vector<std::array<uint16_t, 3>> set, fill, gaps;
    const size_t fillBegin = std::min(m.setPieces, m.pieces.size()), fillEnd = m.pieces.size() - std::min(m.gapPieces, m.pieces.size());
    for (size_t i = 0; i < m.pieces.size(); ++i) {
        const auto& p = m.pieces[i];
        const bool wasFill = i >= fillBegin && i < fillEnd && !in.count(p[0]);
        if (wasFill) fill.push_back(p);
        else if (stateOf(p) == PieceState::Null) {
            if (in.count(p[0])) gaps.push_back(p);  // (a NULL piece of another border, from a template change: gone)
        } else set.push_back(p);  // (a partial piece stays in the set: the engine reads its one tile)
    }
    std::set<uint16_t> have;
    for (const auto& p : m.pieces) have.insert(p[0]);
    for (uint16_t g : groups)
        if (!have.count(g)) gaps.push_back({g, 0, 1});
    // (the template's order within the set and the gaps: the game's layout order)
    auto order = [&](const std::array<uint16_t, 3>& a, const std::array<uint16_t, 3>& b) { return a[0] < b[0]; };
    std::stable_sort(set.begin(), set.end(), order);
    std::stable_sort(gaps.begin(), gaps.end(), order);
    m.pieces = std::move(set);
    m.setPieces = m.pieces.size();
    m.pieces.insert(m.pieces.end(), fill.begin(), fill.end());
    m.pieces.insert(m.pieces.end(), gaps.begin(), gaps.end());
    m.gapPieces = gaps.size();
}

std::pair<int, int> completeness(const Tileset& t, const Tileset::ModTerrain& m) {
    const auto [ground, terrain] = templateOf(t, m);
    const std::vector<uint16_t> groups = borderGroups(t, ground, terrain);
    std::set<uint16_t> done, bad;  // (a group is done when every piece of it is: the engine picks among all of them)
    for (const auto& p : m.pieces)
        if (const PieceState s = stateOf(p); s == PieceState::Own || s == PieceState::Borrowed) done.insert(p[0]);
        else bad.insert(p[0]);
    int n = 0;
    for (uint16_t g : groups) n += done.count(g) && !bad.count(g) ? 1 : 0;
    return {n, int(groups.size())};
}

uint64_t fingerprint(const Tileset& t) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    auto mix = [&](uint16_t v) { h = (h ^ (v & 0xFF)) * 1099511628211ull, h = (h ^ (v >> 8)) * 1099511628211ull; };
    for (const TileGroup& g : t.groups) {
        mix(g.type);
        for (uint16_t m : g.megatiles) mix(m);
    }
    return h;
}

TilesetRules rulesOf(const Tileset& t) {
    TilesetRules r;
    r.era = uint8_t(t.era & 7), r.groups = uint32_t(t.groups.size()), r.fingerprint = fingerprint(t);
    for (const Tileset::ModTerrain& m : t.modTerrains) r.terrains.push_back({m});
    return r;
}

std::vector<uint8_t> write(const Rules& r) {
    Out o;
    for (char c : std::string("SDTR")) o.u8(uint8_t(c));
    o.u16(kVersion), o.u16(uint16_t(r.tilesets.size()));
    for (const TilesetRules& ts : r.tilesets) {
        o.u8(ts.era), o.u32(ts.groups), o.u64(ts.fingerprint), o.u16(uint16_t(ts.terrains.size()));
        for (const Terrain& x : ts.terrains) {
            const Tileset::ModTerrain& m = x.t;
            o.u16(m.id), o.u8(uint8_t((m.fill ? 1 : 0) | (m.fromMaps ? 2 : 0) | (m.draft ? 4 : 0)));
            o.u16(m.standIn), o.u16(m.ground), o.u16(m.groundTemplate), o.u16(m.variantOf), o.u32(m.fillKey);
            o.u32(uint32_t(m.setPieces)), o.u32(uint32_t(m.gapPieces)), o.str(m.name);
            o.u32(uint32_t(m.pieces.size()));
            for (const auto& p : m.pieces) o.u16(p[0]), o.u16(p[1]), o.u16(p[2]);
        }
    }
    return o.b;
}

bool read(const std::vector<uint8_t>& bytes, Rules& r, std::string& err) {
    In in{bytes};
    r = {};
    if (bytes.size() < 8 || std::string(bytes.begin(), bytes.begin() + 4) != "SDTR") return err = "not a terrain rules file", false;
    in.at = 4;
    const uint16_t version = in.u16();
    if (version != 1 && version != kVersion) return err = "terrain rules version " + std::to_string(version) + " (this program reads 1 and 2)", false;
    const uint16_t sets = in.u16();
    for (uint16_t s = 0; s < sets && in.ok; ++s) {
        TilesetRules ts;
        ts.era = in.u8(), ts.groups = in.u32(), ts.fingerprint = in.u64();
        const uint16_t n = in.u16();
        for (uint16_t k = 0; k < n && in.ok; ++k) {
            Terrain x;
            Tileset::ModTerrain& m = x.t;
            m.id = in.u16();
            if (version == 1) in.u8();  // (the kind: gone)
            const uint8_t flags = in.u8();
            m.fill = flags & 1, m.fromMaps = (flags & 2) != 0, m.draft = (flags & 4) != 0;
            m.standIn = in.u16(), m.ground = in.u16(), m.groundTemplate = in.u16(), m.variantOf = in.u16(), m.fillKey = in.u32();
            m.setPieces = in.u32(), m.gapPieces = in.u32(), m.name = in.str();
            const uint32_t pieces = in.u32();
            if (!in.need(size_t(pieces) * 6)) break;
            m.pieces.resize(pieces);
            for (auto& p : m.pieces) p[0] = in.u16(), p[1] = in.u16(), p[2] = in.u16();
            ts.terrains.push_back(std::move(x));
        }
        r.tilesets.push_back(std::move(ts));
    }
    if (!in.ok) return err = "the terrain rules file is cut short", false;
    return true;
}

bool save(const std::filesystem::path& file, const Rules& r, std::string& err) {
    const std::vector<uint8_t> b = write(r);
    std::ofstream f(file, std::ios::binary);
    if (!f || !f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()))) return err = "could not write " + file.filename().string(), false;
    return true;
}

bool load(const std::filesystem::path& file, Rules& r, std::string& err) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return err = "could not open " + file.filename().string(), false;
    const std::vector<uint8_t> b{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    return read(b, r, err);
}

bool apply(Tileset& t, const Rules& r, std::string* why) {
    for (const TilesetRules& ts : r.tilesets) {
        if (ts.era != (t.era & 7)) continue;
        if (ts.groups != t.groups.size() || ts.fingerprint != fingerprint(t)) {
            if (why) *why = "the rules were made for other tiles of this tileset (the mod changed its groups)";
            return false;
        }
        for (const Terrain& x : ts.terrains) {
            if (x.t.variantOf) continue;  // (a variant is made from its terrain: below)
            Tileset::ModTerrain m = x.t;
            normalize(t, m);  // (a file's terrain in the engine's order, every template rect present)
            bool replaced = false;
            for (Tileset::ModTerrain& o : t.modTerrains)
                if (o.id == m.id) o = m, replaced = true;
            if (!replaced) t.modTerrains.push_back(m);
            // (its variants: its set again on another ground, the same pieces)
            for (Tileset::ModTerrain& v : t.modTerrains)
                if (v.variantOf == m.id) v.pieces = m.pieces, v.setPieces = m.setPieces, v.gapPieces = m.gapPieces;
        }
        return true;
    }
    if (why) *why = "the file has no rules for this tileset";
    return false;
}

}  // namespace stardraft::terrainrules
