// Options > Fun mode (a spike, off by default): edits on the map give feedback. Painted terrain plays a short recorded
// sound by its terrain (water splashes, mud squelches, grass swishes, dirt and rock crunch, built floors clank;
// res/fun), and shards of the placed tiles (jagged pieces of their own pixels) fly off, each its own way, turning. A
// placed unit squashes and springs back and says its ready sound; a placed doodad shakes the view; a delete puffs.
// How it finds the edits: each frame (funModeScene, after buildScene) it compares the map with the copy from the last
// frame: the tiles when terrainRevision changed, the units (by serial) and the doodads when revision changed. A map read
// anew (open, undo, redo: loadMark) or another tab only takes a new copy. Notes: docs/dev/fun-mode.md.

// Defined by the program: plays mono samples (-1..1) at `rate` once (sdl_data.inc; a test run plays nothing). `name`:
// the sound's kind (STARDRAFT_FUN_WAV=<folder> writes each sound there as <name>-<n>.wav, also in a test run).
void funPlayPcm(const std::vector<float>& mono, int rate, const char* name);
// Plays an SFX data entry's sound (a unit's ready sound) at half the volume, at most 2 at a time (sdl_data.inc).
void funPlaySfx(int sfx);
const DatTable* rangeTable(const std::string& name);  // (map_scene.inc: units.dat, the project's or the game's)

