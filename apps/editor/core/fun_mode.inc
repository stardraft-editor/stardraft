// Options > Fun mode (a spike, off by default): edits on the map give feedback. Each edit makes a short sound made in
// code (as bfxr and sfxr make them: a wave, a pitch slide, an envelope and a filter; no sound files), by what was
// edited: water splashes, grass brushes, dirt and rock crunch, metal clanks, a doodad thunks (the view shakes a
// little), a unit plops (it squashes and springs back), a deleted unit or doodad goes poof. Shards of the placed tiles
// (jagged pieces of their own pixels) fly off the edited tiles, each its own way, turning.
// How it finds the edits: each frame (funModeScene, after buildScene) it compares the map with the copy from the last
// frame: the tiles when terrainRevision changed, the units (by serial) and the doodads when revision changed. A map read
// anew (open, undo, redo: loadMark) or another tab only takes a new copy. Notes: docs/dev/fun-mode.md.

// Defined by the program: plays mono samples (-1..1) at `rate` once (sdl_data.inc; a test run plays nothing). `name`:
// the sound's kind (STARDRAFT_FUN_WAV=<folder> writes each sound there as <name>-<n>.wav, also in a test run).
void funPlayPcm(const std::vector<float>& mono, int rate, const char* name);

namespace fun {

// --- The sounds: a small sfxr ---------------------------------------------------------------------------------------
enum Wave { Square, Saw, Sine, Noise };
struct Voice {
    Wave wave = Square;
    float freq = 440, slide = 0;  // Hz at the start; octaves a second (+ up)
    float duty = 0.5f;            // (Square)
    float attack = 0, sustain = 0.02f, decay = 0.1f, punch = 0;  // seconds; punch: more at the sustain's start
    float lpf = 1, lpfSweep = 0, hpf = 0;  // one-pole filters' coefficients (1: open), lpfSweep a second
    float grain = 0;  // 0..1: the level jumps at random every few milliseconds (a crunch)
    float vibDepth = 0, vibRate = 0;
    float vol = 0.5f;
    float arpMult = 1, arpTime = 0;  // the pitch times arpMult once, arpTime seconds in (0: never)
};
constexpr int kRate = 44100;

// Adds the voice to `out` (made longer as necessary).
void render(const Voice& v, std::vector<float>& out, std::mt19937& rng) {
    const float len = v.attack + v.sustain + v.decay;
    const size_t n = size_t(len * kRate);
    if (out.size() < n) out.resize(n, 0.0f);
    std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
    float phase = 0, freq = v.freq, lp = 0, hp = 0, noise = uni(rng), grainLevel = 1, lpf = v.lpf;
    int grainLeft = 0;
    const float slideStep = std::pow(2.0f, v.slide / kRate);
    for (size_t i = 0; i < n; ++i) {
        const float t = float(i) / kRate;
        float f = freq * (1 + v.vibDepth * std::sin(6.2831853f * v.vibRate * t));
        if (v.arpTime > 0 && i == size_t(v.arpTime * kRate)) freq *= v.arpMult;
        phase += std::clamp(f, 10.0f, v.wave == Noise ? float(kRate) : 20000.0f) / kRate;  // (noise: up to white)
        if (phase >= 1) {
            phase -= std::floor(phase);
            noise = uni(rng);  // (noise: a new value each period, as sfxr: its pitch is how fast the values change)
        }
        float x = 0;
        switch (v.wave) {
        case Square: x = phase < v.duty ? 0.6f : -0.6f; break;
        case Saw: x = 2 * phase - 1; break;
        case Sine: x = std::sin(6.2831853f * phase); break;
        case Noise: x = noise; break;
        }
        freq *= slideStep;
        lpf = std::clamp(lpf + v.lpfSweep / kRate, 0.005f, 1.0f);
        lp += lpf * (x - lp);
        hp += v.hpf * (lp - hp);
        x = v.hpf > 0 ? lp - hp : lp;
        float env;
        if (t < v.attack) env = t / v.attack;
        else if (t < v.attack + v.sustain) env = 1 + v.punch * (1 - (t - v.attack) / v.sustain);
        else env = 1 - (t - v.attack - v.sustain) / v.decay, env *= env;
        if (v.grain > 0 && --grainLeft <= 0) {
            grainLeft = int(kRate * (0.002f + 0.006f * (uni(rng) * 0.5f + 0.5f)));
            grainLevel = 1 - v.grain * (uni(rng) * 0.5f + 0.5f);
        }
        out[i] += x * env * v.vol * (v.grain > 0 ? grainLevel : 1);
    }
}

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

// The sound of a kind, a little different each time. `pitch`: 1 as made; more is higher (a small unit plops higher).
std::vector<float> makeSound(Kind k, float pitch, float level, std::mt19937& rng) {
    std::uniform_real_distribution<float> j(0.88f, 1.12f);
    std::vector<float> out;
    auto add = [&](Voice v) {
        v.freq *= pitch * j(rng), v.vol *= level;
        render(v, out, rng);
    };
    switch (k) {
    case Kind::Splash:
        add({Noise, 3500, -1.0f, 0.5f, 0, 0.03f, 0.28f, 0.3f, 0.6f, -1.6f, 0, 0, 0, 0, 0.45f});
        add({Sine, 320, 2.2f, 0.5f, 0, 0.015f, 0.09f, 0, 1, 0, 0, 0, 0, 0, 0.35f});
        break;
    case Kind::Gloop:
        add({Noise, 900, -0.5f, 0.5f, 0, 0.04f, 0.22f, 0, 0.25f, -0.6f, 0, 0, 0, 0, 0.45f});
        add({Sine, 150, 1.4f, 0.5f, 0, 0.03f, 0.14f, 0.2f, 1, 0, 0, 0, 0.08f, 18, 0.45f});
        break;
    case Kind::Brush:  // (the user's bfxr grass: a swish that rises for 0.13 s and stops short; bfxr's noise at
                       // 2,643 Hz changes 32 times a period, so here it is white; its high-pass at 0.85: sfxr's 0.072)
        add({Noise, 2643 * 32, 0.2f, 0.5f, 0.1263f, 0.0025f, 0.0082f, 0.514f, 1, 0, 0.072f, 0, 0.0465f, 16.82f, 0.4f, 1.018f, 0.0122f});
        break;
    case Kind::Crunch:
        add({Noise, 2600, -0.4f, 0.5f, 0, 0.02f, 0.11f, 0.3f, 0.35f, -1.0f, 0, 0.7f, 0, 0, 0.6f});
        break;
    case Kind::Rock:
        add({Noise, 5200, -0.6f, 0.5f, 0, 0.015f, 0.09f, 0.4f, 0.7f, -2.0f, 0.04f, 0.85f, 0, 0, 0.5f});
        add({Sine, 95, -1.2f, 0.5f, 0, 0.01f, 0.08f, 0.3f, 1, 0, 0, 0, 0, 0, 0.4f});
        break;
    case Kind::Clank:
        add({Square, 620, -0.4f, 0.3f, 0, 0.01f, 0.11f, 0.3f, 0.5f, -1.5f, 0, 0, 0, 0, 0.25f});
        add({Sine, 1530, 0, 0.5f, 0, 0.005f, 0.22f, 0, 1, 0, 0, 0, 0.004f, 7, 0.14f});
        add({Noise, 9000, 0, 0.5f, 0, 0.004f, 0.02f, 0, 1, 0, 0, 0, 0, 0, 0.18f});
        break;
    case Kind::Void:
        add({Sine, 700, -3.0f, 0.5f, 0.01f, 0.03f, 0.22f, 0, 1, 0, 0, 0, 0.03f, 11, 0.28f});
        break;
    case Kind::Thunk:
        add({Sine, 140, -1.6f, 0.5f, 0, 0.03f, 0.18f, 0.6f, 1, 0, 0, 0, 0, 0, 0.8f});
        add({Noise, 1400, -0.5f, 0.5f, 0, 0.01f, 0.06f, 0.3f, 0.22f, 0, 0, 0.4f, 0, 0, 0.45f});
        break;
    case Kind::Plop:
        add({Square, 210, 2.6f, 0.5f, 0, 0.02f, 0.08f, 0.2f, 0.3f, 0, 0, 0, 0, 0, 0.32f});
        add({Sine, 210, 2.6f, 0.5f, 0, 0.02f, 0.09f, 0, 1, 0, 0, 0, 0, 0, 0.35f});
        break;
    case Kind::Poof:
        add({Noise, 1300, -0.4f, 0.5f, 0.01f, 0.02f, 0.17f, 0, 0.3f, -0.8f, 0, 0, 0, 0, 0.32f});
        add({Sine, 420, -1.5f, 0.5f, 0, 0.01f, 0.1f, 0, 1, 0, 0, 0, 0, 0, 0.18f});
        break;
    default: break;
    }
    for (float& s : out) s = std::tanh(s);  // (soft limit: voices that add up do not clip)
    return out;
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
constexpr size_t kMaxParticles = 1200, kMaxShards = 200;  // (a shard is up to about 250 rects)
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
void play(Kind k, float pitch = 1, float level = 1, uint64_t gapMs = 80) {
    const uint64_t now = nowMs();
    if (now - g.lastSound[size_t(k)] < gapMs) return;
    g.lastSound[size_t(k)] = now;
    funPlayPcm(makeSound(k, pitch, level, g.rng), kRate, kKindNames[size_t(k)]);
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

// A shard of the tile at map pixel (tileX, tileY) (megatile `mega` of `ts`): a jagged shape of 3 to 6 corners, 7 to
// 19 pixels across, cut from a place in the tile at random, with the tile's own pixels. False: no pixels.
bool cutShard(const Tileset& ts, int mega, float tileX, float tileY, Particle& p) {
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const int n = 7 + int(u(g.rng) * 12.99f);
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
        float speed = 50 + 110 * u(g.rng), up = 110 + 160 * u(g.rng);
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
        if (!p.pix.empty()) {  // (a shard: any way, its own speed and spin; some skid low, some go high)
            const float r = u(g.rng);
            const float s = r < 0.25f ? 0.35f : r > 0.85f ? 1.6f : 1.0f;
            speed *= 1.4f + 0.8f * u(g.rng), up *= s;
            p.gravity *= 0.75f + 0.5f * u(g.rng), p.life += 0.2f + 0.3f * u(g.rng);
            p.angle = 6.2831853f * u(g.rng), p.spin = (2 + 14 * u(g.rng)) * (u(g.rng) < 0.5f ? -1.0f : 1.0f);
            p.size = std::max(shellState().zoom, 1.6f) * (1.0f + 0.6f * u(g.rng));  // (a pixel's screen size: at least 1.6)
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
    // Doodads: placed ones thunk (their tiles change too: no terrain sound then), deleted ones go poof. One gone and one
    // of the same doodad come is a move: nothing.
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
    if (placedDoodads) play(Kind::Thunk, 1, 1, 60), g.shakeStart = nowMs();
    else if (deletedDoodads) play(Kind::Poof);
    g.doodads = std::move(doodads);
    // Units: placed ones plop (by serial: a moved unit keeps it), deleted ones go poof.
    std::unordered_set<uint32_t> serials;
    int placed = 0, deleted = 0;
    float pitch = 1;
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
        if (placed == 1) pitch = big ? 0.6f : 1.15f;
    }
    for (const auto& [serial, x, y] : g.unitPlaces)
        if (!serials.count(serial) && ++deleted <= 64) burst(Kind::Poof, x - 12.0f, y - 12.0f, x + 12.0f, y + 12.0f, 0xB8B8B8u, 6);
    if (placed) play(Kind::Plop, pitch, 1, 50);
    else if (deleted) play(Kind::Poof);
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
        if (!changed.empty() && !doodadsChanged) {
            const auto& groupTerrains = solidGroupTerrains();
            int counts[size_t(Kind::Count)]{};
            std::unordered_map<uint16_t, Kind> kinds;
            const size_t step = std::max<size_t>(1, changed.size() / 100);  // (at most about 100 tiles make particles)
            for (size_t c = 0; c < changed.size(); ++c) {
                const size_t i = changed[c];
                const size_t group = now[i] >> 4, oldGroup = g.tiles[i] >> 4;
                const std::array<uint16_t, 2> nt = group < groupTerrains.size() ? groupTerrains[group] : std::array<uint16_t, 2>{};
                const std::array<uint16_t, 2> ot = oldGroup < groupTerrains.size() ? groupTerrains[oldGroup] : std::array<uint16_t, 2>{};
                uint16_t terrain = nt[0];
                for (uint16_t t : nt)
                    if (t && t != ot[0] && t != ot[1]) terrain = t;
                auto it = kinds.find(terrain);
                if (it == kinds.end()) it = kinds.emplace(terrain, terrain ? kindOfTerrain(ed.terrainName(terrain)) : Kind::Crunch).first;
                ++counts[size_t(it->second)];
                if (c % step) continue;
                const float x = float(i % ed.width()) * 32, y = float(i / ed.width()) * 32;
                burst(it->second, x, y, x + 32, y + 32, tileColor(ts, now[i]), 3, &ts, now[i]);  // (shards of the new tile)
                if (it->second == Kind::Splash || it->second == Kind::Clank) burst(it->second, x, y, x + 32, y + 32, tileColor(ts, now[i]), 3);
            }
            const Kind top = Kind(std::max_element(std::begin(counts), std::end(counts)) - std::begin(counts));
            play(top, 1, std::clamp(0.45f + 0.12f * std::log2(float(changed.size())), 0.45f, 1.0f));
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
