// Repair passes over the mod's terrain drafts (the built-in rows, generated, not verified), with the template model
// (stardraft/mods/terrain_rules.h). For each tileset and each draft terrain, up to three passes:
//   1. structure: normalize (every template rect present, the engine's order); tiles that do not exist, pairs that
//      are not two subtiles side by side in one group: reported; NULL rects stay NULL and are counted (no borrowing of
//      the template's tiles: a game tile in a mod border hides a height decision).
//   2. foreign tiles: a piece whose tiles are in groups the maps never use with this terrain (Tileset::groupTerrains)
//      nor in its other pieces' groups: reported as suspects.
//   3. layout and shape check, a report for a person: each mod pair against the template pair by where each puts the
//      terrain and the ground (each pixel classed by colour against the set's own inside and ground tiles; masks
//      compared) and by the structure of its edges (gradient magnitude, normalized cross-correlation, 64 x 32). Nothing
//      is applied: right-looking sets score 0.4 to 0.85 on the layout, and a half-and-half tile matches many masks, so
//      the best candidate is often the same tile for several roles.
// The repaired rules go to one .sdterrain file (version 2; drafts stay drafts), the report to a text file, and (with an
// .inc path) the file's bytes as a C array for src/mods/cmbw.cpp (kRepairedRules: the program applies them over the
// built-in rows).
//   4. variants: a group's pieces for one rect should share one layout; a variant whose mask strays from the others'
//      consensus is dropped (the engine picks among the rest).
//   5. mirrors: a role and its mirror image (edge NE, edge NW) have mirrored tiles; in a set where most mirrored pairs
//      match (flipped pictures, NCC), the worse half of a pair that does not is replaced by the terrain's pair that
//      mirrors the other half clearly.
//   6. row order: a generated set lays its groups out in the game's rect order; a group that breaks the run is
//      reported with its neighbours.
//   7. seams: the border painted as the window's example; a piece whose seams against its neighbours stand out from
//      the set's (the mean plus two deviations, 40 gray levels or more) is reported.
// usage: cmbw_draft_fix <Cosmonarchy install> <out .sdterrain> <report .txt> [out .inc | -] [tileset] [passes 1-7]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/mods/terrain_rules.h"
#include "stardraft/previews.h"
#include "stardraft/sprites.h"
#include "stardraft/tileset.h"

using namespace stardraft;
namespace tr = stardraft::terrainrules;

