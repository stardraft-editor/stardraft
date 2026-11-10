// Hyperion's map format, .hxm (include/stardraft/mods/hyperion.h).
//
// The compile (hxm to CHK, the struct Compile below) and the names (hyperion_names.inc) are ported from Hyperion's
// engine/src117/port/data/port_hxm.cpp and port_hxm_names.h (https://github.com/Mad-Man-Dan/hyperion-public), under
// its license:
//
//   MIT License
//
//   Copyright (c) the Hyperion authors (Mad-Man-Dan/hyperion-public)
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
//   documentation files (the "Software"), to deal in the Software without restriction, including without limitation
//   the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
//   permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all copies or substantial portions of
//   the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
//   THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.
//
// The compile must give the bytes Hyperion's does (`Hyperion.exe --hxm-compile`): it follows Hyperion's step by step,
// with its names. The conversion back (from_chk, the struct FromChk) is StarDraft's own: the inverse of the compile,
// as Hyperion's hxm-format.md ("Conversion") describes Hyperion's tools/hxmap.

#include "stardraft/mods/hyperion.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>

#include "file_write.h"

namespace stardraft::hyperion {

namespace fs = std::filesystem;

namespace {

struct HxmName {
    int n;
    const char* psz;
};
#include "hyperion_names.inc"

// The standard VCOD (new_map.cpp has the same bytes): a document's "vcod": "standard".
const uint8_t kVcod[1040] =
#include "vcod.inc"
    ;

const int NEUTRAL = 11;
const int HXM_VERSION = 1;          // map.json's "version" this reads
const int PLAYER_LOBBY_117 = 8;     // the lobby's slots
const int PLAYERS_MAX = 64;         // the players a Hyperion map lists (slots 0 to 10 and 12 to 64)
const int PLAYER_SLOTS_MAX = 65;    // the slots, neutral's among them
const int MAP_SIDE_117 = 256;
const int MAP_VERSION_HX = 1206;    // the VER of a map past the classic limits
const int MAP_VERSION_HX_ORIGINAL = 1064;

// --- JSON, as Hyperion's reader keeps it: a field left out (NONE) is not null --------------------------------------

struct J;
struct JMember;

struct J {
    enum Kind { NONE, NULLV, BOOL, NUMBER, STRING, ARRAY, OBJECT } kind = NONE;
    bool b = false;
    double d = 0;
    std::string s;
    std::vector<J> items;
    std::vector<JMember> members;

    static const J& none() {
        static const J n;
        return n;
    }
    const J& operator[](std::string_view k) const;
    size_t size() const { return kind == ARRAY ? items.size() : 0; }
    const J& At(size_t i) const { return kind == ARRAY && i < items.size() ? items[i] : none(); }
    std::string Str(const std::string& dflt = {}) const { return kind == STRING ? s : dflt; }

    static J num(long long v) {
        J j;
        j.kind = NUMBER, j.d = double(v);
        return j;
    }
    static J str(std::string v) {
        J j;
        j.kind = STRING, j.s = std::move(v);
        return j;
    }
    static J boolean(bool v) {
        J j;
        j.kind = BOOL, j.b = v;
        return j;
    }
    static J arr() {
        J j;
        j.kind = ARRAY;
        return j;
    }
    static J obj() {
        J j;
        j.kind = OBJECT;
        return j;
    }
    J& add(std::string k, J v);
    J& push(J v) {
        items.push_back(std::move(v));
        return items.back();
    }
};

struct JMember {
    std::string first;
    J second;
};

const J& J::operator[](std::string_view k) const {
    for (const JMember& m : members)
        if (m.first == k) return m.second;
    return none();
}

J& J::add(std::string k, J v) {
    members.push_back({std::move(k), std::move(v)});
    return members.back().second;
}

void putUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | cp >> 6), out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | cp >> 12), out += char(0x80 | (cp >> 6 & 0x3F)), out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | cp >> 18), out += char(0x80 | (cp >> 12 & 0x3F)), out += char(0x80 | (cp >> 6 & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

struct JParser {
    std::string_view t;
    size_t i = 0;
    std::string error;
    int depth = 0;

    bool fail(const char* what) {
        if (error.empty()) error = std::string(what) + " at byte " + std::to_string(i);
        return false;
    }
    void ws() {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) ++i;
    }
    bool hex4(unsigned& v) {
        if (i + 4 > t.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = t[i + k];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else return false;
        }
        i += 4;
        return true;
    }
    bool string(std::string& out) {
        ++i;  // (the quote)
        while (i < t.size() && t[i] != '"') {
            const char c = t[i++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= t.size()) return fail("a cut escape");
            const char e = t[i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp;
                if (!hex4(cp)) return fail("a bad \\u escape");
                if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                    const size_t at = i;
                    i += 2;
                    unsigned lo;
                    if (hex4(lo) && lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else i = at;
                }
                putUtf8(out, cp);
                break;
            }
            default: return fail("a bad escape");
            }
        }
        if (i >= t.size()) return fail("an unended string");
        ++i;
        return true;
    }
    bool value(J& v) {
        ws();
        if (i >= t.size()) return fail("the end of the text");
        if (++depth > 200) return fail("too deep");
        bool ok = true;
        const char c = t[i];
        if (c == '{') {
            ++i;
            v.kind = J::OBJECT;
            ws();
            if (i < t.size() && t[i] == '}') {
                ++i;
            } else {
                for (;;) {
                    ws();
                    if (i >= t.size() || t[i] != '"') {
                        ok = fail("a missing name");
                        break;
                    }
                    std::string k;
                    if (!string(k)) {
                        ok = false;
                        break;
                    }
                    ws();
                    if (i >= t.size() || t[i] != ':') {
                        ok = fail("a missing ':'");
                        break;
                    }
                    ++i;
                    J m;
                    if (!value(m)) {
                        ok = false;
                        break;
                    }
                    v.members.push_back({std::move(k), std::move(m)});
                    ws();
                    if (i < t.size() && t[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < t.size() && t[i] == '}') {
                        ++i;
                        break;
                    }
                    ok = fail("a missing ',' or '}'");
                    break;
                }
            }
        } else if (c == '[') {
            ++i;
            v.kind = J::ARRAY;
            ws();
            if (i < t.size() && t[i] == ']') {
                ++i;
            } else {
                for (;;) {
                    J m;
                    if (!value(m)) {
                        ok = false;
                        break;
                    }
                    v.items.push_back(std::move(m));
                    ws();
                    if (i < t.size() && t[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < t.size() && t[i] == ']') {
                        ++i;
                        break;
                    }
                    ok = fail("a missing ',' or ']'");
                    break;
                }
            }
        } else if (c == '"') {
            v.kind = J::STRING;
            ok = string(v.s);
        } else if (t.compare(i, 4, "true") == 0) {
            v.kind = J::BOOL, v.b = true, i += 4;
        } else if (t.compare(i, 5, "false") == 0) {
            v.kind = J::BOOL, i += 5;
        } else if (t.compare(i, 4, "null") == 0) {
            v.kind = J::NULLV, i += 4;
        } else {
            size_t end = i;
            while (end < t.size() && (std::isdigit(static_cast<unsigned char>(t[end])) || t[end] == '-' || t[end] == '.' ||
                                      t[end] == 'e' || t[end] == 'E' || t[end] == '+'))
                ++end;
            if (end == i) {
                ok = fail("an unknown value");
            } else {
                const std::string number(t.substr(i, end - i));
                char* stop = nullptr;
                v.kind = J::NUMBER, v.d = std::strtod(number.c_str(), &stop);
                if (stop != number.c_str() + number.size()) ok = fail("a bad number");
                i = end;
            }
        }
        --depth;
        return ok;
    }
};

bool parseJson(std::string_view text, J& out, std::string& error) {
    JParser p{text};
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") p.i = 3;  // (a byte order mark)
    if (!p.value(out)) return error = p.error, false;
    p.ws();
    if (p.i != text.size()) return error = "text after the document at byte " + std::to_string(p.i), false;
    return true;
}

void quoteJson(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                out += b;
            } else {
                out += char(c);
            }
        }
    }
    out += '"';
}

// The document as text: one value a line, one space an indent (as Hyperion's own maps are written).
void writeJson(std::string& out, const J& v, int indent) {
    switch (v.kind) {
    case J::NONE:
    case J::NULLV: out += "null"; break;
    case J::BOOL: out += v.b ? "true" : "false"; break;
    case J::NUMBER: {
        char b[40];
        if (v.d == double(static_cast<long long>(v.d))) std::snprintf(b, sizeof b, "%lld", static_cast<long long>(v.d));
        else std::snprintf(b, sizeof b, "%.17g", v.d);
        out += b;
        break;
    }
    case J::STRING: quoteJson(out, v.s); break;
    case J::ARRAY:
        if (v.items.empty()) {
            out += "[]";
            break;
        }
        out += "[";
        for (size_t k = 0; k < v.items.size(); ++k) {
            out += k ? ",\n" : "\n";
            out.append(size_t(indent + 1), ' ');
            writeJson(out, v.items[k], indent + 1);
        }
        out += "\n";
        out.append(size_t(indent), ' ');
        out += "]";
        break;
    case J::OBJECT:
        if (v.members.empty()) {
            out += "{}";
            break;
        }
        out += "{";
        for (size_t k = 0; k < v.members.size(); ++k) {
            out += k ? ",\n" : "\n";
            out.append(size_t(indent + 1), ' ');
            quoteJson(out, v.members[k].first);
            out += ": ";
            writeJson(out, v.members[k].second, indent + 1);
        }
        out += "\n";
        out.append(size_t(indent), ' ');
        out += "}";
        break;
    }
}

// --- Zip --------------------------------------------------------------------------------------------------------

uint32_t le(std::span<const uint8_t> d, size_t at, int n) {
    uint32_t v = 0;
    for (int k = 0; k < n; ++k) v |= uint32_t(d[at + size_t(k)]) << (8 * k);
    return v;
}

struct ZipEntry {
    std::string name;
    uint32_t method = 0, packed = 0, size = 0, local = 0;
};

