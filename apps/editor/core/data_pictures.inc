// The Data workspace's pictures beside the fields: their model (portable, core.inc, after data_nav.inc). The game's
// images drawn as the game draws them (SD, or the HD2 and Carbot art: hdFrame, drawImageHd; the draw functions:
// compositeLookHd, compositeLookSd), a running sprite's picture (spriteSimPicture), the image player (gImagePlayer: the
// animation of an images.dat entry as its script runs it, a tick each 42 ms), its timeline (animTimeline) and a
// portrait's clips (portraitClips: read here, decoded on a thread). The Windows program draws them with the GDI
// (main.cpp's widgets); the SDL program draws them on the Canvas (core/views_data_pictures.inc).

// The clock of the pictures that move, in milliseconds.
uint64_t dataTickMs() {
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
// A player's color (0x00RRGGBB): the map's own for players 1 to 8, the game's for the rest.
uint32_t dataTeamRgb(int owner) {
    const AppState& app = shellState();
    if (app.ed && app.ed->isOpen() && owner >= 0 && owner < 8) return app.ed->playerRgb(owner);
    static const uint32_t rgb[12] = {0xF40404, 0x0C48CC, 0x2CB494, 0x88409C, 0xF88C14, 0x703014,
                                     0xCCE0D0, 0xFCFC38, 0x088008, 0xFCFC7C, 0xECC4B0, 0x4068D4};
    return rgb[std::clamp(owner, 0, 11)];
}
void dataPanesChanged();  // (the program's: core/data_nav.inc)

int imageOfFlingy(int fl) {
    const int sp = datValue("flingy", "Sprite", fl, -1);
    return sp < 0 ? -1 : datValue("sprites", "Image File", sp, -1);
}
int imageOfUnit(int u) { return imageOfFlingy(datValue("units", "Graphics", u, -1)); }

// The frame of an image facing `dir32` (0 up, 8 right, 16 down, clockwise) from a script frame; turning images
// have 17 frames per set (up to down on the right; the left side mirrored).
int facingFrame(int image, int scriptFrame, int dir32, bool& flip) {
    flip = false;
    if (!shellState().ed || image < 0) return scriptFrame;
    UnitSprites& sp = dataSprites();
    if (!sp.turns(image)) return scriptFrame;
    const int base = scriptFrame / 17 * 17, d = ((dir32 % 32) + 32) % 32;
    if (d <= 16) return base + d;
    flip = true;
    return base + 32 - d;
}

// --- Remastered art in the widgets: View > HD2, HD or Carbot draws them from the HD2 textures (or Carbot's) ----------

bool widgetsHd() { return shellState().carbot || shellState().quality != GraphicsQuality::SD; }
constexpr int kHdScale = 2;  // HD2 texture pixels per game pixel

// One decoded frame of an image's HD2 art: color and team-color mask, where it sits in the frame box.
struct HdFrame {
    bool ok = false;
    int w = 0, h = 0, x = 0, y = 0;  // texture pixels; x, y inside the box
    int boxW = 0, boxH = 0;
    std::vector<uint8_t> color, mask;
};
const HdFrame& hdFrame(int image, int frame) {
    static std::map<std::tuple<int, int, bool>, HdFrame> frames;
    static std::map<std::pair<int, bool>, std::unique_ptr<AnimImage>> anims;
    auto [it, fresh] = frames.try_emplace({image, frame, shellState().carbot});
    if (!fresh) return it->second;
    auto& anim = anims[{image, shellState().carbot}];
    if (!anim) {
        anim = std::make_unique<AnimImage>();
        std::string err;
        if (!loadAnimImage(shellState().data, image, GraphicsQuality::HD2, *anim, err, shellState().carbot)) anim->frames.clear();
    }
    HdFrame& f = it->second;
    if (anim->frames.empty() || frame < 0) return f;
    const auto& af = anim->frames[size_t(frame) < anim->frames.size() ? size_t(frame) : 0];
    const float ts = anim->texScale;
    f.w = int(af.width * ts), f.h = int(af.height * ts), f.x = int(af.xOff * ts), f.y = int(af.yOff * ts);
    f.boxW = int(anim->boxWidth * ts), f.boxH = int(anim->boxHeight * ts);
    if (f.w <= 0 || f.h <= 0) return f;
    decodeDds(anim->diffuse, int(af.x * ts), int(af.y * ts), f.w, f.h, f.color);
    if (!anim->teamColor.data.empty()) decodeDds(anim->teamColor, int(af.x * ts), int(af.y * ts), f.w, f.h, f.mask);
    f.ok = true;
    return f;
}

// An image's frame drawn in HD2 onto `s` (texture pixels), its origin at s's origin plus (dx, dy) game pixels;
// shadows darken, glows add light, the team color as the game's shader does it. False: no HD art for it.
bool drawImageHd(Sprite& s, int image, int frame, bool flip, int owner, int dx, int dy, int drawFunction) {
    const HdFrame& f = hdFrame(image, frame);
    if (!f.ok) return false;
    const uint32_t team = dataTeamRgb(owner);
    const float tr = float((team >> 16) & 255), tg = float((team >> 8) & 255), tb = float(team & 255);
    const int left = s.originX + dx * kHdScale - f.boxW / 2, top = s.originY + dy * kHdScale - f.boxH / 2;
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x) {
            const uint8_t* c = &f.color[(size_t(y) * f.w + x) * 4];
            int bx = f.x + x;
            if (flip) bx = f.boxW - 1 - bx;
            const int ox = left + bx, oy = top + f.y + y;
            if (ox < 0 || oy < 0 || ox >= s.width || oy >= s.height) continue;
            uint8_t* o = &s.rgba[(size_t(oy) * s.width + ox) * 4];
            if (drawFunction == 10) {  // a shadow: half black where nothing is drawn yet; else ApplyEffect_Shadow (1 - a / 2)
                if (!o[3]) o[0] = o[1] = o[2] = 0, o[3] = uint8_t(c[3] / 2);
                else
                    for (int n = 0; n < 3; ++n) o[n] = uint8_t(o[n] * (1 - c[3] / 510.0f));
                continue;
            }
            if (drawFunction == 9) {  // a glow: adds its light
                const int a = c[3];
                o[0] = uint8_t(std::min(255, o[0] + c[0] * a / 255)), o[1] = uint8_t(std::min(255, o[1] + c[1] * a / 255));
                o[2] = uint8_t(std::min(255, o[2] + c[2] * a / 255)), o[3] = std::max(o[3], uint8_t(a));
                continue;
            }
            if (c[3] < 102) continue;  // the renderer's alpha test
            const float m = f.mask.empty() ? 0 : f.mask[(size_t(y) * f.w + x) * 4] / 255.0f;
            o[0] = uint8_t(c[0] + (c[0] * tr / 255.0f - c[0]) * m), o[1] = uint8_t(c[1] + (c[1] * tg / 255.0f - c[1]) * m);
            o[2] = uint8_t(c[2] + (c[2] * tb / 255.0f - c[2]) * m), o[3] = 255;
        }
    return true;
}

