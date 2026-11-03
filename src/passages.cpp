// Passages files (include/stardraft/passages.h).
#include "stardraft/passages.h"

#include <exception>
#include <fstream>
#include <iterator>

#include "file_write.h"
#include "json.h"

namespace stardraft::passages {

namespace fs = std::filesystem;

namespace {

constexpr const char* kFormat = "StarCraft passages";
constexpr int kVersion = 1;

std::string pathText(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string tilesText(const TileMap& m) {
    std::string o;
    for (const auto& [a, b] : m) o += (o.empty() ? "[" : ", [") + std::to_string(a) + ", " + std::to_string(b) + "]";
    return "[" + o + "]";
}

bool readTiles(const Json* j, TileMap& out) {
    if (!j) return true;
    if (j->type != Json::Type::Array) return false;
    for (const Json& p : j->items) {
        if (p.type != Json::Type::Array || p.items.size() != 2) return false;
        const Json &a = p.items[0], &b = p.items[1];
        if (a.type != Json::Type::Number || b.type != Json::Type::Number || a.n < 0 || a.n > 0xFFFF || b.n < 0 || b.n > 0xFFFF) return false;
        out[uint16_t(a.n)] = uint16_t(b.n);
    }
    return true;
}

Passages gInUse;
int gRevision = 0;

}  // namespace

std::string toText(const Passages& p) {
    std::string o = std::string("{\"format\": ") + quote(kFormat) + ", \"version\": " + std::to_string(kVersion) + ",\n \"ramps\": [";
    for (size_t i = 0; i < p.ramps.size(); ++i) {
        const RampMapping& r = p.ramps[i];
        std::string t;
        for (const RampTerrains& x : r.terrains) t += (t.empty() ? "" : ", ") + std::string("{\"ground\": ") + quote(x.ground) + ", \"cliff\": " + quote(x.cliff) + "}";
        o += std::string(i ? "," : "") + "\n  {\"name\": " + quote(r.name) + ", \"tileset\": " + std::to_string(r.era) + ", \"template\": " + std::to_string(r.block) +
             ", \"terrains\": [" + t + "],\n   \"tiles\": " + tilesText(r.tiles) + "}";
    }
    o += "],\n \"bridges\": [";
    for (size_t i = 0; i < p.bridges.size(); ++i) {
        const BridgeMapping& b = p.bridges[i];
        std::string t;
        for (const BridgeTerrains& x : b.terrains)
            t += (t.empty() ? "" : ", ") + std::string("{\"coastA\": ") + quote(x.coastA) + ", \"water\": " + quote(x.water) + ", \"coastB\": " + quote(x.coastB) + "}";
        o += std::string(i ? "," : "") + "\n  {\"name\": " + quote(b.name) + ", \"tileset\": " + std::to_string(b.era) + ", \"template\": " +
             std::to_string(b.templateEra) + ", \"terrains\": [" + t + "],\n   \"tiles\": " + tilesText(b.tiles) + "}";
    }
    return o + "]}\n";
}

bool parse(const std::string& text, Passages& out, std::string& error) {
    JsonParser jp{text};
    const Json j = jp.value();
    if (!jp.ok || j.type != Json::Type::Object || j.str("format") != kFormat) return error = "it is not a passages file", false;
    if (j.integer("version", 0) > kVersion) return error = "it is of a newer version (" + std::to_string(j.integer("version", 0)) + ")", false;
    Passages p;
    if (const Json* rs = j.get("ramps"); rs && rs->type == Json::Type::Array)
        for (const Json& r : rs->items) {
            if (r.type != Json::Type::Object) return error = "a ramp is not an object", false;
            RampMapping m;
            m.name = r.str("name"), m.era = r.integer("tileset", 0) & 7, m.block = r.integer("template", -1);
            if (const Json* ts = r.get("terrains"); ts && ts->type == Json::Type::Array)
                for (const Json& t : ts->items) m.terrains.push_back({t.str("ground"), t.str("cliff")});
            if (!readTiles(r.get("tiles"), m.tiles)) return error = "the ramp " + m.name + " has bad tiles", false;
            p.ramps.push_back(std::move(m));
        }
    if (const Json* bs = j.get("bridges"); bs && bs->type == Json::Type::Array)
        for (const Json& b : bs->items) {
            if (b.type != Json::Type::Object) return error = "a bridge is not an object", false;
            BridgeMapping m;
            m.name = b.str("name"), m.era = b.integer("tileset", 0) & 7, m.templateEra = b.integer("template", -1);
            if (const Json* ts = b.get("terrains"); ts && ts->type == Json::Type::Array)
                for (const Json& t : ts->items) m.terrains.push_back({t.str("coastA"), t.str("water"), t.str("coastB")});
            if (!readTiles(b.get("tiles"), m.tiles)) return error = "the bridge " + m.name + " has bad tiles", false;
            p.bridges.push_back(std::move(m));
        }
    out = std::move(p);
    return true;
}

bool load(const fs::path& file, Passages& out, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return error = "cannot read " + pathText(file), false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!parse(text, out, error)) return error = pathText(file) + ": " + error, false;
    return true;
}

bool save(const fs::path& file, const Passages& p, std::string& error) {
    try {  // (writeFile's error text: path::string(), which can throw on Windows)
        return writeFile(file, toText(p), error);
    } catch (const std::exception&) {
        return error = "cannot write " + pathText(file), false;
    }
}

void use(const Passages& p) { gInUse = p, ++gRevision; }
const Passages& inUse() { return gInUse; }
int revision() { return gRevision; }

bool mapTile(const TileMap& m, uint16_t from, uint16_t& to) {
    const auto i = m.find(from);
    if (i == m.end()) return false;
    to = i->second;
    return true;
}

}  // namespace stardraft::passages