bool zipDirectory(std::span<const uint8_t> data, std::vector<ZipEntry>& out) {
    if (data.size() < 22) return false;
    size_t end = data.size() - 22;  // (the end of the central directory: its signature in the last 64 KiB)
    for (;; --end) {
        if (le(data, end, 4) == 0x06054b50) break;
        if (end == 0 || data.size() - end > 65557) return false;
    }
    const uint32_t count = le(data, end + 10, 2);
    size_t p = le(data, end + 16, 4);
    for (uint32_t k = 0; k < count; ++k) {
        if (p + 46 > data.size() || le(data, p, 4) != 0x02014b50) return false;
        ZipEntry e;
        e.method = le(data, p + 10, 2), e.packed = le(data, p + 20, 4), e.size = le(data, p + 24, 4);
        const uint32_t nameLen = le(data, p + 28, 2), extraLen = le(data, p + 30, 2), commentLen = le(data, p + 32, 2);
        e.local = le(data, p + 42, 4);
        if (p + 46 + nameLen > data.size()) return false;
        e.name.assign(reinterpret_cast<const char*>(&data[p + 46]), nameLen);
        std::replace(e.name.begin(), e.name.end(), '\\', '/');
        out.push_back(std::move(e));
        p += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

bool zipRead(std::span<const uint8_t> data, const ZipEntry& e, Bytes& out) {
    if (size_t(e.local) + 30 > data.size() || le(data, e.local, 4) != 0x04034b50) return false;
    const size_t start = e.local + 30 + le(data, e.local + 26, 2) + le(data, e.local + 28, 2);
    if (start + e.packed > data.size()) return false;
    out.assign(e.size, 0);
    if (e.method == 0) {
        if (e.packed != e.size) return false;
        std::copy(data.begin() + std::ptrdiff_t(start), data.begin() + std::ptrdiff_t(start + e.size), out.begin());
        return true;
    }
    if (e.method != 8) return false;
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
    z.next_in = const_cast<uint8_t*>(&data[start]), z.avail_in = e.packed;
    uint8_t none = 0;
    z.next_out = out.empty() ? &none : out.data(), z.avail_out = e.size;
    const int r = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    return r == Z_STREAM_END && z.total_out == e.size;
}

// A zip written entry by entry: each deflated, or stored when that is not smaller.
struct ZipWriter {
    Bytes out, central;
    uint16_t count = 0;

    static void put(Bytes& b, uint32_t v, int n) {
        for (int k = 0; k < n; ++k) b.push_back(uint8_t(v >> (8 * k)));
    }
    bool add(const std::string& name, const Bytes& data) {
        Bytes packed(deflateBound(nullptr, uLong(data.size())) + 16);
        z_stream z{};
        if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
        uint8_t none = 0;
        z.next_in = data.empty() ? &none : const_cast<uint8_t*>(data.data()), z.avail_in = uInt(data.size());
        z.next_out = packed.data(), z.avail_out = uInt(packed.size());
        const int r = deflate(&z, Z_FINISH);
        const size_t n = z.total_out;
        deflateEnd(&z);
        if (r != Z_STREAM_END) return false;
        packed.resize(n);
        const bool store = packed.size() >= data.size();
        const Bytes& body = store ? data : packed;
        const uint32_t crc = uint32_t(crc32(0, data.empty() ? &none : data.data(), uInt(data.size())));
        const uint32_t local = uint32_t(out.size());
        const uint16_t method = store ? 0 : 8, time = 0, date = (1 << 5) | 1;  // (1980-01-01: the same bytes each save)
        put(out, 0x04034b50, 4), put(out, 20, 2), put(out, 0x800, 2), put(out, method, 2), put(out, time, 2);
        put(out, date, 2), put(out, crc, 4), put(out, uint32_t(body.size()), 4), put(out, uint32_t(data.size()), 4);
        put(out, uint32_t(name.size()), 2), put(out, 0, 2);
        out.insert(out.end(), name.begin(), name.end());
        out.insert(out.end(), body.begin(), body.end());
        put(central, 0x02014b50, 4), put(central, 20, 2), put(central, 20, 2), put(central, 0x800, 2);
        put(central, method, 2), put(central, time, 2), put(central, date, 2), put(central, crc, 4);
        put(central, uint32_t(body.size()), 4), put(central, uint32_t(data.size()), 4), put(central, uint32_t(name.size()), 2);
        put(central, 0, 2), put(central, 0, 2), put(central, 0, 2), put(central, 0, 2), put(central, 0, 4);
        put(central, local, 4);
        central.insert(central.end(), name.begin(), name.end());
        ++count;
        return out.size() < 0xFFFFFFFFull && count < 0xFFFF;
    }
    Bytes finish() {
        const uint32_t at = uint32_t(out.size());
        out.insert(out.end(), central.begin(), central.end());
        put(out, 0x06054b50, 4), put(out, 0, 2), put(out, 0, 2), put(out, count, 2), put(out, count, 2);
        put(out, uint32_t(central.size()), 4), put(out, at, 4), put(out, 0, 2);
        return std::move(out);
    }
};

// --- Text --------------------------------------------------------------------------------------------------------

bool utf8Decode(const std::string& s, std::vector<unsigned>* out) {
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = uint8_t(s[i]);
        unsigned cp;
        int n;
        if (c < 0x80) cp = c, n = 1;
        else if ((c & 0xE0) == 0xC0) cp = c & 0x1F, n = 2;
        else if ((c & 0xF0) == 0xE0) cp = c & 0x0F, n = 3;
        else if ((c & 0xF8) == 0xF0) cp = c & 0x07, n = 4;
        else return false;
        if (i + size_t(n) > s.size()) return false;
        for (int k = 1; k < n; ++k) {
            const uint8_t d = uint8_t(s[i + size_t(k)]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        // (Python's decoder refuses overlong forms and surrogates)
        if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        out->push_back(cp);
        i += size_t(n);
    }
    return true;
}

// cp1252's bytes 0x80..0x9F; 0 where it has none (kept as the code point of the same number).
unsigned cp1252High(uint8_t b) {
    static const unsigned k[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                   0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                   0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};
    return k[b - 0x80];
}

unsigned legacyChar(uint8_t b) {
    if (b < 0x80 || b >= 0xA0) return b;
    const unsigned u = cp1252High(b);
    return u ? u : b;
}

// decode_text: UTF-8 when the bytes are UTF-8, the legacy code page otherwise.
std::vector<unsigned> decodeText(const Bytes& raw, bool* legacy = nullptr) {
    std::vector<unsigned> out;
    if (legacy) *legacy = false;
    if (utf8Decode(std::string(raw.begin(), raw.end()), &out)) return out;
    if (legacy) *legacy = true;
    out.clear();
    for (uint8_t b : raw) out.push_back(legacyChar(b));
    return out;
}

void putN(Bytes* p, unsigned long long v, size_t n) {
    for (size_t k = 0; k < n; ++k) p->push_back(uint8_t(v >> (8 * k)));
}
void setN(Bytes& b, size_t at, unsigned long long v, size_t n) {
    for (size_t k = 0; k < n; ++k) b[at + k] = uint8_t(v >> (8 * k));
}

Bytes base64Decode(const std::string& s) {
    Bytes out;
    unsigned acc = 0;
    int bits = 0;
    for (char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else continue;
        acc = (acc << 6) | unsigned(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t(acc >> bits));
        }
    }
    return out;
}

std::string base64Encode(std::span<const uint8_t> d) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 3 <= d.size(); i += 3) {
        const unsigned v = unsigned(d[i]) << 16 | unsigned(d[i + 1]) << 8 | d[i + 2];
        out += k[v >> 18], out += k[v >> 12 & 63], out += k[v >> 6 & 63], out += k[v & 63];
    }
    if (d.size() - i == 1) {
        const unsigned v = unsigned(d[i]) << 16;
        out += k[v >> 18], out += k[v >> 12 & 63], out += "==";
    } else if (d.size() - i == 2) {
        const unsigned v = unsigned(d[i]) << 16 | unsigned(d[i + 1]) << 8;
        out += k[v >> 18], out += k[v >> 12 & 63], out += k[v >> 6 & 63], out += '=';
    }
    return out;
}

using Layers = std::vector<std::pair<std::string, Bytes>>;

// --- The compile: Hyperion's to_chk, step by step (port_hxm.cpp, Compile) -----------------------------------------

struct Compile {
    std::string error;
    bool bHx = false;            // the map has players past 7: HXPL, and units in slots past 11
    int nSlots = NEUTRAL + 1;    // the map's slots, neutral's among them
    static const int kTrigHxCode = 0x100;
    std::vector<unsigned long long> hxte;  // TRIG's triggers' slots past 11 (HXTE)
    bool bModUnits = false;      // the map names a mod's units (StarDraft reads none: an unknown unit)
    bool bClassic = false;       // the document's limits are classic: never a Hyperion map

    void Fail(const std::string& why) {
        if (error.empty()) error = why;
    }

    static bool IsDigits(const std::string& s) {
        if (s.empty()) return false;
        for (char c : s)
            if (c < '0' || c > '9') return false;
        return true;
    }

    // A size compat keeps, in bytes or strings, refused past what any map holds.
    size_t Size(const J& v, long long dflt, const char* what) {
        const long long n = Int(v, dflt);
        if (n >= 0 && n <= (16 << 20)) return size_t(n);
        Fail(std::string("compat's ") + what + " is past what a map holds");
        return 0;
    }

    static long long Int(const J& v, long long dflt = 0) {
        if (v.kind == J::NUMBER) return static_cast<long long>(v.d);
        if (v.kind == J::BOOL) return v.b ? 1 : 0;
        return dflt;
    }

    template <size_t N>
    int Name(const HxmName (&table)[N], const J& v, const char* what, int dflt = 0) {
        if (v.kind == J::NONE || v.kind == J::NULLV) return dflt;
        if (v.kind == J::NUMBER) return static_cast<int>(v.d);
        if (v.kind == J::STRING) {
            for (size_t k = 0; k < N; ++k)
                if (v.s == table[k].psz) return table[k].n;
            if (IsDigits(v.s)) return std::atoi(v.s.c_str());
        }
        Fail(std::string("unknown ") + what + " " + (v.kind == J::STRING ? v.s : "value"));
        return dflt;
    }

    // The document's player or group as CHK's number.
    int Player(const J& v) {
        if (v.kind == J::NUMBER) {
            const int n = static_cast<int>(v.d);
            if (n >= NEUTRAL) Fail("a player past slot 10 does not fit a classic map");
            return n;
        }
        if (v.kind == J::STRING) {
            for (const HxmName& g : kHxmGroups)
                if (v.s == g.psz) return g.n;
            if (v.s.compare(0, 6, "group_") == 0 && IsDigits(v.s.substr(6))) return std::atoi(v.s.c_str() + 6);
        }
        Fail("unknown player or group " + v.s);
        return 0;
    }

    // A trigger's player or group: as Player, and on a Hyperion map a slot past 11 as the code 0x100 + slot.
    int TrigPlayer(const J& v) {
        if (bHx && v.kind == J::NUMBER && v.d > NEUTRAL) {
            if (v.d >= nSlots || v.d != static_cast<int>(v.d)) Fail("a trigger names a slot the map does not have");
            return kTrigHxCode + static_cast<int>(v.d);
        }
        return Player(v);
    }

    int Owner(const J& v) {
        if (v.kind == J::NONE) return 0;
        if (bHx && v.kind == J::NUMBER && v.d > NEUTRAL && v.d < PLAYER_SLOTS_MAX) return static_cast<int>(v.d);
        return Player(v);
    }

    static J named(const char* s) { return J::str(s); }

    // HXPL: the slots from 8 on, document index i as slot i up to 10 and slot i + 1 after, slot 11's entry neutral's;
    // the colour 0xFF where the document names none.
    Bytes Hxpl(const J& players) {
        const size_t n = players.size() > size_t(NEUTRAL) ? players.size() + 1 : players.size();
        Bytes body;
        putN(&body, 1, 4);
        putN(&body, n, 4);
        const J inactive = named("inactive"), zerg = named("zerg");
        for (size_t s = PLAYER_LOBBY_117; s < n; ++s) {
            if (s == size_t(NEUTRAL)) {
                putN(&body, 0, 4);
                continue;
            }
            const J& p = players.At(s < size_t(NEUTRAL) ? s : s - 1);
            body.push_back(uint8_t(Name(kHxmSlots, p["slot"].kind != J::NONE ? p["slot"] : inactive, "slot")));
            body.push_back(uint8_t(Name(kHxmRaces, p["race"].kind != J::NONE ? p["race"] : zerg, "race")));
            body.push_back(uint8_t(Int(p["force"])));
            body.push_back(uint8_t(p["color"].kind == J::NONE ? 0xFF : Name(kHxmColors, p["color"], "colour")));
        }
        return body;
    }

    // HXRG: the RGB colours of the slots from 8 on, as HXPL lays them out; empty when none has one.
    Bytes Hxrg(const J& players) {
        const size_t n = players.size() > size_t(NEUTRAL) ? players.size() + 1 : players.size();
        bool any = false;
        Bytes body;
        putN(&body, 1, 4);
        putN(&body, n, 4);
        for (size_t s = PLAYER_LOBBY_117; s < n; ++s) {
            const J* rgb = s == size_t(NEUTRAL) ? nullptr : &players.At(s < size_t(NEUTRAL) ? s : s - 1)["rgb"];
            const bool set = rgb != nullptr && rgb->kind == J::ARRAY && rgb->size() == 3;
            body.push_back(set ? 1 : 0);
            for (size_t k = 0; k < 3; ++k) body.push_back(uint8_t(set ? Int(rgb->At(k)) : 0));
            any = any || set;
        }
        return any ? body : Bytes();
    }

    // A player key of an availability table: a number, or a name.
    int PlayerKey(const std::string& k) {
        if (IsDigits(k)) return std::atoi(k.c_str());
        return Player(J::str(k));
    }

    // ---- text

    std::string encoding;
    std::vector<Bytes> strings;  // id - 1
    std::vector<bool> nowhere;   // the id points nowhere
    std::vector<std::pair<Bytes, int>> ids;
    std::vector<long long> reserved;

    Bytes EncodeText(const std::string& text) {
        Bytes utf8(text.begin(), text.end());
        if (encoding == "utf-8") return utf8;
        std::vector<unsigned> cps;
        if (!utf8Decode(text, &cps)) return utf8;
        Bytes out;
        for (unsigned cp : cps) {
            int b = -1;
            if (cp < 0x80 || (cp >= 0xA0 && cp < 0x100)) {
                b = int(cp);
            } else {
                for (int k = 0x80; k < 0xA0; ++k)
                    if (cp1252High(uint8_t(k)) == cp) b = k;
                if (b < 0 && cp < 0x100) b = int(cp);
            }
            if (b < 0) return utf8;
            out.push_back(uint8_t(b));
        }
        if (decodeText(out) == cps) return out;
        return utf8;
    }

