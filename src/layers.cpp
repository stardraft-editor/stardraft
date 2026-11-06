#include "stardraft/layers.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <span>
#include <tuple>

namespace stardraft {

namespace {

constexpr uint32_t kMagic = 0x3259414C;  // "LAY2"

struct Writer {
    Bytes& out;
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) { u8(uint8_t(v)), u8(uint8_t(v >> 8)); }
    void u32(uint32_t v) { u16(uint16_t(v)), u16(uint16_t(v >> 16)); }
    void i32(int v) { u32(uint32_t(v)); }
    void text(const std::string& s) {
        u32(uint32_t(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
};

struct Reader {
    const Bytes& in;
    size_t at = 0;
    bool ok = true;
    uint8_t u8() {
        if (at >= in.size()) return ok = false, uint8_t(0);
        return in[at++];
    }
    uint16_t u16() {
        const uint16_t a = u8();
        return uint16_t(a | (u8() << 8));
    }
    uint32_t u32() {
        const uint32_t a = u16();
        return a | (uint32_t(u16()) << 16);
    }
    int i32() { return int(u32()); }
    // A count of items of at least `least` bytes each: no more than the bytes that are left.
    uint32_t count(size_t least) {
        const uint32_t n = u32();
        if (!ok || n > (in.size() - at) / std::max<size_t>(least, 1)) return ok = false, 0u;
        return n;
    }
    std::string text() {
        const uint32_t n = count(1);
        if (!ok) return {};
        std::string s(in.begin() + long(at), in.begin() + long(at + n));
        at += n;
        return s;
    }
};

void writeLayer(Writer& w, const MapLayer& l) {
    w.u32(l.id);
    w.text(l.name);
    w.u8(l.visible);
    w.u8(l.locked);
    w.u32(uint32_t(l.corners.size()));
    for (const auto& [at, terrain] : l.corners) w.i32(at.first), w.i32(at.second), w.u16(terrain);
    w.u32(uint32_t(l.tiles.size()));
    for (const auto& [at, tile] : l.tiles) w.i32(at.first), w.i32(at.second), w.u16(tile);
    w.u32(uint32_t(l.units.size()));
    for (const auto& o : l.units) {
        w.i32(o.x), w.i32(o.y), w.u8(o.shown);
        o.item.encode(w.out);
    }
    w.u32(uint32_t(l.doodads.size()));
    for (const auto& o : l.doodads) w.i32(o.x), w.i32(o.y), w.u8(o.shown), w.u16(o.item.id), w.u8(o.item.owner), w.u8(o.item.disabled);
    w.u32(uint32_t(l.sprites.size()));
    for (const auto& o : l.sprites) w.i32(o.x), w.i32(o.y), w.u8(o.shown), w.u16(o.item.id), w.u8(o.item.owner), w.u8(o.item.unused), w.u16(o.item.flags);
}

bool readLayer(Reader& r, MapLayer& l) {
    l.id = r.u32();
    l.name = r.text();
    l.visible = r.u8() != 0;
    l.locked = r.u8() != 0;
    const uint32_t corners = r.count(10);
    for (uint32_t k = 0; r.ok && k < corners; ++k) {
        const int x = r.i32(), y = r.i32();
        l.corners[{x, y}] = r.u16();
    }
    const uint32_t tiles = r.count(10);
    for (uint32_t k = 0; r.ok && k < tiles; ++k) {
        const int x = r.i32(), y = r.i32();
        l.tiles[{x, y}] = r.u16();
    }
    const uint32_t units = r.count(9 + Unit::kSize);
    for (uint32_t k = 0; r.ok && k < units; ++k) {
        LayerObject<Unit> o;
        o.x = r.i32(), o.y = r.i32(), o.shown = r.u8() != 0;
        if (!r.ok || r.in.size() - r.at < Unit::kSize) return false;
        o.item = Unit::decode(std::span<const uint8_t>(r.in).subspan(r.at, Unit::kSize));
        r.at += Unit::kSize;
        l.units.push_back(o);
    }
    const uint32_t doodads = r.count(13);
    for (uint32_t k = 0; r.ok && k < doodads; ++k) {
        LayerObject<Doodad> o;
        o.x = r.i32(), o.y = r.i32(), o.shown = r.u8() != 0;
        o.item.id = r.u16(), o.item.owner = r.u8(), o.item.disabled = r.u8();
        l.doodads.push_back(o);
    }
    const uint32_t sprites = r.count(15);
    for (uint32_t k = 0; r.ok && k < sprites; ++k) {
        LayerObject<MapSprite> o;
        o.x = r.i32(), o.y = r.i32(), o.shown = r.u8() != 0;
        o.item.id = r.u16(), o.item.owner = r.u8(), o.item.unused = r.u8(), o.item.flags = r.u16();
        l.sprites.push_back(o);
    }
    return r.ok;
}

template <class T>
void moveObjects(std::vector<LayerObject<T>>& list, int px, int py, const std::optional<LayerArea>& area) {
    for (auto& o : list)
        if (!area || area->contains(o.x, o.y)) o.x += px, o.y += py;
}

template <class T>
void addOwners(const std::vector<LayerObject<T>>& list, int layer, std::vector<int>& out) {
    for (const auto& o : list)
        if (o.shown) out.push_back(layer);
}

// Each object of `now` gets the owner it had in `before`. An object that is the same in both keeps its owner wherever
// it is in the list (a moved doodad goes to the list's end, so the others change places). The ones left over are
// edited: they take the owners left over, in order. Any more are new (`active`).
template <class T, class Key>
std::vector<int> ownersAfter(const std::vector<T>& before, const std::vector<T>& now, const std::vector<int>& owners, int active, Key key) {
    std::vector<int> out(now.size(), active);
    using K = decltype(key(std::declval<const T&>()));
    std::map<K, std::vector<size_t>> unused;
    for (size_t i = before.size(); i-- > 0;) unused[key(before[i])].push_back(i);
    std::vector<uint8_t> taken(before.size(), 0);
    std::vector<size_t> edited;
    for (size_t i = 0; i < now.size(); ++i) {
        auto it = unused.find(key(now[i]));
        if (it == unused.end() || it->second.empty()) {
            edited.push_back(i);
            continue;
        }
        const size_t from = it->second.back();
        it->second.pop_back();
        taken[from] = 1;
        if (from < owners.size()) out[i] = owners[from];
    }
    size_t next = 0;
    for (size_t i : edited) {
        while (next < before.size() && taken[next]) ++next;
        if (next >= before.size()) break;
        if (next < owners.size()) out[i] = owners[next];
        ++next;
    }
    return out;
}

// The layers' lists from the flattened map's: what a layer shows on the map is what `now` gives it.
template <class T>
void rebuildObjects(LayerStack& stack, std::vector<LayerObject<T>> MapLayer::*member, const std::vector<T>& now, const std::vector<int>& own) {
    const auto dropShown = [&](MapLayer& l) { std::erase_if(l.*member, [](const LayerObject<T>& o) { return o.shown; }); };
    dropShown(stack.background);
    for (MapLayer& l : stack.layers) dropShown(l);
    for (size_t i = 0; i < now.size(); ++i) {
        const int who = i < own.size() ? own[i] : -1;
        MapLayer& l = who >= 0 && who < int(stack.layers.size()) ? stack.layers[size_t(who)] : stack.background;
        (l.*member).push_back({now[i], int(now[i].x), int(now[i].y), true});
    }
}

auto unitKey(const Unit& u) { return std::tuple(u.serial, u.x, u.y, u.type, u.owner, u.state, u.resources, u.relatedSerial); }
auto doodadKey(const Doodad& d) { return std::tuple(d.id, d.x, d.y, d.owner); }
auto spriteKey(const MapSprite& s) { return std::tuple(s.id, s.x, s.y, s.owner, s.flags); }

}  // namespace

Bytes LayerStack::serialize() const {
    Bytes out;
    Writer w{out};
    w.u32(kMagic);
    w.u32(nextId);
    writeLayer(w, background);
    w.u32(uint32_t(layers.size()));
    for (const MapLayer& l : layers) writeLayer(w, l);
    return out;
}

bool LayerStack::parse(const Bytes& data, LayerStack& out) {
    Reader r{data};
    if (r.u32() != kMagic) return false;
    LayerStack s;
    s.nextId = r.u32();
    if (!readLayer(r, s.background)) return false;
    const uint32_t n = r.count(1);
    for (uint32_t i = 0; r.ok && i < n; ++i) {
        MapLayer l;
        if (!readLayer(r, l)) return false;
        s.layers.push_back(std::move(l));
    }
    if (!r.ok) return false;
    out = std::move(s);
    return true;
}

MapLayer& LayerStack::add(std::string name, int at) {
    MapLayer l;
    l.id = nextId++;
    l.name = std::move(name);
    at = std::clamp(at, 0, int(layers.size()));
    return *layers.insert(layers.begin() + at, std::move(l));
}

int LayerStack::merge(std::vector<int> indices) {
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    std::erase_if(indices, [&](int i) { return i < 0 || i >= int(layers.size()); });
    if (indices.empty()) return -1;
    MapLayer& to = layers[size_t(indices.front())];
    for (size_t k = 1; k < indices.size(); ++k) {
        MapLayer& from = layers[size_t(indices[k])];
        for (const auto& [at, terrain] : from.corners) to.corners[at] = terrain;
        for (const auto& [at, tile] : from.tiles) to.tiles[at] = tile;
        to.units.insert(to.units.end(), from.units.begin(), from.units.end());
        to.doodads.insert(to.doodads.end(), from.doodads.begin(), from.doodads.end());
        to.sprites.insert(to.sprites.end(), from.sprites.begin(), from.sprites.end());
    }
    for (size_t k = indices.size(); k-- > 1;) layers.erase(layers.begin() + indices[k]);
    return indices.front();
}

void LayerStack::sortByName() {
    std::stable_sort(layers.begin(), layers.end(), [](const MapLayer& a, const MapLayer& b) { return a.name < b.name; });
}

LayerOwners LayerStack::owners() const {
    LayerOwners o;
    addOwners(background.units, -1, o.units);
    addOwners(background.doodads, -1, o.doodads);
    addOwners(background.sprites, -1, o.sprites);
    for (size_t i = 0; i < layers.size(); ++i) {
        addOwners(layers[i].units, int(i), o.units);
        addOwners(layers[i].doodads, int(i), o.doodads);
        addOwners(layers[i].sprites, int(i), o.sprites);
    }
    return o;
}

void moveLayerContents(MapLayer& layer, int dx, int dy, const std::optional<LayerArea>& area) {
    std::map<std::pair<int, int>, uint16_t> moved, kept;
    for (const auto& [at, terrain] : layer.corners) {
        if (!area || area->contains(at.first * 64, at.second * 32)) moved[{at.first + dx, at.second + dy}] = terrain;
        else kept[at] = terrain;
    }
    // (what moves goes over what stays there)
    for (const auto& [at, terrain] : moved) kept[at] = terrain;
    layer.corners = std::move(kept);
    std::map<std::pair<int, int>, uint16_t> movedTiles, keptTiles;
    for (const auto& [at, tile] : layer.tiles) {
        if (!area || area->contains(at.first * 32, at.second * 32)) movedTiles[{at.first + dx * 2, at.second + dy}] = tile;
        else keptTiles[at] = tile;
    }
    for (const auto& [at, tile] : movedTiles) keptTiles[at] = tile;
    layer.tiles = std::move(keptTiles);
    const int px = dx * 64, py = dy * 32;
    moveObjects(layer.units, px, py, area);
    moveObjects(layer.doodads, px, py, area);
    moveObjects(layer.sprites, px, py, area);
}

void snapLayerMove(int px, int py, int& dx, int& dy) {
    const auto nearest = [](int v, int step) { return v >= 0 ? (v + step / 2) / step : -((-v + step / 2) / step); };
    dx = nearest(px, 64), dy = nearest(py, 32);
    if ((dx + dy) & 1) dy += py >= dy * 32 ? 1 : -1;  // (the nearer of the two rows that keep the grid)
}

LayerBounds layerBounds(const MapLayer& l) {
    LayerBounds b;
    const auto grow = [&b](int x0, int y0, int x1, int y1) {
        if (!b.any) b.left = x0, b.top = y0, b.right = x1, b.bottom = y1, b.any = true;
        b.left = std::min(b.left, x0), b.top = std::min(b.top, y0), b.right = std::max(b.right, x1), b.bottom = std::max(b.bottom, y1);
    };
    for (const auto& [at, terrain] : l.corners) grow(at.first * 64 - 32, at.second * 32 - 16, at.first * 64 + 32, at.second * 32 + 16);
    for (const auto& [at, tile] : l.tiles) grow(at.first * 32, at.second * 32, at.first * 32 + 32, at.second * 32 + 32);
    for (const auto& o : l.units) grow(o.x - 16, o.y - 16, o.x + 16, o.y + 16);
    for (const auto& o : l.doodads) grow(o.x - 16, o.y - 16, o.x + 16, o.y + 16);
    for (const auto& o : l.sprites) grow(o.x - 16, o.y - 16, o.x + 16, o.y + 16);
    return b;
}

void captureLayerObjects(LayerStack& stack, const LayerObjectLists& before, const LayerObjectLists& now, const LayerOwners& owners, int active) {
    rebuildObjects(stack, &MapLayer::units, now.units, ownersAfter(before.units, now.units, owners.units, active, unitKey));
    rebuildObjects(stack, &MapLayer::doodads, now.doodads, ownersAfter(before.doodads, now.doodads, owners.doodads, active, doodadKey));
    rebuildObjects(stack, &MapLayer::sprites, now.sprites, ownersAfter(before.sprites, now.sprites, owners.sprites, active, spriteKey));
}

bool unpackLayerBase(const Bytes& data, std::vector<std::pair<std::string, Bytes>>& sections) {
    Reader r{data};
    if (r.u32() != kMagic) return false;
    sections.clear();
    while (r.ok && r.at < data.size()) {
        std::string name = r.text();
        const uint32_t n = r.count(1);
        if (!r.ok) return false;
        sections.emplace_back(std::move(name), Bytes(data.begin() + long(r.at), data.begin() + long(r.at + n)));
        r.at += n;
    }
    return r.ok;
}

Bytes packLayerBase(const std::vector<std::pair<std::string, Bytes>>& sections) {
    Bytes out;
    Writer w{out};
    w.u32(kMagic);
    for (const auto& [name, data] : sections) {
        w.text(name);
        w.u32(uint32_t(data.size()));
        out.insert(out.end(), data.begin(), data.end());
    }
    return out;
}

}  // namespace stardraft
