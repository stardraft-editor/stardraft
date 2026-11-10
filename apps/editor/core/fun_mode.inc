// Craft mode (a hidden extra, off by default: a click on About's logo turns it on or off; the code says fun mode): edits
// on the map give feedback. Painted terrain plays a short recorded
// sound by its terrain (water splashes, mud squelches, grass swishes, dirt and rock crunch, built floors clank;
// res/fun), and shards of the placed tiles (jagged pieces of their own pixels) fly off, each its own way, turning. A
// placed unit thuds softly, squashes and springs back and says its ready sound; a placed doodad thunks and shakes the
// view; a delete puffs; space and pits whoosh. Every sound is a recorded take.
// How it finds the edits: each frame (funModeScene, after buildScene) it compares the map with the copy from the last
// frame: the tiles when terrainRevision changed, the units (by serial) and the doodads when revision changed. A map read
// anew (open, undo, redo: loadMark) or another tab only takes a new copy. Notes: docs/dev/fun-mode.md.

// Defined by the program: plays mono samples (-1..1) at `rate` once (sdl_data.inc; a test run plays nothing). `name`:
// the sound's kind (STARDRAFT_FUN_WAV=<folder> writes each sound there as <name>-<n>.wav, also in a test run).
void funPlayPcm(const std::vector<float>& mono, int rate, const char* name);
// Plays an SFX data entry's sound (a unit's ready sound) at half the volume, at most 2 at a time (sdl_data.inc).
void funPlaySfx(int sfx);
// Plays the sound file `path` of `data` (a mod's ready sound) as funPlaySfx (sdl_data.inc).
void funPlaySoundFile(const GameData& data, const std::string& path);
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
    float gravity;
    // A shard of the terrain: its pixels (dx2, dy2: twice the place from its middle) in the tile's own colors, turned
    // by `angle` (radians), which changes by `spin` a second.
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
// A deleted unit's death: its sprite plays its Death animation (iscript animation 1) where the unit was, with the
// animation's sounds, to its end (its remnants too) or for kDeathTicks at most.
struct Death {
    SpriteSim sim;
    int image;
    float x, y;
    uint8_t owner;
    bool air;
    int ticks = 0;
};
struct State {
    const Editor* ed = nullptr;
    bool armed = false;
    uint64_t loadMark = 0, revision = 0, terrainRevision = 0;
    std::vector<uint16_t> tiles;
    std::unordered_set<uint32_t> serials;
    std::unordered_map<uint64_t, int> doodads;  // (id, x, y) -> how many
    std::vector<Unit> units;  // (the map's units then: a deleted unit's death, or its poof)
    std::vector<Death> deaths;
    uint64_t deathLast = 0;  // (the deaths' last game tick)
    std::vector<Particle> particles;
    std::unordered_map<uint32_t, Pop> pops;  // by serial
    uint64_t shakeStart = 0, lastFrame = 0;
    float shakeAmp = 1;  // (the shake's size: 1 a doodad's thunk; a large brush's by its tiles)
    uint64_t lastSound[size_t(Kind::Count)]{};
    std::mt19937 rng{12345};
} g;
constexpr size_t kMaxParticles = 1200, kMaxShards = 120;  // (a shard is up to about 100 rects)
size_t shardCount = 0;
constexpr uint64_t kPopMs = 420, kShakeMs = 140;
constexpr size_t kMaxDeaths = 64;
constexpr int kDeathTicks = 240;  // (10 s of the game's ticks)

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
// Many at once (a long stroke, a big fill) are one soft texture, not a wall: each plays to its end on a voice of its
// own in the program's mixer (sdl_data.inc, gFunMix: 12 voices, a soft limit on their sum); at most kVoices play at a
// time (one more does not start), and each one that plays makes the next one softer (ducked: 1 / (1 + 0.5 n), so 4
// together add up to about 2.6 times one, 12 to about 4.4). Each sound fades in for 8 ms and out for 30 ms (no hard start or
// stop). The replaced terrain's sound starts after the placed one's, so it is ducked too: it stays the quieter one.
constexpr size_t kVoices = 12;
std::vector<uint64_t> playingUntil;  // (the end of each sound that plays now, in nowMs)