// The draw functions that change how an image looks, put on `src` as it goes onto `dst` (the same size and origin).
// `level`: the function's state (OpenBW's modifier_data1: cloaking 0..8, the warp texture's frame, warp flash
// 48..63); `at`: the frame's rectangle on the canvas (the warp texture starts at its top left).
// HD (compositeLookHd), as StarCraft: Remastered's shaders draw them (ShadersGLSL in the game's files). The sprite
// draws into the effect channels (effect_mask.glsl), and ApplyEffect_Cloak (util_effect.glsl) bends what is under it:
//  2-7 cloak (SPRITE_CLOAKED, the blue channel): the brightness b; where the pixel (b + 1/16) * 8 to the right is
//      cloaked too, the pixel shows what is there; else what is under it. The shaders do not make a difference
//      between the enemy's (2-4) and the owner's (5-7) functions.
//  8 EMP shockwave (SPRITE_WARPED, the red channel): the pixel shows what is (r, g, b average) * 8 to the right.
//  11 the health bar: not part of the picture.
//  12 warp texture (sprite_mapped_frag.glsl): the sprite's shape, filled with that frame of the Warp Texture image.
//  16 hallucination (util_hallucinate.glsl).  17 warp flash (sprite_brighten_frag.glsl): white over the sprite.
// SD (compositeLookSd), as OpenBW draws them on the palette: `src` holds palette indices (Blend::Index).
//  2-7 cloak: trans50.pcx mixes each pixel with what is under it; while it cloaks or decloaks, the pixels brighter
//      than the level (cloak_fade_selector) show as they are.  8 EMP: the pixel shows what is (palette index) pixels
//      to the right.  10 shadow: dark.pcx row 18.  12 warp texture: the frame of the Warp Texture image at the same
//      place.  17 warp flash: ofire.pcx row level - 1 over what is under it.
// Where nothing is under the image, the stage's colors stand in for the ground.
constexpr uint8_t kGround[3] = {12, 12, 14}, kGroundLine[3] = {24, 24, 28};
bool drawFunctionLooks(int df) { return (df >= 2 && df <= 8) || df == 11 || df == 12 || df == 16 || df == 17; }
// The draw functions that show what is under the image (the image player then shows the map's terrain under it).
bool drawFunctionBends(int df) { return df >= 2 && df <= 8; }
// The state a picture with one frame shows: cloaked, the warp texture's first frame, the warp flash at its start.
int lookLevel(int df) { return df == 12 ? 0 : df == 17 ? 48 : 8; }
const Sprite* warpTexture(int k, int frame);
struct LookRect {
    int x = 0, y = 0, w = 0, h = 0;
};
// What is under the image at (x, y) of `back`: what is drawn there, over the stage's ground.
void lookBehind(const Sprite& back, int k, int x, int y, float out[3]) {
    const int grid = 32 * k;
    const bool onLine = ((x - back.originX) % grid + grid) % grid == 0 || ((y - back.originY) % grid + grid) % grid == 0;
    const uint8_t* g = onLine ? kGroundLine : kGround;
    out[0] = g[0], out[1] = g[1], out[2] = g[2];
    if (x < 0 || y < 0 || x >= back.width || y >= back.height) return;
    const uint8_t* c = &back.rgba[(size_t(y) * back.width + x) * 4];
    const float a = c[3] / 255.0f;
    for (int n = 0; n < 3; ++n) out[n] = c[n] * a + out[n] * (1 - a);
}
void lookOver(uint8_t* o, float r, float g, float b, float a) {
    const float da = o[3] / 255.0f, oa = a + da * (1 - a);
    if (oa <= 0) return;
    o[0] = uint8_t((r * a + o[0] * da * (1 - a)) / oa), o[1] = uint8_t((g * a + o[1] * da * (1 - a)) / oa);
    o[2] = uint8_t((b * a + o[2] * da * (1 - a)) / oa), o[3] = uint8_t(std::min(255.0f, oa * 255));
}