    bool IsReserved(long long n) const { return std::find(reserved.begin(), reserved.end(), n) != reserved.end(); }

    uint32_t Text(const J& v) {
        if (v.kind == J::NONE || v.kind == J::NULLV) return 0;
        if (v.kind == J::OBJECT) return uint32_t(Int(v["string_id"]));
        if (v.kind != J::STRING) {
            Fail("a text field holds something other than text");
            return 0;
        }
        const Bytes raw = EncodeText(v.s);
        for (const auto& e : ids)
            if (e.first == raw) return uint32_t(e.second);
        while (IsReserved(static_cast<long long>(strings.size()) + 1)) {
            strings.push_back(Bytes());
            nowhere.push_back(true);
        }
        strings.push_back(raw);
        nowhere.push_back(false);
        ids.push_back({raw, int(strings.size())});
        return uint32_t(strings.size());
    }

    void RawStringIds(const J& v) {
        if (v.kind == J::OBJECT) {
            if (v.members.size() == 1 && v.members[0].first == "string_id") reserved.push_back(Int(v.members[0].second));
            for (const JMember& m : v.members) RawStringIds(m.second);
        } else if (v.kind == J::ARRAY) {
            for (const J& k : v.items) RawStringIds(k);
        }
    }

    // chk.write_strings
    bool WriteStrings(bool wide, size_t count, Bytes* out) {
        const size_t width = wide ? 4 : 2;
        const size_t n = strings.size() > count ? strings.size() : count;
        const size_t head = width + n * width;
        Bytes data(1, 0);
        std::vector<std::pair<Bytes, size_t>> at;
        at.push_back({Bytes(), 0});
        std::vector<uint32_t> offsets;
        const uint32_t none = wide ? 0xFFFFFFFF : 0xFFFF;
        for (size_t k = 0; k < n; ++k) {
            if (k < strings.size() && nowhere[k]) {
                offsets.push_back(none);
                continue;
            }
            const Bytes s = k < strings.size() ? strings[k] : Bytes();
            size_t off = SIZE_MAX;
            for (const auto& a : at)
                if (a.first == s) off = a.second;
            if (off == SIZE_MAX) {
                off = data.size();
                at.push_back({s, off});
                data.insert(data.end(), s.begin(), s.end());
                data.push_back(0);
            }
            offsets.push_back(uint32_t(head + off));
        }
        if (!wide && head + data.size() > 0x10000) return false;
        out->clear();
        putN(out, n, width);
        for (uint32_t o : offsets) putN(out, o, width);
        out->insert(out->end(), data.begin(), data.end());
        return true;
    }

    // ---- sections

    struct UnitField {
        const char* psz;
        size_t at, n;
        long long dflt;
    };
    static constexpr UnitField kUnitFields[] = {{"valid_state", 0x0C, 2, 0x1F}, {"valid_props", 0x0E, 2, 0x3F}, {"hp", 0x11, 1, 100},
                                                {"shields", 0x12, 1, 100},     {"energy", 0x13, 1, 100},     {"resources", 0x14, 4, 0},
                                                {"hangar", 0x18, 2, 0},        {"state", 0x1A, 2, 0},        {"unused", 0x1C, 4, 0}};

    Bytes Units(const J& units) {
        Bytes out;
        for (size_t k = 0; k < units.size(); ++k) {
            const J& u = units.At(k);
            Bytes e(36, 0);
            setN(e, 0, Int(u["id"], static_cast<long long>(k)), 4);
            setN(e, 4, Int(u["x"]), 2);
            setN(e, 6, Int(u["y"]), 2);
            if (u["type"].kind == J::NONE) Fail("a unit has no type");
            setN(e, 8, Name(kHxmUnits, u["type"], "unit"), 2);
            setN(e, 10, Int(u["relation"]), 2);
            e[0x10] = uint8_t(Owner(u["owner"]));
            for (const UnitField& f : kUnitFields) setN(e, f.at, Int(u[f.psz], f.dflt), f.n);
            setN(e, 0x20, Int(u["link"]), 4);
            out.insert(out.end(), e.begin(), e.end());
        }
        return out;
    }

    Bytes Doodads(const J& doodads) {
        Bytes out;
        for (size_t k = 0; k < doodads.size(); ++k) {
            const J& d = doodads.At(k);
            long long kind, flags = Int(d["flags"]);
            if (d["sprite"].kind != J::NONE) {
                kind = Int(d["sprite"]);
                flags |= 0x1000;
            } else {
                kind = Name(kHxmUnits, d["unit"], "unit");
            }
            putN(&out, kind, 2);
            putN(&out, Int(d["x"]), 2);
            putN(&out, Int(d["y"]), 2);
            putN(&out, Owner(d["owner"]), 1);
            putN(&out, Int(d["pad"]), 1);
            putN(&out, flags, 2);
        }
        return out;
    }

    Bytes Locations(const J& locs, int version, const J& compat) {
        const size_t count = version == 59 ? 64 : 255;
        Bytes body(count * 20, 0);
        for (size_t k = 0; k < locs.size(); ++k) {
            const J& l = locs.At(k);
            const long long id = Int(l["id"]);
            if (id < 1 || size_t(id) > count) {
                Fail("a location past the map's count");
                continue;
            }
            const size_t at = size_t(id - 1) * 20;
            setN(body, at, Int(l["left"]), 4);
            setN(body, at + 4, Int(l["top"]), 4);
            setN(body, at + 8, Int(l["right"]), 4);
            setN(body, at + 12, Int(l["bottom"]), 4);
            setN(body, at + 16, Text(l["name"]), 2);
            setN(body, at + 18, Int(l["elevation"]), 2);
        }
        if (compat["mrgn_bytes"].kind == J::NUMBER) body.resize(Size(compat["mrgn_bytes"], 0, "mrgn_bytes"), 0);
        return body;
    }

    struct Field {
        const char* psz;
        size_t at, n;
    };

    Bytes Condition(const J& c) {
        static const Field k[] = {{"location", 0x00, 4}, {"player", 0x04, 4},     {"amount", 0x08, 4},
                                  {"unit", 0x0C, 2},     {"comparison", 0x0E, 1}, {"type", 0x0F, 1},
                                  {"resource", 0x10, 1}, {"flags", 0x11, 1},      {"mask", 0x12, 2}};
        Bytes e(20, 0);
        for (const Field& f : k) {
            const J& v = c[f.psz];
            long long n;
            if (!std::strcmp(f.psz, "type")) n = Name(kHxmConditions, v, "condition");
            else if (!std::strcmp(f.psz, "player")) n = (v.kind == J::NONE || (v.kind == J::NUMBER && v.d == 0)) ? 0 : TrigPlayer(v);
            else if (!std::strcmp(f.psz, "unit")) n = Name(kHxmUnits, v, "unit");
            else if (!std::strcmp(f.psz, "comparison")) n = Name(kHxmComparisons, v, "comparison");
            else n = Int(v);
            setN(e, f.at, n, f.n);
        }
        return e;
    }

    Bytes Action(const J& a, bool briefing) {
        static const Field k[] = {{"location", 0x00, 4}, {"text", 0x04, 4},  {"wav", 0x08, 4},      {"time", 0x0C, 4},
                                  {"player", 0x10, 4},   {"number", 0x14, 4}, {"unit", 0x18, 2},    {"type", 0x1A, 1},
                                  {"modifier", 0x1B, 1}, {"flags", 0x1C, 1},  {"pad", 0x1D, 1},     {"mask", 0x1E, 2}};
        const int t = briefing ? Name(kHxmBriefingActions, a["type"], "briefing action") : Name(kHxmActions, a["type"], "action");
        Bytes e(32, 0);
        for (const Field& f : k) {
            const J& v = a[f.psz];
            const bool zero = v.kind == J::NONE || (v.kind == J::NUMBER && v.d == 0);
            long long n;
            if (!std::strcmp(f.psz, "type")) n = t;
            else if (!std::strcmp(f.psz, "text") || !std::strcmp(f.psz, "wav")) n = Text(v);
            else if (!std::strcmp(f.psz, "player")) n = zero ? 0 : (briefing ? Player(v) : TrigPlayer(v));
            else if (!std::strcmp(f.psz, "number") && !briefing && t == 48) n = zero ? 0 : TrigPlayer(v);
            else if (!std::strcmp(f.psz, "unit")) n = Name(kHxmUnits, v, "unit");
            else n = Int(v);
            setN(e, f.at, n, f.n);
        }
        return e;
    }

    Bytes Triggers(const J& trigs, bool briefing) {
        Bytes out;
        for (size_t k = 0; k < trigs.size(); ++k) {
            const J& t = trigs.At(k);
            Bytes body(2400, 0);
            const J &conds = t["conditions"], &acts = t["actions"];
            if (conds.size() > 16 || acts.size() > 64) Fail("a trigger holds 16 conditions and 64 actions");
            for (size_t j = 0; j < conds.size() && j < 16; ++j) {
                const Bytes c = Condition(conds.At(j));
                std::memcpy(&body[j * 20], c.data(), 20);
            }
            for (size_t j = 0; j < acts.size() && j < 64; ++j) {
                const Bytes a = Action(acts.At(j), briefing);
                std::memcpy(&body[320 + j * 32], a.data(), 32);
            }
            setN(body, 2368, Int(t["flags"]), 4);
            const J& players = t["players"];
            unsigned long long hx = 0;
            for (size_t j = 0; j < players.size(); ++j) {
                const J& v = players.At(j);
                if (!briefing && bHx && v.kind == J::NUMBER && v.d > NEUTRAL) {
                    if (v.d >= nSlots || v.d >= 64 || v.d != static_cast<int>(v.d))
                        Fail("a trigger runs for a slot the map does not have, or one past 63");
                    else
                        hx |= 1ull << static_cast<int>(v.d);
                    continue;
                }
                const int p = Player(v);
                if (p >= 0 && p < 28) body[2372 + size_t(p)] = 1;
            }
            if (!briefing) hxte.push_back(hx);
            for (const JMember& m : t["player_bytes"].members) {
                const int p = std::atoi(m.first.c_str());
                if (p >= 0 && p < 28) body[2372 + size_t(p)] = uint8_t(Int(m.second));
            }
            body[2399] = uint8_t(Int(t["current_action"]));
            out.insert(out.end(), body.begin(), body.end());
        }
        return out;
    }

    Bytes Forces(const J& forces, const J& players) {
        Bytes body(20, 0);
        for (size_t k = 0; k < players.size() && k < 8; ++k) body[k] = uint8_t(Int(players.At(k)["force"]));
        for (size_t f = 0; f < forces.size() && f < 4; ++f) {
            const J& force = forces.At(f);
            setN(body, 8 + f * 2, Text(force["name"]), 2);
            body[16 + f] = uint8_t(Flags(force["flags"]));
        }
        return body;
    }

    int Flags(const J& items) {
        int v = 0;
        for (size_t k = 0; k < items.size(); ++k) v |= Name(kHxmForceFlags, items.At(k), "force flag");
        return v;
    }

    Bytes Uprp(const J& props) {
        Bytes body(0x500, 0);
        for (size_t k = 0; k < props.size(); ++k) {
            const J& p = props.At(k);
            if (p["meta"].kind != J::NONE) continue;
            const long long slot = Int(p["slot"]);
            if (slot < 1 || slot > 64) continue;
            const size_t at = size_t(slot - 1) * 20;
            setN(body, at, Int(p["valid_state"]), 2);
            setN(body, at + 2, Int(p["valid_props"]), 2);
            body[at + 4] = uint8_t(Int(p["owner"]));
            body[at + 5] = uint8_t(Int(p["hp"]));
            body[at + 6] = uint8_t(Int(p["shields"]));
            body[at + 7] = uint8_t(Int(p["energy"]));
            setN(body, at + 8, Int(p["resources"]), 4);
            setN(body, at + 12, Int(p["hangar"]), 2);
            setN(body, at + 14, Int(p["state"]), 2);
            setN(body, at + 16, Int(p["unused"]), 4);
        }
        return body;
    }

    Bytes Upus(const J& props, size_t size) {
        Bytes body(size > 64 ? size : 64, 0);
        for (size_t k = 0; k < props.size(); ++k) {
            const J& p = props.At(k);
            const J& used = p["used"];
            const long long slot = Int(p["slot"]);
            if (slot < 1 || slot > 64 || used.kind == J::NONE) continue;
            if (used.kind == J::BOOL ? used.b : Int(used) != 0) body[size_t(slot - 1)] = uint8_t(used.kind == J::BOOL ? 1 : Int(used));
        }
        body.resize(size);
        return body;
    }