void play(Kind k, float pitch = 1, float level = 1, uint64_t gapMs = 80) {
    const uint64_t now = nowMs();
    if (now - g.lastSound[size_t(k)] < gapMs) return;
    std::erase_if(playingUntil, [&](uint64_t end) { return end <= now; });
    if (playingUntil.size() >= kVoices) return;
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
    const float duck = 1.0f / (1.0f + 0.5f * float(playingUntil.size()));
    const size_t in = std::min(out.size() / 4, size_t(0.008f * kRate)), fadeOut = std::min(out.size() / 2, size_t(0.03f * kRate));
    for (size_t i = 0; i < out.size(); ++i) {
        float e = duck;
        if (i < in) e *= float(i) / float(in);
        if (i + fadeOut > out.size()) e *= float(out.size() - i) / float(fadeOut);
        out[i] *= e;
    }
    playingUntil.push_back(now + uint64_t(out.size() * 1000 / kRate));
    funPlayPcm(out, kRate, kKindNames[size_t(k)]);
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

// `n` shards of the tile `tile` at map pixel (tileX, tileY) (cutShard: its own pixels), each from its place in the
// tile, flying and turning its own way as `kind` moves (speeds in screen pixels: an edit looks the same at each zoom).
void burst(Kind k, float tileX, float tileY, int n, const Tileset& ts, uint16_t tile) {
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const int mega = ts.megatileFor(tile);
    if (mega < 0 || size_t(mega) >= ts.megatiles.size()) return;
    for (int i = 0; i < n && g.particles.size() < kMaxParticles && shardCount < kMaxShards; ++i) {
        Particle p{};
        if (!cutShard(ts, mega, tileX, tileY, p)) continue;
        ++shardCount;
        const float a = 6.2831853f * u(g.rng);
        float speed = 40 + 80 * u(g.rng), up = 60 + 90 * u(g.rng);  // (low: a hop, not a fountain)
        p.gravity = 600, p.life = 0.45f + 0.35f * u(g.rng);
        switch (k) {
        case Kind::Splash: up *= 1.5f; break;  // (water: higher)
        case Kind::Brush: speed *= 0.6f, up *= 0.5f, p.gravity = 160, p.life += 0.3f; break;  // (blades: they float down)
        case Kind::Clank: speed *= 2, up *= 0.8f, p.life *= 0.5f; break;  // (metal: fast and short)
        case Kind::Void: speed *= 0.4f, up = -30, p.gravity = -70; break;  // (bits that float away)
        case Kind::Poof: speed *= 0.5f, up *= 0.4f, p.gravity = -60, p.life += 0.2f; break;
        case Kind::Plop:
        case Kind::Thunk: speed *= 1.3f, up *= 0.35f, p.gravity = 360; break;  // (out to the sides)
        default: break;
        }
        // Its own speed and spin; low, small.
        speed *= 0.6f + 0.5f * u(g.rng), up *= u(g.rng) < 0.3f ? 0.45f : 0.8f;
        p.gravity *= 0.9f + 0.4f * u(g.rng), p.life += 0.05f * u(g.rng);
        p.angle = 6.2831853f * u(g.rng), p.spin = (2 + 14 * u(g.rng)) * (u(g.rng) < 0.5f ? -1.0f : 1.0f);
        p.size = std::max(shellState().zoom, 1.0f) * (0.9f + 0.3f * u(g.rng));  // (a pixel's screen size: at least 1)
        p.vx = std::cos(a) * speed, p.vy = std::sin(a) * speed * 0.5f - up;
        g.particles.push_back(p);
    }
}

void snapshot(const Editor& ed) {
    g.ed = &ed, g.armed = true;
    g.loadMark = ed.loadMark(), g.revision = ed.revision(), g.terrainRevision = ed.terrainRevision();
    g.tiles = ed.terrain().tiles;
    g.serials.clear(), g.units = ed.units();
    for (const Unit& u : ed.units()) g.serials.insert(u.serial);
    g.doodads.clear();
    for (const Doodad& d : ed.doodads()) ++g.doodads[uint64_t(d.id) << 32 | uint64_t(d.x) << 16 | d.y];
}

// One game tick of the deaths: their sounds (a mod's map: the mod's sound files), and the ended ones go.
void tickDeaths(const Editor& ed) {
    for (Death& d : g.deaths) {
        d.sim.tick(), ++d.ticks;
        d.sim.takeAttack();
        for (int sfx : d.sim.takeSounds()) {
            if (!modTab()) funPlaySfx(sfx);
            else if (const std::string file = stardraft::cmbw::soundFile(ed.gameData(), uint32_t(sfx)); !file.empty())
                funPlaySoundFile(ed.gameData(), file);
        }
    }
    std::erase_if(g.deaths, [](const Death& d) { return !d.sim.anyRunning() || d.ticks >= kDeathTicks; });
}

// The edits since the last frame: their sounds, particles, pops, deaths and shakes.
void watch(const Editor& ed) {
    if (!g.armed || g.ed != &ed || g.loadMark != ed.loadMark() || g.tiles.size() != ed.terrain().tiles.size()) {
        g.particles.clear(), g.pops.clear(), g.deaths.clear();
        return snapshot(ed);
    }
    if (g.revision == ed.revision() && g.terrainRevision == ed.terrainRevision()) return;
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const Tileset& ts = ed.tileset();
    // Doodads: placed ones thunk and shake the view (their tiles change too: no terrain sound then), deleted ones puff.
    // One gone and one of the same doodad come is a move: nothing.
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
    // Shards of each tile under a box (map pixels), from the tiles now or before the edit, `per` a tile.
    auto boxShards = [&](Kind k, float x0, float y0, float x1, float y1, bool before, int per) {
        const std::vector<uint16_t>& tiles = before ? g.tiles : ed.terrain().tiles;
        for (int ty = std::max(0, int(y0) / 32); ty <= std::min(ed.height() - 1, int(y1 - 1) / 32); ++ty)
            for (int tx = std::max(0, int(x0) / 32); tx <= std::min(ed.width() - 1, int(x1 - 1) / 32); ++tx) {
                const size_t i = size_t(ty) * ed.width() + size_t(tx);
                if (i < tiles.size()) burst(k, float(tx * 32), float(ty * 32), per, ts, tiles[i]);
            }
    };
    auto doodadBox = [&](uint64_t key, float& x0, float& y0, float& x1, float& y1) {
        const float x = float(key >> 16 & 0xFFFF), y = float(key & 0xFFFF);
        const DoodadType* t = ed.doodadTypes().find(uint16_t(key >> 32));
        const float hw = t ? t->width * 16.0f : 32, hh = t ? t->height * 16.0f : 32;
        x0 = x - hw, y0 = y - hh, x1 = x + hw, y1 = y + hh;
    };
    for (uint64_t key : added) {
        float x0, y0, x1, y1;
        doodadBox(key, x0, y0, x1, y1);
        if (++placedDoodads <= 12) boxShards(Kind::Thunk, x0, y0, x1, y1, false, 1);  // (its own tiles)
    }
    for (uint64_t key : removed) {
        float x0, y0, x1, y1;
        doodadBox(key, x0, y0, x1, y1);
        if (++deletedDoodads <= 12) boxShards(Kind::Poof, x0, y0, x1, y1, true, 1);  // (its tiles before: it crumbles)
    }
    if (placedDoodads) play(Kind::Thunk, 1, 1, 60), g.shakeStart = nowMs(), g.shakeAmp = 1;
    else if (deletedDoodads) play(Kind::Poof);
    g.doodads = std::move(doodads);
    // Units: placed ones thud and pop (by serial: a moved unit keeps it), deleted ones die (their Death animation and
    // its sounds; a unit with no Death animation puffs).
    std::unordered_set<uint32_t> serials;
    int placed = 0, deleted = 0, puffed = 0, readyType = -1;
    uint32_t readySerial = 0;
    float pitch = 1;
    for (const Unit& un : ed.units()) {
        serials.insert(un.serial);
        if (g.serials.count(un.serial)) continue;
        const UnitType* type = un.type < ed.unitTypes().size() ? &ed.unitTypes()[un.type] : nullptr;
        const float hw = type ? std::max(8.0f, type->placementWidth / 2.0f) : 8, hh = type ? std::max(8.0f, type->placementHeight / 2.0f) : 8;
        const bool big = hw >= 32;
        if (++placed <= 64) {  // (each one a little after the one before: a row lands one by one)
            g.pops[un.serial] = {nowMs() + uint64_t(placed - 1) * 15, hh};
            boxShards(Kind::Plop, un.x - hw, un.y + hh - 8, un.x + hw, un.y + hh, false, big ? 3 : 2);  // (the ground under it)
        }
        if (placed == 1) pitch = big ? 0.8f : 1.1f, readyType = un.type, readySerial = un.serial;  // (a building lower)
    }
    AppState& app = shellState();
    for (const Unit& un : g.units) {
        if (serials.count(un.serial) || ++deleted > 64) continue;
        UnitSprites* sp = app.ed.get() == &ed ? &app.ed->unitSprites() : nullptr;
        const int image = sp ? mapUnitImage(*sp, un) : -1;
        if (image < 0 || !sp->animOffset(image, 1) || g.deaths.size() >= kMaxDeaths) {
            ++puffed, boxShards(Kind::Poof, un.x - 8.0f, un.y - 8.0f, un.x + 8.0f, un.y + 8.0f, false, 2);
            continue;
        }
        const UnitType* type = un.type < ed.unitTypes().size() ? &ed.unitTypes()[un.type] : nullptr;
        const int dir = sp->facingDirection(un.type, app.randomFacing ? int((un.serial * 2654435761u) >> 27) : -1);  // (as it stood)
        if (g.deaths.empty()) g.deathLast = nowMs();
        g.deaths.push_back({SpriteSim(*sp, image, 1, dir, un.serial, ed.tileset().era), image, float(un.x), float(un.y),
                            uint8_t(std::min<int>(un.owner, 11)), type && type->is(UnitFlag::Flyer) && !type->is(UnitFlag::Building)});
    }
    if (placed) play(Kind::Plop, pitch, 1, 50);
    else if (puffed) play(Kind::Poof);
    // The first placed unit's ready sound, at most one each 0.6 s (a row or a paste of many units says it once): on a
    // Cosmonarchy map the mod's (cmbw::unitReadySound: a unit with some ready sounds says one of them, by its serial),
    // else the game's (units.dat's Ready Sound: units 0 to 105 have one).
    static uint64_t lastReady = 0;
    std::string modSound;
    if (placed && readyType >= 0 && nowMs() - lastReady >= 600 && modTab())
        modSound = stardraft::cmbw::unitReadySound(ed.gameData(), uint16_t(readyType), readySerial * 2654435761u >> 16);
    if (!modSound.empty()) funPlaySoundFile(ed.gameData(), modSound), lastReady = nowMs();
    else if (placed && readyType >= 0 && readyType < 106 && nowMs() - lastReady >= 600 && !modTab())
        if (const DatTable* units = rangeTable("units"))
            if (const DatField* f = units->field("Ready Sound"); f && readyType >= f->first && readyType <= f->last)
                if (const uint32_t sfx = units->value(*f, readyType); sfx > 0) funPlaySfx(int(sfx)), lastReady = nowMs();
    g.serials = std::move(serials);
    g.units = ed.units();
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
        if (doodadsChanged && placedDoodads && replaced != Kind::Count && replaced != Kind::Thunk) play(replaced, 1, 0.35f);  // (under the thunk)
        if (!changed.empty() && !doodadsChanged) {
            // Each tile's terrain that it did not have before (grass painted on dirt: the grass of its edge tiles; 0:
            // none, a tile of the same terrain made again). Only those tiles count when there are any: the dirt round a
            // grass stroke that the brush made again is no dirt sound and throws no dirt.
            std::vector<uint16_t> gained(changed.size(), 0);
            bool anyAdded = false;
            // Tiles that are all of a terrain they did not have: what was painted (the brush's rings of the terrains
            // between it and the ground, Platform and High Platform round painted Space, are not).
            std::unordered_map<uint16_t, int> wholeNew;
            for (size_t c = 0; c < changed.size(); ++c) {
                const auto nt = terrainsOf(now[changed[c]]), ot = terrainsOf(g.tiles[changed[c]]);
                for (uint16_t t : nt)
                    if (t && t != ot[0] && t != ot[1]) gained[c] = t, anyAdded = true;
                if (nt[0] && !nt[1] && nt[0] != ot[0] && nt[0] != ot[1]) ++wholeNew[nt[0]];
            }
            int counts[size_t(Kind::Count)]{};
            std::unordered_map<uint16_t, Kind> kinds;
            for (size_t c = 0; c < changed.size(); ++c) {
                if (anyAdded && !gained[c]) continue;
                const size_t i = changed[c];
                const uint16_t terrain = anyAdded ? gained[c] : terrainsOf(now[i])[0];
                auto it = kinds.find(terrain);
                if (it == kinds.end()) it = kinds.emplace(terrain, kindOf(terrain)).first;
                ++counts[size_t(it->second)];
            }
            // Every changed tile throws shards of its own new tile (a border tile its own mix of terrains), moving as its
            // terrain does; spread over all of them, fewer a tile when there are many (about 110 tiles at most).
            const size_t every = std::max<size_t>(1, changed.size() / 110);
            const int per = changed.size() <= 40 ? 2 : 1;
            for (size_t c = size_t(u(g.rng) * float(every)); c < changed.size(); c += every) {
                const size_t i = changed[c];
                const uint16_t terrain = gained[c] ? gained[c] : terrainsOf(now[i])[0];
                auto it = kinds.find(terrain);
                if (it == kinds.end()) it = kinds.emplace(terrain, kindOf(terrain)).first;
                const float x = float(i % ed.width()) * 32, y = float(i / ed.width()) * 32;
                burst(it->second, x, y, per, ts, now[i]);
            }
            Kind top = Kind(std::max_element(std::begin(counts), std::end(counts)) - std::begin(counts));
            if (!wholeNew.empty())
                top = kindOf(std::max_element(wholeNew.begin(), wholeNew.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first);
            play(top, 1, level);  // (what is placed: louder than what it replaces)
            if (replaced != Kind::Count && replaced != top) play(replaced, 1, level * 0.35f);
        }
        // A very large brush (an edit of 80 tiles or more: a brush of about 12 and up, a big fill; also when it takes
        // doodads away): the view shakes a little, more for more tiles (up to 1.5 times a doodad's thunk), starting at
        // most each 0.25 s, so a long drag is a light rumble.
        if (changed.size() >= 80 && nowMs() - g.shakeStart >= 250)
            g.shakeStart = nowMs(), g.shakeAmp = std::clamp(std::sqrt(float(changed.size())) / 14, 0.6f, 1.5f);
        g.tiles = now, g.terrainRevision = ed.terrainRevision();
    }
    g.revision = ed.revision();
}

}  // namespace fun