namespace fun {

constexpr int kRate = 44100;  // (the rate the sounds play at)

enum class Kind { Splash, Gloop, Brush, Crunch, Rock, Clank, Void, Thunk, Plop, Poof, Count };
const char* const kKindNames[] = {"splash", "gloop", "brush", "crunch", "rock", "clank", "void", "thunk", "plop", "poof"};

// The material of an isometric terrain, from its name (the palette's names: StarEdit's).
Kind kindOfTerrain(const std::string& name) {
    auto has = [&](const char* w) { return name.find(w) != std::string::npos; };
    if (has("Water")) return Kind::Splash;
    if (has("Tar") || has("Lava") || has("Magma") || has("Mud")) return Kind::Gloop;
    if (has("Space") || has("Pit") || has("Bottomless")) return Kind::Void;
    if (has("Grass") || has("Jungle") || has("Snow") || has("Dunes") || has("Moguls")) return Kind::Brush;
    if (has("Rock") || has("Crag") || has("Crevice") || has("Shale") || has("Ice")) return Kind::Rock;
    if (has("Platform") || has("Plating") || has("Structure") || has("Asphalt") || has("Floor") || has("Roof") || has("Catwalk") ||
        has("Solar") || has("Outpost") || has("Compound") || has("Temple") || has("Ruins") || has("Basilica") || has("Flagstones") ||
        has("Substructure"))
        return Kind::Clank;
    return Kind::Crunch;
}

// --- The state ------------------------------------------------------------------------------------------------------
struct Particle {
    float x, y, vx, vy, life, age, size;  // (map pixels; vx, vy and size: screen pixels, the zoom then)
    uint32_t rgb;
    float gravity;
    // A shard of the terrain: its pixels (dx2, dy2: twice the place from its middle) in the tile's own colors, turned
    // by `angle` (radians), which changes by `spin` a second. Empty: a dot in `rgb`.
    struct Pix {
        int8_t dx2, dy2;
        uint32_t rgb;
    };
    std::vector<Pix> pix;
    float angle = 0, spin = 0;
};
struct Pop {
    uint64_t start;
    float feet;  // (map pixels under its center: the pivot of the squash)
};
struct State {
    const Editor* ed = nullptr;
    bool armed = false;
    uint64_t loadMark = 0, revision = 0, terrainRevision = 0;
    std::vector<uint16_t> tiles;
    std::unordered_set<uint32_t> serials;
    std::unordered_map<uint64_t, int> doodads;  // (id, x, y) -> how many
    std::vector<std::tuple<uint32_t, uint16_t, uint16_t>> unitPlaces;  // serial, x, y (a deleted unit's poof)
    std::vector<Particle> particles;
    std::unordered_map<uint32_t, Pop> pops;  // by serial
    uint64_t shakeStart = 0, lastFrame = 0;
    uint64_t lastSound[size_t(Kind::Count)]{};
    std::vector<uint32_t> megaColors;
    const Tileset* colorsFor = nullptr;
    int colorsEra = -1;
    std::mt19937 rng{12345};
} g;
constexpr size_t kMaxParticles = 1200, kMaxShards = 120;  // (a shard is up to about 100 rects)
size_t shardCount = 0;
constexpr uint64_t kPopMs = 420, kShakeMs = 140;

uint64_t skewMs = 0;  // (a snapshot's --wait: the time that went by in the run, funAdvanceFor)
uint64_t nowMs() { return animNowMs() + skewMs; }

void step(float dt, float zoom) {
    for (Particle& p : g.particles) {
        p.age += dt;
        p.vy += p.gravity * dt, p.x += p.vx * dt / zoom, p.y += p.vy * dt / zoom;
        p.vx *= 1 - 1.5f * dt;
        p.angle += p.spin * dt;
    }
    std::erase_if(g.particles, [](const Particle& p) { return p.age >= p.life; });
    shardCount = size_t(std::count_if(g.particles.begin(), g.particles.end(), [](const Particle& p) { return !p.pix.empty(); }));
}

// A sound of `kind`, unless one of that kind started less than `gapMs` ago (a long stroke: about 12 a second at most).
#include "../res/fun/fun_sounds.inc"  // (recorded sounds: tools/fun_sounds_gen.py; sources and licenses: docs/dev/fun-mode.md)

// A recorded sound of the kind (one of its takes at random, not the last one again), at the program's rate: each
// sample held (no smoothing: bit-like), `pitch` faster; at `level`. Empty: the kind has none (it is made in code).
std::vector<float> recorded(Kind k, float pitch, float level) {
    std::vector<const FunSample*> takes;
    for (const FunSample& f : kFunSamples)
        if (std::strcmp(f.kind, kKindNames[size_t(k)]) == 0) takes.push_back(&f);
    if (takes.empty()) return {};
    static const FunSample* last = nullptr;
    const FunSample* f = takes[std::uniform_int_distribution<size_t>(0, takes.size() - 1)(g.rng)];
    if (f == last && takes.size() > 1) f = takes[(size_t(std::find(takes.begin(), takes.end(), f) - takes.begin()) + 1) % takes.size()];
    last = f;
    const double step = double(f->rate) * pitch / kRate;
    std::vector<float> out(size_t(double(f->size) / step));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = (float(f->data[std::min(f->size - 1, size_t(double(i) * step))]) - 128) / 128 * 0.6f * level;
    return out;
}

// As the recorded sounds: 11,025 samples a second, 64 levels (the sounds made in code too, so they all sound alike).
void crush(std::vector<float>& v) {
    for (size_t i = 0; i < v.size(); i += 4)
        for (size_t k = i; k < std::min(v.size(), i + 4); ++k) v[k] = std::round(v[i] * 31) / 31;
}

// The recorded sound of a kind (none: no sound).
void play(Kind k, float pitch = 1, float level = 1, uint64_t gapMs = 80) {
    const uint64_t now = nowMs();
    if (now - g.lastSound[size_t(k)] < gapMs) return;
    g.lastSound[size_t(k)] = now;
    // Each time a pitch of its own: -4 to +4 semitones, another than the last one. The whole sound plays faster or
    // slower.
    static int lastStep = 0;
    int step = lastStep;
    while (step == lastStep) step = int(std::uniform_int_distribution<int>(-4, 4)(g.rng));
    lastStep = step;
    const std::vector<float> made = recorded(k, pitch, level);
    if (made.empty()) return;
    const double rate = std::pow(2.0, step / 12.0);
    std::vector<float> out(size_t(double(made.size()) / rate));
    for (size_t i = 0; i < out.size(); ++i) {  // (linear between the samples)
        const double at = double(i) * rate;
        const size_t a = std::min(size_t(at), made.size() - 1), b2 = std::min(a + 1, made.size() - 1);
        out[i] = made[a] + float(at - double(a)) * (made[b2] - made[a]);
    }
    crush(out);
    funPlayPcm(out, kRate, kKindNames[size_t(k)]);
}

uint32_t tileColor(const Tileset& ts, uint16_t tile) {
    if (g.colorsFor != &ts || g.colorsEra != int(ts.era)) g.megaColors = megatileColors(ts), g.colorsFor = &ts, g.colorsEra = int(ts.era);
    const int m = ts.megatileFor(tile);
    return m >= 0 && size_t(m) < g.megaColors.size() ? g.megaColors[size_t(m)] : 0x808080u;
}

uint32_t shade(uint32_t rgb, float f) {
    auto c = [&](int sh) { return uint32_t(std::clamp(float(rgb >> sh & 0xFF) * f, 0.0f, 255.0f)) << sh; };
    return c(16) | c(8) | c(0);
}

// A shard of the tile at map pixel (tileX, tileY) (megatile `mega` of `ts`): a jagged shape of 3 to 6 corners, 5 to
// 12 pixels across, cut from a place in the tile at random, with the tile's own pixels. False: no pixels.
bool cutShard(const Tileset& ts, int mega, float tileX, float tileY, Particle& p) {
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const int n = 5 + int(u(g.rng) * 7.99f);
    const int ox = int(u(g.rng) * float(32 - n)), oy = int(u(g.rng) * float(32 - n));
    const int corners = 3 + int(u(g.rng) * 3.99f);
    float cx[6], cy[6];
    const float start = 6.2831853f * u(g.rng);
    for (int i = 0; i < corners; ++i) {  // (round the middle, each at its own angle and distance: jagged)
        const float a = start + 6.2831853f * (float(i) + 0.7f * (u(g.rng) - 0.5f)) / float(corners);
        const float r = float(n) * 0.5f * (0.55f + 0.45f * u(g.rng));
        cx[i] = r * std::cos(a), cy[i] = r * std::sin(a);
    }
    const auto& mt = ts.megatiles[size_t(mega)];
    for (int py = 0; py < n; ++py)
        for (int px = 0; px < n; ++px) {
            const float x = float(px) + 0.5f - float(n) * 0.5f, y = float(py) + 0.5f - float(n) * 0.5f;
            bool in = false;  // (a ray to the right crosses the edges an odd number of times)
            for (int i = 0, j = corners - 1; i < corners; j = i++)
                if ((cy[i] > y) != (cy[j] > y) && x < (cx[j] - cx[i]) * (y - cy[i]) / (cy[j] - cy[i]) + cx[i]) in = !in;
            if (!in) continue;
            const int X = ox + px, Y = oy + py;
            const uint32_t ref = mt[size_t((Y / 8) * 4 + X / 8)];
            if ((ref >> 1) >= ts.minitiles.size()) continue;
            const int ix = (ref & 1) ? 7 - X % 8 : X % 8;
            const Rgb c = ts.palette[ts.minitiles[ref >> 1][size_t((Y % 8) * 8 + ix)]];
            p.pix.push_back({int8_t(2 * px + 1 - n), int8_t(2 * py + 1 - n), uint32_t(c.r) << 16 | uint32_t(c.g) << 8 | c.b});
        }
    p.x = tileX + float(ox) + float(n) * 0.5f, p.y = tileY + float(oy) + float(n) * 0.5f;
    return !p.pix.empty();
}

// Particles from the area (x0, y0)-(x1, y1) (map pixels): `n` of them in `rgb`, flying up and out as `kind` does
// (their speeds and sizes are screen pixels: an edit looks the same at each zoom). With `ts`: shards of the tile
// `tile` (cutShard), each from its place in the tile at (x0, y0), flying and turning its own way.
void burst(Kind k, float x0, float y0, float x1, float y1, uint32_t rgb, int n, const Tileset* ts = nullptr, uint16_t tile = 0) {
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const int mega = ts ? ts->megatileFor(tile) : -1;
    for (int i = 0; i < n && g.particles.size() < kMaxParticles; ++i) {
        Particle p{};
        p.x = x0 + (x1 - x0) * u(g.rng), p.y = y0 + (y1 - y0) * u(g.rng);
        if (mega >= 0 && size_t(mega) < ts->megatiles.size() && shardCount < kMaxShards && cutShard(*ts, mega, x0, y0, p)) ++shardCount;
        if (ts && p.pix.empty()) break;  // (no shard: too many fly now)
        const float a = 6.2831853f * u(g.rng);
        float speed = 40 + 80 * u(g.rng), up = 60 + 90 * u(g.rng);  // (low: a hop, not a fountain)
        p.gravity = 600, p.life = 0.45f + 0.35f * u(g.rng), p.size = 3 + 2.5f * u(g.rng);
        p.rgb = shade(rgb, 0.75f + 0.5f * u(g.rng));
        switch (k) {
        case Kind::Splash:  // (droplets: high, light)
            up *= 1.5f, p.size -= 1, p.rgb = u(g.rng) < 0.5f ? shade(rgb, 1.6f) : 0xD8F0FFu;
            break;
        case Kind::Brush:  // (blades: slow, they float down)
            speed *= 0.6f, up *= 0.5f, p.gravity = 160, p.life += 0.3f;
            break;
        case Kind::Clank:  // (sparks: fast and short)
            speed *= 2, up *= 0.8f, p.life *= 0.5f, p.size = 2.5f, p.rgb = u(g.rng) < 0.6f ? 0xFFE080u : 0xFFFFFFu;
            break;
        case Kind::Rock: p.size += 1.5f; break;
        case Kind::Void: speed *= 0.4f, up = -30, p.gravity = -70; break;  // (bits that float away)
        case Kind::Poof: speed *= 0.5f, up *= 0.4f, p.gravity = -60, p.size += 2, p.life += 0.2f; break;
        case Kind::Plop:
        case Kind::Thunk:  // (dust out to the sides: the ground's color, lighter)
            speed *= 1.3f, up *= 0.35f, p.gravity = 360, p.rgb = (p.rgb >> 1 & 0x7F7F7Fu) + 0x645F55u;
            break;
        default: break;
        }
        if (!p.pix.empty()) {  // (a shard: any way, its own speed and spin; low, small)
            const float r = u(g.rng);
            const float s = r < 0.3f ? 0.45f : 0.8f;
            speed *= 0.6f + 0.5f * u(g.rng), up *= s;
            p.gravity *= 0.9f + 0.4f * u(g.rng), p.life += 0.05f * u(g.rng);
            p.angle = 6.2831853f * u(g.rng), p.spin = (2 + 14 * u(g.rng)) * (u(g.rng) < 0.5f ? -1.0f : 1.0f);
            p.size = std::max(shellState().zoom, 1.0f) * (0.9f + 0.3f * u(g.rng));  // (a pixel's screen size: at least 1)
        }
        p.vx = std::cos(a) * speed, p.vy = std::sin(a) * speed * 0.5f - up;
        g.particles.push_back(p);
    }
}

void snapshot(const Editor& ed) {
    g.ed = &ed, g.armed = true;
    g.loadMark = ed.loadMark(), g.revision = ed.revision(), g.terrainRevision = ed.terrainRevision();
    g.tiles = ed.terrain().tiles;
    g.serials.clear(), g.unitPlaces.clear();
    for (const Unit& u : ed.units()) g.serials.insert(u.serial), g.unitPlaces.push_back({u.serial, u.x, u.y});
    g.doodads.clear();
    for (const Doodad& d : ed.doodads()) ++g.doodads[uint64_t(d.id) << 32 | uint64_t(d.x) << 16 | d.y];
}

// The edits since the last frame: their sounds, particles, pops and shakes.
void watch(const Editor& ed) {
    if (!g.armed || g.ed != &ed || g.loadMark != ed.loadMark() || g.tiles.size() != ed.terrain().tiles.size()) {
        g.particles.clear(), g.pops.clear();
        return snapshot(ed);
    }
    if (g.revision == ed.revision() && g.terrainRevision == ed.terrainRevision()) return;
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const Tileset& ts = ed.tileset();
    // Doodads: placed ones shake the view (their tiles change too: no terrain sound then), deleted ones puff. One gone
    // and one of the same doodad come is a move: nothing.
    std::unordered_map<uint64_t, int> doodads;
    for (const Doodad& d : ed.doodads()) ++doodads[uint64_t(d.id) << 32 | uint64_t(d.x) << 16 | d.y];
    std::vector<uint64_t> added, removed;
    for (const auto& [key, n] : doodads) {
        const auto it = g.doodads.find(key);
        for (int i = it == g.doodads.end() ? 0 : it->second; i < n; ++i) added.push_back(key);
    }
    for (const auto& [key, n] : g.doodads) {
        const auto it = doodads.find(key);
        for (int i = it == doodads.end() ? 0 : it->second; i < n; ++i) removed.push_back(key);
    }
    const bool doodadsChanged = !added.empty() || !removed.empty();
    for (auto a = added.begin(); a != added.end();) {
        const auto r = std::find_if(removed.begin(), removed.end(), [&](uint64_t k) { return k >> 32 == *a >> 32; });
        if (r == removed.end()) {
            ++a;
            continue;
        }
        removed.erase(r), a = added.erase(a);
    }
    int placedDoodads = 0, deletedDoodads = 0;
    auto doodadBox = [&](uint64_t key, float& x0, float& y0, float& x1, float& y1) {
        const float x = float(key >> 16 & 0xFFFF), y = float(key & 0xFFFF);
        const DoodadType* t = ed.doodadTypes().find(uint16_t(key >> 32));
        const float hw = t ? t->width * 16.0f : 32, hh = t ? t->height * 16.0f : 32;
        x0 = x - hw, y0 = y - hh, x1 = x + hw, y1 = y + hh;
    };
    for (uint64_t key : added) {
        float x0, y0, x1, y1;
        doodadBox(key, x0, y0, x1, y1);
        const int tx = std::clamp(int((x0 + x1) / 64), 0, ed.width() - 1), ty = std::clamp(int(y1 / 32) - 1, 0, ed.height() - 1);
        const uint16_t ground = ed.terrain().tiles[size_t(ty) * ed.width() + size_t(tx)];
        if (++placedDoodads <= 12)
            burst(Kind::Thunk, x0, y1 - 8, x1, y1 + 4, tileColor(ts, ground), 14), burst(Kind::Thunk, float(tx * 32), float(ty * 32), 0, 0, 0, 6, &ts, ground);
    }
    for (uint64_t key : removed) {
        float x0, y0, x1, y1;
        doodadBox(key, x0, y0, x1, y1);
        if (++deletedDoodads <= 12) burst(Kind::Poof, x0, y0, x1, y1, 0xB0B0B0u, 12);
    }
    if (placedDoodads) g.shakeStart = nowMs();
    g.doodads = std::move(doodads);
    // Units: placed ones pop (by serial: a moved unit keeps it), deleted ones puff.
    std::unordered_set<uint32_t> serials;
    int placed = 0, deleted = 0, readyType = 1000;
    for (const Unit& un : ed.units()) {
        serials.insert(un.serial);
        if (g.serials.count(un.serial)) continue;
        const UnitType* type = un.type < ed.unitTypes().size() ? &ed.unitTypes()[un.type] : nullptr;
        const float hw = type ? std::max(8.0f, type->placementWidth / 2.0f) : 8, hh = type ? std::max(8.0f, type->placementHeight / 2.0f) : 8;
        const bool big = hw >= 32;
        if (++placed <= 64) {  // (each one a little after the one before: a row lands one by one)
            g.pops[un.serial] = {nowMs() + uint64_t(placed - 1) * 15, hh};
            const int tx = std::clamp(un.x / 32, 0, ed.width() - 1), ty = std::clamp((un.y + int(hh)) / 32, 0, ed.height() - 1);
            const uint16_t ground = ed.terrain().tiles[size_t(ty) * ed.width() + size_t(tx)];
            burst(Kind::Plop, un.x - hw, un.y + hh - 6, un.x + hw, un.y + hh + 2, tileColor(ts, ground), big ? 22 : 8);
            burst(Kind::Plop, float(tx * 32), float(ty * 32), 0, 0, 0, big ? 8 : 3, &ts, ground);
        }
        if (placed == 1) readyType = un.type;
    }
    for (const auto& [serial, x, y] : g.unitPlaces)
        if (!serials.count(serial) && ++deleted <= 64) burst(Kind::Poof, x - 12.0f, y - 12.0f, x + 12.0f, y + 12.0f, 0xB8B8B8u, 6);
    // The first placed unit's ready sound (units.dat's Ready Sound: units 0 to 105 have one), at most one each 0.6 s:
    // a row or a paste of many units says it once.
    static uint64_t lastReady = 0;
    if (placed && readyType < 106 && nowMs() - lastReady >= 600)
        if (const DatTable* units = rangeTable("units"))
            if (const DatField* f = units->field("Ready Sound"); f && readyType >= f->first && readyType <= f->last)
                if (const uint32_t sfx = units->value(*f, readyType); sfx > 0) funPlaySfx(int(sfx)), lastReady = nowMs();
    g.serials = std::move(serials);
    g.unitPlaces.clear();
    for (const Unit& un : ed.units()) g.unitPlaces.push_back({un.serial, un.x, un.y});
    // Terrain: the tiles that changed. Their old colors fly off (and some of the new); the sound of the most common new
    // terrain, louder for more tiles.
    if (g.terrainRevision != ed.terrainRevision()) {
        const std::vector<uint16_t>& now = ed.terrain().tiles;
        std::vector<size_t> changed;
        for (size_t i = 0; i < now.size(); ++i)
            if (now[i] != g.tiles[i]) changed.push_back(i);
        const auto& groupTerrains = solidGroupTerrains();
        auto terrainsOf = [&](uint16_t tile) {
            const size_t group = tile >> 4;
            return group < groupTerrains.size() ? groupTerrains[group] : std::array<uint16_t, 2>{};
        };
        auto kindOf = [&](uint16_t terrain) { return terrain ? kindOfTerrain(ed.terrainName(terrain)) : Kind::Crunch; };
        // What the edit replaces: the terrain each tile had and has no more (grass painted on dirt: dirt); the most
        // common one's sound plays under the placed one's, at 35 % of its level (Kind::Count: none).
        Kind replaced = Kind::Count;
        if (!changed.empty()) {
            int lost[size_t(Kind::Count)]{};
            bool any = false;
            for (size_t i : changed) {
                const auto nt = terrainsOf(now[i]), ot = terrainsOf(g.tiles[i]);
                for (uint16_t t : ot)
                    if (t && t != nt[0] && t != nt[1]) ++lost[size_t(kindOf(t))], any = true;
            }
            if (any) replaced = Kind(std::max_element(std::begin(lost), std::end(lost)) - std::begin(lost));
        }
        const float level = std::clamp(0.45f + 0.12f * std::log2(float(std::max<size_t>(changed.size(), 1))), 0.45f, 1.0f);
        if (!changed.empty() && !doodadsChanged) {
            // Each tile's terrain that it did not have before (grass painted on dirt: the grass of its edge tiles; 0:
            // none, a tile of the same terrain made again). Only those tiles count when there are any: the dirt round a
            // grass stroke that the brush made again is no dirt sound and throws no dirt.
            std::vector<uint16_t> added(changed.size(), 0);
            std::unordered_map<uint16_t, uint16_t> solid;  // a changed tile that is all of that terrain: the shards' source
            bool anyAdded = false;
            for (size_t c = 0; c < changed.size(); ++c) {
                const auto nt = terrainsOf(now[changed[c]]), ot = terrainsOf(g.tiles[changed[c]]);
                for (uint16_t t : nt)
                    if (t && t != ot[0] && t != ot[1]) added[c] = t, anyAdded = true;
                if (nt[0] && !nt[1]) solid.try_emplace(nt[0], now[changed[c]]);
            }
            int counts[size_t(Kind::Count)]{};
            std::unordered_map<uint16_t, Kind> kinds;
            size_t counted = 0;
            for (size_t c = 0; c < changed.size(); ++c) counted += !anyAdded || added[c];
            const size_t step = std::max<size_t>(1, counted / 100);  // (at most about 100 tiles make particles)
            for (size_t c = 0, n = 0; c < changed.size(); ++c) {
                if (anyAdded && !added[c]) continue;
                const size_t i = changed[c];
                const uint16_t terrain = anyAdded ? added[c] : terrainsOf(now[i])[0];
                auto it = kinds.find(terrain);
                if (it == kinds.end()) it = kinds.emplace(terrain, kindOf(terrain)).first;
                ++counts[size_t(it->second)];
                if (n++ % step) continue;
                const float x = float(i % ed.width()) * 32, y = float(i / ed.width()) * 32;
                const auto src = solid.find(terrain);  // (shards of that terrain alone, where the edit has a tile of it)
                const uint16_t from = src != solid.end() ? src->second : now[i];
                burst(it->second, x, y, x + 32, y + 32, tileColor(ts, from), 1 + (u(g.rng) < 0.4f), &ts, from);
                if (it->second == Kind::Splash || it->second == Kind::Clank) burst(it->second, x, y, x + 32, y + 32, tileColor(ts, from), 2);
            }
            const Kind top = Kind(std::max_element(std::begin(counts), std::end(counts)) - std::begin(counts));
            play(top, 1, level);  // (what is placed: louder than what it replaces)
            if (replaced != Kind::Count && replaced != top) play(replaced, 1, level * 0.35f);
        }
        g.tiles = now, g.terrainRevision = ed.terrainRevision();
    }
    g.revision = ed.revision();
}

}  // namespace fun