    Bytes Unis(const J& s, bool bw) {
        const size_t nWeap = bw ? 130 : 100;
        Bytes body(0xFD0 + (nWeap - 100) * 4, 0);
        std::memset(body.data(), 1, 228);
        for (const JMember& m : s["units"].members) {
            const int k = Name(kHxmUnits, J::str(m.first), "unit");
            if (k < 0 || k >= 228) continue;
            const size_t i = size_t(k);
            const J& u = m.second;
            body[i] = 0;
            setN(body, 0xE4 + i * 4, Int(u["hp"]), 4);
            setN(body, 0x474 + i * 2, Int(u["shields"]), 2);
            body[0x63C + i] = uint8_t(Int(u["armor"]));
            setN(body, 0x720 + i * 2, Int(u["build_time"]), 2);
            setN(body, 0x8E8 + i * 2, Int(u["minerals"]), 2);
            setN(body, 0xAB0 + i * 2, Int(u["gas"]), 2);
            setN(body, 0xC78 + i * 2, Text(u["name"]), 2);
        }
        for (const JMember& m : s["weapons"].members) {
            const int w = std::atoi(m.first.c_str());
            if (w < 0 || size_t(w) >= nWeap) continue;
            setN(body, 0xE40 + size_t(w) * 2, Int(m.second.At(0)), 2);
            setN(body, 0xE40 + nWeap * 2 + size_t(w) * 2, Int(m.second.At(1)), 2);
        }
        return body;
    }

    static constexpr const char* kUpgradeFields[] = {"minerals", "minerals_factor", "gas", "gas_factor", "time", "time_factor"};
    static constexpr const char* kTechFields[] = {"minerals", "gas", "time", "energy"};

    Bytes Upgs(const J& s, bool bw) {
        const size_t n = bw ? 61 : 46, base = n + (bw ? 1 : 0);
        Bytes body(base + n * 12, 0);
        std::memset(body.data(), 1, n);
        for (const JMember& m : s.members) {
            const int k = Name(kHxmUpgrades, J::str(m.first), "upgrade");
            if (k < 0 || size_t(k) >= n) continue;
            body[size_t(k)] = 0;
            for (size_t f = 0; f < 6; ++f) setN(body, base + f * n * 2 + size_t(k) * 2, Int(m.second[kUpgradeFields[f]]), 2);
        }
        return body;
    }

    Bytes Tecs(const J& s, bool bw) {
        const size_t n = bw ? 44 : 24;
        Bytes body(n + n * 8, 0);
        std::memset(body.data(), 1, n);
        for (const JMember& m : s.members) {
            const int k = Name(kHxmTechs, J::str(m.first), "tech");
            if (k < 0 || size_t(k) >= n) continue;
            body[size_t(k)] = 0;
            for (size_t f = 0; f < 4; ++f) setN(body, n + f * n * 2 + size_t(k) * 2, Int(m.second[kTechFields[f]]), 2);
        }
        return body;
    }

    // fill(): a run of n bytes at off set to dflt, then the listed values over it.
    template <size_t N>
    void Fill(Bytes& body, size_t off, size_t n, int dflt, const J& values, const HxmName (&table)[N], const char* what) {
        std::memset(&body[off], dflt, n);
        for (const JMember& m : values.members) {
            const int k = Name(table, J::str(m.first), what);
            if (k >= 0 && size_t(k) < n) body[off + size_t(k)] = uint8_t(Int(m.second));
        }
    }

    Bytes Puni(const J& a) {
        Bytes body(5700, 0);
        const J& none = J::none();
        for (size_t p = 0; p < 12; ++p) {
            Fill(body, p * 228, 228, 1, none, kHxmUnits, "unit");
            Fill(body, 12 * 228 + 228 + p * 228, 228, 1, none, kHxmUnits, "unit");
        }
        Fill(body, 12 * 228, 228, 1, a["global"], kHxmUnits, "unit");
        for (const JMember& m : a["players"].members) {
            const int p = PlayerKey(m.first);
            if (p < 0 || p >= 12) continue;
            Fill(body, size_t(p) * 228, 228, 1, m.second["available"], kHxmUnits, "unit");
            Fill(body, 12 * 228 + 228 + size_t(p) * 228, 228, 1, m.second["use_global"], kHxmUnits, "unit");
        }
        return body;
    }

    Bytes Pupx(const J& a, size_t n) {
        Bytes body(38 * n, 0);
        const J& none = J::none();
        for (size_t p = 0; p < 12; ++p) Fill(body, 26 * n + p * n, n, 1, none, kHxmUpgrades, "upgrade");
        Fill(body, 24 * n, n, 0, a["global_max"], kHxmUpgrades, "upgrade");
        Fill(body, 25 * n, n, 0, a["global_start"], kHxmUpgrades, "upgrade");
        for (const JMember& m : a["players"].members) {
            const int p = PlayerKey(m.first);
            if (p < 0 || p >= 12) continue;
            Fill(body, size_t(p) * n, n, 0, m.second["max"], kHxmUpgrades, "upgrade");
            Fill(body, 12 * n + size_t(p) * n, n, 0, m.second["start"], kHxmUpgrades, "upgrade");
            Fill(body, 26 * n + size_t(p) * n, n, 1, m.second["use_global"], kHxmUpgrades, "upgrade");
        }
        return body;
    }

    Bytes Ptex(const J& a, size_t n) {
        Bytes body(38 * n, 0);
        const J& none = J::none();
        for (size_t p = 0; p < 12; ++p) Fill(body, 26 * n + p * n, n, 1, none, kHxmTechs, "tech");
        Fill(body, 24 * n, n, 0, a["global_available"], kHxmTechs, "tech");
        Fill(body, 25 * n, n, 0, a["global_researched"], kHxmTechs, "tech");
        for (const JMember& m : a["players"].members) {
            const int p = PlayerKey(m.first);
            if (p < 0 || p >= 12) continue;
            Fill(body, size_t(p) * n, n, 0, m.second["available"], kHxmTechs, "tech");
            Fill(body, 12 * n + size_t(p) * n, n, 0, m.second["researched"], kHxmTechs, "tech");
            Fill(body, 26 * n + size_t(p) * n, n, 1, m.second["use_global"], kHxmTechs, "tech");
        }
        return body;
    }

    Bytes Crgb(const std::vector<const J*>& players) {
        Bytes body(32, 0);
        for (size_t k = 0; k < 8; ++k) {
            const J& p = *players[k];
            const J& rgb = p["rgb"];
            for (size_t c = 0; c < 3; ++c) body[k * 3 + c] = uint8_t(Int(rgb.At(c)));
            const J& mode = p["color_mode"];
            int m = 1;
            if (mode.kind == J::STRING) m = mode.s == "random" ? 0 : mode.s == "rgb" ? 2 : 1;
            else if (mode.kind == J::NUMBER) m = static_cast<int>(mode.d);
            body[24 + k] = uint8_t(m);
        }
        return body;
    }

    static const Bytes* Layer(const Layers& layers, const char* name) {
        for (const auto& l : layers)
            if (l.first == name) return &l.second;
        return nullptr;
    }