namespace {

// The edge structure of a pair of tiles (64 x 32): gradient magnitudes of the gray picture, zero mean, unit variance.
std::vector<float> edges(const Tileset& ts, uint16_t l, uint16_t r) {
    const Sprite s = mapTilesPicture(ts, {l, r}, 2, 0, 0, 2, 1);
    std::vector<float> out;
    if (s.width != 64 || s.height != 32) return out;
    std::vector<float> gray(size_t(64 * 32));
    for (size_t i = 0; i < gray.size(); ++i) gray[i] = 0.299f * s.rgba[i * 4] + 0.587f * s.rgba[i * 4 + 1] + 0.114f * s.rgba[i * 4 + 2];
    out.assign(gray.size(), 0.0f);
    for (int y = 1; y < 31; ++y)
        for (int x = 1; x < 63; ++x) {
            const float gx = gray[size_t(y * 64 + x + 1)] - gray[size_t(y * 64 + x - 1)], gy = gray[size_t((y + 1) * 64 + x)] - gray[size_t((y - 1) * 64 + x)];
            out[size_t(y * 64 + x)] = std::sqrt(gx * gx + gy * gy);
        }
    double mean = 0;
    for (float v : out) mean += v;
    mean /= double(out.size());
    double var = 0;
    for (float& v : out) v = float(v - mean), var += double(v) * v;
    const float sd = float(std::sqrt(var / double(out.size())));
    if (sd > 1e-6f)
        for (float& v : out) v /= sd;
    return out;
}
float ncc(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0;
    double s = 0;
    for (size_t i = 0; i < a.size(); ++i) s += double(a[i]) * b[i];
    return float(s / double(a.size()));
}

// A material's colours: the mean and variance of each channel over some tiles of it (the terrain's inside, the ground).
struct Material {
    float mean[3] = {}, var[3] = {1, 1, 1};
    bool ok = false;
};
Material materialOf(const Tileset& ts, const std::vector<uint16_t>& tiles) {
    Material m;
    double sum[3] = {}, sq[3] = {};
    size_t n = 0;
    for (uint16_t t : tiles) {
        const Sprite s = mapTilesPicture(ts, {t}, 1, 0, 0, 1, 1);
        for (size_t i = 0; i + 3 < s.rgba.size(); i += 4, ++n)
            for (int c = 0; c < 3; ++c) sum[c] += s.rgba[i + c], sq[c] += double(s.rgba[i + c]) * s.rgba[i + c];
    }
    if (!n) return m;
    for (int c = 0; c < 3; ++c) m.mean[c] = float(sum[c] / n), m.var[c] = std::max(25.0f, float(sq[c] / n - (sum[c] / n) * (sum[c] / n)));
    m.ok = true;
    return m;
}
// Each pixel of a pair (64 x 32) as the terrain (1) or the ground (0): the nearer material by variance-weighted distance.
std::vector<uint8_t> maskOf(const Tileset& ts, uint16_t l, uint16_t r, const Material& terrain, const Material& ground) {
    const Sprite s = mapTilesPicture(ts, {l, r}, 2, 0, 0, 2, 1);
    std::vector<uint8_t> out;
    if (s.width != 64 || s.height != 32) return out;
    out.resize(size_t(64 * 32));
    for (size_t i = 0; i < out.size(); ++i) {
        float dt = 0, dg = 0;
        for (int c = 0; c < 3; ++c) {
            const float v = s.rgba[i * 4 + size_t(c)];
            dt += (v - terrain.mean[c]) * (v - terrain.mean[c]) / terrain.var[c], dg += (v - ground.mean[c]) * (v - ground.mean[c]) / ground.var[c];
        }
        out[i] = dt < dg ? 1 : 0;
    }
    return out;
}
// How much two masks agree (0 to 1).
float agree(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size() || a.empty()) return 0;
    size_t same = 0;
    for (size_t i = 0; i < a.size(); ++i) same += a[i] == b[i];
    return float(same) / float(a.size());
}
// Some solid tiles of a game terrain (its left and right solid groups, the first subtiles).
std::vector<uint16_t> solidTiles(const Tileset& ts, uint16_t terrain) {
    std::vector<uint16_t> out;
    for (int half = 0; half < 2; ++half)
        if (const int g = solidTileGroup(ts, terrain, half); g >= 0)
            for (uint16_t k = 0; k < 4; ++k)
                if (ts.megatileFor(uint16_t(g << 4 | k)) > 0) out.push_back(uint16_t(g << 4 | k));
    return out;
}
// The tiles inside a mod terrain: its fill pieces' (a few), else its stand-in's solid tiles.
// The gray picture of a pair (64 x 32), zero mean and unit variance; `flip`: mirrored left to right (a role's mirror
// image: edge NE for edge NW).
std::vector<float> grayOf(const Tileset& ts, uint16_t l, uint16_t r, bool flip) {
    const Sprite s = mapTilesPicture(ts, {l, r}, 2, 0, 0, 2, 1);
    std::vector<float> out;
    if (s.width != 64 || s.height != 32) return out;
    out.resize(size_t(64 * 32));
    double sum = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x) {
            const size_t i = size_t(y * 64 + (flip ? 63 - x : x)) * 4;
            out[size_t(y * 64 + x)] = 0.299f * s.rgba[i] + 0.587f * s.rgba[i + 1] + 0.114f * s.rgba[i + 2];
            sum += out[size_t(y * 64 + x)];
        }
    const float mean = float(sum / double(out.size()));
    double var = 0;
    for (float& v : out) v -= mean, var += double(v) * v;
    const float sd = float(std::sqrt(var / double(out.size())));
    if (sd > 1e-6f)
        for (float& v : out) v /= sd;
    return out;
}
// A role's mirror image's role: E and W swapped in its direction.
std::string mirrorRole(const std::string& role) {
    std::string out;
    size_t i = 0;
    while (i < role.size()) {
        size_t j = i;
        while (j < role.size() && role[j] != ' ') ++j;
        std::string w = role.substr(i, j - i);
        if (w == "NE") w = "NW";
        else if (w == "NW") w = "NE";
        else if (w == "SE") w = "SW";
        else if (w == "SW") w = "SE";
        else if (w == "E") w = "W";
        else if (w == "W") w = "E";
        out += w;
        if (j < role.size()) out += ' ';
        i = j + 1;
    }
    return out;
}