void compositeLookHd(Sprite& dst, const Sprite& src, int df, int level, int k, LookRect at) {
    if (df == 11) return;
    const int W = src.width, H = src.height;
    auto lum = [](const uint8_t* c) { return (0.30196078f * c[0] + 0.59215686f * c[1] + 0.10980392f * c[2]) / 255.0f; };
    if (drawFunctionBends(df)) {
        const Sprite back = dst;
        // How far along the cloak is: 1 cloaked; cloaking and decloaking go through 0..8. EMP: always.
        const float t = df == 8 || df == 3 || df == 6 ? 1.0f : std::clamp(level, 0, 8) / 8.0f;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const uint8_t* c = &src.rgba[(size_t(y) * W + x) * 4];
                if (!c[3]) continue;
                uint8_t* o = &dst.rgba[(size_t(y) * W + x) * 4];
                float bent[3];
                if (df == 8) {  // the red channel: the color average, as much as the sprite covers
                    const float r = (c[0] + c[1] + c[2]) / (3 * 255.0f) * (c[3] / 255.0f);
                    lookBehind(back, k, x + int(r * 8 * k), y, bent);
                    lookOver(o, bent[0], bent[1], bent[2], 1.0f);
                    continue;
                }
                if (c[3] < 102) continue;
                const float b = lum(c);
                const int tx = x + int((b + 0.0625f) * 8 * k);
                const bool alsoCloaked = b > 0 && tx < W && src.rgba[(size_t(y) * W + tx) * 4 + 3] >= 102 && lum(&src.rgba[(size_t(y) * W + tx) * 4]) > 0;
                lookBehind(back, k, alsoCloaked ? tx : x, y, bent);
                if (t < 1) lookOver(o, c[0], c[1], c[2], 1.0f);  // the sprite, as the cloak comes over it
                lookOver(o, bent[0], bent[1], bent[2], t);
            }
        return;
    }
    const Sprite* warpTex = df == 12 ? warpTexture(k, level) : nullptr;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const uint8_t* c = &src.rgba[(size_t(y) * W + x) * 4];
            if (!c[3]) continue;
            uint8_t* o = &dst.rgba[(size_t(y) * W + x) * 4];
            float r = c[0], g = c[1], b = c[2];
            const float a = c[3] / 255.0f;
            if (df == 16) {  // hallucination: mix(color, vec3(0.75, 1.125, 2.65) * brightness, 1)
                const float l = lum(c) * 255;
                r = std::min(255.0f, 0.75f * l), g = std::min(255.0f, 1.125f * l), b = std::min(255.0f, 2.65f * l);
            } else if (df == 12) {  // the texture's pixel at the same place of the frame; black where it has none
                r = g = b = 0;
                const int wx = x - at.x, wy = y - at.y;
                if (warpTex && wx >= 0 && wy >= 0 && wx < warpTex->width && wy < warpTex->height) {
                    const uint8_t* m = &warpTex->rgba[(size_t(wy) * warpTex->width + wx) * 4];
                    r = m[0], g = m[1], b = m[2];
                }
            }
            lookOver(o, r, g, b, a);
            if (df == 17)  // warp flash: white over the sprite, fading as the flash goes 48 -> 63 (as the SD ofire.pcx rows)
                lookOver(o, 255, 255, 255, a * (1 - std::clamp((level - 48) / 15.0f, 0.0f, 1.0f)));
        }
}

// The palette index nearest to a color (exact for the tileset's own colors).
uint8_t paletteIndex(const Tileset& ts, const float c[3]) {
    static std::unordered_map<uint32_t, uint8_t> cache;
    static const Tileset* cachedFor = nullptr;
    static uint16_t cachedEra = 0xFFFF;
    if (cachedFor != &ts || cachedEra != ts.era) cache.clear(), cachedFor = &ts, cachedEra = ts.era;
    const int r = std::clamp(int(c[0] + 0.5f), 0, 255), g = std::clamp(int(c[1] + 0.5f), 0, 255), b = std::clamp(int(c[2] + 0.5f), 0, 255);
    const uint32_t key = uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    int best = 0, bestScore = INT_MAX;
    for (int n = 0; n < 256; ++n) {
        const Rgb& p = ts.palette[size_t(n)];
        const int dr = p.r - r, dg = p.g - g, db = p.b - b, score = dr * dr + dg * dg + db * db;
        if (score < bestScore) best = n, bestScore = score;
        if (!score) break;
    }
    return cache[key] = uint8_t(best);
}

void compositeLookSd(Sprite& dst, const Sprite& src, int df, int level, const Tileset& ts, UnitSprites& sp, LookRect at) {
    if (df == 11) return;
    const int W = src.width, H = src.height;
    const Sprite back = dst;
    auto put = [&](uint8_t* o, int index) {
        const Rgb& c = ts.palette[size_t(index & 255)];
        o[0] = c.r, o[1] = c.g, o[2] = c.b, o[3] = 255;
    };
    auto under = [&](int x, int y) {
        float c[3];
        lookBehind(back, 1, x, y, c);
        return paletteIndex(ts, c);
    };
    // A row of a tileset table for `row`: the color over what is under it (0 when the table has no such row).
    auto table = [&](const std::vector<uint8_t>& t, int row, int old) { return row >= 0 && size_t(row + 1) * 256 <= t.size() ? t[size_t(row) * 256 + size_t(old)] : 0; };
    const std::vector<uint8_t>& trans50 = sp.tilesetTable(ts, "trans50");
    const std::vector<uint8_t>& dark = sp.tilesetTable(ts, "dark");
    const std::vector<uint8_t>& ofire = sp.tilesetTable(ts, "ofire");
    const Grp* tex = df == 12 ? sp.grp(210) : nullptr;  // the Warp Texture image
    const Grp::Frame* tf = tex && !tex->frames.empty() ? &tex->frames[size_t(std::clamp(level, 0, int(tex->frames.size()) - 1))] : nullptr;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const uint8_t* c = &src.rgba[(size_t(y) * W + x) * 4];
            if (!c[3]) continue;
            const int raw = c[0], idx = c[1];
            uint8_t* o = &dst.rgba[(size_t(y) * W + x) * 4];
            if (df == 8) {  // the pixel `raw` to the right
                float b[3];
                lookBehind(back, 1, x + raw < W ? x + raw : x, y, b);
                o[0] = uint8_t(b[0]), o[1] = uint8_t(b[1]), o[2] = uint8_t(b[2]), o[3] = 255;
            } else if (drawFunctionBends(df)) {
                int v = table(trans50, idx - 1, under(x, y));
                // Cloaking and decloaking: the pixels brighter than the level show as they are.
                if (df != 3 && df != 6) {
                    const Rgb& p = ts.palette[size_t(raw)];
                    if ((p.r * 28 + p.g * 77 + p.b * 151 + 4096) / 8192 > level) v = idx;
                }
                put(o, v);
            } else if (df == 10) {
                put(o, table(dark, 18, under(x, y)));
            } else if (df == 12) {
                const int wx = x - at.x, wy = y - at.y;
                put(o, tf && wx >= 0 && wy >= 0 && wx < tf->width && wy < tf->height ? tf->pixels[size_t(wy) * tf->width + wx] : 0);
            } else if (df == 17) {
                put(o, table(ofire, level - 1, under(x, y)));
            } else if (df == 16) {  // hallucination: OpenBW does not draw it; the HD colors
                const Rgb& p = ts.palette[size_t(idx)];
                const float l = 0.30196078f * p.r + 0.59215686f * p.g + 0.10980392f * p.b;
                o[0] = uint8_t(std::min(255.0f, 0.75f * l)), o[1] = uint8_t(std::min(255.0f, 1.125f * l)), o[2] = uint8_t(std::min(255.0f, 2.65f * l)), o[3] = 255;
            } else {
                put(o, idx);
            }
        }
}