    bool Run(const J& doc, const Layers& layers, Bytes* chk) {
        if (doc["format"].Str() != "hyperion-map") {
            Fail("not a Hyperion map");
            return false;
        }
        if (Int(doc["version"], HXM_VERSION) > HXM_VERSION) {  // (a newer format may mean fields this would drop)
            Fail("format version " + std::to_string(Int(doc["version"])) + ", where this reads version " + std::to_string(HXM_VERSION));
            return false;
        }
        const J& compat = doc["compat"];
        const J& size = doc["size"];
        const long long width = Int(size.At(0)), height = Int(size.At(1));
        const bool expansion = doc["expansion"].kind == J::BOOL ? doc["expansion"].b : true;
        encoding = doc["text_encoding"].Str("utf-8");
        RawStringIds(doc);
        bModUnits = false;
        const bool converted = compat["chk_version"].kind == J::NUMBER;
        int version = converted ? static_cast<int>(Int(compat["chk_version"])) : (expansion ? 205 : 63);

        struct Part {
            std::string tag;
            Bytes body;
        };
        std::vector<Part> parts, later;
        auto add = [](std::vector<Part>& v, const std::string& tag, const Bytes& b) { v.push_back(Part{tag, b}); };
        auto word = [](long long w) {
            Bytes b;
            putN(&b, w, 2);
            return b;
        };

        if (!converted || compat["type"].kind == J::STRING) {
            const std::string type = compat["type"].Str(expansion ? "RAWB" : "RAWS");
            add(parts, "TYPE", Bytes(type.begin(), type.end()));
        }
        const size_t verAt = parts.size();
        add(parts, "VER ", Bytes());
        if (compat["iver"].kind == J::NUMBER) add(parts, "IVER", word(Int(compat["iver"])));
        if (compat["ive2"].kind == J::NUMBER || !converted) add(parts, "IVE2", word(Int(compat["ive2"], 11)));
        const std::string vcod = compat["vcod"].Str(converted ? "" : "standard");
        if (vcod == "standard") add(parts, "VCOD", Bytes(std::begin(kVcod), std::end(kVcod)));
        else if (!vcod.empty()) add(parts, "VCOD", base64Decode(vcod));

        // the slots: the document's players, inactive ones up to 10, then neutral; on a Hyperion map HXPL has the
        // slots from 8 on
        const J& docPlayers = doc["players"];
        bHx = false;
        const J& limits = doc["limits"];
        bClassic = limits.kind == J::STRING && limits.s == "classic";
        if (limits.kind != J::NONE && limits.kind != J::NULLV && !bClassic) {
            Fail("unknown limits " + (limits.kind == J::STRING ? limits.s : std::string("value")));
            return false;
        }
        for (size_t k = PLAYER_LOBBY_117; !bClassic && k < docPlayers.size(); ++k) {
            const J& slot = docPlayers.At(k)["slot"];
            const int n = slot.kind == J::NONE ? 0 : Name(kHxmSlots, slot, "slot");
            bHx = bHx || n == 5 || n == 6;
        }
        if (docPlayers.size() > size_t(bHx ? PLAYERS_MAX : NEUTRAL)) {
            Fail(bHx ? "more players than Hyperion holds" : "more player slots than a classic map holds");
            return false;
        }
        nSlots = docPlayers.size() > size_t(NEUTRAL) ? int(docPlayers.size()) + 1 : NEUTRAL + 1;
        if (bHx) {
            add(parts, "HXPL", Hxpl(docPlayers));
            const Bytes hxrg = Hxrg(docPlayers);
            if (!hxrg.empty()) add(parts, "HXRG", hxrg);
        }
        std::vector<const J*> players;
        const J blank = J::obj();
        for (size_t k = 0; k < docPlayers.size() && k < size_t(NEUTRAL); ++k) players.push_back(&docPlayers.At(k));
        while (players.size() < size_t(NEUTRAL)) players.push_back(&blank);
        players.push_back(doc["neutral"].kind == J::OBJECT ? &doc["neutral"] : &blank);
        Bytes ownr, side, iown;
        const J inactive = named("inactive"), zerg = named("zerg");
        for (const J* p : players) {
            const J& slot = (*p)["slot"].kind != J::NONE ? (*p)["slot"] : inactive;
            const J& race = (*p)["race"].kind != J::NONE ? (*p)["race"] : zerg;
            const J& eslot = (*p)["editor_slot"].kind != J::NONE ? (*p)["editor_slot"] : slot;
            ownr.push_back(uint8_t(Name(kHxmSlots, slot, "slot")));
            side.push_back(uint8_t(Name(kHxmRaces, race, "race")));
            iown.push_back(uint8_t(Name(kHxmSlots, eslot, "slot")));
        }
        add(parts, "IOWN", iown);
        add(parts, "OWNR", ownr);
        add(parts, "ERA ", word(Name(kHxmTilesets, doc["tileset"], "tileset")));
        Bytes dim;
        putN(&dim, width, 2);
        putN(&dim, height, 2);
        add(parts, "DIM ", dim);
        add(parts, "SIDE", side);

        const Bytes* tiles = Layer(layers, "tiles");
        Bytes mtxm = tiles ? *tiles : Bytes();
        if (compat["mtxm_bytes"].kind == J::NUMBER) {
            if (const Bytes* extra = Layer(layers, "tiles_extra")) mtxm.insert(mtxm.end(), extra->begin(), extra->end());
            mtxm.resize(Size(compat["mtxm_bytes"], 0, "mtxm_bytes"), 0);
        }
        add(parts, "MTXM", mtxm);

        const J& avail = doc["availability"];
        if (avail["units"].kind != J::NONE) add(parts, "PUNI", Puni(avail["units"]));
        if (avail["upgrades_classic"].kind != J::NONE) add(parts, "UPGR", Pupx(avail["upgrades_classic"], 46));
        if (avail["techs_classic"].kind != J::NONE) add(parts, "PTEC", Ptex(avail["techs_classic"], 24));
        add(parts, "UNIT", Units(doc["units"]));
        if (const Bytes* isom = Layer(layers, "isom")) add(parts, "ISOM", *isom);
        const J& same = doc["editor"]["tiles_same_as_game"];
        if (same.kind == J::BOOL && same.b) add(parts, "TILE", mtxm);
        else if (const Bytes* et = Layer(layers, "editor_tiles")) add(parts, "TILE", *et);
        if (const Bytes* dd2 = Layer(layers, "editor_doodads")) add(parts, "DD2 ", *dd2);
        add(parts, "THG2", Doodads(doc["doodads"]));
        if (const Bytes* fog = Layer(layers, "fog")) add(parts, "MASK", *fog);

        const J& props = doc["unit_properties"];
        const J* meta = nullptr;
        for (size_t k = 0; k < props.size(); ++k)
            if (props.At(k)["meta"].kind == J::OBJECT) meta = &props.At(k)["meta"];
        const bool noUprp = meta && (*meta)["no_uprp"].kind == J::BOOL && (*meta)["no_uprp"].b;
        const bool noUpus = meta && (*meta)["no_upus"].kind == J::BOOL && (*meta)["no_upus"].b;
        if (!noUprp) add(later, "UPRP", Uprp(props));
        if (!noUpus) add(later, "UPUS", Upus(props, meta ? Size((*meta)["upus_bytes"], 64, "upus_bytes") : 64));
        add(later, "MRGN", Locations(doc["locations"], version, compat));
        hxte.clear();
        add(later, "TRIG", Triggers(doc["triggers"], false));
        bool anyHxte = false;
        for (unsigned long long q : hxte) anyHxte = anyHxte || q != 0;
        if (anyHxte) {
            Bytes body;
            putN(&body, 1, 4);
            putN(&body, hxte.size(), 4);
            for (unsigned long long q : hxte) putN(&body, q, 8);
            add(later, "HXTE", body);
        }
        if (doc["briefing"].kind != J::NONE) add(later, "MBRF", Triggers(doc["briefing"], true));
        Bytes sprp;
        putN(&sprp, Text(doc["name"]), 2);
        putN(&sprp, Text(doc["description"]), 2);
        add(later, "SPRP", sprp);
        add(later, "FORC", Forces(doc["forces"], docPlayers));
        if (doc["sounds"].kind == J::OBJECT) {
            Bytes wav(2048, 0);
            for (const JMember& m : doc["sounds"].members) {
                const int k = std::atoi(m.first.c_str());
                if (k >= 0 && k < 512) setN(wav, size_t(k) * 4, Text(m.second), 4);
            }
            wav.resize(Size(compat["wav_bytes"], 2048, "wav_bytes"), 0);
            add(later, "WAV ", wav);
        }
        const J& settings = doc["settings"];
        if (settings["units_classic"].kind != J::NONE) add(later, "UNIS", Unis(settings["units_classic"], false));
        if (settings["upgrades_classic"].kind != J::NONE) add(later, "UPGS", Upgs(settings["upgrades_classic"], false));
        if (settings["techs_classic"].kind != J::NONE) add(later, "TECS", Tecs(settings["techs_classic"], false));
        if (doc["switches"].kind == J::OBJECT) {
            Bytes swnm(1024, 0);
            for (const JMember& m : doc["switches"].members) {
                const int k = std::atoi(m.first.c_str()) - 1;
                if (k >= 0 && k < 256) setN(swnm, size_t(k) * 4, Text(m.second), 4);
            }
            swnm.resize(Size(compat["swnm_bytes"], 1024, "swnm_bytes"), 0);
            add(later, "SWNM", swnm);
        }
        bool colors = false, crgb = false;
        for (size_t k = 0; k < 8; ++k) {
            colors = colors || (*players[k])["color"].kind != J::NONE;
            crgb = crgb || (*players[k])["color_mode"].kind != J::NONE;
        }
        if (colors) {
            Bytes colr;
            for (size_t k = 0; k < 8; ++k) {
                const J& c = (*players[k])["color"];
                colr.push_back(uint8_t(c.kind == J::NONE ? int(k) : Name(kHxmColors, c, "colour")));
            }
            add(later, "COLR", colr);
        }
        if (crgb) add(later, "CRGB", Crgb(players));
        if (avail["upgrades"].kind != J::NONE) add(later, "PUPx", Pupx(avail["upgrades"], 61));
        if (avail["techs"].kind != J::NONE) add(later, "PTEx", Ptex(avail["techs"], 44));
        if (settings["units"].kind != J::NONE) add(later, "UNIx", Unis(settings["units"], true));
        if (settings["upgrades"].kind != J::NONE) add(later, "UPGx", Upgs(settings["upgrades"], true));
        if (settings["techs"].kind != J::NONE) add(later, "TECx", Tecs(settings["techs"], true));

        // ids kept as numbers that fall inside the table point nowhere, as they did
        const size_t stringCount = Size(compat["string_count"], 0, "string_count");
        std::vector<long long> sorted = reserved;
        std::sort(sorted.begin(), sorted.end());
        for (long long n : sorted) {
            if (n < 1 || size_t(n) > stringCount) continue;
            while (strings.size() < size_t(n)) {
                strings.push_back(Bytes());
                nowhere.push_back(false);
            }
            if (!nowhere[size_t(n - 1)] && strings[size_t(n - 1)].empty()) nowhere[size_t(n - 1)] = true;
        }
        const size_t count = strings.size() > stringCount ? strings.size() : stringCount;
        bool wide = compat["strx"].kind == J::BOOL && compat["strx"].b;
        Bytes table;
        if (!WriteStrings(wide, count, &table)) {
            wide = true;
            WriteStrings(wide, count, &table);
        }
        if (wide && (version == 59 || version == 63)) version = 64;
        else if (wide && version == 205) version = 206;
        if (bClassic && (bModUnits || width > MAP_SIDE_117 || height > MAP_SIDE_117)) {
            Fail(bModUnits ? "the map's limits are classic, and it places a mod's units"
                           : "the map's limits are classic, and it is more than 256 tiles a side");
            return false;
        }
        // a map past what a classic map holds says so in its VER
        if (bHx || anyHxte || bModUnits || width > MAP_SIDE_117 || height > MAP_SIDE_117)
            version = expansion ? MAP_VERSION_HX : MAP_VERSION_HX_ORIGINAL;
        else if (version == MAP_VERSION_HX || version == MAP_VERSION_HX_ORIGINAL)
            version = expansion ? (wide ? 206 : 205) : (wide ? 64 : 63);
        // every version after 59 reads MRGN as 255 locations, so a map of 64 raised past 59 gets the rest as empty
        // ones; the 64th, Anywhere, keeps its place
        if (version != 59)
            for (Part& p : later)
                if (p.tag == "MRGN" && p.body.size() < 255 * 20) p.body.resize(255 * 20, 0);
        parts[verAt].body = word(version);
        add(parts, wide ? "STRx" : "STR ", table);
        const J& others = doc["sections"];
        for (size_t k = 0; k < others.size(); ++k) {
            // the tag's four bytes, written as Latin-1 text
            std::vector<unsigned> cps;
            std::string tag;
            if (utf8Decode(others.At(k)["tag"].Str(), &cps))
                for (unsigned cp : cps) tag.push_back(cp < 0x100 ? char(cp) : '?');
            if (tag.size() != 4) {
                Fail("a section's tag is not 4 characters");
                continue;
            }
            add(later, tag, base64Decode(others.At(k)["data"].Str()));
        }

        if (!error.empty()) return false;
        chk->clear();
        for (const std::vector<Part>* list : {&parts, &later})
            for (const Part& p : *list) {
                chk->insert(chk->end(), p.tag.begin(), p.tag.begin() + 4);
                putN(chk, p.body.size(), 4);
                chk->insert(chk->end(), p.body.begin(), p.body.end());
            }
        return true;
    }
};

// --- The conversion back: from_chk ------------------------------------------------------------------------------

template <size_t N>
J nameOf(const HxmName (&table)[N], long long n) {
    for (const HxmName& e : table)
        if (e.n == n) {
            for (const HxmName& f : table)  // (the name the compile reads back as this number: the first of its name)
                if (!std::strcmp(f.psz, e.psz)) return f.n == n ? J::str(e.psz) : J::num(n);
        }
    return J::num(n);
}

// A table key: the name, else the number as text.
template <size_t N>
std::string keyOf(const HxmName (&table)[N], long long n) {
    const J j = nameOf(table, n);
    return j.kind == J::STRING ? j.s : std::to_string(n);
}

const char* const kKnown[] = {"TYPE", "VER ", "IVER", "IVE2", "VCOD", "IOWN", "OWNR", "ERA ", "DIM ", "SIDE", "MTXM",
                              "PUNI", "UPGR", "PTEC", "UNIT", "ISOM", "TILE", "DD2 ", "THG2", "MASK", "STR ", "STRx",
                              "UPRP", "UPUS", "MRGN", "TRIG", "MBRF", "SPRP", "FORC", "WAV ", "UNIS", "UPGS", "TECS",
                              "SWNM", "COLR", "CRGB", "PUPx", "PTEx", "UNIx", "UPGx", "TECx", "HXPL", "HXRG", "HXTE"};

struct FromChk {
    std::vector<std::pair<std::string, Bytes>> eff;  // the known sections as the game reads them
    std::vector<Section> raw;                       // sections kept as they are ("sections")
    std::vector<std::optional<Bytes>> strs;          // the string table, id - 1; none: points nowhere
    bool legacyText = false, utf8Text = false;
    bool hx = false;
    int nSlots = NEUTRAL + 1;

    const Bytes* get(const char* tag) const {
        for (const auto& e : eff)
            if (e.first == tag) return &e.second;
        return nullptr;
    }
    // A section's bytes, padded with zeros to `n` (the game reads a short one so).
    Bytes padded(const char* tag, size_t n) const {
        const Bytes* b = get(tag);
        Bytes out = b ? *b : Bytes();
        if (out.size() < n) out.resize(n, 0);
        return out;
    }
    void keepRaw(const char* tag) {
        if (const Bytes* b = get(tag)) raw.push_back({SectionName{}, *b}), std::memcpy(raw.back().name.c.data(), tag, 4);
    }

    void read(const Chk& chk) {
        for (const Section& s : chk.sections) {
            const std::string tag = s.name.str();
            const bool known = std::any_of(std::begin(kKnown), std::end(kKnown), [&](const char* k) { return tag == k; });
            if (!known) {
                raw.push_back(s);
                continue;
            }
            auto it = std::find_if(eff.begin(), eff.end(), [&](const auto& e) { return e.first == tag; });
            if (it == eff.end()) {
                eff.push_back({tag, s.data});
                continue;
            }
            if (tag == "UNIT" || tag == "THG2" || tag == "TRIG" || tag == "MBRF") {
                it->second.insert(it->second.end(), s.data.begin(), s.data.end());
            } else if (tag == "MTXM" || tag == "MASK") {  // (each copy over the one before)
                if (it->second.size() < s.data.size()) it->second.resize(s.data.size(), 0);
                std::copy(s.data.begin(), s.data.end(), it->second.begin());
            } else {
                it->second = s.data;
            }
        }
    }

    void readStrings() {
        const Bytes* t = get("STRx");
        const bool wide = t != nullptr;
        if (!t) t = get("STR ");
        if (wide && get("STR ")) keepRaw("STR ");
        if (!t) return;
        const size_t w = wide ? 4 : 2;
        if (t->size() < w) return;
        const size_t count = le(*t, 0, int(w));
        for (size_t k = 0; k < count; ++k) {
            const size_t at = w + k * w;
            if (at + w > t->size()) {
                strs.push_back(std::nullopt);
                continue;
            }
            const size_t off = le(*t, at, int(w));
            if (off >= t->size()) {
                strs.push_back(std::nullopt);
                continue;
            }
            size_t end = off;
            while (end < t->size() && (*t)[end]) ++end;
            strs.push_back(Bytes(t->begin() + std::ptrdiff_t(off), t->begin() + std::ptrdiff_t(end)));
        }
    }