// Something moves: the map paints again each frame (sdlFrameClock).
bool funModeBusy() { return !fun::g.particles.empty() || !fun::g.pops.empty() || fun::nowMs() - fun::g.shakeStart < fun::kShakeMs; }

// A placed unit's pop: its sprites (s.sprites from `from` on) squash, then spring up and settle.
void funScaleUnit(render::Scene& s, const Unit& u, size_t from) {
    if (fun::g.ed != shellState().ed.get()) return;  // (another tab's pops: watch has not cleared them yet)
    const auto it = fun::g.pops.find(u.serial);
    if (it == fun::g.pops.end()) return;
    const uint64_t now = fun::nowMs();
    if (now < it->second.start) {  // (not yet: it lands a little after the one before it)
        for (size_t i = from; i < s.sprites.size(); ++i) s.sprites[i].alpha = 0;
        return;
    }
    const float t = float(now - it->second.start);
    if (t >= fun::kPopMs) return;
    const float a = 0.38f * std::exp(-t / 95.0f) * std::cos(t * 6.2831853f / 210.0f);
    for (size_t i = from; i < s.sprites.size(); ++i) {
        render::SpriteDraw& d = s.sprites[i];
        d.scaleX = 1 + a * 0.6f, d.scaleY = 1 - a;
        d.pivotX = u.x, d.pivotY = u.y + it->second.feet;
    }
}

