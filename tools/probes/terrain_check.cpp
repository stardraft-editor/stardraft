// Terrain and map sprite check on a map corpus (roadmap N.6), without the GPU:
// - each tileset: every HD, HD2 and Carbot tileset has a tile for each SD megatile, and the HD tiles look like
//   the SD megatile of the same index (mean colors and the 4x4 brightness pattern);
// - each map: every MTXM tile names a CV5 group and a megatile (maps made for a mod's larger tilesets do not;
//   they are listed, the editor shows those tiles magenta);
// - each map sprite (THG2): its image has HD2 graphics, and the editor's picture of it (the frame and the
//   images under and over it) is the one SpriteSim gives as the game starts the sprite.
//
// usage: terrain_check <install dir> <folder or map>... [--verbose]
//
// Exit code 1 if a tileset or sprite check fails. Maps with tiles that do not resolve are only listed.

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "stardraft/doodads.h"
#include "stardraft/game_data.h"
#include "stardraft/hd_assets.h"
#include "stardraft/map_file.h"
#include "stardraft/sprites.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;
namespace fs = std::filesystem;

namespace {

struct TilesetInfo {
    Tileset sd;
    size_t hd = 0, hd2 = 0, carbot = 0, carbot2 = 0;  // HD megatile counts
    bool ok = false;
};

// The 4x4 grid of mean colors (one per 8x8 minitile) of an SD megatile.
std::array<float, 48> sdGrid(const Tileset& ts, size_t mega) {
    std::array<float, 48> g{};
    for (int k = 0; k < 16; ++k) {
        const uint32_t ref = ts.megatiles[mega][k];
        const size_t mini = ref >> 1;
        if (mini >= ts.minitiles.size()) continue;
        for (int p = 0; p < 64; ++p) {
            const Rgb& c = ts.palette[ts.minitiles[mini][p]];
            g[k * 3] += c.r / 64.0f, g[k * 3 + 1] += c.g / 64.0f, g[k * 3 + 2] += c.b / 64.0f;
        }
    }
    return g;
}

// The same grid of an HD megatile (decoded).
std::array<float, 48> hdGrid(const HdTiles& hd, size_t mega) {
    std::array<float, 48> g{};
    DdsTexture t;
    t.format = hd.format, t.width = t.height = hd.tileSize, t.data = hd.tiles[mega];
    std::vector<uint8_t> px;
    decodeDds(t, 0, 0, hd.tileSize, hd.tileSize, px);
    const int cell = hd.tileSize / 4;
    const float n = float(cell * cell);
    for (int y = 0; y < hd.tileSize; ++y)
        for (int x = 0; x < hd.tileSize; ++x) {
            const int k = (y / cell) * 4 + x / cell;
            const uint8_t* c = &px[(size_t(y) * hd.tileSize + x) * 4];
            g[k * 3] += c[0] / n, g[k * 3 + 1] += c[1] / n, g[k * 3 + 2] += c[2] / n;
        }
    return g;
}

// Loads one HD tileset and compares each megatile that the CV5 names with the SD megatile of the same
// index: the HD art is the same picture redrawn, so the 4x4 grids of mean colors are close. A large
// difference means that the HD tile at that index is a different picture. Returns the tile count.
size_t checkHd(const GameData& data, const Tileset& sd, uint16_t era, GraphicsQuality q, bool carbot, int& failures,
               bool verbose) {
    HdTiles hd;
    std::string err;
    if (!loadHdTiles(data, era, q, hd, err, carbot)) {
        std::printf("    %s%s: %s\n", qualityName(q), carbot ? " Carbot" : "", err.c_str());
        ++failures;
        return 0;
    }
    std::vector<bool> used(sd.megatiles.size());
    for (const auto& g : sd.groups)
        for (uint16_t m : g.megatiles)
            if (m < used.size()) used[m] = true;
    // Two measures per megatile: the mean color difference, and the correlation of the 4x4 brightness
    // pattern on tiles with a pattern. Carbot art has other colors and shapes: its numbers are only reported
    // (a look at the tiles side by side showed the same terrain at each index).
    size_t checked = 0, far = 0, patterned = 0, alike = 0;
    double sum = 0;
    for (size_t m = 1; m < sd.megatiles.size() && m < hd.tiles.size(); ++m) {
        if (!used[m]) continue;
        const auto a = sdGrid(sd, m), b = hdGrid(hd, m);
        float d = 0;
        for (int i = 0; i < 48; ++i) d += std::abs(a[i] - b[i]);
        d /= 48;
        sum += d;
        ++checked;
        if (d > 40) {
            ++far;
            if (verbose && !carbot && far <= 5)
                std::printf("    %s megatile %zu: mean difference %.0f\n", qualityName(q), m, d);
        }
        float la[16], lb[16], ma = 0, mb = 0;
        for (int k = 0; k < 16; ++k) {
            la[k] = 0.3f * a[k * 3] + 0.59f * a[k * 3 + 1] + 0.11f * a[k * 3 + 2];
            lb[k] = 0.3f * b[k * 3] + 0.59f * b[k * 3 + 1] + 0.11f * b[k * 3 + 2];
            ma += la[k] / 16, mb += lb[k] / 16;
        }
        float sab = 0, saa = 0, sbb = 0;
        for (int k = 0; k < 16; ++k)
            sab += (la[k] - ma) * (lb[k] - mb), saa += (la[k] - ma) * (la[k] - ma), sbb += (lb[k] - mb) * (lb[k] - mb);
        if (std::sqrt(saa / 16) < 12 || sbb <= 0) continue;  // flat: no pattern to compare
        ++patterned;
        if (sab / std::sqrt(saa * sbb) > 0.5f) ++alike;
    }
    std::printf("    %-3s%-7s %zu tiles; %zu used megatiles: mean color difference %.1f, %zu differ by more than 40; "
                "%zu of %zu with a pattern have the same pattern\n",
                qualityName(q), carbot ? " Carbot" : "", hd.tiles.size(), checked, checked ? sum / checked : 0.0, far, alike,
                patterned);
    if (!carbot && far * 100 > checked) {
        std::printf("    FAIL: more than 1%% of the HD megatiles do not look like the SD megatile of the same index\n");
        ++failures;
    }
    if (!carbot && alike * 10 < patterned * 8) {  // Carbot art is drawn again in another style: report only
        std::printf("    FAIL: less than 80%% of the patterned megatiles have the pattern of the SD megatile\n");
        ++failures;
    }
    return hd.tiles.size();
}

bool isMap(const fs::path& p) {
    std::string e = p.extension().string();
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e == ".scm" || e == ".scx" || e == ".chk";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: terrain_check <install dir> <folder or map>... [--verbose]\n");
        return 2;
    }
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    bool verbose = false;
    std::vector<fs::path> maps;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
            continue;
        }
        fs::path p = argv[i];
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
                if (it->is_regular_file(ec) && isMap(it->path())) maps.push_back(it->path());
        } else {
            maps.push_back(p);
        }
    }

    // Tilesets: the SD tile counts, the HD tile counts, and CV5 megatile ids that no tileset has.
    std::printf("Tilesets:\n");
    TilesetInfo sets[8];
    int failures = 0;
    for (uint16_t era = 0; era < 8; ++era) {
        TilesetInfo& t = sets[era];
        if (!t.sd.load(data, era, err)) {
            std::printf("  %s: %s\n", tilesetName(era), err.c_str());
            ++failures;
            continue;
        }
        size_t maxRef = 0;
        for (const auto& g : t.sd.groups)
            for (uint16_t m : g.megatiles) maxRef = std::max<size_t>(maxRef, m);
        std::printf("  %s: %zu groups, %zu SD megatiles (largest CV5 megatile id %zu)\n", tilesetName(era), t.sd.groups.size(),
                    t.sd.megatiles.size(), maxRef);
        t.hd = checkHd(data, t.sd, era, GraphicsQuality::HD, false, failures, verbose);
        t.hd2 = checkHd(data, t.sd, era, GraphicsQuality::HD2, false, failures, verbose);
        t.carbot = checkHd(data, t.sd, era, GraphicsQuality::HD, true, failures, verbose);
        t.carbot2 = checkHd(data, t.sd, era, GraphicsQuality::HD2, true, failures, verbose);
        t.ok = true;
        for (size_t n : {t.hd, t.hd2, t.carbot, t.carbot2})
            if (n < t.sd.megatiles.size()) {
                std::printf("    FAIL: an HD tileset has fewer megatiles than VX4EX\n");
                ++failures;
                break;
            }
    }

    // Map sprites (THG2: doodad overlays and the like) draw with images.dat images; each must have its HD2
    // graphics. Checked once per image.
    UnitSprites sprites;
    if (!sprites.load(data, err)) {
        std::printf("  units and sprites: %s\n", err.c_str());
        ++failures;
    }
    std::map<int, bool> imageOk;  // image -> it loads
    // Each sprite's picture as the editor draws it (the main image at ImageScript's Init frame, and the images its
    // script puts under and over it) against the sprite as the game starts it (SpriteSim): checked once per image
    // and tileset.
    std::set<std::pair<int, int>> simChecked;  // (image, era)
    size_t spriteCount = 0, spriteBad = 0, spriteNoId = 0, simDiffer = 0;

    // Maps.
    size_t opened = 0, unreadable = 0, bad = 0;
    std::map<int, size_t> perEra;
    for (const fs::path& path : maps) {
        Bytes raw;
        if (path.extension() == ".chk") {
            std::ifstream in(path, std::ios::binary);
            raw.assign(std::istreambuf_iterator<char>(in), {});
        } else if (!readMapChk(path, raw, err)) {
            ++unreadable;
            if (verbose) std::printf("  cannot open %s: %s\n", path.string().c_str(), err.c_str());
            continue;
        }
        Chk chk = Chk::parse(raw);
        const uint16_t era = readEra(chk);
        const TilesetInfo& t = sets[era];
        if (!t.ok) continue;
        ++opened;
        ++perEra[era];
        for (const MapSprite& m : readSprites(chk)) {
            const bool asSprite = m.flags & SpriteFlag::DrawAsSprite;
            const int image = asSprite ? sprites.imageForSprite(m.id) : sprites.imageFor(m.id);
            auto it = imageOk.find(image);
            if (it == imageOk.end()) {
                AnimImage a;
                std::string e;
                const bool ok = image >= 0 && loadAnimImage(data, image, GraphicsQuality::HD2, a, e) && !a.frames.empty();
                it = imageOk.emplace(image, ok).first;
            }
            if (asSprite && image >= 0 && simChecked.insert({image, era}).second) {
                SpriteSim sim(sprites, image, 0, 0, 1, era);  // facing up, as a new sprite faces
                const ImageScript& sc = sprites.script(image);
                std::string why;
                if (sim.mainFrame() != sc.startFrameOn(era))
                    why = "frame " + std::to_string(sc.startFrameOn(era)) + ", the game shows " + std::to_string(sim.mainFrame());
                for (const auto& d : sim.draws()) {
                    bool drawn = d.image == image;
                    for (const auto* list : {&sc.under, &sc.over})
                        for (const auto& o : *list) drawn = drawn || o.image == d.image;
                    if (!drawn) why += (why.empty() ? "" : "; ") + std::string("no image ") + std::to_string(d.image);
                }
                if (verbose && sc.startFrameOn(era))
                    std::printf("  THG2 sprite %u (image %d) on %s starts at frame %d%s\n", m.id, image, tilesetName(era),
                                sc.startFrameOn(era), sc.startFrameTile ? " (playframtile)" : "");
                if (!why.empty()) {
                    ++simDiffer;
                    std::printf("  THG2 sprite %u (image %d) on %s: %s\n", m.id, image, tilesetName(era), why.c_str());
                }
            }
            ++spriteCount;
            if (image < 0) {  // an id out of range (65535 in some protected ladder maps): nothing to draw
                ++spriteNoId;
                continue;
            }
            if (!it->second && (!spriteBad++ || verbose))
                std::printf("  %s: THG2 %s %u at (%u, %u) has no picture (image %d)\n", path.string().c_str(),
                            asSprite ? "sprite" : "unit", m.id, m.x, m.y, image);
        }
        TerrainLayer mtxm = readTerrain(chk, "MTXM");
        size_t badGroup = 0, badMega = 0;
        int firstBad = -1;
        for (size_t i = 0; i < mtxm.tiles.size(); ++i) {
            const uint16_t v = mtxm.tiles[i];
            const int mega = t.sd.megatileFor(v);
            if (!t.sd.groupFor(v)) ++badGroup;
            else if (mega < 0) ++badMega;
            else continue;
            if (firstBad < 0) firstBad = int(i);
        }
        if (badGroup || badMega) {
            ++bad;
            std::printf("  %s (%s): %zu tiles name no CV5 group, %zu no megatile; first at (%d, %d) value 0x%04x\n",
                        path.string().c_str(), tilesetName(era), badGroup, badMega, firstBad % mtxm.width,
                        firstBad / mtxm.width, mtxm.tiles[size_t(firstBad)]);
            if (verbose) {
                std::printf("    sections:");
                for (const auto& s : chk.sections)
                    std::printf(" %s(%zu)", s.name.str().c_str(), s.data.size());
                TerrainLayer tile = readTerrain(chk, "TILE");
                size_t tileBad = 0, same = 0;
                for (size_t i = 0; i < tile.tiles.size(); ++i) {
                    if (t.sd.megatileFor(tile.tiles[i]) < 0) ++tileBad;
                    if (tile.tiles[i] == mtxm.tiles[i]) ++same;
                }
                std::printf("\n    TILE: %zu do not resolve, %zu of %zu equal MTXM\n", tileBad, same, tile.tiles.size());
            }
        }
    }
    std::printf("\n%zu maps checked", opened);
    for (auto [era, n] : perEra) std::printf(", %s %zu", tilesetName(uint16_t(era)), n);
    std::printf("; %zu do not open (protected); %zu with tiles that do not resolve\n", unreadable, bad);
    std::printf("%zu map sprites (THG2), %zu images; %zu with an id that names no image; %zu without a picture\n", spriteCount,
                imageOk.size(), spriteNoId, spriteBad);
    std::printf("%zu sprite pictures (image and tileset) checked against the game's start; %zu differ\n", simChecked.size(), simDiffer);
    return failures || spriteBad || simDiffer ? 1 : 0;
}
