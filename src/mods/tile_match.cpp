#include "stardraft/mods/tile_match.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <tuple>
#include <unordered_map>

namespace stardraft::tilematch {

namespace {

constexpr int kN = 32;

// A tile's pictures: its gray values (0..255), the same with mean 0 and deviation 1, and that blurred (its big
// shapes: the line between two terrains, not the texture).
struct Tile {
    std::array<float, kN * kN> gray{}, norm{}, shape{}, mask{}, edge{};
    std::array<float, 64 * 3> blocks{};  // (its colours: the mean of each 4x4 block, r g b)
    std::array<std::array<float, 3>, kN * kN> rgb{};
    bool ok = false;
};

void normalize(std::array<float, kN * kN>& a) {
    double s = 0, q = 0;
    for (float f : a) s += f, q += double(f) * f;
    const double mean = s / a.size(), sd = std::sqrt(std::max(0.0, q / a.size() - mean * mean));
    for (float& f : a) f = sd > 1e-3 ? float((f - mean) / sd) : 0.f;
}

Tile tileOf(const Tileset& t, uint16_t v) {
    Tile o;
    const int mt = t.megatileFor(v);
    if (mt <= 0 || size_t(mt) >= t.megatiles.size()) return o;
    o.ok = true;
    for (int m = 0; m < 16; ++m) {
        const uint32_t e = t.megatiles[size_t(mt)][size_t(m)];
        const size_t mi = e >> 1;
        const bool flip = e & 1;
        if (mi >= t.minitiles.size()) continue;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const Rgb c = t.palette[t.minitiles[mi][size_t(y * 8 + (flip ? 7 - x : x))]];
                const int px = (m % 4) * 8 + x, py = (m / 4) * 8 + y;
                o.gray[size_t(py * kN + px)] = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
                o.rgb[size_t(py * kN + px)] = {float(c.r), float(c.g), float(c.b)};
                const size_t b = size_t((py / 4) * 8 + px / 4) * 3;
                o.blocks[b] += c.r / 16.f, o.blocks[b + 1] += c.g / 16.f, o.blocks[b + 2] += c.b / 16.f;
            }
    }
    o.norm = o.gray;
    normalize(o.norm);
    for (int y = 0; y < kN; ++y)
        for (int x = 0; x < kN; ++x) {
            float s = 0;
            int n = 0;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    if (unsigned(x + dx) < unsigned(kN) && unsigned(y + dy) < unsigned(kN)) s += o.norm[size_t((y + dy) * kN + x + dx)], ++n;
            o.shape[size_t(y * kN + x)] = s / float(n);
        }
    normalize(o.shape);
    for (size_t i = 0; i < o.shape.size(); ++i) o.mask[i] = o.shape[i] >= 0 ? 1.f : -1.f;
    normalize(o.mask);
    for (int y = 0; y < kN; ++y)
        for (int x = 0; x < kN; ++x) {
            const auto at = [&](int xx, int yy) { return o.shape[size_t(std::clamp(yy, 0, kN - 1) * kN + std::clamp(xx, 0, kN - 1))]; };
            const float gx = at(x + 1, y) - at(x - 1, y), gy = at(x, y + 1) - at(x, y - 1);
            o.edge[size_t(y * kN + x)] = std::sqrt(gx * gx + gy * gy);
        }
    normalize(o.edge);
    return o;
}

float corr(const std::array<float, kN * kN>& a, const std::array<float, kN * kN>& b) {
    double s = 0;
    for (size_t i = 0; i < a.size(); ++i) s += double(a[i]) * b[i];
    return float(s / a.size());
}

// Two colours that split a set of tiles' pixels best (2-means, from the darkest and the lightest): a border's terrain
// and its ground, whatever their brightness (grass on dirt: the same brightness, other colours).
std::array<std::array<float, 3>, 2> twoColors(const std::vector<const Tile*>& tiles) {
    std::array<std::array<float, 3>, 2> c{};
    float lo = 1e9f, hi = -1e9f;
    for (const Tile* t : tiles)
        for (const auto& p : t->rgb) {
            const float l = p[0] + p[1] + p[2];
            if (l < lo) lo = l, c[0] = p;
            if (l > hi) hi = l, c[1] = p;
        }
    for (int it = 0; it < 8; ++it) {
        std::array<std::array<double, 4>, 2> sum{};
        for (const Tile* t : tiles)
            for (size_t i = 0; i < t->rgb.size(); i += 3) {  // (every third pixel: enough)
                const auto& p = t->rgb[i];
                float d[2];
                for (int k = 0; k < 2; ++k) d[k] = (p[0] - c[size_t(k)][0]) * (p[0] - c[size_t(k)][0]) + (p[1] - c[size_t(k)][1]) * (p[1] - c[size_t(k)][1]) + (p[2] - c[size_t(k)][2]) * (p[2] - c[size_t(k)][2]);
                auto& sm = sum[d[0] <= d[1] ? 0 : 1];
                sm[0] += p[0], sm[1] += p[1], sm[2] += p[2], sm[3] += 1;
            }
        for (int k = 0; k < 2; ++k)
            if (sum[size_t(k)][3] > 0) c[size_t(k)] = {float(sum[size_t(k)][0] / sum[size_t(k)][3]), float(sum[size_t(k)][1] / sum[size_t(k)][3]), float(sum[size_t(k)][2] / sum[size_t(k)][3])};
    }
    return c;
}
// A tile as its two sides: each pixel +1 nearer colour 1, -1 nearer colour 0, blurred (the line between them, not
// the texture), mean 0 and deviation 1.
std::array<float, kN * kN> sidesOf(const Tile& t, const std::array<std::array<float, 3>, 2>& c) {
    std::array<float, kN * kN> m{}, o{};
    for (size_t i = 0; i < m.size(); ++i) {
        const auto& p = t.rgb[i];
        float d[2];
        for (int k = 0; k < 2; ++k) d[k] = (p[0] - c[size_t(k)][0]) * (p[0] - c[size_t(k)][0]) + (p[1] - c[size_t(k)][1]) * (p[1] - c[size_t(k)][1]) + (p[2] - c[size_t(k)][2]) * (p[2] - c[size_t(k)][2]);
        m[i] = d[1] < d[0] ? 1.f : -1.f;
    }
    for (int y = 0; y < kN; ++y)
        for (int x = 0; x < kN; ++x) {
            float sm = 0;
            int n = 0;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    if (unsigned(x + dx) < unsigned(kN) && unsigned(y + dy) < unsigned(kN)) sm += m[size_t((y + dy) * kN + x + dx)], ++n;
            o[size_t(y * kN + x)] = sm / float(n);
        }
    normalize(o);
    return o;
}

// How far apart two tiles' touching edges are (mean gray difference): b right of a (dir 0) or under it (dir 1).
float seam(const Tile& a, const Tile& b, int dir) {
    double s = 0;
    for (int i = 0; i < kN; ++i)
        s += dir == 0 ? std::abs(a.gray[size_t(i * kN + kN - 1)] - b.gray[size_t(i * kN)]) : std::abs(a.gray[size_t((kN - 1) * kN + i)] - b.gray[size_t(i)]);
    return float(s / kN);
}

}  // namespace