// Something moves: the map paints again each frame (sdlFrameClock).
bool funModeBusy() {
    return !fun::g.particles.empty() || !fun::g.pops.empty() || !fun::g.deaths.empty() || fun::nowMs() - fun::g.shakeStart < fun::kShakeMs;
}
// The deaths hold their editor's sprites: none are left when those go (shellForgetAnims).
void funForgetDeaths() { fun::g.deaths.clear(); }

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
        fun::g.armed = false, fun::g.particles.clear(), fun::g.pops.clear(), fun::g.deaths.clear();
        return;
    }
    fun::watch(*app.ed);
    const uint64_t now = fun::nowMs();
    const float dt = std::clamp(float(now - fun::g.lastFrame) / 1000.0f, 0.0f, 0.05f);
    fun::g.lastFrame = now;
    std::erase_if(fun::g.pops, [&](const auto& kv) { return now > kv.second.start && now - kv.second.start >= fun::kPopMs; });
    const float zoom = std::max(s.camera.zoom, 0.01f);
    fun::step(dt, zoom);
    // The deaths at the game's speed (after a pause: no catching up), then their images where the units were.
    if (now - fun::g.deathLast > 500) fun::g.deathLast = now - kTickMs;
    for (; !fun::g.deaths.empty() && now - fun::g.deathLast >= kTickMs; fun::g.deathLast += kTickMs) fun::tickDeaths(*app.ed);
    for (const fun::Death& d : fun::g.deaths) {
        render::SpriteDraw b;
        b.image = d.image, b.frame = d.sim.mainFrame();
        b.x = d.x, b.y = d.y, b.owner = d.owner, b.alpha = 1, b.air = d.air;
        addSimSprites(s, d.sim, b);
    }
    for (const fun::Particle& p : fun::g.particles) {
        const float fade = std::clamp((p.life - p.age) / (p.life * 0.35f), 0.0f, 1.0f);
        // (each pixel at its turned place; a little larger than its step, so no gaps show)
        const float cell = p.size / zoom * (0.5f + 0.5f * fade), h = cell * 0.6f;
        const float ca = std::cos(p.angle) * cell * 0.5f, sa = std::sin(p.angle) * cell * 0.5f;
        const uint32_t alpha = uint32_t(255 * std::min(1.0f, fade * 1.5f));
        for (const fun::Particle::Pix& q : p.pix) {
            const float x = p.x + q.dx2 * ca - q.dy2 * sa, y = p.y + q.dx2 * sa + q.dy2 * ca;
            s.rects.push_back({x - h, y - h, x + h, y + h, q.rgb << 8 | alpha, true});
        }
    }
    if (const uint64_t gone = now - fun::g.shakeStart; gone < fun::kShakeMs) {  // (a doodad's thunk, a large brush: the view shakes)
        const float k = 1 - float(gone) / fun::kShakeMs, amp = 3.0f * fun::g.shakeAmp / zoom * k;
        s.camera.y += amp * std::sin(float(gone) * 0.21f), s.camera.x += amp * 0.4f * std::cos(float(gone) * 0.17f);
    }
}

// A snapshot's --wait (sdl_menus.inc): `ms` go by for the particles, the pops and the shake, at once.
void funAdvanceFor(int ms) {
    const float zoom = std::max(shellState().zoom, 0.01f);
    for (int t = 0; t < ms; t += 16) fun::step(0.016f, zoom);
    if (const AppState& app = shellState(); app.ed && fun::g.ed == app.ed.get())
        for (int t = 0; t < ms / int(kTickMs) && !fun::g.deaths.empty(); ++t) fun::tickDeaths(*app.ed);
    fun::skewMs += uint64_t(std::max(ms, 0));
    fun::g.lastFrame = fun::nowMs();
}
