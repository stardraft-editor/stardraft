// Passages files (include/stardraft/passages.h).
#include "stardraft/passages.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

#include "file_write.h"
#include "json.h"

namespace stardraft::passages {

namespace fs = std::filesystem;

namespace {

constexpr const char* kFormat = "StarCraft passages";
constexpr int kVersion = 2;

std::string pathText(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string cellsText(const std::vector<Cell>& cells) {
    std::string o;
    for (const Cell& c : cells)
        o += (o.empty() ? "[" : ", [") + std::to_string(c.x) + ", " + std::to_string(c.y) + ", " + std::to_string(c.tile) + ", " + std::to_string(int(c.role)) + ", " +
             std::to_string(int(c.mark)) + "]";
    return "[" + o + "]";
}

bool readCells(const Json* j, std::vector<Cell>& out) {
    if (!j) return true;
    if (j->type != Json::Type::Array) return false;
    std::set<std::pair<int, int>> seen;
    for (const Json& p : j->items) {
        if (p.type != Json::Type::Array || p.items.size() < 3) return false;
        for (const Json& v : p.items)
            if (v.type != Json::Type::Number) return false;
        Cell c;
        const double x = p.items[0].n, y = p.items[1].n, t = p.items[2].n;
        if (!(x >= 0 && x < kCanvasW && y >= 0 && y < kCanvasH && t >= -1 && t <= 0xFFFF)) return false;  // (on the canvas; a tile or none)
        c.x = int(x), c.y = int(y), c.tile = int(t);
        if (p.items.size() > 3) c.role = uint8_t(std::clamp(int(p.items[3].n), 0, int(kEnd)));
        if (p.items.size() > 4) c.mark = uint8_t(std::clamp(int(p.items[4].n), 0, int(kLow)));
        if (seen.insert({c.x, c.y}).second) out.push_back(c);  // (a cell once: the first)
    }
    return true;
}

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// The axis of an edge facing that way (tiles: one ISOM period along it).
std::pair<int, int> edgeAxis(int facing) {
    switch (facing) {
    case 0: case 4: return {4, 0};    // N, S
    case 2: case 6: return {0, 2};    // E, W
    case 1: case 5: return {2, 1};    // NE, SW
    default: return {2, -1};          // SE, NW
    }
}

std::pair<double, double> centroid(const std::vector<Cell>& v) {
    double x = 0, y = 0;
    for (const Cell& c : v) x += c.x + 0.5, y += c.y + 0.5;
    return v.empty() ? std::pair<double, double>{0, 0} : std::pair<double, double>{x / double(v.size()), y / double(v.size())};
}

// The smallest multiple of (ux, uy) that moves the cells off themselves (1: none).
int periodOf(const std::vector<Cell>& v, int ux, int uy) {
    std::set<std::pair<int, int>> on;
    for (const Cell& c : v) on.insert({c.x, c.y});
    for (int k = 1; k < 64; ++k) {
        bool clash = false;
        for (const Cell& c : v) clash = clash || on.count({c.x + k * ux, c.y + k * uy});
        if (!clash) return k;
    }
    return 1;
}

// Each corner's piece: the piece with a cell nearest it (the slope or the deck when two are as near).
void assignPieces(std::vector<CanvasCorner>& corners, const std::array<std::vector<Cell>, 3>& pieces) {
    for (CanvasCorner& c : corners) {
        double best = 1e18;
        for (const int k : {1, 0, 2})
            for (const Cell& p : pieces[size_t(k)])
                if (const double d = std::hypot(p.x + 0.5 - c.x, p.y + 0.5 - c.y); d < best - 1e-9) best = d, c.piece = k;
    }
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
        o += std::string(i ? "," : "") + "\n  {\"name\": " + quote(r.name) + ", \"tileset\": " + std::to_string(r.era) + ", \"facing\": " + std::to_string(r.facing) +
             ", \"repeat\": " + (r.repeat ? "true" : "false") + ", \"grid\": " + std::to_string(r.grid) + ", \"gridAuto\": " + (r.gridAuto ? "true" : "false") +
             ", \"terrains\": [" + t + "],\n   \"cells\": " + cellsText(r.cells) + "}";
    }
    o += "],\n \"bridges\": [";
    for (size_t i = 0; i < p.bridges.size(); ++i) {
        const BridgeMapping& b = p.bridges[i];
        std::string t;
        for (const BridgeTerrains& x : b.terrains)
            t += (t.empty() ? "" : ", ") + std::string("{\"coastA\": ") + quote(x.coastA) + ", \"water\": " + quote(x.water) + ", \"coastB\": " + quote(x.coastB) + "}";
        o += std::string(i ? "," : "") + "\n  {\"name\": " + quote(b.name) + ", \"tileset\": " + std::to_string(b.era) + ", \"deck\": " + std::to_string(b.deck) +
             ", \"grid\": " + std::to_string(b.grid) + ", \"gridAuto\": " + (b.gridAuto ? "true" : "false") + ", \"terrains\": [" + t + "],\n   \"cells\": " + cellsText(b.cells) +
             "}";
    }
    return o + "]}\n";
}

bool parse(const std::string& text, Passages& out, std::string& error) {
    JsonParser jp{text};
    const Json j = jp.value();
    if (!jp.ok || j.type != Json::Type::Object || j.str("format") != kFormat) return error = "it is not a passages file", false;
    if (j.integer("version", 0) > kVersion) return error = "it is of a newer version (" + std::to_string(j.integer("version", 0)) + ")", false;
    if (j.integer("version", 0) < kVersion) return error = "it is of an older version (" + std::to_string(j.integer("version", 0)) + "): make the mappings again", false;
    Passages p;
    if (const Json* rs = j.get("ramps"); rs && rs->type == Json::Type::Array)
        for (const Json& r : rs->items) {
            if (r.type != Json::Type::Object) return error = "a ramp is not an object", false;
            RampMapping m;
            m.name = r.str("name"), m.era = r.integer("tileset", 0) & 7, m.facing = std::clamp(r.integer("facing", -1), -1, 7);
            m.repeat = r.boolean("repeat", false), m.grid = r.integer("grid", 0) & 3, m.gridAuto = r.boolean("gridAuto", true);
            if (const Json* ts = r.get("terrains"); ts && ts->type == Json::Type::Array)
                for (const Json& t : ts->items) m.terrains.push_back({t.str("ground"), t.str("cliff")});
            if (!readCells(r.get("cells"), m.cells)) return error = "the ramp " + m.name + " has bad cells", false;
            p.ramps.push_back(std::move(m));
        }
    if (const Json* bs = j.get("bridges"); bs && bs->type == Json::Type::Array)
        for (const Json& b : bs->items) {
            if (b.type != Json::Type::Object) return error = "a bridge is not an object", false;
            BridgeMapping m;
            m.name = b.str("name"), m.era = b.integer("tileset", 0) & 7, m.deck = std::clamp(b.integer("deck", -1), -1, 1);
            m.grid = b.integer("grid", 0) & 3, m.gridAuto = b.boolean("gridAuto", true);
            if (const Json* ts = b.get("terrains"); ts && ts->type == Json::Type::Array)
                for (const Json& t : ts->items) m.terrains.push_back({t.str("coastA"), t.str("water"), t.str("coastB")});
            if (!readCells(b.get("cells"), m.cells)) return error = "the bridge " + m.name + " has bad cells", false;
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

bool cornerOnGrid(int grid, int x, int y) {
    const int mx = (grid & 1) + x, my = ((grid >> 1) & 1) + y;
    return ((mx % 2) + 2) % 2 == 0 && (((floorDiv(mx, 2) + my) % 2) + 2) % 2 == 1;
}

std::vector<CanvasCorner> cornersOf(const std::vector<Cell>& cells, int grid, bool) {
    std::vector<CanvasCorner> out;
    if (cells.empty()) return out;
    std::map<std::pair<int, int>, const Cell*> at;
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
    for (const Cell& c : cells) at[{c.x, c.y}] = &c, x0 = std::min(x0, c.x), y0 = std::min(y0, c.y), x1 = std::max(x1, c.x), y1 = std::max(y1, c.y);
    for (int y = y0; y <= y1 + 1; ++y)
        for (int x = x0; x <= x1 + 1; ++x) {
            if (!cornerOnGrid(grid, x, y)) continue;
            int high = 0, low = 0;
            for (const auto& [dx, dy] : {std::pair{-1, -1}, {0, -1}, {-1, 0}, {0, 0}}) {
                const auto i = at.find({x + dx, y + dy});
                if (i == at.end()) continue;
                const Cell& c = *i->second;
                if (c.mark == kHigh) ++high;
                else if (c.mark == kLow) ++low;
            }
            if ((high > 0) != (low > 0)) out.push_back({x, y, high > 0, 0});
        }
    return out;
}

int facingOfCorners(const std::vector<CanvasCorner>& corners) {
    double hx = 0, hy = 0, lx = 0, ly = 0;
    int h = 0, l = 0;
    for (const CanvasCorner& c : corners) (c.high ? hx : lx) += c.x, (c.high ? hy : ly) += c.y, ++(c.high ? h : l);
    if (!h || !l) return -1;
    const double vx = lx / l - hx / h, vy = ly / l - hy / h;  // (the way down: from the cliff to the ground)
    if (std::fabs(vx) < 1e-9 && std::fabs(vy) < 1e-9) return -1;
    const double a = std::atan2(vx, -2 * vy) * 180 / 3.14159265358979;  // (clockwise from north; the ISOM's 2:1 made square)
    return ((int(std::lround(a / 45)) % 8) + 8) % 8;
}

int rampFacing(const RampMapping& m) {
    if (m.facing >= 0) return m.facing & 7;
    return facingOfCorners(cornersOf(m.cells, m.grid, false));
}

RampShape rampShape(const RampMapping& m) {
    RampShape s;
    for (const Cell& c : m.cells)
        if (c.tile >= 0 && c.role >= kWallA && c.role <= kWallB) s.pieces[size_t(c.role - kWallA)].push_back(c);
    s.corners = cornersOf(m.cells, m.grid, false);
    if (s.pieces[0].empty() && s.pieces[1].empty() && s.pieces[2].empty()) return s.why = "no pieces", s;
    bool high = false, low = false;
    for (const CanvasCorner& c : s.corners) (c.high ? high : low) = true;
    if (!high) return s.why = "no cliff mark", s;
    if (!low) return s.why = "no ground", s;
    s.facing = m.facing >= 0 ? m.facing & 7 : facingOfCorners(s.corners);
    if (s.facing < 0) return s.why = "no facing", s;
    auto [ux, uy] = edgeAxis(s.facing);
    {  // (from wall A to wall B: the slope's way along the edge)
        const auto& a = s.pieces[0].empty() ? s.pieces[1] : s.pieces[0];
        const auto& b = s.pieces[2].empty() ? s.pieces[1] : s.pieces[2];
        const auto [ax, ay] = centroid(a);
        const auto [bx, by] = centroid(b);
        if ((bx - ax) * ux + (by - ay) * uy < 0) ux = -ux, uy = -uy;
    }
    const int k = s.pieces[1].empty() ? 1 : periodOf(s.pieces[1], ux, uy);
    s.stepX = k * ux, s.stepY = k * uy;
    if (s.pieces[1].empty()) s.minSlopes = s.maxSlopes = 0;
    else if (m.repeat) s.minSlopes = 1, s.maxSlopes = 64;
    assignPieces(s.corners, s.pieces);
    s.ok = true;
    return s;
}

int bridgeDeck(const BridgeMapping& m) {
    if (m.deck >= 0) return m.deck & 1;
    std::vector<Cell> a, b;
    for (const Cell& c : m.cells)
        if (c.tile >= 0 && c.role == kStart) a.push_back(c);
        else if (c.tile >= 0 && c.role == kEnd) b.push_back(c);
    if (a.empty() || b.empty()) return -1;
    const auto [ax, ay] = centroid(a);
    const auto [bx, by] = centroid(b);
    return (bx - ax) * (by - ay) >= 0 ? 0 : 1;
}

BridgeShape bridgeShape(const BridgeMapping& m) {
    BridgeShape s;
    for (const Cell& c : m.cells)
        if (c.tile >= 0 && c.role >= kStart && c.role <= kEnd) s.pieces[size_t(c.role - kStart)].push_back(c);
    s.corners = cornersOf(m.cells, m.grid, true);
    if (s.pieces[1].empty()) return s.why = "no deck", s;
    if (s.pieces[0].empty() || s.pieces[2].empty()) return s.why = "no caps", s;
    s.deck = bridgeDeck(m);
    if (s.deck < 0) return s.why = "no deck way", s;
    int ux = 2, uy = s.deck ? -1 : 1;
    {
        const auto [ax, ay] = centroid(s.pieces[0]);
        const auto [bx, by] = centroid(s.pieces[2]);
        if ((bx - ax) * ux + (by - ay) * uy < 0) ux = -ux, uy = -uy;
    }
    const int k = periodOf(s.pieces[1], ux, uy);
    s.stepX = k * ux, s.stepY = k * uy;
    assignPieces(s.corners, s.pieces);
    s.ok = true;
    return s;
}

}  // namespace stardraft::passages