float pixelMatch(const Tileset& a, uint16_t va, const Tileset& b, uint16_t vb) {
    const Tile x = tileOf(a, va), y = tileOf(b, vb);
    return x.ok && y.ok ? std::clamp(corr(x.norm, y.norm), 0.f, 1.f) : 0.f;
}

std::vector<Suggestion> suggest(const Input& in) {
    std::vector<Suggestion> out;
    if (!in.tmplTs || !in.modTs || in.known.empty()) return out;
    const Tileset& T = *in.tmplTs;
    const Tileset& M = *in.modTs;
    // (the candidates: the mod's tiles in the matched tile groups and a window around them)
    std::set<uint16_t> usedMods;
    std::set<int> near;  // (the candidates' groups: a window around each matched one, not all between them)
    for (const auto& [t, m] : in.known) {
        usedMods.insert(m);
        for (int g = std::max(0, int(m >> 4) - in.window); g <= std::min(int(M.groups.size()) - 1, int(m >> 4) + in.window); ++g) near.insert(g);
    }
    std::vector<uint16_t> cands;
    for (int g : near)
        for (int s = 0; s < 16; ++s)
            if (const uint16_t v = uint16_t(g * 16 + s); M.megatileFor(v) > 0 && !usedMods.count(v) && !in.taken.count(v)) cands.push_back(v);
    std::vector<uint16_t> open;
    for (uint16_t s : in.slots)
        if (!in.known.count(s) && std::find(open.begin(), open.end(), s) == open.end()) open.push_back(s);
    if (cands.empty() || open.empty()) return out;

    std::unordered_map<uint16_t, Tile> tt, mt;
    auto tmplTile = [&](uint16_t v) -> const Tile& {
        auto it = tt.find(v);
        return it != tt.end() ? it->second : tt.emplace(v, tileOf(T, v)).first->second;
    };
    auto modTile = [&](uint16_t v) -> const Tile& {
        auto it = mt.find(v);
        return it != mt.end() ? it->second : mt.emplace(v, tileOf(M, v)).first->second;
    };
    // (each slot against each candidate, once: likeness and colours (below), by their places in open and cands)
    std::unordered_map<uint16_t, size_t> slotIx, candIx;
    for (size_t i = 0; i < open.size(); ++i) slotIx[open[i]] = i;
    for (size_t i = 0; i < cands.size(); ++i) candIx[cands[i]] = i;
    std::vector<float> base(open.size() * cands.size(), 0.f);
    auto like = [&](const Tile& a, const Tile& b) {
        return in.wLike * (0.3f * corr(a.norm, b.norm) + corr(a.shape, b.shape)) + (in.wEdge != 0 ? in.wEdge * corr(a.edge, b.edge) : 0.f);
    };
    // (the matched pairs' own scores, the same signals: how well a right match scores here (sure needs about that))
    std::vector<float> knownScore;
    for (const auto& [t, m] : in.known) knownScore.push_back(like(tmplTile(t), modTile(m)));
    for (size_t i = 0; i < open.size(); ++i)
        for (size_t j = 0; j < cands.size(); ++j) base[i * cands.size() + j] = like(tmplTile(open[i]), modTile(cands[j]));
    if (in.wMask != 0) {  // (the tiles as their two sides by colour: the matches say which mod side is which template side)
        std::vector<const Tile*> ts, ms;
        for (uint16_t v : in.slots) ts.push_back(&tmplTile(v));
        for (const auto& [t, m] : in.known) ts.push_back(&tmplTile(t)), ms.push_back(&modTile(m));
        for (uint16_t v : cands) ms.push_back(&modTile(v));
        const auto tc = twoColors(ts), mc = twoColors(ms);
        std::unordered_map<uint16_t, std::array<float, kN * kN>> tSide, mSide;
        for (uint16_t v : open) tSide[v] = sidesOf(tmplTile(v), tc);
        for (uint16_t v : cands) mSide[v] = sidesOf(modTile(v), mc);
        double pol = 0;
        for (const auto& [t, m] : in.known) pol += corr(sidesOf(tmplTile(t), tc), sidesOf(modTile(m), mc));
        const float sign = pol < 0 ? -1.f : 1.f;
        {
            size_t n = 0;
            for (const auto& [t, m] : in.known) knownScore[n++] += in.wMask * sign * corr(sidesOf(tmplTile(t), tc), sidesOf(modTile(m), mc));
        }
        for (size_t i = 0; i < open.size(); ++i)
            for (size_t j = 0; j < cands.size(); ++j) base[i * cands.size() + j] += in.wMask * sign * corr(tSide[open[i]], mSide[cands[j]]);
    }
    std::map<std::tuple<uint16_t, uint16_t, int>, float> modSeams;
    auto modSeam = [&](uint16_t a, uint16_t b, int dir) {
        const auto key = std::make_tuple(a, b, dir);
        auto it = modSeams.find(key);
        return it != modSeams.end() ? it->second : (modSeams[key] = seam(modTile(a), modTile(b), dir));
    };
    // (the rects: a tile's partners (the other half: right of it, or left), the tiles of its rects)
    std::multimap<uint16_t, uint16_t> rightOf, leftOf;
    std::map<uint16_t, std::set<uint16_t>> sameRect;
    {
        std::map<uint16_t, std::set<uint16_t>> byLeft;  // (a rect's variants: pairs whose left tiles are in one group and right tiles in one group)
        for (const auto& p : in.pairs) rightOf.emplace(p[0], p[1]), leftOf.emplace(p[1], p[0]);
        for (const auto& p : in.pairs)
            for (const auto& q : in.pairs)
                if ((p[0] >> 4) == (q[0] >> 4) && (p[1] >> 4) == (q[1] >> 4))
                    for (uint16_t a : p)
                        for (uint16_t b : q)
                            if (a != b) sameRect[a].insert(b);
    }
    // (colours: the template's colours to the mod's, a line a channel fitted on the matched tiles (their blocks at the
    // same places); a candidate's colour score: how near its blocks are to its slot's blocks put through that, 1: the same)
    std::array<float, 3> ca{1, 1, 1}, cb{0, 0, 0};
    for (int ch = 0; ch < 3; ++ch) {
        double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (const auto& [t, m] : in.known) {
            const Tile& a = tmplTile(t);
            const Tile& b = modTile(m);
            if (!a.ok || !b.ok) continue;
            for (int k = 0; k < 64; ++k) {
                const double x = a.blocks[size_t(k * 3 + ch)], y = b.blocks[size_t(k * 3 + ch)];
                n += 1, sx += x, sy += y, sxx += x * x, sxy += x * y;
            }
        }
        if (n < 2) continue;
        const double den = n * sxx - sx * sx;
        ca[size_t(ch)] = den > 1e-6 ? float((n * sxy - sx * sy) / den) : 0.f;
        cb[size_t(ch)] = float((sy - ca[size_t(ch)] * sx) / n);
    }
    auto colorOff = [&](const Tile& a, const Tile& b) {
        double d = 0;
        for (int k = 0; k < 64 * 3; ++k) d += std::abs(ca[size_t(k % 3)] * a.blocks[size_t(k)] + cb[size_t(k % 3)] - b.blocks[size_t(k)]);
        return in.wColor * float(d / (64 * 3)) / 16.f;
    };
    if (in.wColor != 0) {
        size_t n = 0;
        for (const auto& [t, m] : in.known) knownScore[n++] -= colorOff(tmplTile(t), modTile(m));
        for (size_t i = 0; i < open.size(); ++i)
            for (size_t j = 0; j < cands.size(); ++j) base[i * cands.size() + j] -= colorOff(tmplTile(open[i]), modTile(cands[j]));
    }
    // (sure: the best far ahead of the next (the margin) and as good as the matches score (the level): a slot's typical
    // candidate (its mean) is 0, a typical match (the matches' median) is 1)
    float knownLevel = 0;
    if (!knownScore.empty()) {
        std::nth_element(knownScore.begin(), knownScore.begin() + long(knownScore.size() / 2), knownScore.end());
        knownLevel = knownScore[knownScore.size() / 2];
    }
    std::unordered_map<uint16_t, float> slotMean;
    for (size_t i = 0; i < open.size(); ++i) {
        double sum = 0;
        for (size_t j = 0; j < cands.size(); ++j) sum += base[i * cands.size() + j];
        slotMean[open[i]] = float(sum / double(cands.size()));
    }
    auto level = [&](uint16_t slot, float sc) {
        if (knownScore.empty()) return 0.f;
        const float lo = slotMean.at(slot);
        return knownLevel > lo ? std::clamp((sc - lo) / (knownLevel - lo), 0.f, 1.f) : 0.f;
    };

    std::map<uint16_t, uint16_t> known = in.known;
    std::set<uint16_t> used = usedMods;
    auto score = [&](uint16_t s, uint16_t c, const std::map<int, std::map<int, int>>& steps) {
        float sc = base[slotIx.at(s) * cands.size() + candIx.at(c)];
        if (in.wSeam == 0 && in.wLayout == 0) return sc;
        double ws = 0, ds = 0;  // (seams: with its rect's other half)
        for (auto [it, end] = leftOf.equal_range(s); it != end; ++it)
            if (const auto k = known.find(it->second); k != known.end()) ws += 1, ds += modSeam(k->second, c, 0);
        for (auto [it, end] = rightOf.equal_range(s); it != end; ++it)
            if (const auto k = known.find(it->second); k != known.end()) ws += 1, ds += modSeam(c, k->second, 0);
        if (ws > 0) sc -= in.wSeam * float(ds / ws) / 12.f;
        double votes = 0;  // (layout: the steps the matches took within a rect)
        int asked = 0;
        const auto rs = sameRect.find(s);
        for (const auto& [x, a] : known)
            if (rs == sameRect.end() || !rs->second.count(x)) continue;
            else if (const auto it = steps.find(int(s) - int(x)); it != steps.end()) {
                int all = 0, same = 0;
                for (const auto& [d, n] : it->second) all += n, same += d == int(c) - int(a) ? n : 0;
                votes += double(same) / all, ++asked;
            }
        if (asked) sc += in.wLayout * float(votes / asked);
        return sc;
    };
    while (!open.empty()) {
        std::map<int, std::map<int, int>> steps;  // (template step -> mod step -> how often)
        for (const auto& [x, a] : known)
            if (const auto rx = sameRect.find(x); rx != sameRect.end())
                for (const auto& [y, b] : known)
                    if (rx->second.count(y)) ++steps[int(y) - int(x)][int(b) - int(a)];
        size_t bestSlot = 0;
        uint16_t bestMod = 0;
        float best = -1e9f, second = -1e9f;
        // (a slot whose rect's other half is open too goes with it, as a pair, below)
        std::set<uint16_t> inPair;
        {
            const std::set<uint16_t> os(open.begin(), open.end());
            for (const auto& p : in.pairs)
                if (p[0] != p[1] && os.count(p[0]) && os.count(p[1])) inPair.insert(p[0]), inPair.insert(p[1]);
        }
        for (size_t i = 0; i < open.size(); ++i) {
            if (inPair.count(open[i])) continue;
            float b1 = -1e9f, b2 = -1e9f;
            uint16_t m1 = 0;
            for (uint16_t c : cands) {
                if (used.count(c)) continue;
                const float sc = score(open[i], c, steps);
                if (sc > b1) b2 = b1, b1 = sc, m1 = c;
                else if (sc > b2) b2 = sc;
            }
            if (m1 && b1 > best) best = b1, second = b2, bestSlot = i, bestMod = m1;
        }
        // (a rect's two halves with neither matched: together, its right one the step from its left one that the matched
        // rects took (the next subtile, the next group...))
        std::map<int, int> halfSteps;
        std::map<int, std::set<int>> starts;  // (a step of 1: the left tiles' subtiles, odd or even: pairs packed 0+1, 2+3... start on one)
        for (const auto& p : in.pairs)
            if (const auto l = known.find(p[0]), r = known.find(p[1]); l != known.end() && r != known.end() && r->second != l->second) {
                const int d = int(r->second) - int(l->second);
                ++halfSteps[d];
                if (d == 1) starts[d].insert(l->second & 1);
            }
        if (halfSteps.empty()) halfSteps = {{1, 1}, {16, 1}};
        const std::array<uint16_t, 2>* bestPair = nullptr;
        uint16_t bestPairMod = 0;
        int bestStep = 0;
        float pairBest = -1e9f, pairSecond = -1e9f;
        const std::set<uint16_t> openSet(open.begin(), open.end());
        for (const auto& p : in.pairs) {
            if (p[0] == p[1] || !openSet.count(p[0]) || !openSet.count(p[1])) continue;
            float b1 = -1e9f, b2 = -1e9f;
            uint16_t m1 = 0;
            int s1 = 0;
            for (uint16_t c : cands) {
                if (used.count(c)) continue;
                for (const auto& [d, n] : halfSteps) {
                    const int c2 = int(c) + d;
                    if (c2 < 0 || c2 > 0xFFFF || !candIx.count(uint16_t(c2)) || used.count(uint16_t(c2))) continue;
                    if (const auto st = starts.find(d); st != starts.end() && !st->second.count(c & 1)) continue;
                    const float sc = (score(p[0], c, steps) + score(p[1], uint16_t(c2), steps)) / 2 - (in.wSeam != 0 ? in.wSeam * modSeam(c, uint16_t(c2), 0) / 24.f : 0.f);
                    if (sc > b1) b2 = b1, b1 = sc, m1 = c, s1 = d;
                    else if (sc > b2) b2 = sc;
                }
            }
            if (m1 && b1 > pairBest) pairBest = b1, pairSecond = b2, bestPair = &p, bestPairMod = m1, bestStep = s1;
        }
        if (bestPair && (pairBest >= best || !bestMod)) {
            const uint16_t r = uint16_t(int(bestPairMod) + bestStep);
            const float lv = std::min(level((*bestPair)[0], base[slotIx.at((*bestPair)[0]) * cands.size() + candIx.at(bestPairMod)]),
                                      level((*bestPair)[1], base[slotIx.at((*bestPair)[1]) * cands.size() + candIx.at(r)]));
            const float conf = pairSecond > -1e8f ? std::min(lv, std::clamp((pairBest - pairSecond) * 2.f, 0.f, 1.f)) : 0.f;  // (no other choice: not sure)
            out.push_back({(*bestPair)[0], bestPairMod, pairBest, conf}), out.push_back({(*bestPair)[1], r, pairBest, conf});
            known[(*bestPair)[0]] = bestPairMod, known[(*bestPair)[1]] = r, used.insert(bestPairMod), used.insert(r);
            const auto a = (*bestPair)[0], b = (*bestPair)[1];
            open.erase(std::remove_if(open.begin(), open.end(), [&](uint16_t v) { return v == a || v == b; }), open.end());
            continue;
        }
        if (!bestMod) break;
        const float lv = level(open[bestSlot], base[slotIx.at(open[bestSlot]) * cands.size() + candIx.at(bestMod)]);
        out.push_back({open[bestSlot], bestMod, best, second > -1e8f ? std::min(lv, std::clamp((best - second) * 2.f, 0.f, 1.f)) : 0.f});
        known[open[bestSlot]] = bestMod, used.insert(bestMod);
        open.erase(open.begin() + long(bestSlot));
    }
    // (each suggestion's alternatives: its slot's next best candidates by likeness, sides and colours, any not matched)
    for (Suggestion& s : out) {
        const auto si = slotIx.find(s.tmpl);
        if (si == slotIx.end()) continue;
        std::vector<std::pair<float, uint16_t>> rank;
        for (size_t j = 0; j < cands.size(); ++j)
            if (cands[j] != s.mod) rank.push_back({base[si->second * cands.size() + j], cands[j]});
        const size_t n = std::min<size_t>(8, rank.size());
        std::partial_sort(rank.begin(), rank.begin() + long(n), rank.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t k = 0; k < n; ++k) s.alts.push_back(rank[k].second);
    }
    return out;
}

}  // namespace stardraft::tilematch