    // A text field: the string, or {"string_id": n} where it points at none. 0: none (the field is left out).
    std::optional<J> text(uint32_t id) {
        if (!id) return std::nullopt;
        if (id <= strs.size() && strs[id - 1]) {
            bool legacy = false;
            const std::vector<unsigned> cps = decodeText(*strs[id - 1], &legacy);
            std::string s;
            for (unsigned cp : cps) putUtf8(s, cp);
            if (legacy) legacyText = true;
            else if (s.size() != cps.size()) utf8Text = true;
            return J::str(s);
        }
        J o = J::obj();
        o.add("string_id", J::num(id));
        return o;
    }
    void addText(J& obj, const char* key, uint32_t id) {
        if (std::optional<J> t = text(id)) obj.add(key, std::move(*t));
    }

    // A player number past what "group_N" holds: the compile reads N with atoi, which stops at 2147483647.
    mutable bool pastInt = false;

    J groupOf(long long n) const {
        for (const HxmName& g : kHxmGroups)
            if (g.n == n) return J::str(g.psz);
        if (n > 0x7FFFFFFF) pastInt = true;
        return J::str("group_" + std::to_string(n));
    }
    // A unit's or doodad's owner.
    J owner(unsigned o) const {
        if (o < unsigned(NEUTRAL)) return J::num(o);
        if (hx && o > unsigned(NEUTRAL) && o < unsigned(PLAYER_SLOTS_MAX)) return J::num(o);
        return groupOf(o);
    }
    // A trigger's player or group (the compile's TrigPlayer, or Player in a briefing).
    J trigPlayer(uint32_t v, bool briefing) const {
        if (v < uint32_t(NEUTRAL)) return J::num(v);
        if (!briefing && hx && v > 0x100u + NEUTRAL && v < 0x100u + uint32_t(nSlots)) return J::num(v - 0x100);
        return groupOf(v);
    }

    static void addNum(J& o, const char* key, long long v, long long dflt = 0) {
        if (v != dflt) o.add(key, J::num(v));
    }

    J triggers(const Bytes& d, bool briefing, const Bytes* hxte) {
        J list = J::arr();
        const size_t n = d.size() / 2400;
        for (size_t k = 0; k < n; ++k) {
            const uint8_t* t = &d[k * 2400];
            J trig = J::obj();
            J players = J::arr(), bytes = J::obj();
            for (int p = 0; p < 27; ++p) {
                const uint8_t b = t[2372 + p];
                if (b == 1) players.push(p < NEUTRAL ? J::num(p) : groupOf(p));
                else if (b) bytes.add(std::to_string(p), J::num(b));
            }
            if (hxte) {
                const unsigned long long q = le(*hxte, 8 + k * 8, 4) | (static_cast<unsigned long long>(le(*hxte, 12 + k * 8, 4)) << 32);
                for (int s = NEUTRAL + 1; s < 64 && s < nSlots; ++s)
                    if (q >> s & 1) players.push(J::num(s));
            }
            if (!players.items.empty()) trig.add("players", std::move(players));
            auto zero = [](const uint8_t* p, size_t n) { return std::all_of(p, p + n, [](uint8_t b) { return b == 0; }); };
            int last = -1;
            for (int c = 0; c < 16; ++c)
                if (!zero(t + c * 20, 20)) last = c;
            if (last >= 0) {
                J& conds = trig.add("conditions", J::arr());
                for (int c = 0; c <= last; ++c) {
                    const uint8_t* e = t + c * 20;
                    J o = J::obj();
                    o.add("type", nameOf(kHxmConditions, e[0x0F]));
                    addNum(o, "location", le({e, 20}, 0, 4));
                    if (const uint32_t p = le({e, 20}, 4, 4)) o.add("player", trigPlayer(p, false));
                    addNum(o, "amount", le({e, 20}, 8, 4));
                    if (const uint32_t u = le({e, 20}, 0x0C, 2)) o.add("unit", nameOf(kHxmUnits, u));
                    if (e[0x0E]) o.add("comparison", nameOf(kHxmComparisons, e[0x0E]));
                    addNum(o, "resource", e[0x10]);
                    addNum(o, "flags", e[0x11]);
                    addNum(o, "mask", le({e, 20}, 0x12, 2));
                    conds.push(std::move(o));
                }
            }
            last = -1;
            for (int a = 0; a < 64; ++a)
                if (!zero(t + 320 + a * 32, 32)) last = a;
            if (last >= 0) {
                J& acts = trig.add("actions", J::arr());
                for (int a = 0; a <= last; ++a) {
                    const uint8_t* e = t + 320 + a * 32;
                    const std::span<const uint8_t> s{e, 32};
                    J o = J::obj();
                    const uint8_t type = e[0x1A];
                    o.add("type", briefing ? nameOf(kHxmBriefingActions, type) : nameOf(kHxmActions, type));
                    addNum(o, "location", le(s, 0, 4));
                    addText(o, "text", le(s, 4, 4));
                    addText(o, "wav", le(s, 8, 4));
                    addNum(o, "time", le(s, 0x0C, 4));
                    if (const uint32_t p = le(s, 0x10, 4)) o.add("player", trigPlayer(p, briefing));
                    if (const uint32_t num = le(s, 0x14, 4)) o.add("number", !briefing && type == 48 ? trigPlayer(num, false) : J::num(num));
                    if (const uint32_t u = le(s, 0x18, 2)) o.add("unit", nameOf(kHxmUnits, u));
                    addNum(o, "modifier", e[0x1B]);
                    addNum(o, "flags", e[0x1C]);
                    addNum(o, "pad", e[0x1D]);
                    addNum(o, "mask", le(s, 0x1E, 2));
                    acts.push(std::move(o));
                }
            }
            addNum(trig, "flags", le({t, 2400}, 2368, 4));
            if (!bytes.members.empty()) trig.add("player_bytes", std::move(bytes));
            addNum(trig, "current_action", t[2399]);
            list.push(std::move(trig));
        }
        return list;
    }

    // An availability table's run of n bytes: the values that are not `dflt`, by name.
    template <size_t N>
    static J values(const Bytes& b, size_t off, size_t n, int dflt, const HxmName (&table)[N]) {
        J o = J::obj();
        for (size_t k = 0; k < n; ++k)
            if (b[off + k] != dflt) o.add(keyOf(table, static_cast<long long>(k)), J::num(b[off + k]));
        return o;
    }

    J puni(const Bytes& b) {
        J a = J::obj();
        a.add("global", values(b, 12 * 228, 228, 1, kHxmUnits));
        J players = J::obj();
        for (size_t p = 0; p < 12; ++p) {
            J o = J::obj();
            J av = values(b, p * 228, 228, 1, kHxmUnits), use = values(b, 12 * 228 + 228 + p * 228, 228, 1, kHxmUnits);
            if (!av.members.empty()) o.add("available", std::move(av));
            if (!use.members.empty()) o.add("use_global", std::move(use));
            if (!o.members.empty()) players.add(std::to_string(p), std::move(o));
        }
        if (!players.members.empty()) a.add("players", std::move(players));
        return a;
    }

    template <size_t N>
    J pupx(const Bytes& b, size_t n, const HxmName (&table)[N], const char* const (&names)[5]) {
        // names: global max/available, global start/researched, per player max/available, start/researched
        J a = J::obj();
        J gm = values(b, 24 * n, n, 0, table), gs = values(b, 25 * n, n, 0, table);
        if (!gm.members.empty()) a.add(names[0], std::move(gm));
        if (!gs.members.empty()) a.add(names[1], std::move(gs));
        J players = J::obj();
        for (size_t p = 0; p < 12; ++p) {
            J o = J::obj();
            J m = values(b, p * n, n, 0, table), s = values(b, 12 * n + p * n, n, 0, table), u = values(b, 26 * n + p * n, n, 1, table);
            if (!m.members.empty()) o.add(names[2], std::move(m));
            if (!s.members.empty()) o.add(names[3], std::move(s));
            if (!u.members.empty()) o.add(names[4], std::move(u));
            if (!o.members.empty()) players.add(std::to_string(p), std::move(o));
        }
        if (!players.members.empty()) a.add("players", std::move(players));
        return a;
    }

    J unis(const Bytes& b, bool bw) {
        const size_t nWeap = bw ? 130 : 100;
        J s = J::obj();
        J& units = s.add("units", J::obj());
        for (size_t k = 0; k < 228; ++k) {
            if (b[k]) continue;
            const std::span<const uint8_t> d{b};
            J u = J::obj();
            addNum(u, "hp", le(d, 0xE4 + k * 4, 4));
            addNum(u, "shields", le(d, 0x474 + k * 2, 2));
            addNum(u, "armor", b[0x63C + k]);
            addNum(u, "build_time", le(d, 0x720 + k * 2, 2));
            addNum(u, "minerals", le(d, 0x8E8 + k * 2, 2));
            addNum(u, "gas", le(d, 0xAB0 + k * 2, 2));
            addText(u, "name", le(d, 0xC78 + k * 2, 2));
            units.add(keyOf(kHxmUnits, static_cast<long long>(k)), std::move(u));
        }
        if (!units.members.empty()) {  // (the whole table, whenever a unit is listed)
            J w = J::obj();
            for (size_t k = 0; k < nWeap; ++k) {
                const uint32_t dmg = le(b, 0xE40 + k * 2, 2), bonus = le(b, 0xE40 + nWeap * 2 + k * 2, 2);
                if (dmg || bonus) {
                    J pair = J::arr();
                    pair.push(J::num(dmg)), pair.push(J::num(bonus));
                    w.add(std::to_string(k), std::move(pair));
                }
            }
            s.add("weapons", std::move(w));
        }
        return s;
    }

    J upgs(const Bytes& b, bool bw) {
        const size_t n = bw ? 61 : 46, base = n + (bw ? 1 : 0);
        J s = J::obj();
        for (size_t k = 0; k < n; ++k) {
            if (b[k]) continue;
            J u = J::obj();
            for (size_t f = 0; f < 6; ++f) addNum(u, Compile::kUpgradeFields[f], le(b, base + f * n * 2 + k * 2, 2));
            s.add(keyOf(kHxmUpgrades, static_cast<long long>(k)), std::move(u));
        }
        return s;
    }

    J tecs(const Bytes& b, bool bw) {
        const size_t n = bw ? 44 : 24;
        J s = J::obj();
        for (size_t k = 0; k < n; ++k) {
            if (b[k]) continue;
            J u = J::obj();
            for (size_t f = 0; f < 4; ++f) addNum(u, Compile::kTechFields[f], le(b, n + f * n * 2 + k * 2, 2));
            s.add(keyOf(kHxmTechs, static_cast<long long>(k)), std::move(u));
        }
        return s;
    }

    // A section of a size the document does not hold: kept as it is, false (it is not read into the document).
    bool sized(const char* tag, size_t n) {
        const Bytes* b = get(tag);
        if (!b) return false;
        if (b->size() == n) return true;
        keepRaw(tag);
        return false;
    }