// After buildScene: the edits since the last frame (Options > Fun mode), then the particles and the shake.
void funModeScene(render::Scene& s) {
    AppState& app = shellState();
    if (!app.funMode || !app.ed || !app.ed->isOpen()) {
        fun::g.armed = false, fun::g.particles.clear(), fun::g.pops.clear();
        return;
    }
    fun::watch(*app.ed);
    const uint64_t now = fun::nowMs();
    const float dt = std::clamp(float(now - fun::g.lastFrame) / 1000.0f, 0.0f, 0.05f);
    fun::g.lastFrame = now;
    std::erase_if(fun::g.pops, [&](const auto& kv) { return now > kv.second.start && now - kv.second.start >= fun::kPopMs; });
    const float zoom = std::max(s.camera.zoom, 0.01f);
    fun::step(dt, zoom);
    for (const fun::Particle& p : fun::g.particles) {
        const float fade = std::clamp((p.life - p.age) / (p.life * 0.35f), 0.0f, 1.0f);
        const float half = p.size * 0.5f / zoom;
        if (!p.pix.empty()) {  // (each pixel at its turned place; a little larger than its step, so no gaps show)
            const float cell = p.size / zoom * (0.5f + 0.5f * fade), h = cell * 0.6f;
            const float ca = std::cos(p.angle) * cell * 0.5f, sa = std::sin(p.angle) * cell * 0.5f;
            const uint32_t alpha = uint32_t(255 * std::min(1.0f, fade * 1.5f));
            for (const fun::Particle::Pix& q : p.pix) {
                const float x = p.x + q.dx2 * ca - q.dy2 * sa, y = p.y + q.dx2 * sa + q.dy2 * ca;
                s.rects.push_back({x - h, y - h, x + h, y + h, q.rgb << 8 | alpha, true});
            }
            continue;
        }
        s.rects.push_back({p.x - half, p.y - half, p.x + half, p.y + half, p.rgb << 8 | uint32_t(255 * fade), true});
    }
    if (const uint64_t gone = now - fun::g.shakeStart; gone < fun::kShakeMs) {  // (a doodad's thunk: the view shakes)
        const float k = 1 - float(gone) / fun::kShakeMs, amp = 3.0f / zoom * k;
        s.camera.y += amp * std::sin(float(gone) * 0.21f), s.camera.x += amp * 0.4f * std::cos(float(gone) * 0.17f);
    }
}

// A snapshot's --wait (sdl_menus.inc): `ms` go by for the particles, the pops and the shake, at once.
void funAdvanceFor(int ms) {
    const float zoom = std::max(shellState().zoom, 0.01f);
    for (int t = 0; t < ms; t += 16) fun::step(0.016f, zoom);
    fun::skewMs += uint64_t(std::max(ms, 0));
    fun::g.lastFrame = fun::nowMs();
}