std::vector<uint16_t> insideTiles(const Tileset& ts, const Tileset::ModTerrain& m) {
    std::vector<uint16_t> out;
    for (size_t k = m.setPieces; k < m.pieces.size() - std::min(m.gapPieces, m.pieces.size()) && out.size() < 8; ++k)
        if (!tr::nullHalf(m.pieces[k][1])) out.push_back(m.pieces[k][1]), out.push_back(m.pieces[k][2]);
    return out.empty() ? solidTiles(ts, m.standIn) : out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) return std::fprintf(stderr, "usage: cmbw_draft_fix <install> <out .sdterrain> <report .txt> [tileset] [passes]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    cmbw::skipRepairedRules(true);  // (from the rows themselves, not from the last repair)
    const std::string incPath = argc > 4 && std::string(argv[4]) != "-" ? argv[4] : "";
    const int only = argc > 5 ? std::atoi(argv[5]) : -1, passes = argc > 6 ? std::clamp(std::atoi(argv[6]), 1, 7) : 7;
    FILE* rep = std::fopen(argv[3], "w");
    if (!rep) return std::fprintf(stderr, "cannot write %s\n", argv[3]), 1;
    auto out = [&](const char* fmt, auto... args) { std::fprintf(rep, fmt, args...), std::printf(fmt, args...); };
    tr::Rules rules;
    int totalBorrowed = 0, totalReplaced = 0, totalSuspects = 0, totalBroken = 0, totalApplied = 0, totalDropped = 0, totalMirrored = 0, totalOrder = 0, totalSeams = 0;
    std::unique_ptr<Editor> painter;  // (pass 7: the border painted, one map per tileset)
    int painterEra = -1;
    for (int era = 0; era < 8; ++era) {
        if (only >= 0 && era != only) continue;
        Tileset ts;
        if (!ts.load(data, uint16_t(era), err)) {
            out("== tileset %d: %s\n", era, err.c_str());
            continue;
        }
        const IsomTerrainTable* table = IsomTerrainTable::forEra(ts.era);
        tr::TilesetRules tsr = tr::rulesOf(ts);
        out("== tileset %d: %zu terrains\n", era, tsr.terrains.size());
        std::map<uint16_t, std::vector<float>> edgeCache;
        auto edgesOf = [&](uint16_t l, uint16_t r) -> const std::vector<float>& {
            auto& e = edgeCache[uint16_t(l)];  // (keyed by the left tile: pairs are k, k+1 of one group)
            if (e.empty()) e = edges(ts, l, r);
            return e;
        };
        for (tr::Terrain& x : tsr.terrains) {
            Tileset::ModTerrain& m = x.t;
            if (m.variantOf || !m.draft) continue;
            const auto [a, b] = tr::templateOf(ts, m);
            const std::vector<tr::TemplateRect> rects = tr::templateRects(ts, a, b);
            std::map<uint16_t, const tr::TemplateRect*> byGroup;
            for (const auto& r : rects) byGroup[r.group] = &r;
            const auto before = tr::completeness(ts, m);
            out("-- %s  (%s, template %s (%s), %d of %d rects)\n", m.name.c_str(), tr::familyName(tr::familyOf(rects, ts)), table->terrainName(b), table->terrainName(a),
                before.first, before.second);
            // Pass 1: structure.
            tr::normalize(ts, m);
            int borrowed = 0, broken = 0;
            auto isTemplatePiece = [&](size_t k) { return k < m.setPieces || k + m.gapPieces >= m.pieces.size(); };
            for (size_t k = 0; k < m.pieces.size(); ++k) {
                auto& p = m.pieces[k];
                if (!isTemplatePiece(k)) continue;
                for (int h = 1; h <= 2; ++h)
                    if (!tr::nullHalf(p[size_t(h)]) && ts.megatileFor(p[size_t(h)]) <= 0) {
                        out("   pass 1  %-24s rect %u: tile %u.%u does not exist -> NULL\n", byGroup.count(p[0]) ? byGroup[p[0]]->role.c_str() : "other", unsigned(p[0]),
                            unsigned(p[size_t(h)] >> 4), unsigned(p[size_t(h)] & 15));
                        p[size_t(h)] = uint16_t(h - 1), ++broken;
                    }
                if (tr::stateOf(p) == tr::PieceState::Own && ((p[1] >> 4) != (p[2] >> 4) || (p[1] & 15) + 1 != (p[2] & 15))) {
                    out("   pass 1  %-24s rect %u: pair %u.%u %u.%u is not two subtiles side by side (kept)\n", byGroup.count(p[0]) ? byGroup[p[0]]->role.c_str() : "other",
                        unsigned(p[0]), unsigned(p[1] >> 4), unsigned(p[1] & 15), unsigned(p[2] >> 4), unsigned(p[2] & 15));
                    ++broken;
                }
                if (tr::nullHalf(p[1]) || tr::nullHalf(p[2])) ++borrowed;  // (counted, not filled: a template tile would hide a height decision)
            }
            tr::normalize(ts, m);
            if (borrowed) out("   pass 1  %d rects borrowed the template's tiles\n", borrowed);
            totalBorrowed += borrowed, totalBroken += broken;
            if (passes < 2) continue;
            // Pass 2: the terrain's own groups; foreign tiles.
            std::set<uint16_t> mine;
            for (const auto& [g, t, other] : ts.groupTerrains)
                if (t == m.id || other == m.id) mine.insert(g);
            std::map<uint16_t, int> groupUse;
            for (size_t k = 0; k < m.pieces.size(); ++k)
                if (isTemplatePiece(k) && tr::stateOf(m.pieces[k]) == tr::PieceState::Own) ++groupUse[uint16_t(m.pieces[k][1] >> 4)];
            for (const auto& [g, n] : groupUse)
                if (n >= 2) mine.insert(g);
            int suspects = 0;
            for (size_t k = 0; k < m.pieces.size(); ++k) {
                const auto& p = m.pieces[k];
                if (!isTemplatePiece(k) || tr::stateOf(p) != tr::PieceState::Own) continue;
                if (!mine.count(uint16_t(p[1] >> 4)))
                    out("   pass 2  %-24s rect %u: tiles %u.%u %u.%u are from a group the terrain does not use elsewhere (suspect)\n",
                        byGroup.count(p[0]) ? byGroup[p[0]]->role.c_str() : "other", unsigned(p[0]), unsigned(p[1] >> 4), unsigned(p[1] & 15), unsigned(p[2] >> 4),
                        unsigned(p[2] & 15)),
                        ++suspects;
            }
            totalSuspects += suspects;
            if (passes < 3) continue;
            // Pass 3: where each own pair puts the terrain and the ground, against the template pair. Each pixel is the
            // terrain or the ground by its colour (the set's own inside tiles and its ground's; the template's: the game's
            // solid tiles of its two terrains), so the materials drop out and the masks compare the layout: an edge N
            // tile has the terrain below, a corner has it in one quadrant. The shape (edge structure) is a second opinion.
            std::vector<std::pair<uint16_t, uint16_t>> candidates;
            for (uint16_t g : mine)
                for (uint16_t s = 0; s + 1 < 16; ++s)
                    if (ts.megatileFor(uint16_t(g << 4 | s)) > 0 && ts.megatileFor(uint16_t(g << 4 | (s + 1))) > 0) candidates.push_back({uint16_t(g << 4 | s), uint16_t(g << 4 | (s + 1))});
            const Material modIn = materialOf(ts, insideTiles(ts, m));
            const Tileset::ModTerrain* modGround = m.ground >= Tileset::kModTerrainIds ? ts.modTerrain(m.ground) : nullptr;
            const Material modOut = materialOf(ts, modGround ? insideTiles(ts, *modGround) : solidTiles(ts, m.ground));
            const Material gameIn = materialOf(ts, solidTiles(ts, b)), gameOut = materialOf(ts, solidTiles(ts, a));
            std::map<uint16_t, std::vector<uint8_t>> maskCache;  // (by the left tile)
            auto modMask = [&](uint16_t l, uint16_t r) -> const std::vector<uint8_t>& {
                auto& v = maskCache[l];
                if (v.empty()) v = maskOf(ts, l, r, modIn, modOut);
                return v;
            };
            struct Fit {
                size_t k;
                float layout, shape;
            };
            std::vector<Fit> fits;
            double sumLayout = 0, sumShape = 0;
            const bool canMask = modIn.ok && modOut.ok && gameIn.ok && gameOut.ok;
            for (size_t k = 0; k < m.pieces.size(); ++k) {
                const auto& p = m.pieces[k];
                if (!isTemplatePiece(k) || tr::stateOf(p) != tr::PieceState::Own) continue;
                const uint16_t gl = tr::gameTile(ts, p[0]), gr = tr::gameTile(ts, uint32_t(p[0]) + 1);
                if (!gl || !gr) continue;
                const float shape = ncc(edgesOf(gl, gr), edgesOf(p[1], p[2]));
                const float layout = canMask ? agree(maskOf(ts, gl, gr, gameIn, gameOut), modMask(p[1], p[2])) : 0.0f;
                fits.push_back({k, layout, shape}), sumLayout += layout, sumShape += shape;
            }
            const int scored = int(fits.size());
            const double setLayout = scored ? sumLayout / scored : 0.0, setShape = scored ? sumShape / scored : 0.0;
            int replaced = 0, weak = 0, applied = 0;
            for (const Fit& f : fits) {
                auto& p = m.pieces[f.k];
                if (!canMask || f.layout >= 0.75f) continue;
                const std::vector<uint8_t> want = maskOf(ts, tr::gameTile(ts, p[0]), tr::gameTile(ts, uint32_t(p[0]) + 1), gameIn, gameOut);
                std::pair<uint16_t, uint16_t> best{0, 0};
                float bestScore = f.layout;
                for (const auto& c : candidates)
                    if (const float sc = agree(want, modMask(c.first, c.second)); sc > bestScore) bestScore = sc, best = c;
                const char* role = byGroup.count(p[0]) ? byGroup[p[0]]->role.c_str() : "other";
                // (never applied: a half-and-half tile matches many masks, so one candidate wins for several roles at once;
                // the two signals are for a person's review)
                if (false && best.first && setLayout >= 0.7 && (best.first >> 4) != (p[1] >> 4) && f.layout < 0.6f && bestScore >= 0.85f && bestScore >= f.layout + 0.25f) {
                    out("   pass 3  %-24s rect %u: %u.%u layout %.2f, %u.%u layout %.2f -> replaced\n", role, unsigned(p[0]), unsigned(p[1] >> 4), unsigned(p[1] & 15),
                        double(f.layout), unsigned(best.first >> 4), unsigned(best.first & 15), double(bestScore));
                    p[1] = best.first, p[2] = best.second, ++applied;
                } else if (best.first && bestScore >= 0.8f && bestScore >= f.layout + 0.2f) {
                    out("   pass 3  %-24s rect %u: %u.%u layout %.2f (shape %.2f), %u.%u would be %.2f: check by hand\n", role, unsigned(p[0]), unsigned(p[1] >> 4),
                        unsigned(p[1] & 15), double(f.layout), double(f.shape), unsigned(best.first >> 4), unsigned(best.first & 15), double(bestScore));
                    ++replaced;
                } else {
                    out("   pass 3  %-24s rect %u: %u.%u layout %.2f (shape %.2f, best other %.2f): check by hand\n", role, unsigned(p[0]), unsigned(p[1] >> 4), unsigned(p[1] & 15),
                        double(f.layout), double(f.shape), double(bestScore));
                    ++weak;
                }
            }
            totalReplaced += replaced, totalApplied += applied;
            if (applied) out("   pass 3  %d pairs replaced (the set's layout fit %.2f)\n", applied, setLayout);
            auto roleOf = [&](uint16_t g) { return byGroup.count(g) ? byGroup[g]->role : std::string("other"); };
            // Pass 4: a group's variants (several pieces for one rect) should share one layout; a variant whose mask
            // strays from the others' consensus (an off-by-one subtile, a tile of another role) is dropped: the engine
            // picks among the rest.
            int dropped = 0;
            if (passes >= 4 && canMask) {
                std::map<uint16_t, std::vector<size_t>> byRect;
                for (size_t k = 0; k < m.pieces.size(); ++k)
                    if (isTemplatePiece(k) && tr::stateOf(m.pieces[k]) == tr::PieceState::Own) byRect[m.pieces[k][0]].push_back(k);
                std::set<size_t> drop;
                for (const auto& [g, ks] : byRect) {
                    if (ks.size() < 3) continue;
                    std::vector<int> votes(size_t(64 * 32), 0);
                    for (size_t k : ks)
                        for (size_t i = 0; i < votes.size(); ++i) votes[i] += modMask(m.pieces[k][1], m.pieces[k][2])[i] ? 1 : -1;
                    std::vector<uint8_t> consensus(votes.size());
                    for (size_t i = 0; i < votes.size(); ++i) consensus[i] = votes[i] > 0 ? 1 : 0;
                    std::vector<std::pair<float, size_t>> ag;
                    for (size_t k : ks) ag.push_back({agree(consensus, modMask(m.pieces[k][1], m.pieces[k][2])), k});
                    std::vector<float> sorted;
                    for (const auto& a : ag) sorted.push_back(a.first);
                    std::sort(sorted.begin(), sorted.end());
                    const float median = sorted[sorted.size() / 2];
                    if (median < 0.85f) continue;  // (the variants do not agree among themselves: no call)
                    for (const auto& [a, k] : ag)
                        if (a < 0.7f && ks.size() - drop.size() > 1) {
                            out("   pass 4  %-24s rect %u: variant %u.%u strays from the group's layout (%.2f, the others %.2f) -> dropped\n", roleOf(g).c_str(), unsigned(g),
                                unsigned(m.pieces[k][1] >> 4), unsigned(m.pieces[k][1] & 15), double(a), double(median));
                            drop.insert(k), ++dropped;
                        }
                }
                if (!drop.empty()) {
                    std::vector<std::array<uint16_t, 3>> kept;
                    for (size_t k = 0; k < m.pieces.size(); ++k)
                        if (!drop.count(k)) kept.push_back(m.pieces[k]);
                    m.setPieces -= drop.size();
                    m.pieces = std::move(kept);
                    tr::normalize(ts, m);
                }
            }
            totalDropped += dropped;
            // Pass 5: mirror images. A role and its mirror (edge NE and edge NW) have mirrored tiles in the game and in a
            // set that follows the game; where most mirrored pairs of a set match (their pictures flipped, NCC) and one
            // pair does not, the half that fits its template's layout worse is replaced by the terrain's pair that best
            // mirrors the other half, when that mirrors clearly.
            int mirrored = 0, mirrorPairs = 0, mirrorOk = 0;
            if (passes >= 5 && canMask) {
                std::map<std::string, size_t> firstOf;  // (a role -> the first own piece of its rect)
                for (size_t k = 0; k < m.pieces.size(); ++k)
                    if (isTemplatePiece(k) && tr::stateOf(m.pieces[k]) == tr::PieceState::Own && byGroup.count(m.pieces[k][0])) firstOf.emplace(roleOf(m.pieces[k][0]), k);
                std::map<uint16_t, std::vector<float>> grayCache, flipCache;
                auto gray = [&](uint16_t l, uint16_t r, bool flip) -> const std::vector<float>& {
                    auto& v = (flip ? flipCache : grayCache)[l];
                    if (v.empty()) v = grayOf(ts, l, r, flip);
                    return v;
                };
                struct Pair {
                    std::string a, b;
                    size_t ka, kb;
                    float match;
                };
                std::vector<Pair> pairs;
                for (const auto& [role, k] : firstOf) {
                    const std::string mr = mirrorRole(role);
                    if (mr <= role || !firstOf.count(mr)) continue;  // (each pair once; a self-mirror (edge N) has no partner)
                    const size_t kb = firstOf[mr];
                    pairs.push_back({role, mr, k, kb, ncc(gray(m.pieces[k][1], m.pieces[k][2], false), gray(m.pieces[kb][1], m.pieces[kb][2], true))});
                }
                mirrorPairs = int(pairs.size());
                for (const Pair& pr : pairs) mirrorOk += pr.match >= 0.8f ? 1 : 0;
                if (mirrorPairs >= 4 && mirrorOk * 10 >= mirrorPairs * 6)
                    for (const Pair& pr : pairs) {
                        if (pr.match >= 0.8f) continue;
                        // (the worse half by its template's layout)
                        auto layoutOf = [&](size_t k) {
                            const auto& p = m.pieces[k];
                            return agree(maskOf(ts, tr::gameTile(ts, p[0]), tr::gameTile(ts, uint32_t(p[0]) + 1), gameIn, gameOut), modMask(p[1], p[2]));
                        };
                        const float la = layoutOf(pr.ka), lb = layoutOf(pr.kb);
                        const size_t bad = la < lb ? pr.ka : pr.kb, good = la < lb ? pr.kb : pr.ka;
                        const std::vector<float>& want = gray(m.pieces[good][1], m.pieces[good][2], true);
                        std::pair<uint16_t, uint16_t> best{0, 0};
                        float bestMatch = pr.match;
                        for (const auto& c : candidates)
                            if (const float sc = ncc(gray(c.first, c.second, false), want); sc > bestMatch) bestMatch = sc, best = c;
                        auto& p = m.pieces[bad];
                        if (best.first && bestMatch >= 0.85f && bestMatch >= pr.match + 0.3f) {
                            out("   pass 5  %-24s rect %u: %u.%u mirrors %s at %.2f; %u.%u mirrors it at %.2f -> replaced\n", roleOf(p[0]).c_str(), unsigned(p[0]), unsigned(p[1] >> 4),
                                unsigned(p[1] & 15), roleOf(m.pieces[good][0]).c_str(), double(pr.match), unsigned(best.first >> 4), unsigned(best.first & 15), double(bestMatch));
                            p[1] = best.first, p[2] = best.second, ++mirrored;
                        } else {
                            out("   pass 5  %-24s rect %u: %u.%u mirrors %s at %.2f only (best other %.2f): check by hand\n", roleOf(p[0]).c_str(), unsigned(p[0]), unsigned(p[1] >> 4),
                                unsigned(p[1] & 15), roleOf(m.pieces[good][0]).c_str(), double(pr.match), double(bestMatch));
                        }
                    }
            }
            totalMirrored += mirrored;
            // Pass 6: the order of the rows. A generated set lays its groups out in the game's rect order; a piece whose
            // group breaks a run of consecutive groups is reported (the run tells which group was meant).
            int order = 0;
            if (passes >= 6) {
                std::vector<std::pair<uint16_t, uint16_t>> seq;  // (game group, mod group) of the first own piece per rect
                std::set<uint16_t> seen;
                for (size_t k = 0; k < m.pieces.size(); ++k)
                    if (isTemplatePiece(k) && tr::stateOf(m.pieces[k]) == tr::PieceState::Own && byGroup.count(m.pieces[k][0]) && seen.insert(m.pieces[k][0]).second)
                        seq.push_back({m.pieces[k][0], uint16_t(m.pieces[k][1] >> 4)});
                std::sort(seq.begin(), seq.end());
                const size_t n = seq.size();
                if (n >= 8) {  // (the longest non-decreasing run of mod groups over the game's order)
                    std::vector<size_t> len(n, 1), prev(n, n);
                    size_t bestEnd = 0;
                    for (size_t i = 0; i < n; ++i) {
                        for (size_t j = 0; j < i; ++j)
                            if (seq[j].second <= seq[i].second && len[j] + 1 > len[i]) len[i] = len[j] + 1, prev[i] = j;
                        if (len[i] > len[bestEnd]) bestEnd = i;
                    }
                    std::set<size_t> onRun;
                    for (size_t i = bestEnd; i < n; i = prev[i]) {
                        onRun.insert(i);
                        if (prev[i] >= n) break;
                    }
                    if (onRun.size() * 10 >= n * 8)
                        for (size_t i = 0; i < n; ++i)
                            if (!onRun.count(i)) {
                                out("   pass 6  %-24s rect %u: group %u breaks the set's row order (its neighbours %u, %u): check by hand\n", roleOf(seq[i].first).c_str(),
                                    unsigned(seq[i].first), unsigned(seq[i].second), unsigned(i ? seq[i - 1].second : 0), unsigned(i + 1 < n ? seq[i + 1].second : 0));
                                ++order;
                            }
                }
            }
            totalOrder += order;
            // Pass 7: seams in the painted border. The terrain is painted on its ground in the editor's shape (as the
            // window's example); where two tiles meet, the gray values along the shared edge should carry on. A piece
            // whose seams stand out against the set's (the mean plus two deviations, and 40 gray levels or more) is
            // reported for a person: a tile of the wrong role shows a hard edge against its neighbours.
            int seams = 0;
            if (passes >= 7) {
                if (!painter || painterEra != era) painter = std::make_unique<Editor>(data), painterEra = era;
                uint16_t root = m.ground;
                for (int n = 0; n < 16 && root >= Tileset::kModTerrainIds; ++n)
                    if (const Tileset::ModTerrain* g = ts.modTerrain(root)) root = g->ground;
                    else break;
                if (!root) root = 1;
                std::string perr;
                constexpr int W = 40, H = 28;
                if ((painter->isOpen() && painter->era() == era) || painter->newMap(W, H, uint16_t(era), root, perr)) {
                    std::vector<Tileset::ModTerrain> all;
                    for (const tr::Terrain& y : tsr.terrains) all.push_back(y.t);
                    painter->useModTerrains(all);
                    const int cx = W * 16, cy = H * 16;
                    painter->paintBrush(cx, cy, root, Brush{Brush::Diamond, 64.0f});
                    painter->paintBrush(cx, cy, m.ground ? m.ground : 1, Brush{Brush::Diamond, 24.0f});
                    static const int kShape[9][2] = {{0, 0}, {0, -2}, {-1, -3}, {-1, 3}, {3, -3}, {-1, 1}, {0, 2}, {3, 1}, {-3, -3}};
                    for (const auto& ij : kShape) painter->paintBrush(cx + ij[0] * 64, cy + ij[1] * 32, m.id, Brush{Brush::Diamond, 1.0f});
                    const TerrainLayer tl = readTerrain(painter->chk(), "TILE");
                    std::map<uint16_t, size_t> pieceOfTile;  // (a tile value -> the piece it is the left or right half of)
                    for (size_t k = 0; k < m.pieces.size(); ++k)
                        if (isTemplatePiece(k) && tr::stateOf(m.pieces[k]) == tr::PieceState::Own) pieceOfTile.emplace(m.pieces[k][1], k), pieceOfTile.emplace(m.pieces[k][2], k);
                    std::map<uint16_t, std::vector<float>> grays;  // (a tile's gray pixels, 32 x 32)
                    auto grayTile = [&](uint16_t v) -> const std::vector<float>& {
                        auto& g = grays[v];
                        if (g.empty()) {
                            const Sprite sp = mapTilesPicture(ts, {v}, 1, 0, 0, 1, 1);
                            g.resize(size_t(32 * 32), 0.0f);
                            if (sp.width == 32 && sp.height == 32)
                                for (size_t i = 0; i < g.size(); ++i) g[i] = 0.299f * sp.rgba[i * 4] + 0.587f * sp.rgba[i * 4 + 1] + 0.114f * sp.rgba[i * 4 + 2];
                        }
                        return g;
                    };
                    std::map<size_t, std::pair<double, int>> seamOf;  // (a piece -> the sum of its seams and their count)
                    auto seam = [&](uint16_t a, uint16_t b, bool vertical) {  // (the mean gray step along the shared edge)
                        const auto &ga = grayTile(a), &gb = grayTile(b);
                        double d = 0;
                        for (int i = 0; i < 32; ++i) d += std::fabs((vertical ? ga[size_t(31 * 32 + i)] : ga[size_t(i * 32 + 31)]) - (vertical ? gb[size_t(i)] : gb[size_t(i * 32)]));
                        return d / 32;
                    };
                    for (int y = 0; y < int(tl.height); ++y)
                        for (int x = 0; x < int(tl.width); ++x) {
                            const uint16_t v = tl.tiles[size_t(y) * tl.width + size_t(x)];
                            const auto it = pieceOfTile.find(v);
                            if (it == pieceOfTile.end()) continue;
                            if (x + 1 < int(tl.width)) {
                                const uint16_t r = tl.tiles[size_t(y) * tl.width + size_t(x) + 1];
                                if (r && r != v) {
                                    const double d = seam(v, r, false);
                                    auto& s1 = seamOf[it->second];
                                    s1.first += d, ++s1.second;
                                }
                            }
                            if (y + 1 < int(tl.height)) {
                                const uint16_t b = tl.tiles[size_t(y + 1) * tl.width + size_t(x)];
                                if (b && b != v) {
                                    const double d = seam(v, b, true);
                                    auto& s1 = seamOf[it->second];
                                    s1.first += d, ++s1.second;
                                }
                            }
                        }
                    std::vector<std::pair<size_t, double>> means;
                    double sum = 0, sq = 0;
                    for (const auto& [k, sc] : seamOf)
                        if (sc.second) {
                            const double mv = sc.first / sc.second;
                            means.push_back({k, mv}), sum += mv, sq += mv * mv;
                        }
                    if (means.size() >= 6) {
                        const double mean = sum / double(means.size()), sd = std::sqrt(std::max(0.0, sq / double(means.size()) - mean * mean));
                        for (const auto& [k, mv] : means)
                            if (mv >= mean + 2 * sd && mv >= 40) {
                                const auto& p = m.pieces[k];
                                out("   pass 7  %-24s rect %u: %u.%u %u.%u has hard seams against its neighbours (%.0f gray levels; the set %.0f): check by hand\n",
                                    roleOf(p[0]).c_str(), unsigned(p[0]), unsigned(p[1] >> 4), unsigned(p[1] & 15), unsigned(p[2] >> 4), unsigned(p[2] & 15), mv, mean);
                                ++seams;
                            }
                    }
                }
            }
            totalSeams += seams;
            const auto after = tr::completeness(ts, m);
            out("   result  %d of %d rects; layout fit %.2f, shape fit %.2f over %d pairs; %d replaced, %d with a better pair, %d weak; %d variants dropped; mirrors %d of %d, %d "
                "replaced; %d off the row order; %d with hard seams\n",
                after.first, after.second, setLayout, setShape, scored, applied, replaced, weak, dropped, mirrorOk, mirrorPairs, mirrored, order, seams);
        }
        rules.tilesets.push_back(std::move(tsr));
    }
    out("== totals: %d rects left NULL, %d pairs replaced by layout, %d pairs with a better-fitting pair, %d suspects, %d broken pairs, %d variants dropped, %d pairs replaced by their "
        "mirror, %d off the row order, %d with hard seams\n",
        totalBorrowed, totalApplied, totalReplaced, totalSuspects, totalBroken, totalDropped, totalMirrored, totalOrder, totalSeams);
    std::fclose(rep);
    if (!tr::save(argv[2], rules, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::printf("wrote %s\n", argv[2]);
    if (!incPath.empty()) {  // (the same bytes as a C array: src/mods/cmbw_repaired_rules.inc)
        const std::vector<uint8_t> bytes = tr::write(rules);
        FILE* inc = std::fopen(incPath.c_str(), "w");
        if (!inc) return std::fprintf(stderr, "cannot write %s\n", incPath.c_str()), 1;
        std::fprintf(inc, "// Made by tools/plugins/cmbw/probes/cmbw_draft_fix.cpp: the built-in terrain rows repaired (every template rect present,\n"
                          "// the template's tiles where a row had none), as a terrain rules file (version 2) the program applies over the rows.\n"
                          "// Do not edit by hand. (Its probe builds with -Dcmbw_probes=ON.)\n");
        for (size_t i = 0; i < bytes.size(); ++i) std::fprintf(inc, "%s%u,", i % 32 ? "" : "\n", unsigned(bytes[i]));
        std::fprintf(inc, "\n");
        std::fclose(inc);
        std::printf("wrote %s (%zu bytes)\n", incPath.c_str(), bytes.size());
    }
    return 0;
}