// Where a frame of an image is on a canvas (its origin there, plus (dx, dy) game pixels): SD pixels, or HD2.
LookRect sdFrameRect(UnitSprites& sp, int image, int frame, bool flip, const Sprite& s, int dx, int dy) {
    const Grp* g = sp.grp(image);
    if (!g || g->frames.empty()) return {};
    const Grp::Frame& f = g->frames[size_t(frame) < g->frames.size() ? size_t(frame) : 0];
    return {s.originX - g->width / 2 + (flip ? g->width - f.x - f.width : f.x) + dx, s.originY - g->height / 2 + f.y + dy, f.width, f.height};
}
LookRect hdFrameRect(int image, int frame, bool flip, const Sprite& s, int dx, int dy) {
    const HdFrame& f = hdFrame(image, frame);
    if (!f.ok) return {};
    return {s.originX + dx * kHdScale - f.boxW / 2 + (flip ? f.boxW - f.x - f.w : f.x), s.originY + dy * kHdScale - f.boxH / 2 + f.y, f.w, f.h};
}

// A frame of the Warp Texture image (210) in HD2 art: the pixels of the frame, opaque.
const Sprite* warpTexture(int, int frame) {
    static std::map<std::pair<int, bool>, Sprite> tex;
    auto [it, fresh] = tex.try_emplace({frame, shellState().carbot});
    Sprite& t = it->second;
    if (fresh && shellState().ed) {
        const HdFrame& f = hdFrame(210, frame);
        if (f.ok) {
            t.width = f.w, t.height = f.h, t.rgba = f.color;
            for (size_t n = 3; n < t.rgba.size(); n += 4) t.rgba[n] = 255;
        }
    }
    return &t;
}

// One frame of an image in HD2 art, on a canvas of its frame box (texture pixels); empty when there is no HD art.
Sprite hdImagePicture(int image, int frame) {
    Sprite s;
    if (image < 0) return s;
    const HdFrame& f = hdFrame(image, frame);
    if (!f.ok) return s;
    s.width = f.boxW, s.height = f.boxH, s.originX = f.boxW / 2, s.originY = f.boxH / 2;
    s.rgba.assign(size_t(s.width) * s.height * 4, 0);
    if (!drawImageHd(s, image, frame, false, 0, 0, 0, 0)) s = {};
    return s;
}

struct ImagePlayer {
    int image = -1, anim = 0, dir = 12;
    bool playing = true, loop = true, muted = true;  // the script's sounds: off until turned on
    bool showCode = false;                           // the script's code under the timeline
    SpriteSim run;  // the sprite as the game runs it: overlays, shadows, flips, turning
    uint64_t last = 0;
    int ticks = 0, idle = 0;  // ticks played; ticks since the animation came to its end
    int pending = -1, pendingKind = 0;  // the order's next step (simOrderStep)
    int sinceAttack = 99;                // ticks since the script attacked (the weapon's cooldown counts from there)
    int target = -1;                     // the target's direction (256ths, 0 up, clockwise); -1: no target
    bool ground = false;                 // the map's terrain under the sprite (once an image of it bends what is under it)
};
ImagePlayer gImagePlayer;
const wchar_t* kAnimNames[28] = {L"Init", L"Death", L"Ground attack", L"Air attack", L"Unused 1", L"Ground attack repeat", L"Air attack repeat",
                                 L"Cast spell", L"Ground attack end", L"Air attack end", L"Unused 2", L"Walking", L"Walking end", L"Special 1",
                                 L"Special 2", L"Almost built", L"Built", L"Landing", L"Lift-off", L"Working", L"Working end", L"Warp in",
                                 L"Unused 3", L"Editor init", L"Disable", L"Burrow", L"Unburrow", L"Enable"};

// The unit whose graphics are `image` (the first one; a turret's image gives the turret unit), or -1.
int unitOfImage(int image) {
    static std::map<int, int> cache;
    static int cachedEdits = -1;
    if (cachedEdits != dataState().edits) cache.clear(), cachedEdits = dataState().edits;
    auto [it, fresh] = cache.try_emplace(image, -1);
    if (fresh)
        for (int u = 0; u < 228; ++u)
            if (imageOfUnit(u) == image) {
                it->second = u;
                break;
            }
    return it->second;
}

