// The recolor CRIGs (include/stardraft/mods/cmbw.h: Recolor): the mod's exe plugin's palette recolor, its line's
// syntax as the plugin's trigger parser reads it, and the map's recolor lines.

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>

#include "stardraft/mods/cmbw.h"

namespace stardraft::cmbw {

namespace {

// The plugin's interpolation.h, in f32 as it has them.
float lerp(float t, float a, float b) { return t * (b - a) + a; }
float easeInOut(float t) { return lerp(t, t * t, 1.0f - (t - 1.0f) * (t - 1.0f)); }
float smoothstep(float t, float a, float b) { return lerp(easeInOut(t), a, b); }

Recolor lerpRecolor(float t, Recolor a, const Recolor& b) {
    a.r = smoothstep(t, a.r, b.r);
    a.g = smoothstep(t, a.g, b.g);
    a.b = smoothstep(t, a.b, b.b);
    a.gamma = smoothstep(t, a.gamma, b.gamma);
    a.saturation = smoothstep(t, a.saturation, b.saturation);
    return a;
}

// color_palette.cpp: lerpn_recolor.
Recolor lerpnRecolor(float t, const std::vector<Recolor>& stops) {
    const size_t count = std::min(stops.size(), kRecolorStops);
    if (count == 0) return Recolor{};
    if (count == 1) return stops[0];
    const uint32_t last = uint32_t(count - 1);
    t = std::clamp(easeInOut(t), 0.0f, 1.0f);
    t *= float(last);
    const uint32_t index = uint32_t(t);
    if (index == last) return stops[last];
    t -= float(index);
    return lerpRecolor(t, stops[index], stops[index + 1]);
}

// color_palette.cpp: mix_color (the u8 cast cuts the fraction off).
uint8_t mixColor(float base, float blend, float saturation, float brightness) {
    base = smoothstep(saturation, brightness, base);
    return uint8_t(std::clamp(base * blend, 0.0f, 1.0f) * 255.0f);
}

// The trigger parser's word characters (trigger_parser.cpp: is_word_char).
bool wordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '*'; }
bool spaceChar(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

// The parser's get_float: the non-word characters skipped, then a word as a number.
bool readFloat(const std::string& s, size_t& at, float& out) {
    while (at < s.size() && !wordChar(s[at])) ++at;
    const size_t start = at;
    while (at < s.size() && wordChar(s[at])) ++at;
    if (at == start) return false;
    const auto r = std::from_chars(s.data() + start, s.data() + at, out);
    return r.ec == std::errc{};
}

// The parser's get_time_seconds_raw: numbers with h, m, s, t (ticks: 1/24 s) or ms; a last number without one is
// whole seconds (rounded up).
bool readSeconds(const std::string& s, size_t& at, double& out) {
    out = 0;
    std::string token;
    while (at < s.size() && !spaceChar(s[at]) && s[at] != '|' && s[at] != '&') {
        const char c = s[at];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || (token.empty() && (c == '+' || c == '-'))) {
            token += c, ++at;
            continue;
        }
        double v = 0;
        if (std::from_chars(token.data(), token.data() + token.size(), v).ec != std::errc{}) return false;
        if (c == 'm' && at + 1 < s.size() && s[at + 1] == 's') out += v / 1000, at += 2;
        else if (c == 'h') out += v * 3600, ++at;
        else if (c == 'm') out += v * 60, ++at;
        else if (c == 's') out += v, ++at;
        else if (c == 't') out += v / 24, ++at;
        else return false;
        token.clear();
    }
    if (!token.empty()) {
        double v = 0;
        if (std::from_chars(token.data(), token.data() + token.size(), v).ec != std::errc{}) return false;
        out += std::ceil(v);
    }
    return true;
}

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s == "-0" || s.empty() ? "0" : s;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

bool parseRecolorLine(const std::string& line, RecolorValue& out) {
    size_t at = 0;
    while (at < line.size() && spaceChar(line[at])) ++at;
    const size_t start = at;
    while (at < line.size() && wordChar(line[at])) ++at;
    const std::string name = lower(line.substr(start, at - start));
    if (name != "recolor" && name != "recolor_loop_add") return false;
    RecolorValue v;
    v.command = name;
    v.stops.clear();
    for (;;) {  // (get_recolor_set: the stops while the next character after the spaces is "(")
        while (at < line.size() && spaceChar(line[at])) ++at;
        if (at >= line.size() || line[at] != '(') break;
        if (v.stops.size() >= kRecolorStops) return false;  // (the plugin reads 16; the rest would be the time)
        ++at;
        Recolor r;
        if (!readFloat(line, at, r.r) || !readFloat(line, at, r.g) || !readFloat(line, at, r.b) || !readFloat(line, at, r.gamma) ||
            !readFloat(line, at, r.saturation))
            return false;
        if (at < line.size() && line[at] == ')') ++at;
        v.stops.push_back(r);
    }
    if (v.stops.empty()) return false;
    while (at < line.size() && spaceChar(line[at])) ++at;
    if (at < line.size() && !readSeconds(line, at, v.seconds)) return false;
    out = std::move(v);
    return true;
}

std::string recolorLineText(const RecolorValue& v) {
    std::string s = v.command.empty() ? "recolor" : v.command;
    for (size_t i = 0; i < v.stops.size() && i < kRecolorStops; ++i) {
        const Recolor& r = v.stops[i];
        s += " (" + number(r.r) + " " + number(r.g) + " " + number(r.b) + " " + number(r.gamma) + " " + number(r.saturation) + ")";
    }
    const double sec = std::max(0.0, v.seconds);
    s += " " + number(sec);
    if (std::floor(sec) != sec) s += "s";  // (a number without a unit is whole seconds, rounded up)
    return s;
}

void recolorRgb(const std::vector<Recolor>& stops, uint8_t& r, uint8_t& g, uint8_t& b) {
    const float brightness = float((int(r) + int(g) + int(b)) / 3.0 / 255.0);
    const Recolor c = lerpnRecolor(brightness, stops);
    const uint8_t nr = mixColor(float(r / 255.0), c.r * c.gamma, c.saturation, brightness);
    const uint8_t ng = mixColor(float(g / 255.0), c.g * c.gamma, c.saturation, brightness);
    const uint8_t nb = mixColor(float(b / 255.0), c.b * c.gamma, c.saturation, brightness);
    r = nr, g = ng, b = nb;
}

std::vector<RecolorLineRef> recolorLines(const Chk& chk) {
    std::vector<RecolorLineRef> out;
    const ClassicTriggers t = readClassicTriggers(chk);
    const StringTable strings = StringTable::read(chk);
    for (size_t ti = 0; ti < t.triggers.size(); ++ti)
        for (const ClassicAction& a : t.triggers[ti].actions) {
            if (a.type != 47 || !a.text || (a.flags & 2) || a.text > strings.size()) continue;  // (Comments that are on)
            const std::string& text = strings.get(a.text);
            size_t start = 0;
            while (start < text.size()) {
                size_t end = start;
                while (end < text.size() && text[end] != '\n' && text[end] != '\r') ++end;
                const std::string line = text.substr(start, end - start);
                start = end + 1;
                RecolorValue v;
                if (parseRecolorLine(line, v)) out.push_back({int(ti + 1), line});
            }
        }
    return out;
}

}  // namespace stardraft::cmbw
