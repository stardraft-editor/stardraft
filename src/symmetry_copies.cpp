#include "stardraft/symmetry_copies.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>

namespace stardraft {
std::string CopyTransform::key() const {
    std::string s;
    for (double v : {a, b, c, d}) s += (s.empty() ? "" : ",") + std::to_string(std::lround(v * 1000000));
    return s;
}
std::vector<CopyTransform> copyTransforms(int mirrors, int turns, bool direct, bool mirroredRotations, bool opposite) {
    turns = std::clamp(turns, 1, 16);
    std::vector<CopyTransform> out;
    std::set<std::string> seen;
    auto add = [&](CopyTransform t) {
        if (out.size() < 64 && seen.insert(t.key()).second) out.push_back(t);
    };
    for (int k = 0; k < turns; ++k) {
        const double a = 6.283185307179586 * k / turns;
        add({std::cos(a), -std::sin(a), std::sin(a), std::cos(a)});
    }
    if (mirroredRotations && turns > 1) {
        const auto rotations = out;
        for (const auto t : rotations) add({-t.a, t.b, -t.c, t.d});
    }
    const CopyTransform reflections[] = {{-1, 0, 0, 1}, {1, 0, 0, -1}, {0, 1, 1, 0}, {0, -1, -1, 0}, {-1, 0, 0, -1}};
    for (size_t k = 0; k < out.size(); ++k) {
        if (direct && k) break;
        const auto t = out[k];
        for (int m = 0; m < 5; ++m)
            if (m == 4 ? opposite : (mirrors & (1 << m))) {
                const auto r = reflections[m];
                add({r.a * t.a + r.b * t.c, r.a * t.b + r.b * t.d, r.c * t.a + r.d * t.c, r.c * t.b + r.d * t.d});
            }
    }
    return out;
}
std::string copyOptionsText(const CopyOptions& o) {
    std::string text = "3;" + std::to_string(o.direct) + ";" + std::to_string(o.terrain) + ";" + std::to_string(o.rawTile) + ";" +
                       std::to_string(o.mirroredRotations) + ";" + std::to_string(o.opposite);
    for (const auto& c : o.choices)
        text += ";" + c.key + ":" + std::to_string(c.enabled) + ":" + std::to_string(c.terrain) + ":" + std::to_string(c.rawTile);
    return text;
}
CopyOptions readCopyOptions(const std::string& text) {
    CopyOptions out;
    if (text.size() > 16384) return out;
    std::vector<std::string> fields;
    std::stringstream stream(text);
    for (std::string s; std::getline(stream, s, ';');) fields.push_back(s);
    auto number = [](const std::string& s, int lo, int hi) {
        size_t end = 0;
        const int n = std::stoi(s, &end);
        if (end != s.size() || n < lo || n > hi) throw std::invalid_argument("copy setting");
        return n;
    };
    try {
        if (fields.size() < 4 || (fields[0] != "2" && fields[0] != "3")) return {};
        out.direct = number(fields[1], 0, 1);
        out.terrain = number(fields[2], -1, 65534), out.rawTile = number(fields[3], -1, 65534);
        size_t firstChoice = 4;
        if (fields[0] == "3") {
            if (fields.size() < 6) return {};
            out.mirroredRotations = number(fields[4], 0, 1), out.opposite = number(fields[5], 0, 1);
            firstChoice = 6;
        }
        for (size_t i = firstChoice; i < fields.size() && out.choices.size() < 128; ++i) {
            std::vector<std::string> parts;
            std::stringstream row(fields[i]);
            for (std::string s; std::getline(row, s, ':');) parts.push_back(s);
            if (parts.size() != 4 || parts[0].size() > 96 || parts[0].find_first_not_of("-0123456789,") != std::string::npos) return {};
            out.choices.push_back({parts[0], bool(number(parts[1], 0, 1)), number(parts[2], -1, 65534), number(parts[3], -1, 65534)});
        }
    } catch (...) {
        return {};
    }
    return out;
}
} // namespace stardraft