// The sprite as its unit's order runs it: it turns toward the target at the unit's turn rate (flingy.dat Turn
// Radius), and the Warp in animation of a building plays the whole warp-in (the warp texture, then the flash).
SpriteSim imagePlayerSim(int image, int anim, int dir32) {
    const bool warp = anim == 21 && dataSprites().animOffset(image, 21);
    SpriteSim sim(dataSprites(), image, warp ? 0 : anim, dir32, 1, shellState().ed->tileset().era);
    const int unit = unitOfImage(image);
    if (unit >= 0) sim.setTurnRate(datValue("flingy", "Turn Radius", datValue("units", "Graphics", unit, -1), 0));
    sim.setTarget(gImagePlayer.target);
    if (warp) sim.warpIn();
    return sim;
}

void imagePlayerStart(int image, int anim) {
    if (!shellState().ed) return;
    if (gImagePlayer.image != image) gImagePlayer.target = -1;
    gImagePlayer.image = image, gImagePlayer.anim = anim, gImagePlayer.ticks = 0, gImagePlayer.idle = 0, gImagePlayer.pending = -1;
    gImagePlayer.sinceAttack = 99, gImagePlayer.ground = false;
    gImagePlayer.run = imagePlayerSim(image, anim, gImagePlayer.dir);
    gImagePlayer.last = dataTickMs();
}

// The open map's terrain as the image player's ground: 16 x 16 tiles, SD (k = 1) or HD2 (k = 2).
const Sprite& stageGround(int k) {
    static Sprite ground[3];
    static const Editor* madeFor[3] = {};
    static int madeMode[3] = {-1, -1, -1};
    const int n = std::clamp(k, 1, 2), mode = shellState().carbot ? 2 : 1;
    Sprite& g = ground[n];
    if (!shellState().ed || (madeFor[n] == shellState().ed.get() && madeMode[n] == mode)) return g;
    madeFor[n] = shellState().ed.get(), madeMode[n] = mode;
    static HdTiles hd;
    static std::pair<int, int> hdFor{-1, -1};
    if (n > 1 && hdFor != std::pair<int, int>{shellState().ed->era(), mode}) {
        std::string err;
        hdFor = {shellState().ed->era(), mode};
        if (!loadHdTiles(shellState().data, shellState().ed->era(), GraphicsQuality::HD2, hd, err, shellState().carbot)) hd = {};
    }
    // The middle 4 x 4 tiles (what the stage shows) where the map has the most different megatiles (cliffs,
    // doodads), so that what bends shows; the middle of the map when all are the same.
    const int w = shellState().ed->width(), h = shellState().ed->height();
    const auto& tiles = shellState().ed->terrain().tiles;
    int bx = w / 2 - 8, by = h / 2 - 8;
    for (int y = 0, most = 1; y + 16 <= h; y += 2)
        for (int x = 0; x + 16 <= w; x += 2) {
            std::set<int> kinds;
            for (int ty = y + 6; ty < y + 10; ++ty)
                for (int tx = x + 6; tx < x + 10; ++tx) kinds.insert(shellState().ed->tileset().megatileFor(tiles[size_t(ty) * w + tx]));
            if (int(kinds.size()) > most) most = int(kinds.size()), bx = x, by = y;
        }
    g = mapTilesPicture(shellState().ed->tileset(), tiles, w, bx, by, 16, 16, n > 1 && !hd.tiles.empty() ? &hd : nullptr);
    if (g.width != 512 * n) g = {};  // no HD2 tiles: no ground in HD
    return g;
}

// A running sprite's picture: its images back to front, shadows darkening, glows adding light, on a 512 x 512 game pixel
// canvas (its origin in the middle). `ground`: the map's terrain under it (what a cloak bends). Returns the picture's
// pixels per game pixel (kHdScale for the HD art, 1 for SD), 0 for none.
int spriteSimPicture(const SpriteSim& sim, int owner, bool ground, Sprite& out) {
    if (!shellState().ed) return 0;
    UnitSprites& sp = dataSprites();
    if (widgetsHd()) {
        Sprite hs;
        hs.width = hs.height = 512 * kHdScale, hs.originX = hs.originY = 256 * kHdScale;
        const Sprite& gr = stageGround(kHdScale);
        if (ground && gr.width == hs.width) hs.rgba = gr.rgba;
        else hs.rgba.assign(size_t(hs.width) * hs.height * 4, 0);
        bool any = false;
        Sprite scratch;
        for (const auto& d : sim.draws()) {
            if (!drawFunctionLooks(d.drawFunction)) {
                any |= drawImageHd(hs, d.image, d.frame, d.flip, owner, d.x, d.y, d.drawFunction);
                continue;
            }
            scratch.width = hs.width, scratch.height = hs.height, scratch.originX = hs.originX, scratch.originY = hs.originY;
            scratch.rgba.assign(hs.rgba.size(), 0);
            if (drawImageHd(scratch, d.image, d.frame, d.flip, owner, d.x, d.y, 0))
                any = true, compositeLookHd(hs, scratch, d.drawFunction, d.level, kHdScale, hdFrameRect(d.image, d.frame, d.flip, hs, d.x, d.y));
        }
        if (any) return out = std::move(hs), kHdScale;
    }
    const int size = 512;
    Sprite& s = out;
    s = {};
    s.width = s.height = size, s.originX = s.originY = size / 2;
    const Sprite& gr = stageGround(1);
    if (ground && gr.width == size) s.rgba = gr.rgba;
    else s.rgba.assign(size_t(size) * size * 4, 0);
    Sprite scratch;
    for (const auto& d : sim.draws()) {
        // On the ground, a shadow darkens it as OpenBW does (dark.pcx); else it darkens where nothing is drawn yet.
        if (drawFunctionLooks(d.drawFunction) || (ground && d.drawFunction == 10)) {
            scratch.width = s.width, scratch.height = s.height, scratch.originX = s.originX, scratch.originY = s.originY;
            scratch.rgba.assign(s.rgba.size(), 0);
            sp.drawImage(scratch, d.image, d.frame, d.flip, uint8_t(owner), shellState().ed->tileset(), d.x, d.y, UnitSprites::Blend::Index);
            compositeLookSd(s, scratch, d.drawFunction, d.level, shellState().ed->tileset(), sp, sdFrameRect(sp, d.image, d.frame, d.flip, s, d.x, d.y));
            continue;
        }
        const auto blend = d.drawFunction == 10 ? UnitSprites::Blend::Shadow : d.drawFunction == 9 ? UnitSprites::Blend::Glow : UnitSprites::Blend::Normal;
        sp.drawImage(s, d.image, d.frame, d.flip, uint8_t(owner), shellState().ed->tileset(), d.x, d.y, blend);
    }
    return 1;
}

