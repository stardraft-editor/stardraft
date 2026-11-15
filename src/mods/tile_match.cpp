#include "stardraft/mods/tile_match.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <functional>
#include <numeric>
#include <thread>
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

// f(i) for each i in [0, n), on the processor's cores (each i writes only its own row of a table).
void parallelFor(size_t n, const std::function<void(size_t)>& f) {
    const size_t k = std::min<size_t>(n, std::clamp<size_t>(std::thread::hardware_concurrency(), 1, 8));
    if (k <= 1 || n < 8) {
        for (size_t i = 0; i < n; ++i) f(i);
        return;
    }
    std::atomic<size_t> next{0};
    std::vector<std::thread> threads;
    for (size_t t = 0; t < k; ++t)
        threads.emplace_back([&] {
            for (size_t i; (i = next++) < n;) f(i);
        });
    for (auto& t : threads) t.join();
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

uint16_t samePicture(const Tileset& a, uint16_t va, const Tileset& b) {
    auto pixel = [](const Tileset& t, int mt, int px, int py) {  // (0xRRGGBB; -1: no picture there)
        const uint32_t e = t.megatiles[size_t(mt)][size_t((py / 8) * 4 + px / 8)];
        if ((e >> 1) >= t.minitiles.size()) return -1;
        const Rgb c = t.palette[t.minitiles[e >> 1][size_t((py % 8) * 8 + ((e & 1) ? 7 - px % 8 : px % 8))]];
        return int(c.r) << 16 | int(c.g) << 8 | int(c.b);
    };
    const int ma = a.megatileFor(va);
    if (ma <= 0 || size_t(ma) >= a.megatiles.size()) return 0xFFFF;
    std::array<int, kN * kN> want{};
    for (int y = 0; y < kN; ++y)
        for (int x = 0; x < kN; ++x) want[size_t(y * kN + x)] = pixel(a, ma, x, y);
    // (the error: the mean colour difference a pixel, r + g + b; the same picture in two palettes: a few steps)
    constexpr long kMaxMean = 24;
    std::unordered_map<int, long> err;  // (by megatile: many tiles share one)
    auto errorOf = [&](uint16_t v, long budget) {
        const int mb = b.megatileFor(v);
        if (mb <= 0 || size_t(mb) >= b.megatiles.size()) return LONG_MAX;
        if (const auto it = err.find(mb); it != err.end()) return it->second;
        long e = 0;
        for (int i = 0; i < kN * kN && e <= budget; ++i) {
            const int p = pixel(b, mb, i % kN, i / kN), q = want[size_t(i)];
            if (p < 0 || q < 0) e += p == q ? 0 : 765;
            else e += std::abs((p >> 16) - (q >> 16)) + std::abs(((p >> 8) & 255) - ((q >> 8) & 255)) + std::abs((p & 255) - (q & 255));
        }
        return err[mb] = e <= budget ? e : LONG_MAX;
    };
    long best = kMaxMean * kN * kN;
    uint16_t at = 0xFFFF;
    if (const long e = errorOf(va, best); e <= best) best = e, at = va;
    for (size_t g = 0; g < b.groups.size() && best > 0; ++g)
        for (int s = 0; s < 16; ++s)
            if (const uint16_t v = uint16_t(g * 16 + size_t(s)); v != va)
                if (const long e = errorOf(v, best - 1); e < best) best = e, at = v;
    return at;
}

float pixelMatch(const Tileset& a, uint16_t va, const Tileset& b, uint16_t vb) {
    const Tile x = tileOf(a, va), y = tileOf(b, vb);
    return x.ok && y.ok ? std::clamp(corr(x.norm, y.norm), 0.f, 1.f) : 0.f;
}

std::set<int> searchGroups(const Input& in) {
    std::set<int> near;
    if (!in.modTs) return near;
    const int n = int(in.modTs->groups.size());
    if (in.onlyGroups.empty())
        for (const auto& [t, m] : in.known)
            for (int g = std::max(0, int(m >> 4) - in.window); g <= std::min(n - 1, int(m >> 4) + in.window); ++g) near.insert(g);
    for (int g : in.onlyGroups)  // (a search space: only its groups)
        if (g >= 0 && g < n) near.insert(g);
    return near;
}

std::vector<Suggestion> suggest(const Input& in) {
    std::vector<Suggestion> out;
    if (!in.tmplTs || !in.modTs || in.known.empty()) return out;
    const Tileset& T = *in.tmplTs;
    const Tileset& M = *in.modTs;
    // (the candidates: the mod's tiles in the matched tile groups and a window around them)
    std::set<uint16_t> usedMods;
    for (const auto& [t, m] : in.known) usedMods.insert(m);
    const std::set<int> near = searchGroups(in);  // (the candidates' groups: a window around each matched one, not all between them)
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
    // (each signal on its own first, raw: likeness, sides, colours; then their weights (learned from the matches, below),
    // then the table)
    auto like = [&](const Tile& a, const Tile& b) {
        return 0.3f * corr(a.norm, b.norm) + corr(a.shape, b.shape) + (in.wEdge != 0 ? in.wEdge / std::max(in.wLike, 1e-6f) * corr(a.edge, b.edge) : 0.f);
    };
    {  // (the candidates' tiles at once, on the cores: most of them are new)
        std::vector<Tile> made(cands.size());
        parallelFor(cands.size(), [&](size_t j) { made[j] = tileOf(M, cands[j]); });
        for (size_t j = 0; j < cands.size(); ++j) mt.emplace(cands[j], std::move(made[j]));
    }
    std::vector<const Tile*> ot, ct;
    for (uint16_t v : open) ot.push_back(&tmplTile(v));
    for (uint16_t v : cands) ct.push_back(&modTile(v));
    const size_t nc = cands.size(), no = open.size();
    std::vector<std::pair<uint16_t, uint16_t>> knownList(in.known.begin(), in.known.end());
    for (const auto& [t, m] : knownList) tmplTile(t), modTile(m);  // (made here: the threads below only read)
    const bool learn = in.learn && knownList.size() >= 24;
    std::vector<float> rL(no * nc), rM(no * nc, 0.f), rC(no * nc, 0.f);  // (raw: likeness, sides, colour distance)
    std::vector<float> kL(knownList.size()), kM(knownList.size(), 0.f), kC(knownList.size(), 0.f);
    parallelFor(no, [&](size_t i) {
        for (size_t j = 0; j < nc; ++j) rL[i * nc + j] = like(*ot[i], *ct[j]);
    });
    for (size_t n = 0; n < knownList.size(); ++n) kL[n] = like(tmplTile(knownList[n].first), modTile(knownList[n].second));
    // (the sides: the tiles as their two sides by colour: the matches say which mod side is which template side)
    std::array<std::array<float, 3>, 2> tc{}, mc{};
    float sign = 1.f;
    const bool useMask = in.wMask != 0 || learn;
    std::vector<std::array<float, kN * kN>> mSide;
    if (useMask) {
        std::vector<const Tile*> ts, ms;
        for (uint16_t v : in.slots) ts.push_back(&tmplTile(v));
        for (const auto& [t, m] : knownList) ts.push_back(&tmplTile(t)), ms.push_back(&modTile(m));
        for (uint16_t v : cands) ms.push_back(&modTile(v));
        tc = twoColors(ts), mc = twoColors(ms);
        std::vector<std::array<float, kN * kN>> tSide(no);
        mSide.resize(nc);
        parallelFor(no, [&](size_t i) { tSide[i] = sidesOf(*ot[i], tc); });
        parallelFor(nc, [&](size_t j) { mSide[j] = sidesOf(*ct[j], mc); });
        double pol = 0;
        for (size_t n = 0; n < knownList.size(); ++n)
            pol += kM[n] = corr(sidesOf(tmplTile(knownList[n].first), tc), sidesOf(modTile(knownList[n].second), mc));
        sign = pol < 0 ? -1.f : 1.f;
        for (float& v : kM) v *= sign;
        parallelFor(no, [&](size_t i) {
            for (size_t j = 0; j < nc; ++j) rM[i * nc + j] = sign * corr(tSide[i], mSide[j]);
        });
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
        for (const auto& p : in.pairs) rightOf.emplace(p[0], p[1]), leftOf.emplace(p[1], p[0]);
        for (const auto& p : in.pairs)
            for (const auto& q : in.pairs)
                if ((p[0] >> 4) == (q[0] >> 4) && (p[1] >> 4) == (q[1] >> 4))
                    for (uint16_t a : p)
                        for (uint16_t b : q)
                            if (a != b) sameRect[a].insert(b);
    }
    // (colours: the template's colours to the mod's, a line a channel fitted on the matched tiles (their blocks at the
    // same places); a candidate's colour distance: how far its blocks are from its slot's blocks put through that)
    std::array<float, 3> ca{1, 1, 1}, cb{0, 0, 0};
    for (int ch = 0; ch < 3; ++ch) {
        double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (const auto& [t, m] : knownList) {
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
        return float(d / (64 * 3)) / 16.f;
    };
    const bool useColor = in.wColor != 0 || learn;
    if (useColor) {
        for (size_t n = 0; n < knownList.size(); ++n) kC[n] = colorOff(tmplTile(knownList[n].first), modTile(knownList[n].second));
        parallelFor(no, [&](size_t i) {
            for (size_t j = 0; j < nc; ++j) rC[i * nc + j] = colorOff(*ot[i], *ct[j]);
        });
    }
    // (the weights, learned from the matches: each matched template tile against the candidates, as if open; the
    // weights that rank its own mod tile highest (the mean of 1 / its place) win. The set default unless one is clearly
    // better: a terrain of flat textures leans on colour, one of sharp edges on the shapes)
    float wl = in.wLike, wm = in.wMask, wc = in.wColor;
    if (learn) {
        const size_t ns = std::min<size_t>(48, knownList.size()), stride = knownList.size() / ns;
        std::vector<size_t> pick;
        for (size_t k = 0; k < ns; ++k) pick.push_back(k * stride);
        std::vector<float> sL(ns * nc), sM(ns * nc), sC(ns * nc), sOwnC(ns);
        // (the colour map of each tested match fitted without it: else colour looks better than it is)
        std::array<std::array<double, 5>, 3> all{};  // (a channel's n, sx, sy, sxx, sxy over every match)
        auto addPair = [&](std::array<std::array<double, 5>, 3>& acc, const Tile& a, const Tile& b, double k) {
            if (!a.ok || !b.ok) return;
            for (int ch = 0; ch < 3; ++ch)
                for (int q = 0; q < 64; ++q) {
                    const double x = a.blocks[size_t(q * 3 + ch)], y = b.blocks[size_t(q * 3 + ch)];
                    auto& v = acc[size_t(ch)];
                    v[0] += k, v[1] += k * x, v[2] += k * y, v[3] += k * x * x, v[4] += k * x * y;
                }
        };
        for (const auto& [t, m] : knownList) addPair(all, tmplTile(t), modTile(m), 1);
        parallelFor(ns, [&](size_t s) {
            const Tile& t = tmplTile(knownList[pick[s]].first);
            auto acc = all;
            addPair(acc, t, modTile(knownList[pick[s]].second), -1);
            std::array<float, 3> a3{1, 1, 1}, b3{0, 0, 0};
            for (int ch = 0; ch < 3; ++ch) {
                const auto& v = acc[size_t(ch)];
                if (v[0] < 2) continue;
                const double den = v[0] * v[3] - v[1] * v[1];
                a3[size_t(ch)] = den > 1e-6 ? float((v[0] * v[4] - v[1] * v[2]) / den) : 0.f;
                b3[size_t(ch)] = float((v[2] - a3[size_t(ch)] * v[1]) / v[0]);
            }
            auto off = [&](const Tile& b) {
                double d = 0;
                for (int k = 0; k < 64 * 3; ++k) d += std::abs(a3[size_t(k % 3)] * t.blocks[size_t(k)] + b3[size_t(k % 3)] - b.blocks[size_t(k)]);
                return float(d / (64 * 3)) / 16.f;
            };
            const auto ts = sidesOf(t, tc);
            for (size_t j = 0; j < nc; ++j) sL[s * nc + j] = like(t, *ct[j]), sM[s * nc + j] = sign * corr(ts, mSide[j]), sC[s * nc + j] = off(*ct[j]);
            sOwnC[s] = off(modTile(knownList[pick[s]].second));
        });
        auto mrr = [&](float a, float m, float c) {
            double sum = 0;
            for (size_t s = 0; s < ns; ++s) {
                const size_t n = pick[s];
                const float own = a * kL[n] + m * kM[n] - c * sOwnC[s];
                int rank = 1;
                for (size_t j = 0; j < nc; ++j) rank += a * sL[s * nc + j] + m * sM[s * nc + j] - c * sC[s * nc + j] > own;
                sum += 1.0 / rank;
            }
            return sum / double(ns);
        };
        double best = mrr(wl, wm, wc) + 0.02;  // (the default wins ties and near ties)
        for (float a : {0.5f, 1.f, 2.f})
            for (float m : {0.f, 0.5f, 1.f, 2.f})
                for (float c : {0.f, 0.5f, 1.f, 2.f, 4.f})
                    if (const double r = mrr(a, m, c); r > best) best = r, wl = a, wm = m, wc = c;
    }
    if (in.learned) *in.learned = {wl, wm, wc};
    std::vector<float> base(no * nc);
    parallelFor(no, [&](size_t i) {
        for (size_t j = 0; j < nc; ++j) base[i * nc + j] = wl * rL[i * nc + j] + wm * rM[i * nc + j] - wc * rC[i * nc + j];
    });
    std::vector<float> knownScore(knownList.size());
    for (size_t n = 0; n < knownList.size(); ++n) knownScore[n] = wl * kL[n] + wm * kM[n] - wc * kC[n];
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
    // (the weights in use (no seams, no layout): a score is the table's; each slot's candidates sorted once, best first,
    // so the best one not taken is the first one not taken; the same choices as the general pass below, much sooner)
    const bool fast = in.wSeam == 0 && in.wLayout == 0;
    if (fast) {
        std::vector<char> usedC(nc, 0), isOpen(no, 1);
        std::vector<int> cIx(65536, -1);
        for (size_t j = 0; j < nc; ++j) cIx[cands[j]] = int(j);
        std::vector<std::vector<uint32_t>> order(no);
        std::vector<size_t> head(no, 0);
        parallelFor(no, [&](size_t i) {
            auto& o = order[i];
            o.resize(nc);
            std::iota(o.begin(), o.end(), 0u);
            const float* r = &base[i * nc];
            std::sort(o.begin(), o.end(), [r](uint32_t a, uint32_t b) { return r[a] > r[b] || (r[a] == r[b] && a < b); });  // (ties: the lower one, as below)
        });
        auto row = [&](size_t i) { return &base[i * nc]; };
        auto top2 = [&](size_t i, int& j1, float& b1, float& b2) {  // (slot i's best candidate not taken, and the next one's score)
            const auto& o = order[i];
            size_t& h = head[i];
            while (h < nc && usedC[o[h]]) ++h;
            j1 = -1, b1 = b2 = -1e9f;
            if (h == nc) return;
            j1 = int(o[h]), b1 = row(i)[j1];
            for (size_t k = h + 1; k < nc; ++k)
                if (!usedC[o[k]]) {
                    b2 = row(i)[o[k]];
                    break;
                }
        };
        struct PairIx {
            size_t a, b;
        };
        std::vector<PairIx> pix;
        for (const auto& p : in.pairs)
            if (const auto ia = slotIx.find(p[0]), ib = slotIx.find(p[1]); p[0] != p[1] && ia != slotIx.end() && ib != slotIx.end()) pix.push_back({ia->second, ib->second});
        for (size_t left = no; left > 0;) {
            std::map<int, int> halfSteps;
            std::map<int, std::set<int>> starts;
            for (const auto& p : in.pairs)
                if (const auto l = known.find(p[0]), r = known.find(p[1]); l != known.end() && r != known.end() && r->second != l->second) {
                    const int d = int(r->second) - int(l->second);
                    ++halfSteps[d];
                    if (d == 1) starts[d].insert(l->second & 1);
                }
            if (halfSteps.empty()) halfSteps = {{1, 1}, {16, 1}};
            std::vector<char> inPair(no, 0);
            for (const PairIx& q : pix)
                if (isOpen[q.a] && isOpen[q.b]) inPair[q.a] = inPair[q.b] = 1;
            size_t bestSlot = 0;
            int bestJ = -1;
            float best = -1e9f, second = -1e9f;
            for (size_t i = 0; i < no; ++i) {
                if (!isOpen[i] || inPair[i]) continue;
                int j1;
                float b1, b2;
                top2(i, j1, b1, b2);
                if (j1 >= 0 && b1 > best) best = b1, second = b2, bestSlot = i, bestJ = j1;
            }
            const PairIx* bp = nullptr;
            int bpJ = -1, bpStep = 0;
            float pairBest = -1e9f, pairSecond = -1e9f;
            for (const PairIx& q : pix) {
                if (!isOpen[q.a] || !isOpen[q.b]) continue;
                int jb;
                float bb1, bb2;
                top2(q.b, jb, bb1, bb2);
                if (jb < 0) continue;
                float p1 = -1e9f, p2 = -1e9f;
                int m1 = -1, s1 = 0;
                const auto& o = order[q.a];
                for (size_t k = head[q.a]; k < nc; ++k) {
                    const uint32_t j = o[k];
                    if (usedC[j]) continue;
                    const float ra = row(q.a)[j];
                    if ((ra + bb1) / 2 < p2) break;  // (no later one can be among the two best)
                    for (const auto& [d, n] : halfSteps) {
                        const int c2 = int(cands[j]) + d;
                        if (c2 < 0 || c2 > 0xFFFF) continue;
                        const int j2 = cIx[size_t(c2)];
                        if (j2 < 0 || usedC[size_t(j2)]) continue;
                        if (const auto st = starts.find(d); st != starts.end() && !st->second.count(cands[j] & 1)) continue;
                        const float sc = (ra + row(q.b)[j2]) / 2;
                        if (sc > p1 || (sc == p1 && (int(j) < m1 || (int(j) == m1 && d < s1)))) p2 = p1, p1 = sc, m1 = int(j), s1 = d;  // (ties: the lower one, as below)
                        else if (sc > p2) p2 = sc;
                    }
                }
                if (m1 >= 0 && p1 > pairBest) pairBest = p1, pairSecond = p2, bp = &q, bpJ = m1, bpStep = s1;
            }
            if (bp && (pairBest >= best || bestJ < 0)) {
                const size_t j2 = size_t(cIx[size_t(int(cands[size_t(bpJ)]) + bpStep)]);
                const uint16_t a = open[bp->a], b = open[bp->b], l = cands[size_t(bpJ)], r = cands[j2];
                const float lv = std::min(level(a, row(bp->a)[bpJ]), level(b, row(bp->b)[j2]));
                const float conf = pairSecond > -1e8f ? std::min(lv, std::clamp((pairBest - pairSecond) * 2.f, 0.f, 1.f)) : 0.f;
                out.push_back({a, l, pairBest, conf}), out.push_back({b, r, pairBest, conf});
                known[a] = l, known[b] = r, usedC[size_t(bpJ)] = usedC[j2] = 1, isOpen[bp->a] = isOpen[bp->b] = 0, left -= 2;
                continue;
            }
            if (bestJ < 0) break;
            const float lv = level(open[bestSlot], row(bestSlot)[bestJ]);
            out.push_back({open[bestSlot], cands[size_t(bestJ)], best, second > -1e8f ? std::min(lv, std::clamp((best - second) * 2.f, 0.f, 1.f)) : 0.f});
            known[open[bestSlot]] = cands[size_t(bestJ)], usedC[size_t(bestJ)] = 1, isOpen[bestSlot] = 0, --left;
        }
    }
    while (!fast && !open.empty()) {
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