    bool run(std::span<const uint8_t> bytes, const std::string& stardraft, J& doc, Layers& layers, std::string& error) {
        read(Chk::parse(bytes));
        readStrings();
        doc = J::obj();
        doc.add("format", J::str("hyperion-map"));
        doc.add("version", J::num(HXM_VERSION));

        const Bytes* verB = get("VER ");
        const int ver = verB && verB->size() >= 2 ? int(le(*verB, 0, 2)) : 205;
        if (verB && verB->size() != 2) keepRaw("VER ");
        const Bytes dimB = padded("DIM ", 4);
        if (get("DIM ") && get("DIM ")->size() != 4) keepRaw("DIM ");
        const uint32_t width = le(dimB, 0, 2), height = le(dimB, 2, 2);
        if (!width || !height) return error = "the map has no size (DIM)", false;
        const bool expansion = !(ver < 200 || ver == MAP_VERSION_HX_ORIGINAL);

        // Hyperion's slots
        const Bytes* hxpl = get("HXPL");
        uint32_t hxCount = 0;
        if (hxpl && hxpl->size() >= 8) {
            hxCount = le(*hxpl, 4, 4);
            hx = le(*hxpl, 0, 4) == 1 && hxCount >= 9 && hxCount <= uint32_t(PLAYER_SLOTS_MAX) && hxCount != 12 &&
                 hxpl->size() == 8 + 4 * size_t(hxCount - 8);
        }
        if (hxpl && !hx) keepRaw("HXPL");
        const bool classic = !hx && ver != MAP_VERSION_HX && ver != MAP_VERSION_HX_ORIGINAL && width <= 256 && height <= 256;

        const size_t sprpSize = get("SPRP") ? get("SPRP")->size() : 0;
        const Bytes sprp = padded("SPRP", 4);
        if (sprpSize > 4) keepRaw("SPRP");
        if (get("SPRP")) {
            addText(doc, "name", le(sprp, 0, 2));
            addText(doc, "description", le(sprp, 2, 2));
        }
        const Bytes* era = get("ERA ");
        const uint32_t tileset = era && era->size() >= 2 ? le(*era, 0, 2) : 0;
        if (era && era->size() != 2) keepRaw("ERA ");
        doc.add("tileset", nameOf(kHxmTilesets, tileset));
        J& size = doc.add("size", J::arr());
        size.push(J::num(width)), size.push(J::num(height));
        if (!expansion) doc.add("expansion", J::boolean(false));
        if (classic) doc.add("limits", J::str("classic"));
        const size_t encodingAt = doc.members.size();

        // the players
        for (const char* tag : {"OWNR", "SIDE", "IOWN"})
            if (get(tag) && get(tag)->size() != 12) keepRaw(tag);
        const Bytes ownr = padded("OWNR", 12), side = padded("SIDE", 12);
        const Bytes iown = get("IOWN") ? padded("IOWN", 12) : ownr;
        const Bytes forc = padded("FORC", 20);
        if (get("FORC") && get("FORC")->size() > 20) keepRaw("FORC");
        const bool colr = sized("COLR", 8), crgb = sized("CRGB", 32);
        const Bytes* hxrg = get("HXRG");
        const bool hxrgOk = hx && hxrg && hxrg->size() == 8 + 4 * size_t(hxCount - 8) && le(*hxrg, 0, 4) == 1 && le(*hxrg, 4, 4) == hxCount;
        if (hxrg && !hxrgOk) keepRaw("HXRG");
        size_t listed = 8;
        if (hx) {
            listed = hxCount > 11 ? hxCount - 1 : hxCount;
        } else {
            for (size_t k = 8; k < 11; ++k)
                if (ownr[k] || side[k] || iown[k] != ownr[k]) listed = k + 1;
        }
        nSlots = listed > size_t(NEUTRAL) ? int(listed) + 1 : NEUTRAL + 1;
        J players = J::arr();
        bool anyColor = false, anyMode = false;
        for (size_t k = 0; k < listed; ++k) {
            const size_t s = k < size_t(NEUTRAL) ? k : k + 1;
            const uint8_t* h = hx && s >= 8 ? &(*hxpl)[8 + 4 * (s - 8)] : nullptr;
            J p = J::obj();
            const int kind = k < size_t(NEUTRAL) ? ownr[k] : h[0], race = k < size_t(NEUTRAL) ? side[k] : h[1];
            if (kind) p.add("slot", nameOf(kHxmSlots, kind));
            if (race) p.add("race", nameOf(kHxmRaces, race));
            const int force = k < 8 ? forc[k] : h ? h[2] : 0;
            addNum(p, "force", force);
            if (k < 8 && colr && size_t((*get("COLR"))[k]) != k) {
                p.add("color", nameOf(kHxmColors, (*get("COLR"))[k])), anyColor = true;
            } else if (h && h[3] != 0xFF) {
                p.add("color", nameOf(kHxmColors, h[3]));
            }
            if (k < 8 && crgb) {
                const Bytes& c = *get("CRGB");
                const int m = c[24 + k];
                if (m != 1) p.add("color_mode", m == 0 ? J::str("random") : m == 2 ? J::str("rgb") : J::num(m)), anyMode = true;
                if (c[k * 3] || c[k * 3 + 1] || c[k * 3 + 2]) {
                    J& rgb = p.add("rgb", J::arr());
                    for (size_t n = 0; n < 3; ++n) rgb.push(J::num(c[k * 3 + n]));
                }
            } else if (hxrgOk && s >= 8 && (*hxrg)[8 + 4 * (s - 8)]) {
                J& rgb = p.add("rgb", J::arr());
                for (size_t n = 1; n < 4; ++n) rgb.push(J::num((*hxrg)[8 + 4 * (s - 8) + n]));
            }
            if (k < size_t(NEUTRAL) && iown[k] != ownr[k]) p.add("editor_slot", nameOf(kHxmSlots, iown[k]));
            players.push(std::move(p));
        }
        if (colr && !anyColor) players.items[0].add("color", nameOf(kHxmColors, 0));  // (COLR is there: every slot its own)
        if (crgb && !anyMode) players.items[0].add("color_mode", J::str("player_choice"));
        doc.add("players", std::move(players));
        J neutral = J::obj();
        if (ownr[11]) neutral.add("slot", nameOf(kHxmSlots, ownr[11]));
        if (side[11]) neutral.add("race", nameOf(kHxmRaces, side[11]));
        if (iown[11] != ownr[11]) neutral.add("editor_slot", nameOf(kHxmSlots, iown[11]));
        if (!neutral.members.empty()) doc.add("neutral", std::move(neutral));

        if (get("FORC")) {
            J forces = J::arr();
            size_t used = 0;
            for (size_t f = 0; f < 4; ++f) {
                J o = J::obj();
                addText(o, "name", le(forc, 8 + f * 2, 2));
                if (const uint8_t fl = forc[16 + f]) {
                    J& flags = o.add("flags", J::arr());
                    uint8_t rest = fl;
                    for (const HxmName& n : kHxmForceFlags)
                        if (fl & n.n) flags.push(J::str(n.psz)), rest = uint8_t(rest & ~n.n);
                    if (rest) flags.push(J::num(rest));
                }
                if (!o.members.empty()) used = f + 1;
                forces.push(std::move(o));
            }
            forces.items.resize(used);
            doc.add("forces", std::move(forces));
        }

        // units and doodads
        J units = J::arr();
        if (const Bytes* b = get("UNIT")) {
            for (size_t k = 0; k + 36 <= b->size(); k += 36) {
                const std::span<const uint8_t> e(b->data() + k, 36);
                J u = J::obj();
                u.add("type", nameOf(kHxmUnits, le(e, 8, 2)));
                u.add("x", J::num(le(e, 4, 2)));
                u.add("y", J::num(le(e, 6, 2)));
                if (e[0x10]) u.add("owner", owner(e[0x10]));
                addNum(u, "id", le(e, 0, 4), static_cast<long long>(k / 36));
                addNum(u, "link", le(e, 0x20, 4));
                addNum(u, "relation", le(e, 10, 2));
                for (const Compile::UnitField& f : Compile::kUnitFields) addNum(u, f.psz, le(e, f.at, int(f.n)), f.dflt);
                units.push(std::move(u));
            }
        }
        doc.add("units", std::move(units));
        J doodads = J::arr();
        if (const Bytes* b = get("THG2")) {
            for (size_t k = 0; k + 10 <= b->size(); k += 10) {
                const std::span<const uint8_t> e(b->data() + k, 10);
                J d = J::obj();
                uint32_t flags = le(e, 8, 2);
                if (flags & 0x1000) d.add("sprite", J::num(le(e, 0, 2))), flags &= ~0x1000u;
                else d.add("unit", nameOf(kHxmUnits, le(e, 0, 2)));
                d.add("x", J::num(le(e, 2, 2)));
                d.add("y", J::num(le(e, 4, 2)));
                if (e[6]) d.add("owner", owner(e[6]));
                addNum(d, "flags", flags);
                addNum(d, "pad", e[7]);
                doodads.push(std::move(d));
            }
        }
        doc.add("doodads", std::move(doodads));

        // locations
        J compat = J::obj();
        J locations = J::arr();
        if (const Bytes* b = get("MRGN")) {
            const size_t count = ver == 59 ? 64 : 255;
            if (b->size() != count * 20) compat.add("mrgn_bytes", J::num(static_cast<long long>(b->size())));
            Bytes m = *b;
            const size_t n = std::min((b->size() + 19) / 20, count);
            m.resize(n * 20, 0);
            for (size_t k = 0; k < n; ++k) {
                const std::span<const uint8_t> e(m.data() + k * 20, 20);
                if (std::all_of(e.begin(), e.end(), [](uint8_t v) { return v == 0; })) continue;
                J l = J::obj();
                l.add("id", J::num(static_cast<long long>(k + 1)));
                addText(l, "name", le(e, 16, 2));
                l.add("left", J::num(le(e, 0, 4)));
                l.add("top", J::num(le(e, 4, 4)));
                l.add("right", J::num(le(e, 8, 4)));
                l.add("bottom", J::num(le(e, 12, 4)));
                addNum(l, "elevation", le(e, 18, 2));
                locations.push(std::move(l));
            }
        }
        doc.add("locations", std::move(locations));

        // triggers
        const Bytes* hxte = get("HXTE");
        const size_t nTrig = get("TRIG") ? get("TRIG")->size() / 2400 : 0;
        const bool hxteOk = hx && hxte && hxte->size() == 8 + 8 * nTrig && le(*hxte, 0, 4) == 1 && le(*hxte, 4, 4) == nTrig;
        if (hxte && !hxteOk) keepRaw("HXTE");
        doc.add("triggers", get("TRIG") ? triggers(*get("TRIG"), false, hxteOk ? hxte : nullptr) : J::arr());
        if (const Bytes* b = get("MBRF")) doc.add("briefing", triggers(*b, true, nullptr));

        // unit properties
        J props = J::arr();
        const bool uprp = sized("UPRP", 0x500);
        const Bytes* upus = get("UPUS");
        for (size_t k = 0; k < 64; ++k) {
            J p = J::obj();
            p.add("slot", J::num(static_cast<long long>(k + 1)));
            if (uprp) {
                const std::span<const uint8_t> e(get("UPRP")->data() + k * 20, 20);
                addNum(p, "valid_state", le(e, 0, 2));
                addNum(p, "valid_props", le(e, 2, 2));
                addNum(p, "owner", e[4]);
                addNum(p, "hp", e[5]);
                addNum(p, "shields", e[6]);
                addNum(p, "energy", e[7]);
                addNum(p, "resources", le(e, 8, 4));
                addNum(p, "hangar", le(e, 12, 2));
                addNum(p, "state", le(e, 14, 2));
                addNum(p, "unused", le(e, 16, 4));
            }
            if (upus && k < upus->size() && (*upus)[k]) p.add("used", (*upus)[k] == 1 ? J::boolean(true) : J::num((*upus)[k]));
            if (p.members.size() > 1) props.push(std::move(p));
        }
        if (!uprp || !upus || upus->size() != 64) {
            J meta = J::obj();
            if (!uprp) meta.add("no_uprp", J::boolean(true));
            if (!upus) meta.add("no_upus", J::boolean(true));
            else if (upus->size() != 64) meta.add("upus_bytes", J::num(static_cast<long long>(upus->size())));
            J m = J::obj();
            m.add("meta", std::move(meta));
            props.push(std::move(m));
        }
        doc.add("unit_properties", std::move(props));

        // settings and availability
        J settings = J::obj();
        if (sized("UNIS", 0xFD0)) settings.add("units_classic", unis(*get("UNIS"), false));
        if (sized("UPGS", 46 + 46 * 12)) settings.add("upgrades_classic", upgs(*get("UPGS"), false));
        if (sized("TECS", 24 * 9)) settings.add("techs_classic", tecs(*get("TECS"), false));
        if (sized("UNIx", 0xFD0 + 120)) settings.add("units", unis(*get("UNIx"), true));
        if (sized("UPGx", 62 + 61 * 12)) settings.add("upgrades", upgs(*get("UPGx"), true));
        if (sized("TECx", 44 * 9)) settings.add("techs", tecs(*get("TECx"), true));
        if (!settings.members.empty()) doc.add("settings", std::move(settings));
        static const char* const kUpgradeNames[5] = {"global_max", "global_start", "max", "start", "use_global"};
        static const char* const kTechNames[5] = {"global_available", "global_researched", "available", "researched", "use_global"};
        J avail = J::obj();
        if (sized("PUNI", 5700)) avail.add("units", puni(*get("PUNI")));
        if (sized("UPGR", 38 * 46)) avail.add("upgrades_classic", pupx(*get("UPGR"), 46, kHxmUpgrades, kUpgradeNames));
        if (sized("PTEC", 38 * 24)) avail.add("techs_classic", pupx(*get("PTEC"), 24, kHxmTechs, kTechNames));
        if (sized("PUPx", 38 * 61)) avail.add("upgrades", pupx(*get("PUPx"), 61, kHxmUpgrades, kUpgradeNames));
        if (sized("PTEx", 38 * 44)) avail.add("techs", pupx(*get("PTEx"), 44, kHxmTechs, kTechNames));
        if (!avail.members.empty()) doc.add("availability", std::move(avail));

        // sounds and switches
        if (const Bytes* b = get("WAV ")) {
            J sounds = J::obj();
            Bytes w = *b;
            w.resize(2048, 0);
            for (size_t k = 0; k < 512; ++k) addText(sounds, std::to_string(k).c_str(), le(w, k * 4, 4));
            if (b->size() != 2048) compat.add("wav_bytes", J::num(static_cast<long long>(b->size())));
            doc.add("sounds", std::move(sounds));
        }
        if (const Bytes* b = get("SWNM")) {
            J switches = J::obj();
            Bytes w = *b;
            w.resize(1024, 0);
            for (size_t k = 0; k < 256; ++k) addText(switches, std::to_string(k + 1).c_str(), le(w, k * 4, 4));
            if (b->size() != 1024) compat.add("swnm_bytes", J::num(static_cast<long long>(b->size())));
            doc.add("switches", std::move(switches));
        }

        // the layers
        const size_t tilesBytes = size_t(width) * height * 2;
        const Bytes* mtxm = get("MTXM");
        if (mtxm) {
            layers.push_back({"tiles", Bytes(mtxm->begin(), mtxm->begin() + std::ptrdiff_t(std::min(mtxm->size(), tilesBytes)))});
            if (mtxm->size() > tilesBytes) layers.push_back({"tiles_extra", Bytes(mtxm->begin() + std::ptrdiff_t(tilesBytes), mtxm->end())});
            if (mtxm->size() != tilesBytes) compat.add("mtxm_bytes", J::num(static_cast<long long>(mtxm->size())));
        }
        if (const Bytes* b = get("MASK")) layers.push_back({"fog", *b});
        if (const Bytes* b = get("ISOM")) layers.push_back({"isom", *b});
        if (const Bytes* b = get("DD2 ")) layers.push_back({"editor_doodads", *b});
        J editor = J::obj();
        if (const Bytes* tile = get("TILE")) {
            if (*tile == (mtxm ? *mtxm : Bytes())) editor.add("tiles_same_as_game", J::boolean(true));
            else layers.push_back({"editor_tiles", *tile});
        }
        if (!stardraft.empty()) {
            J sd;
            std::string why;
            if (!parseJson(stardraft, sd, why)) return error = "the editor's data: " + why, false;
            editor.add("stardraft", std::move(sd));
        }
        if (!editor.members.empty()) doc.add("editor", std::move(editor));

        // compat: what the map needs to come back the same
        J c = J::obj();
        c.add("chk_version", J::num(ver));
        if (const Bytes* b = get("TYPE")) {
            std::vector<unsigned> cps;
            if (utf8Decode(std::string(b->begin(), b->end()), &cps) && std::find(b->begin(), b->end(), 0) == b->end())
                c.add("type", J::str(std::string(b->begin(), b->end())));
            else
                keepRaw("TYPE");
        }
        if (sized("IVER", 2)) c.add("iver", J::num(le(*get("IVER"), 0, 2)));
        if (sized("IVE2", 2)) c.add("ive2", J::num(le(*get("IVE2"), 0, 2)));
        if (const Bytes* b = get("VCOD"))
            c.add("vcod", J::str(b->size() == sizeof kVcod && std::equal(b->begin(), b->end(), kVcod) ? std::string("standard") : base64Encode(*b)));
        if (get("STRx")) c.add("strx", J::boolean(true));
        if (!strs.empty()) c.add("string_count", J::num(static_cast<long long>(strs.size())));
        for (JMember& m : compat.members) c.members.push_back(std::move(m));
        doc.add("compat", std::move(c));

        if (!raw.empty()) {
            J sections = J::arr();
            for (const Section& s : raw) {
                J o = J::obj();
                std::string tag;
                for (char ch : s.name.c) putUtf8(tag, uint8_t(ch));
                o.add("tag", J::str(tag));
                o.add("data", J::str(base64Encode(s.data)));
                sections.push(std::move(o));
            }
            doc.add("sections", std::move(sections));
        }

        std::sort(layers.begin(), layers.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        J layerList = J::obj();
        for (const auto& l : layers) {
            J o = J::obj();
            o.add("file", J::str("layers/" + l.first + ".bin"));
            o.add("type", J::str(l.first == "fog" || l.first == "editor_doodads" || l.first == "tiles_extra" ? "u8" : "u16"));
            layerList.add(l.first, std::move(o));
        }
        doc.add("layers", std::move(layerList));

        // text in 1.17's code page, as the classic editors wrote it: cp1252 keeps those strings' bytes
        if (legacyText) doc.members.insert(doc.members.begin() + std::ptrdiff_t(encodingAt), JMember{"text_encoding", J::str("cp1252")});
        return true;
    }
};

// The package: map.json and the files it names, from a zip or a folder.
using Find = bool (*)(const void* ctx, const std::string& name, Bytes& out);

bool readPackage(const void* ctx, Find find, HxmMap& out, std::string& error) {
    Bytes json;
    if (!find(ctx, "map.json", json)) return error = "no map.json", false;
    J doc;
    std::string why;
    if (!parseJson(std::string_view(reinterpret_cast<const char*>(json.data()), json.size()), doc, why))
        return error = "map.json: " + why, false;
    Layers layers;
    for (const JMember& m : doc["layers"].members) {
        Bytes data;
        const std::string file = m.second["file"].Str();
        if (!find(ctx, file, data)) return error = "no " + file, false;
        layers.push_back({m.first, std::move(data)});
    }
    out.files.clear();
    const J& files = doc["files"];
    for (size_t k = 0; k < files.size(); ++k) {
        const std::string name = files.At(k).Str();
        std::string inZip = "files/" + name;
        std::replace(inZip.begin(), inZip.end(), '\\', '/');
        Bytes data;
        if (!find(ctx, inZip, data)) return error = "no " + inZip, false;
        out.files.push_back({name, std::move(data)});
    }
    Compile c;
    if (!c.Run(doc, layers, &out.chk)) return error = c.error, false;
    out.stardraft.clear();
    const J& sd = doc["editor"]["stardraft"];
    if (sd.kind != J::NONE) writeJson(out.stardraft, sd, 0);
    return true;
}

// A path as UTF-8 text (an error message; path.string() fails on names outside the code page).
std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

struct ZipCtx {
    std::span<const uint8_t> data;
    std::vector<ZipEntry> entries;
};

}  // namespace