// After a tick: what the unit's order would do when the script gives control back. `pending` counts down to it
// (-1: nothing pending), `kind` is what gave control back.
// An attack repeats when the weapon's cooldown (weapons.dat Weapon Cooldown of the unit's weapon) has gone by since
// the script attacked.
void simOrderStep(SpriteSim& sim, int image, int anim, int& pending, int& kind, int& sinceAttack) {
    if (sim.takeAttack()) sinceAttack = 0;
    else if (sinceAttack < 9999) ++sinceAttack;
    // A warp-in: the order waits for the warp texture and the flash; then the sprite starts again.
    if (const int w = sim.warpState(); w == 1 || w == 2 || (w == 3 && pending < 0)) {
        sim.takeHandoff();
        if (w != 3) return;
        kind = 3, pending = 24;
    }
    if (const int h = sim.takeHandoff(); h && pending < 0) {
        kind = h, pending = h == 2 ? 12 : 24;
        if (h == 1) {
            const int unit = unitOfImage(image), air = anim == 3 || anim == 6;
            const int weapon = unit < 0 ? -1 : datValue("units", air ? "Air Weapon" : "Ground Weapon", unit, -1);
            const int cooldown = weapon >= 0 && weapon < 130 ? datValue("weapons", "Weapon Cooldown", weapon, 15) : 15;
            pending = std::max(1, cooldown - sinceAttack);
        }
    }
    if (pending < 0 || --pending > 0) return;
    pending = -1;
    const int again = anim == 2 || anim == 5 ? 5 : anim == 3 || anim == 6 ? 6 : anim;  // an attack: its repeat
    if (kind == 1 || kind == 2) sim.playAnim(again);
    else sim = imagePlayerSim(image, anim, sim.direction());
}

void sfxPlayOnce(int sfx);
void imagePlayerStep() {
    gImagePlayer.run.tick(), ++gImagePlayer.ticks;
    if (gImagePlayer.loop)
        simOrderStep(gImagePlayer.run, gImagePlayer.image, gImagePlayer.anim, gImagePlayer.pending, gImagePlayer.pendingKind, gImagePlayer.sinceAttack);
    for (int sfx : gImagePlayer.run.takeSounds())
        if (!gImagePlayer.muted) sfxPlayOnce(sfx);
    // A walking sprite that leaves the stage comes back on the other side.
    if (auto [x, y] = gImagePlayer.run.position(); std::abs(x) > 128 || std::abs(y) > 128)
        gImagePlayer.run.setPosition(std::abs(x) > 128 ? x - std::copysign(256.0, x) : x, std::abs(y) > 128 ? y - std::copysign(256.0, y) : y);
    // The end: the death's remnants too (sprites the script made), then it starts again.
    if (!gImagePlayer.run.anyRunning() && gImagePlayer.loop && ++gImagePlayer.idle > 12) imagePlayerStart(gImagePlayer.image, gImagePlayer.anim);
}
void imagePlayerSeek(int tick) {
    imagePlayerStart(gImagePlayer.image, gImagePlayer.anim);
    gImagePlayer.playing = false;
    const bool muted = gImagePlayer.muted;
    gImagePlayer.muted = true;  // no sounds while it runs to the tick
    const bool loop = gImagePlayer.loop;
    gImagePlayer.loop = false;
    while (gImagePlayer.ticks < tick && gImagePlayer.run.anyRunning()) imagePlayerStep();
    gImagePlayer.muted = muted, gImagePlayer.loop = loop;
}

// One game tick per 42 ms while the image plays; true if the picture changed.
bool imagePlayerTick() {
    if (gImagePlayer.image < 0 || !gImagePlayer.playing || gWorkspace != Workspace::Data) return false;
    const uint64_t now = dataTickMs();
    if (now - gImagePlayer.last > 500) gImagePlayer.last = now;
    bool stepped = false;
    // (A step can start the animation again, which sets `last` to the time now: compare, never subtract.)
    for (; now >= gImagePlayer.last + 42; gImagePlayer.last += 42) imagePlayerStep(), stepped = true;
    return stepped;
}

