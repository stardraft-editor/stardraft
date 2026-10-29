// A mod's isometric terrain rules as a file (include/stardraft/mods/terrain_rules.h).
#include "stardraft/mods/terrain_rules.h"

#include <fstream>
#include <iterator>
#include <set>

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

}  // namespace

uint64_t fingerprint(const Tileset& t) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    auto mix = [&](uint16_t v) { h = (h ^ (v & 0xFF)) * 1099511628211ull, h = (h ^ (v >> 8)) * 1099511628211ull; };
    for (const TileGroup& g : t.groups) {
        mix(g.type);
        for (uint16_t m : g.megatiles) mix(m);
    }
    return h;
}

Kind kindOf(const Tileset&, const Tileset::ModTerrain& m) {
    std::set<uint16_t> rects;  // (the game rects its own set has pieces for)
    for (size_t i = 0; i < m.setPieces && i < m.pieces.size(); ++i) rects.insert(m.pieces[i][0]);
    if (rects.empty()) return Kind::Field;
    return rects.size() > 16 ? Kind::Wall : Kind::Path;  // (a path's border has 16 rects, a cliff's or a wall's 22 and more)
}

TilesetRules rulesOf(const Tileset& t) {
    TilesetRules r;
    r.era = uint8_t(t.era & 7), r.groups = uint32_t(t.groups.size()), r.fingerprint = fingerprint(t);
    for (const Tileset::ModTerrain& m : t.modTerrains) r.terrains.push_back({kindOf(t, m), m});
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
            o.u16(m.id), o.u8(uint8_t(x.kind)), o.u8(uint8_t((m.fill ? 1 : 0) | (m.fromMaps ? 2 : 0)));
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
    if (const uint16_t v = in.u16(); v != kVersion) return err = "terrain rules version " + std::to_string(v) + " (this program reads 1)", false;
    const uint16_t sets = in.u16();
    for (uint16_t s = 0; s < sets && in.ok; ++s) {
        TilesetRules ts;
        ts.era = in.u8(), ts.groups = in.u32(), ts.fingerprint = in.u64();
        const uint16_t n = in.u16();
        for (uint16_t k = 0; k < n && in.ok; ++k) {
            Terrain x;
            Tileset::ModTerrain& m = x.t;
            m.id = in.u16(), x.kind = Kind(in.u8());
            const uint8_t flags = in.u8();
            m.fill = flags & 1, m.fromMaps = (flags & 2) != 0;
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
            bool replaced = false;
            for (Tileset::ModTerrain& m : t.modTerrains)
                if (m.id == x.t.id) m = x.t, replaced = true;
            if (!replaced) t.modTerrains.push_back(x.t);
        }
        return true;
    }
    if (why) *why = "the file has no rules for this tileset";
    return false;
}

}  // namespace stardraft::terrainrules