namespace {
// The colours' RGB: StarCraft's and Remastered's (0 to 22), then Hyperion's own (23 up), whose values Hyperion does not
// publish: these are near the colours their names give.
constexpr uint32_t kColorRgb[] = {
    0xF40404, 0x0C48CC, 0x2CB494, 0x88409C, 0xF88C14, 0x703014, 0xCCE0D0, 0xFCFC38, 0x088008, 0xFCFC7C, 0xECC4B0,
    0x4068D4, 0x74A47C, 0x7290B8, 0xF4F4A0, 0x00E4FC, 0xFFC4E4, 0x808000, 0xD2F53C, 0x000080, 0xF032E6, 0x808080,
    0x3C3C3C,
    0x3C78FF, 0x228B22, 0xFFF0C8, 0x87CEEB, 0xF0DC28, 0x1E9690, 0x800000, 0x6B8E23, 0x6495ED, 0xFFE640, 0xFAFAA0,
    0x32C832, 0xFF8C28, 0xDC2828, 0xFFBF00, 0x3CB43C, 0xC83232, 0x98C8B0, 0xA8C8C8, 0xE6D232, 0x2850DC, 0xB784A7,
    0x0A5A1E, 0x4B0082, 0xF09628, 0x3C64C8, 0xE67828, 0x8B0A0A, 0xDC8232, 0xDCC83C, 0x5078DC, 0xC86E28, 0x6E8C6E,
    0xF0A050, 0xF0F0F0, 0xC88C00, 0x50508C, 0xC88C64, 0xB47850, 0x961414, 0xF0E650, 0xD2C846};
static_assert(std::size(kColorRgb) == std::size(kHxmColors));
}  // namespace

int colorCount() { return int(std::size(kHxmColors)); }

std::string colorName(int id) {
    if (id < 0 || id >= colorCount()) return "Colour " + std::to_string(id);
    std::string n = kHxmColors[id].psz;  // (the table is in id order)
    for (char& ch : n)
        if (ch == '_') ch = ' ';
    if (n.size() > 3 && n.compare(n.size() - 3, 3, " hx") == 0) n.resize(n.size() - 3);  // (Hyperion's own of a name)
    if (!n.empty()) n[0] = char(std::toupper(static_cast<unsigned char>(n[0])));
    return n;
}

uint32_t colorRgb(int id) { return id >= 0 && id < colorCount() ? kColorRgb[id] : 0x808080; }

int defaultColor(int p) {
    if (p < 12) return p;
    return 23 + (p - 12) % (colorCount() - 23);
}

bool isHxm(const fs::path& path) {
    std::string ext = utf8(path.extension());
    for (char& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return ext == ".hxm";
}

bool readHxm(std::span<const uint8_t> zip, HxmMap& out, std::string& error) {
    ZipCtx ctx{zip, {}};
    if (!zipDirectory(zip, ctx.entries)) return error = "not a zip", false;
    return readPackage(&ctx, [](const void* p, const std::string& name, Bytes& data) {
        const ZipCtx& z = *static_cast<const ZipCtx*>(p);
        for (const ZipEntry& e : z.entries)
            if (e.name == name) return zipRead(z.data, e, data);
        return false;
    }, out, error);
}

bool readHxmFile(const fs::path& path, HxmMap& out, std::string& error) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        return readPackage(&path, [](const void* p, const std::string& name, Bytes& data) {
            const fs::path file = *static_cast<const fs::path*>(p) / fs::path(std::u8string(name.begin(), name.end()));
            std::ifstream in(file, std::ios::binary);
            if (!in) return false;
            data.assign(std::istreambuf_iterator<char>(in), {});
            return true;
        }, out, error);
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) return error = "cannot open " + utf8(path), false;
    const Bytes zip((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!readHxm(zip, out, error)) return error = utf8(path.filename()) + ": " + error, false;
    return true;
}

bool compileHxm(const std::string& mapJson, const std::vector<std::pair<std::string, Bytes>>& layers, Bytes& chk, std::string& error) {
    J doc;
    std::string why;
    if (!parseJson(mapJson, doc, why)) return error = "map.json: " + why, false;
    Compile c;
    if (!c.Run(doc, layers, &chk)) return error = c.error, false;
    return true;
}

bool chkToDocument(std::span<const uint8_t> chk, const std::string& stardraft, std::string& mapJson,
                   std::vector<std::pair<std::string, Bytes>>& layers, std::string& error, std::vector<std::string>* lost) {
    FromChk f;
    J doc;
    layers.clear();
    if (!f.run(chk, stardraft, doc, layers, error)) return false;
    if (lost && f.pastInt) lost->push_back("trigger players past 2147483647 (EUD values) become 2147483647");
    mapJson.clear();
    writeJson(mapJson, doc, 0);
    mapJson += '\n';
    return true;
}

bool writeHxm(const HxmMap& map, Bytes& zip, std::string& error, std::vector<std::string>* lost) {
    std::string json;
    Layers layers;
    if (!chkToDocument(map.chk, map.stardraft, json, layers, error, lost)) return false;
    if (!map.files.empty()) {  // (the files: their list after the layers, at the document's end)
        J files = J::arr();
        for (const ArchiveFile& f : map.files) files.push(J::str(f.name));
        std::string list;
        writeJson(list, files, 1);
        const size_t end = json.rfind('}');
        json.insert(end - 1, ",\n \"files\": " + list);
    }
    ZipWriter w;
    bool ok = w.add("map.json", Bytes(json.begin(), json.end()));
    for (const auto& l : layers) ok = ok && w.add("layers/" + l.first + ".bin", l.second);
    for (const ArchiveFile& f : map.files) {
        std::string name = "files/" + f.name;
        std::replace(name.begin(), name.end(), '\\', '/');
        ok = ok && w.add(name, f.data);
    }
    if (!ok) return error = "the map is too large for a zip", false;
    zip = w.finish();
    return true;
}

bool writeHxmFile(const fs::path& path, const HxmMap& map, std::string& error, std::vector<std::string>* lost) {
    Bytes zip;
    if (!writeHxm(map, zip, error, lost)) return false;
    return writeFile(path, std::string_view(reinterpret_cast<const char*>(zip.data()), zip.size()), error);
}

}  // namespace stardraft::hyperion