struct TimelineTick {
    int frame = 0;
    bool sound = false, attack = false;
};
std::vector<TimelineTick> animTimeline(int image, int anim, int maxTicks, bool& ends) {
    std::vector<TimelineTick> out;
    ends = false;
    if (!shellState().ed) return out;
    SpriteSim a = imagePlayerSim(image, anim, 0);
    // The constructor runs the first tick; what it played is the first step.
    TimelineTick first{a.mainFrame(), !a.takeSounds().empty(), a.takeAttack()};
    out.push_back(first);
    int pending = -1, kind = 0, sinceAttack = first.attack ? 0 : 99;
    for (int t = 1; t < maxTicks; ++t) {
        if (!a.running()) {
            ends = true;
            break;
        }
        a.tick();
        const bool attack = a.takeAttack();
        if (attack) sinceAttack = -1;
        if (gImagePlayer.loop) simOrderStep(a, image, anim, pending, kind, sinceAttack);
        if (!a.running()) {  // it ended in this tick: nothing more shows
            ends = true;
            break;
        }
        out.push_back({a.mainFrame(), !a.takeSounds().empty(), attack});
    }
    return out;
}

// A portraits.dat entry's file path, as portdata.tbl writes it ("tmarine\TMaFid0"), or "" for none.
std::string portraitPath(int e) {
    static Bytes tbl;
    static const Editor* tblFor = nullptr;  // read again for another map or project (its own portdata.tbl, the game's folder set later)
    if (tblFor != shellState().ed.get() || tbl.empty()) {
        tblFor = shellState().ed.get();
        std::string err;
        shellState().data.readFile("arr\\portdata.tbl", tbl, err);
    }
    const uint32_t file = datValue("portdata", "Portrait File", e);
    std::string path;
    if (tbl.size() >= 2 && file && file <= size_t(tbl[0] | tbl[1] << 8)) {
        const size_t o = size_t(tbl[2 + 2 * (file - 1)] | tbl[3 + 2 * (file - 1)] << 8);
        for (size_t i = o; i < tbl.size() && tbl[i]; ++i) path += char(tbl[i]);
    }
    return path;
}

// The clips of a portrait (GitHub #132: switching was slow with Remastered portraits). The files are read here (the
// game's storage is read on this thread only); their videos are decoded on a thread of their own, and `ready` says
// when. The last 8 portraits stay (decoded HD video is large), so going back to one is at once.
struct PortraitClips {
    std::atomic<bool> ready{false};
    std::vector<PortraitVideo> clips;
};
std::shared_ptr<PortraitClips> portraitClips(const std::string& path) {
    using Key = std::tuple<std::string, int, bool>;
    static std::map<Key, std::pair<uint64_t, std::shared_ptr<PortraitClips>>> cache;  // by the last use
    static uint64_t uses = 0;
    const Key key{path, int(shellState().quality), shellState().carbot};
    if (auto it = cache.find(key); it != cache.end()) return it->second.first = ++uses, it->second.second;
    if (cache.size() >= 8) {  // the one not used for the longest time goes
        auto old = cache.begin();
        for (auto it = cache.begin(); it != cache.end(); ++it)
            if (it->second.first < old->second.first) old = it;
        cache.erase(old);
    }
    auto entry = std::make_shared<PortraitClips>();
    cache[key] = {++uses, entry};
    std::vector<Bytes> files;
    for (int v = 0; v < 8 && !path.empty(); ++v) {
        Bytes f;
        std::string err;
        if (!readPortraitFile(shellState().data, path, v, shellState().quality, shellState().carbot, f, err)) break;
        files.push_back(std::move(f));
    }
    std::thread([entry, files = std::move(files)] {
        std::vector<PortraitVideo> clips;
        for (const Bytes& f : files) {
            PortraitVideo pv;
            std::string err;
            if (!decodeWebm(f, pv, err) || pv.frames.empty()) break;
            clips.push_back(std::move(pv));
        }
        entry->clips = std::move(clips);
        entry->ready.store(true, std::memory_order_release);
        shellPost([] { dataPanesChanged(); });  // the form shows it (else "Loading" stays until a repaint)
    }).detach();
    return entry;
}


// A sfxdata.dat entry's sound file, as sfxdata.tbl names it under sound\ ("" for none).
std::wstring sfxPath(int sfx) {
    static std::vector<std::string> tbl;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        Bytes b;
        std::string err;
        if (shellState().data.readFile("arr\\sfxdata.tbl", b, err) && b.size() >= 2) {
            const size_t n = size_t(b[0] | b[1] << 8);
            for (size_t i = 0; i < n && 2 + i * 2 + 1 < b.size(); ++i) {
                size_t at = size_t(b[2 + i * 2] | b[3 + i * 2] << 8);
                std::string s;
                while (at < b.size() && b[at]) s += char(b[at++]);
                tbl.push_back(s);
            }
        }
    }
    const int file = datValue("sfxdata", "Sound File", sfx);
    return file > 0 && size_t(file - 1) < tbl.size() ? widen(tbl[size_t(file - 1)]) : L"";
}

// One frame of an image as the Data pictures draw it (core/views_data_diagrams.inc, main.cpp: drawGameImage): in the
// art View shows (HD2 or Carbot: texture pixels; SD: game pixels), with its draw function's look, its origin in the
// middle of its frame box. Returns the sprite's pixels per game pixel (kHdScale or 1); 0 when there is no picture.
int gameImageSprite(int image, int frame, bool flip, int owner, Sprite& s) {
    s = {};
    if (!shellState().ed || image < 0) return 0;
    if (widgetsHd()) {
        const HdFrame& f = hdFrame(image, frame);
        if (f.ok) {
            s.width = f.boxW, s.height = f.boxH, s.originX = f.boxW / 2, s.originY = f.boxH / 2;
            s.rgba.assign(size_t(s.width) * s.height * 4, 0);
            const int df = dataSprites().drawFunction(image);
            drawImageHd(s, image, frame, flip, owner, 0, 0, drawFunctionLooks(df) ? 0 : df);
            if (drawFunctionLooks(df)) {
                Sprite out = s;
                std::fill(out.rgba.begin(), out.rgba.end(), uint8_t(0));
                compositeLookHd(out, s, df, lookLevel(df), kHdScale, hdFrameRect(image, frame, flip, out, 0, 0));
                s = std::move(out);
            }
            return kHdScale;
        }
    }
    UnitSprites& sp = dataSprites();
    const Grp* g = sp.grp(image);
    if (!g || g->frames.empty() || !g->width || !g->height) return 0;
    frame = std::clamp(frame, 0, int(g->frames.size()) - 1);
    s.width = g->width, s.height = g->height, s.originX = g->width / 2, s.originY = g->height / 2;
    s.rgba.assign(size_t(s.width) * s.height * 4, 0);
    const int df = sp.drawFunction(image);
    const Tileset& ts = shellState().ed->tileset();
    sp.drawImage(s, image, frame, flip, uint8_t(std::clamp(owner, 0, 11)), ts, 0, 0, drawFunctionLooks(df) ? UnitSprites::Blend::Index : UnitSprites::Blend::Normal);
    if (drawFunctionLooks(df)) {
        Sprite out = s;
        std::fill(out.rgba.begin(), out.rgba.end(), uint8_t(0));
        compositeLookSd(out, s, df, lookLevel(df), ts, sp, sdFrameRect(sp, image, frame, flip, out, 0, 0));
        s = std::move(out);
    }
    return 1;
}

// --- The Data diagrams' rules (the Windows widgets in main.cpp and core/views_data_diagrams.inc use them) ---------------

// The game's distance (OpenBW xy_length): not a circle. With x the longer side and y the shorter,
// x - x/16 + 3y/8 - x/64 + 3y/256 when y is over x/4, else x. So a range is a polygon.
double gameLength(double dx, double dy) {
    double x = std::abs(dx), y = std::abs(dy);
    if (x < y) std::swap(x, y);
    return x / 4 < y ? x - x / 16 + y * 3 / 8 - x / 64 + y * 3 / 256 : x;
}
// The points within `range` of a box (screen pixels; `range` already zoomed): ranges between units are measured
// from box to box (OpenBW units_distance), so the box grows by the game's distance shape on every side.
std::vector<draw::Point> rangeShapeAround(draw::Rect box, double range) {
    std::vector<draw::Point> pts;
    const int steps = 256;
    for (int k = 0; k <= steps; ++k) {
        const double a = 2 * 3.14159265358979323846 * k / steps, dx = std::cos(a), dy = std::sin(a);
        const double r = range / gameLength(dx, dy);
        const double px = dx * r, py = dy * r;
        pts.push_back({int(std::lround((px >= 0 ? box.right : box.left) + px)), int(std::lround((py >= 0 ? box.bottom : box.top) + py))});
    }
    return pts;
}

// The building an addon goes on (for its picture).
int addonParent(int addon) {
    switch (addon) {
    case 107: case 108: return 106;  // Comsat Station, Nuclear Silo: Command Center
    case 115: return 114;            // Control Tower: Starport
    case 117: case 118: return 116;  // Covert Ops, Physics Lab: Science Facility
    case 120: return 113;            // Machine Shop: Factory
    default: return 106;
    }
}

// One hit: amount + bonus x level, less armor (not for Ignore Armor), then the type's share for the size
// (Explosive 50/75/100 %, Concussive 100/50/25 %); at least 0.5.
double hitDamage(int weapon, int level, int size, int armor) {
    const int type = datValue("weapons", "Weapon Type", weapon);
    double d = datValue("weapons", "Damage Amount", weapon) + datValue("weapons", "Damage Bonus", weapon) * level;
    if (type != 4) d = d > armor ? d - armor : 0;
    if (type == 1) d *= size == 1 ? 0.5 : size == 2 ? 0.75 : 1.0;
    if (type == 2) d *= size == 1 ? 1.0 : size == 2 ? 0.5 : 0.25;
    if (type == 0) d = 0;
    return std::max(d, 0.5);
}

// --- units: the tiles a unit sees (OpenBW generate_sight_values, the base mask) ---

// The sight mask for a range of 0..11 tiles: (3 + 2 r) tiles square, true where the unit sees.
std::vector<bool> sightMask(int range, int& size) {
    range = std::clamp(range, 0, 11);
    const int maxW = 3 + range * 2, maxH = maxW;
    size = maxW;
    std::vector<bool> m(size_t(maxW) * maxH, false);
    auto mask = [&](int index) {
        if (index >= 0 && size_t(index) < m.size()) m[size_t(index)] = true;
    };
    const int offx = maxW / 2 - 1, offy = maxH / 2 - 1;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) mask((offy + y) * maxW + offx + x);
    const int offset = maxH / 2 - maxW / 2, halfW = maxW / 2;
    int maxX2 = halfW, maxX1 = halfW * 2, curX1 = 0, curX2 = halfW, i = 0, maxI = halfW;
    int cursize1 = 0, cursize2 = halfW * halfW, minCursize2 = halfW * (halfW - 1), minCursize2Chg = halfW * 2;
    while (true) {
        if (curX1 <= maxX1)
            for (int k = 0; k <= maxX1 - curX1; ++k) mask((offset + curX2) * maxW + curX1 + k), mask((offset + maxX2) * maxW + curX1 + k);
        if (curX2 <= maxX2)
            for (int k = 0; k <= maxX2 - curX2; ++k) mask((offset + curX1) * maxW + curX2 + k), mask((offset + maxX1) * maxW + curX2 + k);
        cursize2 += 1 - cursize1 - 2;
        cursize1 += 2;
        --curX2, ++maxX2;
        if (cursize2 <= minCursize2) --maxI, ++curX1, --maxX1, minCursize2 -= minCursize2Chg - 2, minCursize2Chg -= 2;
        if (++i > maxI) break;
    }
    return m;
}
