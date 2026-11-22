// Shared maps: invite links, the keys and messages, the terrain's patches and the host's and guest's sessions
// (include/stardraft/collab.h). Monocypher (third party, CC0/BSD) does the cryptography.

#include "stardraft/collab.h"

#include <algorithm>
#include <cstring>
#include <set>

#include "json.h"
#include "monocypher.h"
#include "stardraft/editor.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <stdlib.h>
#else
#include <sys/random.h>
#endif

namespace stardraft::collab {

namespace {

constexpr uint8_t kProto = 2;  // (2: tickets, presence, pings, handover)
constexpr size_t kMaxName = 32;
constexpr size_t kMaxChat = 100;  // cursor chat: bytes
constexpr size_t kMaxWaiting = 8;  // connections that have no keys yet (the relay takes 8 members)
constexpr size_t kMaxMembers = 7;
constexpr double kCursorEvery = 0.05;  // seconds: at most 20 cursor messages a second
const char kKdfLabel[] = "stardraft-collab-1";

// Frames (the first byte of a binary message, inside the relay's).
enum Frame : uint8_t { kHello = 1, kHelloBack = 2, kSealed = 3 };
// Messages (the first byte of a sealed frame's text).
enum Msg : uint8_t { mJoin = 1, mWelcome, mDeny, mPatch, mAck, mReject, mCursor, mPeers, mRole, mBye, mCursors, mObjEdit, mObjState, mObjResync, mRename,
           mPresence, mPresences, mPing, mPinged, mHandover };
constexpr double kPresenceEvery = 0.2;  // seconds: presence at most 5 times a second
constexpr size_t kMaxTickets = 64;
constexpr double kObjectsEvery = 0.1;  // seconds: the host sends a changed object section at most 10 times a second
constexpr double kCursorsEvery = 0.1;  // seconds: the host sends the cursors of all at most 10 times a second
constexpr double kFlushEvery = 0.05;   // seconds: this program's edits go at most 20 times a second
constexpr double kHelloWait = 10;      // seconds: a connection with no keys after this goes
constexpr size_t kMaxAsking = 8;       // join requests that wait for the host's answer at one time (the relay takes 8 members)

// --- Bytes in and out -----------------------------------------------------------------------------------------------
struct Writer {
    Bytes b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { b.push_back(uint8_t(v)), b.push_back(uint8_t(v >> 8)); }
    void u32(uint32_t v) { u16(uint16_t(v)), u16(uint16_t(v >> 16)); }
    void str(const std::string& s) {
        const size_t n = std::min<size_t>(s.size(), 255);
        u8(uint8_t(n));
        b.insert(b.end(), s.begin(), s.begin() + long(n));
    }
};
struct Reader {
    const uint8_t* p;
    size_t n, i = 0;
    bool ok = true;
    bool need(size_t k) {
        if (!ok || n - i < k) return ok = false;
        return true;
    }
    uint8_t u8() { return need(1) ? p[i++] : 0; }
    uint16_t u16() {
        if (!need(2)) return 0;
        const uint16_t v = uint16_t(p[i] | p[i + 1] << 8);
        i += 2;
        return v;
    }
    uint32_t u32() {
        const uint32_t lo = u16();
        return lo | uint32_t(u16()) << 16;
    }
    std::string str() {
        const size_t k = u8();
        if (!need(k)) return {};
        std::string s(reinterpret_cast<const char*>(p + i), k);
        i += k;
        return s;
    }
    bool done() const { return ok && i == n; }
};

// A name as people see it: valid UTF-8 (shortest forms, no surrogates), no control or format characters (C0, C1,
// the direction marks and overrides, line and paragraph separators), at most kMaxName bytes (whole characters).
std::string cleanText(const std::string& in, size_t maxBytes);
std::string cleanName(const std::string& in) {
    std::string out = cleanText(in, kMaxName);
    return out.empty() ? "Guest" : out;
}
std::string cleanText(const std::string& in, size_t maxBytes) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        const uint8_t c = uint8_t(in[i]);
        const size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!len || i + len > in.size()) break;
        uint32_t cp = len == 1 ? c : len == 2 ? c & 0x1F : len == 3 ? c & 0x0F : c & 0x07;
        bool ok = true;
        for (size_t k = 1; k < len; ++k) {
            const uint8_t b = uint8_t(in[i + k]);
            if ((b & 0xC0) != 0x80) ok = false;
            cp = cp << 6 | (b & 0x3F);
        }
        static const uint32_t kMin[5] = {0, 0, 0x80, 0x800, 0x10000};
        if (!ok || cp < kMin[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) break;
        if (out.size() + len > maxBytes) break;
        const bool control = cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) || cp == 0x200E || cp == 0x200F || (cp >= 0x202A && cp <= 0x202E) ||
                             (cp >= 0x2066 && cp <= 0x2069) || cp == 0x2028 || cp == 0x2029 || cp == 0x061C || cp == 0xFEFF;
        if (!control) out.append(in, i, len);
        i += len;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
std::string base64url(const uint8_t* p, size_t n) {
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        acc = acc << 8 | p[i], bits += 8;
        while (bits >= 6) out += kB64[(acc >> (bits - 6)) & 63], bits -= 6;
    }
    if (bits) out += kB64[(acc << (6 - bits)) & 63];
    return out;
}
bool unbase64url(const std::string& s, uint8_t* out, size_t n) {
    if (s.size() != (n * 8 + 5) / 6) return false;
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    for (char ch : s) {
        const char* at = std::strchr(kB64, ch);
        if (!ch || !at) return false;
        acc = acc << 6 | uint32_t(at - kB64), bits += 6;
        if (bits >= 8) {
            if (o >= n) return false;
            out[o++] = uint8_t(acc >> (bits - 8)), bits -= 8;
        }
    }
    return o == n && (acc & ((1u << bits) - 1)) == 0;  // (the last character's spare bits are 0)
}
std::string hex(const uint8_t* p, size_t n) {
    static const char d[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) s += d[p[i] >> 4], s += d[p[i] & 15];
    return s;
}
bool roomId(const std::string& s) {
    return s.size() == 12 && std::all_of(s.begin(), s.end(), [](char c) { return c && std::strchr(kB64, c); });
}

uint64_t cellKey(uint8_t section, uint32_t cell) { return uint64_t(section) << 32 | cell; }

// --- Presence in messages -----------------------------------------------------------------------------------------------
uint16_t clamp16(int v) { return uint16_t(std::clamp(v, 0, 65535)); }
void writePresence(Writer& w, const Presence& p) {
    w.u16(clamp16(p.viewX)), w.u16(clamp16(p.viewY)), w.u16(clamp16(p.viewW)), w.u16(clamp16(p.viewH));
    w.u8(uint8_t(p.tool)), w.u8(p.brushW), w.u8(p.brushH);
    const size_t nu = std::min(p.units.size(), kMaxSelected), nl = std::min(p.locations.size(), kMaxSelected);
    w.u8(uint8_t(nu));
    for (size_t i = 0; i < nu; ++i) w.u32(p.units[i]);
    w.u8(uint8_t(nl));
    for (size_t i = 0; i < nl; ++i) w.u16(p.locations[i]);
}
bool readPresence(Reader& r, Presence& p) {
    p = Presence{};
    p.viewX = r.u16(), p.viewY = r.u16(), p.viewW = r.u16(), p.viewH = r.u16();
    const uint8_t tool = r.u8();
    p.brushW = r.u8(), p.brushH = r.u8();
    if (tool > uint8_t(Tool::Locations)) return false;
    p.tool = Tool(tool);
    const uint8_t nu = r.u8();
    if (nu > kMaxSelected) return false;
    for (int i = 0; i < nu && r.ok; ++i) p.units.push_back(r.u32());
    const uint8_t nl = r.u8();
    if (nl > kMaxSelected) return false;
    for (int i = 0; i < nl && r.ok; ++i) p.locations.push_back(r.u16());
    return r.ok;
}

// The places of a change (map pixels), at most kMaxChangeRects: more become their bounding box.
void addChangeRect(Change& c, int l, int t, int r, int b) {
    if (r <= l || b <= t) return;
    if (!c.rects.empty()) {  // (the next tiles of a run in the same row: one rectangle)
        auto& last = c.rects.back();
        if (last[1] == t && last[3] == b && last[2] == l) return void(last[2] = r);
    }
    if (c.rects.size() < kMaxChangeRects) return c.rects.push_back({l, t, r, b});
    std::array<int, 4> box = c.rects[0];
    for (const auto& x : c.rects) box = {std::min(box[0], x[0]), std::min(box[1], x[1]), std::max(box[2], x[2]), std::max(box[3], x[3])};
    box = {std::min(box[0], l), std::min(box[1], t), std::max(box[2], r), std::max(box[3], b)};
    c.rects.assign(1, box);
}

// --- Patches in messages ----------------------------------------------------------------------------------------------
void writePatchTo(Writer& w, const Patch& p) {
    w.u32(uint32_t(p.size()));
    for (const Run& r : p) {
        w.u8(r.section), w.u32(r.at), w.u32(uint32_t(r.values.size()));
        for (uint16_t v : r.values) w.u16(v);
    }
}
bool readPatchFrom(Reader& r, Patch& p, size_t maxCells) {
    const uint32_t runs = r.u32();
    if (!r.ok || runs > maxCells) return false;
    size_t cells = 0;
    p.clear();
    p.reserve(runs);
    for (uint32_t k = 0; k < runs; ++k) {
        Run run;
        run.section = r.u8(), run.at = r.u32();
        const uint32_t len = r.u32();
        cells += len;
        if (!r.ok || cells > maxCells || !r.need(size_t(len) * 2)) return false;
        run.values.resize(len);
        for (uint32_t i = 0; i < len; ++i) run.values[i] = r.u16();
        p.push_back(std::move(run));
    }
    return r.ok;
}

void writeTerrainTo(Writer& w, const Terrain& t) {
    w.u16(t.width), w.u16(t.height), w.u16(t.era), w.u8(t.hasTile ? 1 : 0);
    for (int s = 0; s < kSections; ++s)
        for (uint16_t v : t.cells[s]) w.u16(v);
}
bool readTerrainFrom(Reader& r, Terrain& t) {
    t.width = r.u16(), t.height = r.u16(), t.era = r.u16();
    const uint8_t tile = r.u8();
    if (!r.ok || !t.width || !t.height || t.width > 256 || t.height > 256 || t.era > 15 || tile > 1) return false;
    t.hasTile = tile == 1;
    for (int s = 0; s < kSections; ++s) {
        const size_t n = s == kTile && !t.hasTile ? 0 : Terrain::cellCount(Part(s), t.width, t.height);
        if (!r.need(n * 2)) return false;
        t.cells[s].resize(n);
        for (size_t i = 0; i < n; ++i) t.cells[s][i] = r.u16();
    }
    return r.ok;
}

const SectionName kNames[kSections] = {SectionName("ISOM"), SectionName("MTXM"), SectionName("TILE")};

}  // namespace

const char* roleName(Role r) { return r == Role::Edit ? "edit" : r == Role::View ? "view" : "none"; }

bool randomBytes(void* out, size_t n) {
#ifdef _WIN32
    return BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), ULONG(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__)
    arc4random_buf(out, n);
    return true;
#else
    auto* p = static_cast<uint8_t*>(out);
    while (n) {
        const ssize_t got = getrandom(p, n, 0);
        if (got <= 0) return false;
        p += got, n -= size_t(got);
    }
    return true;
#endif
}

// --- Links --------------------------------------------------------------------------------------------------------------

namespace {
// "host[:port]": letters, digits, dots and dashes, a port of 1-65535. Loopback: may use http.
bool validHost(const std::string& h, bool& loopback) {
    const size_t colon = h.find(':');
    const std::string name = h.substr(0, colon);
    if (name.empty() || name.size() > 253) return false;
    for (char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-')) return false;
    if (name.front() == '.' || name.front() == '-' || name.back() == '.' || name.back() == '-') return false;
    if (colon != std::string::npos) {
        const std::string port = h.substr(colon + 1);
        if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
        const int n = std::stoi(port);
        if (n < 1 || n > 65535) return false;
    }
    std::string lower = name;
    for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
    loopback = lower == "localhost" || lower == "127.0.0.1";
    return true;
}
std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
std::string percentDecode(const std::string& s, bool& ok) {
    std::string out;
    ok = true;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') {
            out += s[i];
            continue;
        }
        if (i + 2 >= s.size() || !std::isxdigit(static_cast<unsigned char>(s[i + 1])) || !std::isxdigit(static_cast<unsigned char>(s[i + 2]))) return ok = false, out;
        out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
        i += 2;
    }
    return out;
}
}  // namespace

bool normalizeServer(const std::string& text, std::string& out, std::string& error) {
    std::string s = trimmed(text);
    while (!s.empty() && s.back() == '/') s.pop_back();
    std::string scheme = "https";
    if (const size_t at = s.find("://"); at != std::string::npos) {
        scheme = s.substr(0, at);
        for (char& c : scheme) c = char(std::tolower(static_cast<unsigned char>(c)));
        s = s.substr(at + 3);
    }
    bool loopback = false;
    if ((scheme != "https" && scheme != "http") || !validHost(s, loopback)) {
        error = "The server address is not valid. Type an address such as https://example.trycloudflare.com.";
        return false;
    }
    if (scheme == "http" && !loopback) {
        error = "Use https for a server on another computer (http only for localhost).";
        return false;
    }
    out = scheme + "://" + s;
    return true;
}

std::string relayUrl(const std::string& server, const std::string& room) {
    const std::string path = room.empty() ? "/relay" : roomId(room) ? "/relay/" + room : "";
    if (path.empty()) return {};
    if (server.rfind("https://", 0) == 0) return "wss://" + server.substr(8) + path;
    if (server.rfind("http://", 0) == 0) return "ws://" + server.substr(7) + path;
    return {};
}

std::string relayToken(const Secret& secret) {
    uint8_t out[32];
    static const char label[] = "stardraft-collab relay token";
    crypto_blake2b_keyed(out, sizeof out, secret.data(), secret.size(), reinterpret_cast<const uint8_t*>(label), sizeof label - 1);
    return hex(out, sizeof out);
}

std::string makeInviteLink(const std::string& server, const std::string& room, Role role, const Secret& secret) {
    return server + "/j/" + room + "#" + (role == Role::Edit ? "e" : "v") + base64url(secret.data(), secret.size());
}

bool parseInviteLink(const std::string& text, InviteLink& out, std::string& error) {
    error = "This is not a StarDraft invite link. It looks like https://<server>/j/<room>#e<code>.";
    std::string s = trimmed(text);
    // A link from a chat can have quotes or angle brackets around it.
    while (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'') || (s.front() == '<' && s.back() == '>')))
        s = trimmed(s.substr(1, s.size() - 2));
    auto custom = [](const std::string& t) {  // stardraft: (any case)
        static const char scheme[] = "stardraft:";
        if (t.size() < sizeof scheme - 1) return false;
        for (size_t i = 0; i + 1 < sizeof scheme; ++i)
            if (std::tolower(static_cast<unsigned char>(t[i])) != scheme[i]) return false;
        return true;
    };
    // The stardraft:// form (the invite page gives stardraft://join?link=<the link, encoded>). The system or the browser
    // can change it a little on the way: a / before the ? or at the end (stardraft://join/?link=...), or no //.
    if (custom(s)) {
        const size_t at = s.find("link=");
        if (at == std::string::npos) return false;
        std::string v = s.substr(at + 5);
        if (const size_t amp = v.find('&'); amp != std::string::npos) v.resize(amp);
        while (!v.empty() && v.back() == '/') v.pop_back();
        bool ok = false;
        s = trimmed(percentDecode(v, ok));
        if (!ok || custom(s)) return false;  // (one level only)
    }
    while (!s.empty() && s.back() == '/') s.pop_back();  // (the secret has no /)
    const size_t hash = s.find('#');
    if (hash == std::string::npos) return false;
    const std::string fragment = s.substr(hash + 1);
    s = s.substr(0, hash);
    const size_t j = s.find("/j/", s.find("://") == std::string::npos ? 0 : s.find("://") + 3);
    if (j == std::string::npos) return false;
    std::string server;
    if (!normalizeServer(s.substr(0, j), server, error)) return false;
    const std::string room = s.substr(j + 3);
    if (!roomId(room) || fragment.size() != 23 || (fragment[0] != 'e' && fragment[0] != 'v')) {
        error = "This invite link is not complete. Ask for the link again.";
        return false;
    }
    if (!unbase64url(fragment.substr(1), out.secret.data(), out.secret.size())) {
        error = "This invite link is not complete. Ask for the link again.";
        return false;
    }
    out.server = server, out.room = room, out.role = fragment[0] == 'e' ? Role::Edit : Role::View;
    error.clear();
    return true;
}

// --- The terrain ------------------------------------------------------------------------------------------------------

bool readTerrain(const Chk& chk, Terrain& out, std::string& error) {
    uint16_t w = 0, h = 0;
    if (!readDimensions(chk, w, h) || !w || !h || w > 256 || h > 256) {
        error = "The map has no valid size (DIM).";
        return false;
    }
    out.width = w, out.height = h, out.era = readEra(chk);
    for (int s = 0; s < kSections; ++s) {
        const Section* sec = chk.find(kNames[s]);
        if (s == kTile) out.hasTile = sec != nullptr;
        const size_t n = s == kTile && !sec ? 0 : Terrain::cellCount(Part(s), w, h);
        if (n && (!sec || sec->data.size() < n * 2)) {
            error = std::string("The map's ") + (s == kIsom ? "ISOM" : s == kMtxm ? "MTXM" : "TILE") + " section is missing or short.";
            return false;
        }
        out.cells[s].resize(n);
        if (n) std::memcpy(out.cells[s].data(), sec->data.data(), n * 2);  // (little-endian, as the file)
    }
    return true;
}

Patch diffTerrain(const Terrain& from, const Terrain& to) {
    Patch p;
    for (int s = 0; s < kSections; ++s) {
        const auto& a = from.cells[s];
        const auto& b = to.cells[s];
        const size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; ++i) {
            if (a[i] == b[i]) continue;
            // A run: the changed cells and the same ones between them, while fewer than 4 are the same in a row.
            size_t end = i + 1, same = 0;
            for (size_t k = i + 1; k < n && same < 4; ++k) {
                if (a[k] != b[k]) end = k + 1, same = 0;
                else ++same;
            }
            Run r;
            r.section = uint8_t(s), r.at = uint32_t(i);
            r.values.assign(b.begin() + long(i), b.begin() + long(end));
            p.push_back(std::move(r));
            i = end - 1;
        }
    }
    return p;
}

void applyPatch(Terrain& t, const Patch& p) {
    for (const Run& r : p) {
        auto& c = t.cells[r.section];
        if (r.section < kSections && size_t(r.at) + r.values.size() <= c.size()) std::copy(r.values.begin(), r.values.end(), c.begin() + r.at);
    }
}

void writePatch(Chk& chk, const Patch& p) {
    for (const Run& r : p) {
        if (r.section >= kSections) continue;
        Section* s = chk.find(kNames[r.section]);
        if (!s || (size_t(r.at) + r.values.size()) * 2 > s->data.size()) continue;
        uint8_t* d = s->data.data() + size_t(r.at) * 2;
        for (size_t i = 0; i < r.values.size(); ++i) d[i * 2] = uint8_t(r.values[i]), d[i * 2 + 1] = uint8_t(r.values[i] >> 8);
    }
}

bool validPatch(const Terrain& shape, const Patch& p, size_t tileGroups) {
    int lastSection = -1;
    size_t lastEnd = 0, total = 0;
    for (const Run& r : p) {
        if (r.section >= kSections || r.values.empty()) return false;
        const size_t size = shape.cells[r.section].size();
        if (size_t(r.at) + r.values.size() > size) return false;
        if (int(r.section) < lastSection || (int(r.section) == lastSection && r.at < lastEnd)) return false;  // (in order, no overlap)
        lastSection = r.section, lastEnd = size_t(r.at) + r.values.size();
        total += r.values.size();
        if (r.section != kIsom && tileGroups)
            for (uint16_t v : r.values)
                if (size_t(v >> 4) >= tileGroups) return false;
    }
    return total <= Terrain::cellCount(kIsom, shape.width, shape.height) + 2 * Terrain::cellCount(kMtxm, shape.width, shape.height);
}

size_t patchCells(const Patch& p) {
    size_t n = 0;
    for (const Run& r : p) n += r.values.size();
    return n;
}

// --- Objects ------------------------------------------------------------------------------------------------------------

namespace {
struct ObjDef {
    SectionName name;
    size_t record;  // a record's or a slot's size (0: whole)
    bool slots;
    size_t maxBytes;
};
const ObjDef kObj[kObjSections] = {
    {SectionName("UNIT"), 36, false, 8192 * 36},  {SectionName("DD2 "), 8, false, 65535 * 8}, {SectionName("THG2"), 10, false, 65535 * 10},
    {SectionName("MRGN"), 20, true, 1024 * 20},  {SectionName("STR "), 0, false, 512 * 1024}, {SectionName("STRx"), 0, false, 512 * 1024},
};

void writeObjEdit(Writer& w, const ObjEdit& e) {
    w.u8(e.section);
    const ObjDef& d = kObj[e.section];
    if (d.record && !d.slots) {
        w.u32(uint32_t(e.removed.size()));
        for (const Bytes& r : e.removed) w.b.insert(w.b.end(), r.begin(), r.end());
        w.u32(uint32_t(e.added.size()));
        for (const Bytes& r : e.added) w.b.insert(w.b.end(), r.begin(), r.end());
    } else if (d.slots) {
        w.u32(uint32_t(e.slots.size()));
        for (const auto& [i, r] : e.slots) w.u32(i), w.b.insert(w.b.end(), r.begin(), r.end());
    } else {
        w.u32(uint32_t(e.data.size()));
        w.b.insert(w.b.end(), e.data.begin(), e.data.end());
    }
}
bool readObjEdit(Reader& r, ObjEdit& e) {
    e = ObjEdit{};
    e.section = r.u8();
    if (!r.ok || e.section >= kObjSections) return false;
    const ObjDef& d = kObj[e.section];
    auto bytes = [&](size_t n, Bytes& out) {
        if (!r.need(n)) return false;
        out.assign(r.p + r.i, r.p + r.i + n);
        r.i += n;
        return true;
    };
    if (d.record && !d.slots) {
        for (auto* list : {&e.removed, &e.added}) {
            const uint32_t n = r.u32();
            if (!r.ok || size_t(n) * d.record > d.maxBytes) return false;
            list->resize(n);
            for (Bytes& rec : *list)
                if (!bytes(d.record, rec)) return false;
        }
    } else if (d.slots) {
        const uint32_t n = r.u32();
        if (!r.ok || size_t(n) * d.record > d.maxBytes) return false;
        e.slots.resize(n);
        for (auto& [i, rec] : e.slots) {
            i = r.u32();
            if (!r.ok || (size_t(i) + 1) * d.record > d.maxBytes || !bytes(d.record, rec)) return false;
        }
    } else {
        const uint32_t n = r.u32();
        if (!r.ok || n > d.maxBytes) return false;
        e.whole = true;
        if (!bytes(n, e.data)) return false;
    }
    return r.ok;
}
}  // namespace

Bytes readObjects(const Chk& chk, uint8_t section) {
    Bytes out;
    if (section >= kObjSections) return out;
    const bool all = section == oUnits || section == oDoodads || section == oSprites;  // (these add up; the others: the last one counts)
    for (const Section& s : chk.sections)
        if (s.name == kObj[section].name) {
            if (!all) out.clear();
            out.insert(out.end(), s.data.begin(), s.data.end());
        }
    return out;
}

void writeObjects(Chk& chk, uint8_t section, const Bytes& data) {
    if (section >= kObjSections) return;
    const ObjDef& d = kObj[section];
    if (readObjects(chk, section) == data) return;
    while (chk.find(d.name)) chk.remove(d.name);
    if (!data.empty() || d.record) chk.set(d.name, data);  // (no strings: no section)
}

ObjEdit diffObjects(uint8_t section, const Bytes& from, const Bytes& to) {
    ObjEdit e;
    e.section = section;
    const ObjDef& d = kObj[section];
    if (!d.record || from.size() % d.record || to.size() % d.record) {  // (whole, or a section that is not whole records)
        if (from != to) e.whole = true, e.data = to;
        return e;
    }
    if (d.slots) {
        const size_t n = std::max(from.size(), to.size()) / d.record;
        for (size_t i = 0; i < n; ++i) {
            const size_t at = i * d.record;
            Bytes a = at < from.size() ? Bytes(from.begin() + long(at), from.begin() + long(at + d.record)) : Bytes(d.record, 0);
            Bytes b = at < to.size() ? Bytes(to.begin() + long(at), to.begin() + long(at + d.record)) : Bytes(d.record, 0);
            if (a != b) e.slots.push_back({uint32_t(i), std::move(b)});
        }
        return e;
    }
    std::map<Bytes, int> have;
    for (size_t at = 0; at < from.size(); at += d.record) ++have[Bytes(from.begin() + long(at), from.begin() + long(at + d.record))];
    for (size_t at = 0; at < to.size(); at += d.record) {
        Bytes rec(to.begin() + long(at), to.begin() + long(at + d.record));
        auto it = have.find(rec);
        if (it != have.end() && it->second > 0) --it->second;
        else e.added.push_back(std::move(rec));
    }
    for (const auto& [rec, n] : have)
        for (int k = 0; k < n; ++k) e.removed.push_back(rec);
    return e;
}

Bytes applyObjects(uint8_t section, const Bytes& data, const ObjEdit& e) {
    const ObjDef& d = kObj[section];
    if (e.whole) return e.data;
    Bytes out = data;
    if (d.slots) {
        for (const auto& [i, rec] : e.slots) {
            const size_t at = size_t(i) * d.record;
            if (out.size() < at + d.record) out.resize(at + d.record, 0);
            std::copy(rec.begin(), rec.end(), out.begin() + long(at));
        }
        return out;
    }
    if (!d.record || out.size() % d.record) return out;
    // (the records by their bytes, and units by their serial: each removed record takes one of them)
    const size_t n = out.size() / d.record;
    std::map<Bytes, std::vector<size_t>> byBytes;
    std::map<uint32_t, std::vector<size_t>> bySerial;
    auto serialAt = [&](const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; };
    for (size_t k = n; k-- > 0;) {  // (from the end: back() is the first)
        const uint8_t* p = out.data() + k * d.record;
        byBytes[Bytes(p, p + d.record)].push_back(k);
        if (section == oUnits) bySerial[serialAt(p)].push_back(k);
    }
    std::vector<uint8_t> gone(n, 0);
    auto take = [&](std::vector<size_t>& v) {
        while (!v.empty() && gone[v.back()]) v.pop_back();
        if (v.empty()) return false;
        gone[v.back()] = 1, v.pop_back();
        return true;
    };
    for (const Bytes& rec : e.removed) {
        if (rec.size() != d.record) continue;
        auto it = byBytes.find(rec);
        if (it != byBytes.end() && take(it->second)) continue;
        if (section == oUnits)  // (moved or changed by another user meanwhile: the unit is the one of that serial)
            if (auto s = bySerial.find(serialAt(rec.data())); s != bySerial.end()) take(s->second);
    }
    Bytes kept;
    kept.reserve(out.size() + e.added.size() * d.record);
    for (size_t k = 0; k < n; ++k)
        if (!gone[k]) kept.insert(kept.end(), out.begin() + long(k * d.record), out.begin() + long(k * d.record + d.record));
    for (const Bytes& rec : e.added)
        if (rec.size() == d.record) kept.insert(kept.end(), rec.begin(), rec.end());
    return kept;
}

bool validObjects(uint8_t section, const Bytes& data, size_t unitTypes) {
    if (section >= kObjSections) return false;
    const ObjDef& d = kObj[section];
    if (data.size() > d.maxBytes || (d.record && data.size() % d.record)) return false;
    if (section == oUnits && unitTypes)
        for (size_t at = 0; at + 36 <= data.size(); at += 36)
            if (size_t(data[at + 8] | data[at + 9] << 8) >= unitTypes) return false;  // (the unit's type: bytes 8 and 9)
    return true;
}

uint32_t objectsHash(const Bytes& data) {  // (FNV-1a: a check that two copies are the same, not a secret)
    uint32_t h = 2166136261u;
    for (uint8_t b : data) h = (h ^ b) * 16777619u;
    return h ^ uint32_t(data.size());
}

// Ops: u8 0, u32 from, u32 count (records the member has, in order); u8 1, a record. Whole sections: one record, all bytes.
Bytes deltaObjects(uint8_t section, const Bytes& from, const Bytes& to) {
    const size_t rec = kObj[section].record;
    Writer w;
    if (!rec || from.size() % rec || to.size() % rec) {
        w.u8(2), w.u32(uint32_t(to.size()));
        w.b.insert(w.b.end(), to.begin(), to.end());
        return w.b;
    }
    // (the records that are the same at the start and at the end are one run each: only the middle is looked up)
    const size_t nf = from.size() / rec, nt = to.size() / rec;
    size_t pre = 0, suf = 0;
    while (pre < nf && pre < nt && std::memcmp(from.data() + pre * rec, to.data() + pre * rec, rec) == 0) ++pre;
    while (suf < nf - pre && suf < nt - pre && std::memcmp(from.data() + (nf - 1 - suf) * rec, to.data() + (nt - 1 - suf) * rec, rec) == 0) ++suf;
    std::map<Bytes, std::deque<uint32_t>> at;
    for (size_t k = pre; k < nf - suf; ++k) at[Bytes(from.begin() + long(k * rec), from.begin() + long(k * rec + rec))].push_back(uint32_t(k));
    uint32_t runFrom = 0, runCount = 0;
    auto flush = [&] {
        if (runCount) w.u8(0), w.u32(runFrom), w.u32(runCount);
        runCount = 0;
    };
    if (pre) runFrom = 0, runCount = uint32_t(pre);
    for (size_t k = pre; k < nt - suf; ++k) {
        Bytes r(to.begin() + long(k * rec), to.begin() + long(k * rec + rec));
        auto it = at.find(r);
        if (it != at.end() && !it->second.empty()) {
            const uint32_t i = it->second.front();
            it->second.pop_front();
            if (runCount && runFrom + runCount == i) ++runCount;
            else flush(), runFrom = i, runCount = 1;
        } else {
            flush();
            w.u8(1);
            w.b.insert(w.b.end(), r.begin(), r.end());
        }
    }
    if (suf) {
        if (runCount && runFrom + runCount == nf - suf) runCount += uint32_t(suf);
        else flush(), runFrom = uint32_t(nf - suf), runCount = uint32_t(suf);
    }
    flush();
    return w.b;
}

bool undeltaObjects(uint8_t section, const Bytes& from, const Bytes& delta, Bytes& out) {
    const ObjDef& d = kObj[section];
    out.clear();
    Reader r{delta.data(), delta.size()};
    while (r.ok && r.i < r.n) {
        const uint8_t op = r.u8();
        if (op == 0) {
            const uint32_t a = r.u32(), n = r.u32();
            if (!r.ok || !d.record || (size_t(a) + n) * d.record > from.size() || out.size() + size_t(n) * d.record > d.maxBytes) return false;
            out.insert(out.end(), from.begin() + long(size_t(a) * d.record), from.begin() + long((size_t(a) + n) * d.record));
        } else if (op == 1) {
            if (!d.record || !r.need(d.record) || out.size() + d.record > d.maxBytes) return false;
            out.insert(out.end(), r.p + r.i, r.p + r.i + d.record);
            r.i += d.record;
        } else if (op == 2) {
            const uint32_t n = r.u32();
            if (!r.ok || n > d.maxBytes || !r.need(n) || !out.empty()) return false;
            out.assign(r.p + r.i, r.p + r.i + n);
            r.i += n;
        } else {
            return false;
        }
    }
    return r.ok;
}

// --- Keys ---------------------------------------------------------------------------------------------------------------

struct Session::Link {
    Key ePriv{}, ePub{}, peerPub{};
    uint8_t dh[32]{};
    bool gotHello = false, keyed = false;
    Key tx{}, rx{};
    uint64_t txN = 0, rxN = 0;
    std::string code;
    ~Link() {
        crypto_wipe(ePriv.data(), ePriv.size()), crypto_wipe(dh, sizeof dh);
        crypto_wipe(tx.data(), tx.size()), crypto_wipe(rx.data(), rx.size());
    }
    bool makeEphemeral() {
        if (!randomBytes(ePriv.data(), ePriv.size())) return false;
        crypto_x25519_public_key(ePub.data(), ePriv.data());
        return true;
    }
    // dh from the peer's key; false for a key that gives no secret (a low-order point).
    bool agree(const uint8_t* peer) {
        std::memcpy(peerPub.data(), peer, 32);
        crypto_x25519(dh, ePriv.data(), peerPub.data());
        uint8_t zero = 0;
        for (uint8_t b : dh) zero |= b;
        return zero != 0;
    }
    // The two directions' keys for a link secret. joinerPub, hostPub: the transcript.
    static void derive(const Secret& secret, const uint8_t dh[32], const uint8_t* joinerPub, const uint8_t* hostPub, Key& j2h, Key& h2j) {
        uint8_t msg[sizeof kKdfLabel - 1 + 96], out[64];
        std::memcpy(msg, kKdfLabel, sizeof kKdfLabel - 1);
        std::memcpy(msg + sizeof kKdfLabel - 1, dh, 32);
        std::memcpy(msg + sizeof kKdfLabel - 1 + 32, joinerPub, 32);
        std::memcpy(msg + sizeof kKdfLabel - 1 + 64, hostPub, 32);
        crypto_blake2b_keyed(out, sizeof out, secret.data(), secret.size(), msg, sizeof msg);
        std::memcpy(j2h.data(), out, 32), std::memcpy(h2j.data(), out + 32, 32);
        crypto_wipe(out, sizeof out), crypto_wipe(msg, sizeof msg);
    }
    static std::string safetyCode(const uint8_t dh[32], const uint8_t* joinerPub, const uint8_t* hostPub) {
        uint8_t msg[96], out[32];
        std::memcpy(msg, dh, 32), std::memcpy(msg + 32, joinerPub, 32), std::memcpy(msg + 64, hostPub, 32);
        crypto_blake2b(out, sizeof out, msg, sizeof msg);
        const uint32_t v = (uint32_t(out[0]) << 16 | uint32_t(out[1]) << 8 | out[2]) % 1000000;
        char text[8];
        std::snprintf(text, sizeof text, "%03u %03u", v / 1000, v % 1000);
        return text;
    }
    static void nonce(uint64_t n, uint8_t out[24]) {
        std::memset(out, 0, 24);
        for (int i = 0; i < 8; ++i) out[i] = uint8_t(n >> (8 * i));
    }
    Bytes seal(const Bytes& plain) {
        Bytes f(1 + 16 + plain.size());
        f[0] = kSealed;
        uint8_t n[24];
        nonce(txN++, n);
        crypto_aead_lock(f.data() + 17, f.data() + 1, tx.data(), n, nullptr, 0, plain.data(), plain.size());
        return f;
    }
    // A sealed frame (after its first byte) with key k and counter `counter`. False: not from the peer (or replayed).
    static bool open(const Key& k, uint64_t counter, const uint8_t* p, size_t n, Bytes& plain) {
        if (n < 16) return false;
        plain.resize(n - 16);
        uint8_t nn[24];
        nonce(counter, nn);
        return crypto_aead_unlock(plain.data(), p, k.data(), nn, nullptr, 0, p + 16, n - 16) == 0;
    }
};

// --- The session --------------------------------------------------------------------------------------------------------

Session::~Session() = default;

std::unique_ptr<Session> Session::host(Editor& editor, const std::string& server, const std::string& name, Outbox& out, Events ev, std::string& error) {
    std::unique_ptr<Session> s(new Session);
    if (!editor.isOpen() || !editor.canPaintTerrain()) {
        error = "This map's terrain cannot be shared: it has no isometric terrain (ISOM).";
        return nullptr;
    }
    if (!readTerrain(editor.chk(), s->base_, error)) return nullptr;
    if (!normalizeServer(server, s->server_, error)) return nullptr;
    if (!randomBytes(s->secrets_[1].data(), s->secrets_[1].size()) || !randomBytes(s->secrets_[2].data(), s->secrets_[2].size()) ||
        !randomBytes(s->hostSecret_.data(), s->hostSecret_.size()) || !randomBytes(s->memberSecret_.data(), s->memberSecret_.size())) {
        error = "The system gave no random numbers.";
        return nullptr;
    }
    s->host_ = true, s->role_ = Role::Edit, s->ed_ = &editor, s->out_ = &out, s->ev_ = std::move(ev);
    s->hasMemberSecret_ = true;
    s->name_ = cleanName(name);
    s->seenRevision_ = editor.revision();
    s->maxState_ = editor.stateId();
    for (uint8_t k = 0; k < kObjSections; ++k) {
        s->objBase_[k] = readObjects(editor.chk(), k);
        if (!validObjects(k, s->objBase_[k], 0)) {
            error = "This map's units, doodads, sprites, locations or strings are larger than collaborative editing takes.";
            return nullptr;
        }
        s->slotWriter_[k].assign(kObj[k].slots ? s->objBase_[k].size() / kObj[k].record : 0, 0);
    }
    s->slotLimit_ = std::max<size_t>(s->objBase_[oLocations].size(), 64 * 20);
    for (int k = 0; k < kSections; ++k) s->writer_[k].assign(s->base_.cells[k].size(), 0);
    return s;
}

std::unique_ptr<Session> Session::join(const InviteLink& link, const std::string& name, const std::string& version, Outbox& out, Events ev) {
    std::unique_ptr<Session> s(new Session);
    s->server_ = link.server, s->room_ = link.room, s->secrets_[0] = link.secret, s->role_ = link.role;
    s->name_ = cleanName(name), s->version_ = version.substr(0, 64);
    s->out_ = &out, s->ev_ = std::move(ev);
    return s;
}

void Session::end(const std::string& why, bool notify) {
    if (ended_) return;
    ended_ = true;
    out_->close();
    links_.clear();  // (the keys wiped)
    for (Secret& k : secrets_) crypto_wipe(k.data(), k.size());
    crypto_wipe(hostSecret_.data(), hostSecret_.size()), crypto_wipe(memberSecret_.data(), memberSecret_.size());
    tickets_.clear(), ticketOf_.clear();
    if (notify && ev_.ended) ev_.ended(why);
}

void Session::opened() {
    if (ended_) return;
    const std::string tokens = ",\"view\":" + quote(relayToken(secrets_[1])) + ",\"edit\":" + quote(relayToken(secrets_[2])) + ",\"host\":" + quote(relayToken(hostSecret_)) +
                               ",\"member\":" + quote(relayToken(memberSecret_));
    if (host_ && !room_.empty()) {  // (a new connection after one was lost: the room back)
        out_->text("{\"t\":\"resume\",\"v\":1,\"room\":" + quote(room_) + tokens + "}");
        if (ev_.status) ev_.status("Connected to the server again; resuming the session");
    } else if (host_) {
        out_->text("{\"t\":\"create\",\"v\":1" + tokens + "}");
        if (ev_.status) ev_.status("Connected to the server; making the invite links");
    } else {
        out_->text("{\"t\":\"join\",\"v\":1,\"room\":" + quote(room_) + ",\"token\":" + quote(relayToken(secrets_[0])) + "}");
        if (ev_.status) ev_.status(reconnecting_ ? "Connected to the server again; waiting for the host" : "Connected to the server; waiting for the host");
    }
}

// The connection ended. A session that runs (the host has a room; a member has the map) tries again when the connection
// was lost (not when the server closed it on purpose: removed, ended, not valid).
void Session::closed(const std::string& why) {
    if (ended_) return;
    if (reconnecting_) {  // (a try that did not work: the program tries again, or gives up)
        if (ev_.connection) ev_.connection(true);
        return;
    }
    const bool established = host_ ? !room_.empty() : ed_ != nullptr;
    const bool onPurpose = why.empty() || why.find("(1008)") != std::string::npos || why.find("(1003)") != std::string::npos ||
                           why.find("(1009)") != std::string::npos;
    if (established && !onPurpose && ev_.connection) return lost();
    end(why.empty() ? "The connection to the server ended." : "The connection to the server ended (" + why + ").");
}

void Session::lost() {
    reconnecting_ = true;
    links_.clear(), linkTime_.clear();  // (new keys with each member: their old counters are of no use)
    later_.clear();
    if (host_) {  // (the members come back with their tickets)
        peers_.clear();
        objSentTo_.clear(), objKnown_.clear(), objAck_.clear();
        hostObjLater_.clear();
        ticketOf_.clear();
    } else {
        myId_ = 0;
        objLater_.clear();
        for (Peer& p : peers_) p.cursorOn = false, p.chat.clear(), p.presence = {};
    }
    if (ev_.status) ev_.status("The connection to the server was lost. Reconnecting");
    if (ev_.peersChanged) ev_.peersChanged();
    if (ev_.connection) ev_.connection(true);
}

void Session::reconnectFailed() {
    if (!ended_) end("The connection to the server was lost. The session ended.");
}

void Session::received(const std::string& text) {
    if (ended_) return;
    if (text.size() > 4096) return end("The server sent a message that is not valid.");
    JsonParser parser{text};
    const Json j = parser.value();
    if (!parser.ok || j.type != Json::Type::Object) return end("The server sent a message that is not valid.");
    const std::string t = j.str("t");
    if (t == "error") {
        const std::string code = j.str("code");
        if (code == "not-found" && reconnecting_) return end("The session ended while the connection was lost.");
        if (code == "not-found") return end("The invite link does not work: the session ended, or the host made new links.");
        if (code == "full") return end("The shared map is full.");
        if (code == "busy" || code == "too-many-rooms") return end("The server cannot take more shared maps now. Try again later.");
        if (code == "slow-down") return end("Too many tries. Wait a minute, then try again.");
        return end("The server refused the session.");
    }
    if (host_) {
        if (t == "created") {
            const std::string room = j.str("room");
            if (!roomId(room)) return end("The server sent a message that is not valid.");
            room_ = room;
            if (ev_.linksChanged) ev_.linksChanged();
            if (ev_.status) ev_.status("Sharing the map: copy an invite link");
        } else if (t == "resumed") {
            reconnecting_ = false;
            if (ev_.status) ev_.status("Reconnected: the session is live again");
            if (ev_.connection) ev_.connection(false);
            if (ev_.linksChanged) ev_.linksChanged();
        } else if (t == "peer") {
            const int id = j.integer("id", 0);
            if (id <= 0) return;
            size_t waiting = 0;
            for (const auto& [k, l] : links_)
                if (!l->keyed) ++waiting;
            if (waiting >= kMaxWaiting) {
                out_->text("{\"t\":\"kick\",\"id\":" + std::to_string(id) + "}");
                return;
            }
            links_[uint32_t(id)] = std::make_unique<Link>();
            linkTime_[uint32_t(id)] = now();
        } else if (t == "left") {
            const int id = j.integer("id", 0);
            if (id > 0) forget(uint32_t(id));
        }
        return;
    }
    if (t == "joined") {
        const int id = j.integer("id", 0);
        if (id <= 0 || links_.count(0)) return end("The server sent a message that is not valid.");
        auto l = std::make_unique<Link>();
        if (!l->makeEphemeral()) return end("The system gave no random numbers.");
        Bytes f{kHello};
        f.insert(f.end(), l->ePub.begin(), l->ePub.end());
        links_[0] = std::move(l);
        out_->binary(std::move(f));
    }
}

void Session::received(const Bytes& data) {
    if (ended_) return;
    if (host_) {
        if (data.size() < 5) return;
        const uint32_t from = uint32_t(data[0]) << 24 | uint32_t(data[1]) << 16 | uint32_t(data[2]) << 8 | data[3];
        hostFrame(from, data.data() + 4, data.size() - 4);
    } else {
        guestFrame(data.data(), data.size());
    }
    if (!ended_) tick();
}

void Session::sendSealed(Link& l, uint32_t id, const Bytes& plain) {
    Bytes f = l.seal(plain);
    if (host_) {
        Bytes framed{uint8_t(id >> 24), uint8_t(id >> 16), uint8_t(id >> 8), uint8_t(id)};
        framed.insert(framed.end(), f.begin(), f.end());
        out_->binary(std::move(framed));
    } else {
        out_->binary(std::move(f));
    }
}

void Session::sendTo(uint32_t id, const Bytes& plain) {
    auto it = links_.find(id);
    if (it != links_.end() && it->second->keyed) sendSealed(*it->second, id, plain);
}

Peer* Session::peer(uint32_t id) {
    for (Peer& p : peers_)
        if (p.id == id) return &p;
    return nullptr;
}

void Session::forget(uint32_t id) {
    links_.erase(id);
    objSentTo_.erase(id), objKnown_.erase(id), objAck_.erase(id);
    linkTime_.erase(id);
    ticketOf_.erase(id);  // (its ticket stays: the member may come back with it)
    const bool had = peer(id) != nullptr;
    const bool wasIn = had && peer(id)->approved;
    std::string name = had ? peer(id)->name : "";
    std::erase_if(peers_, [&](const Peer& p) { return p.id == id; });
    if (wasIn) {
        broadcastPeers();
        if (ev_.status) ev_.status(name + " left the shared map");
    }
    if (had && ev_.peersChanged) ev_.peersChanged();
}

void Session::hostFrame(uint32_t from, const uint8_t* p, size_t n) {
    auto it = links_.find(from);
    if (it == links_.end()) return;
    Link& l = *it->second;
    auto drop = [&] {
        out_->text("{\"t\":\"kick\",\"id\":" + std::to_string(from) + "}");
        forget(from);
    };
    if (p[0] == kHello) {
        if (l.gotHello || n != 33 || !l.makeEphemeral() || !l.agree(p + 1)) return drop();
        l.gotHello = true;
        Bytes f{kHelloBack};
        f.insert(f.end(), l.ePub.begin(), l.ePub.end());
        Bytes framed{uint8_t(from >> 24), uint8_t(from >> 16), uint8_t(from >> 8), uint8_t(from)};
        framed.insert(framed.end(), f.begin(), f.end());
        out_->binary(std::move(framed));
        return;
    }
    if (p[0] != kSealed || !l.gotHello) return drop();
    Bytes plain;
    if (!l.keyed) {  // the first sealed message: which link's secret opens it gives the role
        for (Role r : {Role::Edit, Role::View, Role::None}) {
            if (r == Role::None && !hasMemberSecret_) continue;
            Key j2h, h2j;
            Link::derive(r == Role::None ? memberSecret_ : secrets_[int(r)], l.dh, l.peerPub.data(), l.ePub.data(), j2h, h2j);
            if (Link::open(j2h, 0, p + 1, n - 1, plain)) {
                l.rx = j2h, l.tx = h2j, l.keyed = true, l.rxN = 1;
                l.code = Link::safetyCode(l.dh, l.peerPub.data(), l.ePub.data());
                Reader rd{plain.data(), plain.size()};
                if (rd.u8() != mJoin || rd.u8() != kProto) return drop();
                Peer np;
                np.id = from, np.name = cleanName(rd.str()), np.role = r, np.code = l.code;
                rd.str();  // (the program's version: not used yet)
                const std::string ticket = rd.str();
                if (!rd.done()) return drop();
                // A member that comes back (its connection was lost, or the session has a new host) with its ticket: in
                // at once, as the same person (the ticket is used: the welcome gives a new one).
                if (auto t = tickets_.find(ticket); !ticket.empty() && t != tickets_.end()) {
                    np.color = t->second.color, np.role = t->second.role;
                    resumeAck_[from] = t->second.lastSeq;
                    tickets_.erase(t);
                    peers_.push_back(np);
                    approve(from, true);
                    return;
                }
                if (r == Role::None) return drop();  // (the members' secret with no ticket: a removed member, or not one)
                size_t asking = 0;
                for (const Peer& o : peers_) asking += !o.approved;
                if (asking >= kMaxAsking) return drop();  // (no more questions at one time)
                np.color = nextColor_;
                nextColor_ = uint8_t(nextColor_ % 7 + 1);
                peers_.push_back(np);
                if (ev_.peersChanged) ev_.peersChanged();
                if (ev_.joinRequest) ev_.joinRequest(np);
                return;
            }
            crypto_wipe(j2h.data(), 32), crypto_wipe(h2j.data(), 32);
        }
        return drop();  // (an old link's secret, or not from a link at all)
    }
    if (!Link::open(l.rx, l.rxN, p + 1, n - 1, plain)) return drop();
    ++l.rxN;
    hostMessage(from, plain);
}

void Session::guestFrame(const uint8_t* p, size_t n) {
    auto it = links_.find(0);
    if (n < 1 || it == links_.end()) return end("The server sent a message that is not valid.");
    Link& l = *it->second;
    if (p[0] == kHelloBack) {
        if (l.gotHello || n != 33 || !l.agree(p + 1)) return end("The host's answer is not valid.");
        l.gotHello = true;
        Link::derive(secrets_[0], l.dh, l.ePub.data(), l.peerPub.data(), l.tx, l.rx);
        l.keyed = true;
        l.code = Link::safetyCode(l.dh, l.ePub.data(), l.peerPub.data());
        Writer w;
        w.u8(mJoin), w.u8(kProto), w.str(name_), w.str(version_), w.str(ticket_);
        sendSealed(l, 0, w.b);
        if (ev_.status) ev_.status("Waiting for the host to let you in (safety code " + l.code + ")");
        return;
    }
    Bytes plain;
    if (p[0] != kSealed || !l.keyed || !Link::open(l.rx, l.rxN, p + 1, n - 1, plain))
        return end("A message did not come from the host (it could not be opened). The session ended.");
    ++l.rxN;
    guestMessage(plain);
}

void Session::hostMessage(uint32_t from, const Bytes& m) {
    Peer* pr = peer(from);
    if (!pr || m.empty()) return;
    Reader r{m.data(), m.size()};
    const uint8_t type = r.u8();
    auto drop = [&](const char* why) {
        Writer w;
        w.u8(mBye), w.str(why);
        sendTo(from, w.b);
        out_->text("{\"t\":\"kick\",\"id\":" + std::to_string(from) + "}");
        forget(from);
    };
    if (type == mBye) {  // (left on purpose: no coming back with the ticket)
        if (auto t = ticketOf_.find(from); t != ticketOf_.end()) tickets_.erase(t->second), ticketOf_.erase(t);
        return forget(from);
    }
    if (!pr->approved) return;  // (nothing but goodbye before the host's yes)
    if (type == mPresence) {
        Presence p;
        if (!readPresence(r, p) || !r.done()) return drop("A message was not valid.");
        if (!(p == pr->presence)) pr->presence = std::move(p), presencesDirty_ = true;
        if (ev_.peersChanged) ev_.peersChanged();
        return;
    }
    if (type == mPing) {
        const uint16_t x = r.u16(), y = r.u16();
        if (!r.done()) return drop("A message was not valid.");
        Writer w;
        w.u8(mPinged), w.u32(from), w.u16(x), w.u16(y);
        for (const Peer& o : peers_)
            if (o.approved && o.id != from) sendTo(o.id, w.b);
        if (ev_.ping) ev_.ping(from, x, y);
        return;
    }
    if (type == mRename) {  // (a member's new display name: everyone gets the list again)
        const std::string name = cleanName(r.str());
        if (!r.done()) return drop("A message was not valid.");
        if (name != pr->name) pr->name = name, broadcastPeers();
        if (ev_.peersChanged) ev_.peersChanged();
        return;
    }
    if (type == mCursor) {
        const uint8_t on = r.u8();
        const uint16_t x = r.u16(), y = r.u16();
        std::string text = r.str();
        if (!r.done() || on > 1 || text.size() > kMaxChat) return drop("A message was not valid.");
        pr->cursorOn = on, pr->x = x, pr->y = y, pr->cursorTime = now(), pr->chat = cleanText(text, kMaxChat);
        cursorsDirty_ = true;  // (sendCursors: all at once, at most 10 times a second)
        if (ev_.peersChanged) ev_.peersChanged();
        return;
    }
    if (type == mObjResync) {
        const uint8_t s = r.u8();
        if (!r.done() || s >= kObjSections) return drop("A message was not valid.");
        objKnown_[from][s] = false;
        objDirty_[s] = true;
        return;
    }
    if (type == mObjEdit) {
        const uint32_t seq = r.u32();
        ObjEdit e;
        if (!readObjEdit(r, e) || !r.done()) return drop("A change was not valid.");
        if (pr->role != Role::Edit) {  // (view only: the host's state goes back to it)
            objAck_[from] = seq;
            objDirty_[e.section] = true;
            return;
        }
        hostObjLater_.push_back({from, seq, std::move(e)});  // (tick applies it, after this program's own edits, not in a stroke)
        return;
    }
    if (type == mPatch) {
        const uint32_t seq = r.u32();
        Patch p;
        const size_t maxCells = base_.cells[kIsom].size() + base_.cells[kMtxm].size() + base_.cells[kTile].size();
        if (!readPatchFrom(r, p, maxCells) || !r.done() || !validPatch(base_, p, ed_ ? ed_->tileset().groups.size() : 0))
            return drop("A change was not valid.");
        later_.push_back({from, seq, std::move(p)});
        return;  // (tick applies it, after this program's own changes)
    }
    drop("A message was not valid.");
}

void Session::guestMessage(const Bytes& m) {
    if (m.empty()) return;
    Reader r{m.data(), m.size()};
    const uint8_t type = r.u8();
    auto bad = [&] { end("The host sent data that is not valid. The session ended."); };
    switch (type) {
    case mWelcome: {
        if (ed_ && !reconnecting_) return bad();
        const uint8_t role = r.u8();
        const uint32_t id = r.u32();
        const std::string hostName = cleanName(r.str());
        const uint32_t groups = r.u32();
        const std::string ticket = r.str(), members = r.str();
        const uint32_t ack = r.u32();
        Terrain t;
        if (!readTerrainFrom(r, t) || (role != uint8_t(Role::View) && role != uint8_t(Role::Edit)) || ticket.size() != 16 || members.size() != 16) return bad();
        Bytes objs[kObjSections];
        for (uint8_t s = 0; s < kObjSections; ++s) {
            const uint32_t n = r.u32();
            if (!r.ok || n > kObj[s].maxBytes || !r.need(n)) return bad();
            objs[s].assign(r.p + r.i, r.p + r.i + n);
            r.i += n;
            if (!validObjects(s, objs[s], 0)) return bad();
        }
        if (!r.done()) return bad();
        myId_ = id, hostName_ = hostName, ticket_ = ticket;
        std::memcpy(secrets_[0].data(), members.data(), 16);  // (from now on this program comes back with the members' secret)
        role_ = Role(role);
        if (ed_) {  // (back after a lost connection, or with a new host: the map in its tab is the host's again)
            if (groups != tileGroups_) return bad();
            welcomeTerrain_ = std::move(t), welcomeAck_ = ack, welcomeWaiting_ = true;  // (tick: not in a stroke or a hold)
            for (uint8_t s = 0; s < kObjSections; ++s) welcomeObjs_[s] = std::move(objs[s]);
            return;
        }
        tileGroups_ = groups;
        for (uint8_t s = 0; s < kObjSections; ++s) objBase_[s] = std::move(objs[s]);
        base_ = t;
        for (int s = 0; s < kSections; ++s) writer_[s].assign(base_.cells[s].size(), 0);
        if (!ev_.welcome || !ev_.welcome(t, hostName_, role_)) return end("The shared map could not be opened.");
        if (!ed_) return end("The shared map could not be opened.");
        if (ev_.status) ev_.status("You joined " + hostName_ + "'s map to " + roleName(role_));
        return;
    }
    case mDeny: {
        r.str();
        return end("The host did not let you in.");
    }
    case mBye: {
        // The host's own sentence, whole (a name's 32-byte limit cut it); none: the general one.
        const std::string why = cleanText(r.str(), 255);
        return end(why.empty() ? "The host ended the session." : why);
    }
    case mRole: {
        const uint8_t role = r.u8();
        if (!r.done() || (role != uint8_t(Role::View) && role != uint8_t(Role::Edit))) return bad();
        role_ = Role(role);
        if (ev_.status) ev_.status(role_ == Role::Edit ? "The host lets you edit the map now" : "The host lets you view the map only now");
        if (ev_.peersChanged) ev_.peersChanged();
        return;
    }
    case mPeers: {
        const uint8_t n = r.u8();
        std::vector<Peer> list;
        for (int i = 0; i < n && r.ok; ++i) {
            Peer p;
            p.id = r.u32();
            const uint8_t role = r.u8();
            p.color = r.u8();
            p.name = cleanName(r.str());
            p.approved = true;
            if (role != uint8_t(Role::View) && role != uint8_t(Role::Edit)) return bad();
            p.role = Role(role);
            p.color &= 7;
            if (p.id == myId_) continue;
            if (const Peer* old = peer(p.id))
                p.cursorOn = old->cursorOn, p.x = old->x, p.y = old->y, p.cursorTime = old->cursorTime, p.chat = old->chat, p.presence = old->presence;
            list.push_back(p);
        }
        if (!r.done()) return bad();
        peers_ = std::move(list);
        if (ev_.peersChanged) ev_.peersChanged();
        return;
    }
    case mCursors: {
        const uint8_t n = r.u8();
        for (int i = 0; i < n && r.ok; ++i) {
            const uint32_t id = r.u32();
            const uint8_t on = r.u8();
            const uint16_t x = r.u16(), y = r.u16();
            const std::string text = r.str();
            if (on > 1 || text.size() > kMaxChat) return bad();
            Peer* p = id == myId_ ? nullptr : peer(id);
            if (p && (p->cursorOn != bool(on) || p->x != x || p->y != y || p->chat != text))
                p->cursorOn = on, p->x = x, p->y = y, p->cursorTime = now(), p->chat = cleanText(text, kMaxChat);
        }
        if (!r.done()) return bad();
        if (ev_.peersChanged) ev_.peersChanged();
        return;
    }
    case mObjState: {
        if (!ed_) return bad();
        const uint8_t section = r.u8();
        const uint32_t ack = r.u32(), writer = r.u32();
        const uint8_t delta = r.u8();
        const uint32_t hash = r.u32(), n = r.u32();
        if (!r.ok || section >= kObjSections || delta > 1 || n > kObj[section].maxBytes * 2 + 64 || !r.need(n)) return bad();
        Bytes data(r.p + r.i, r.p + r.i + n);
        r.i += n;
        if (!r.done()) return bad();
        if (!delta && !validObjects(section, data, ed_->unitTypes().size())) return bad();
        objLater_.push_back({section, ack, writer, std::move(data), delta == 1, hash});
        return;
    }
    case mPatch: {
        if (!ed_) return bad();
        const uint32_t author = r.u32();
        Patch p;
        const size_t maxCells = base_.cells[kIsom].size() + base_.cells[kMtxm].size() + base_.cells[kTile].size();
        if (!readPatchFrom(r, p, maxCells) || !r.done() || !validPatch(base_, p, ed_->tileset().groups.size())) return bad();
        later_.push_back({author, 0, std::move(p)});
        return;
    }
    case mAck:
    case mReject: {
        const uint32_t seq = r.u32();
        Patch correction;
        if (type == mReject) {
            r.str();
            const size_t maxCells = base_.cells[kIsom].size() + base_.cells[kMtxm].size() + base_.cells[kTile].size();
            if (!readPatchFrom(r, correction, maxCells) || !validPatch(base_, correction, 0)) return bad();
        }
        if (!r.done()) return bad();
        // (an answer waits behind the patches that came before it, so the order of cells stays the host's)
        later_.push_back({type == mReject ? 0xFFFFFFFEu : 0xFFFFFFFFu, seq, std::move(correction)});
        return;
    }
    case mPresences: {
        const uint8_t n = r.u8();
        for (int i = 0; i < n && r.ok; ++i) {
            const uint32_t id = r.u32();
            Presence p;
            if (!readPresence(r, p)) return bad();
            if (Peer* o = id == myId_ ? nullptr : peer(id)) o->presence = std::move(p);
        }
        if (!r.done()) return bad();
        if (ev_.peersChanged) ev_.peersChanged();
        return;
    }
    case mPinged: {
        const uint32_t id = r.u32();
        const uint16_t x = r.u16(), y = r.u16();
        if (!r.done()) return bad();
        if (id != myId_ && ev_.ping) ev_.ping(id, x, y);
        return;
    }
    case mHandover: {
        if (!ed_ || handoverWaiting_) return bad();
        handover_.assign(m.begin() + 1, m.end()), handoverWaiting_ = true;  // (tick: after the host's last edits are in)
        return;
    }
    default: return bad();
    }
}

bool Session::applyToEditor(const Patch& p) {
    if (!ed_ || p.empty()) return true;
    const bool unsent = ed_->revision() != seenRevision_;  // (this program's edits that are not sent yet: the next flush sends them)
    if (!ed_->editTerrainSections([&](Chk& c) { writePatch(c, p); })) return false;
    if (!unsent) seenRevision_ = ed_->revision();
    maxState_ = std::max(maxState_, ed_->stateId());
    return true;
}

// `editorNow`: the editor's terrain at this time (its cells that differ from base_ are this program's edits that are not
// sent yet: they keep their values, and go after this patch, so the host's order stays).
void Session::applyRemote(const Waiting& w, Terrain* editorNow) {
    if (host_) {
        Peer* pr = peer(w.from);
        if (!pr || !pr->approved) return;
        // The values now: what goes back to the author when the host cannot take the change.
        Patch before = w.patch;
        for (Run& r : before) std::copy_n(base_.cells[r.section].begin() + r.at, r.values.size(), r.values.begin());
        auto reject = [&](const char* why) {
            Writer rw;
            rw.u8(mReject), rw.u32(w.seq), rw.str(why);
            writePatchTo(rw, before);
            sendTo(w.from, rw.b);
        };
        if (pr->role != Role::Edit) return reject("You can only view this map.");
        // (the host's own edits that are not sent yet keep their cells: they go after this one)
        Patch use;
        for (const Run& r : w.patch) {
            Run cur;
            for (size_t i = 0; i < r.values.size(); ++i) {
                const uint32_t cell = uint32_t(r.at + i);
                if (editorNow && editorNow->cells[r.section][cell] != base_.cells[r.section][cell]) {
                    if (!cur.values.empty()) use.push_back(std::move(cur)), cur = Run{};
                    continue;
                }
                if (cur.values.empty()) cur.section = r.section, cur.at = cell;
                cur.values.push_back(r.values[i]);
                if (editorNow) editorNow->cells[r.section][cell] = r.values[i];
            }
            if (!cur.values.empty()) use.push_back(std::move(cur));
        }
        Patch was = use;
        for (Run& r : was) std::copy_n(base_.cells[r.section].begin() + r.at, r.values.size(), r.values.begin());
        if (!applyToEditor(use)) return reject("The change could not be made.");
        if (!ed_->canPaintTerrain()) {  // (the ISOM could not be read after it: back as it was)
            applyToEditor(was);
            return reject("The change could not be made.");
        }
        applyPatch(base_, w.patch);
        for (const Run& r : w.patch) std::fill_n(writer_[r.section].begin() + r.at, r.values.size(), w.from);
        Writer ack;
        ack.u8(mAck), ack.u32(w.seq);
        noteApplied(w.from, w.seq);
        sendTo(w.from, ack.b);
        Writer out;
        out.u8(mPatch), out.u32(w.from);
        writePatchTo(out, w.patch);
        for (const Peer& o : peers_)
            if (o.approved && o.id != w.from) sendTo(o.id, out.b);
        report(w.from, 0, &use, nullptr, nullptr);
        return;
    }
    // Guest. An answer to one of this program's changes: its cells are no longer waiting.
    if (w.from >= 0xFFFFFFFEu) {
        if (auto it = unconfirmed_.find(w.seq); it != unconfirmed_.end()) {
            for (const Run& r : it->second)
                for (size_t i = 0; i < r.values.size(); ++i) {
                    auto c = pending_.find(cellKey(r.section, uint32_t(r.at + i)));
                    if (c != pending_.end() && --c->second <= 0) pending_.erase(c);
                }
            unconfirmed_.erase(it);
        }
        if (w.from == 0xFFFFFFFFu) return;
        if (ev_.status) ev_.status("The host did not take a change: it was put back");
    }
    // Others' values, but not for the cells that this program changed and the host has not confirmed yet, nor for
    // those it changed and did not send yet (they go after this, base_ has the others' value).
    Patch use;
    for (const Run& r : w.patch) {
        Run cur;
        for (size_t i = 0; i < r.values.size(); ++i) {
            const uint32_t cell = uint32_t(r.at + i);
            const bool pend = pending_.count(cellKey(r.section, cell)) != 0;
            const bool unsent = editorNow && editorNow->cells[r.section][cell] != base_.cells[r.section][cell];
            if (!pend) {
                base_.cells[r.section][cell] = r.values[i];
                writer_[r.section][cell] = w.from >= 0xFFFFFFFEu ? 0 : w.from;
            }
            if (!pend && !unsent && editorNow) editorNow->cells[r.section][cell] = r.values[i];
            if (pend || unsent) {
                if (!cur.values.empty()) use.push_back(std::move(cur)), cur = Run{};
                continue;
            }
            if (cur.values.empty()) cur.section = r.section, cur.at = cell;
            cur.values.push_back(r.values[i]);
        }
        if (!cur.values.empty()) use.push_back(std::move(cur));
    }
    if (use.empty()) return;
    applyToEditor(use);
    if (w.from < 0xFFFFFFFEu) report(w.from, 0, &use, nullptr, nullptr);
}

// Another person's edit, as this program applied it: the program shows where (Events::changed). Terrain: the tiles
// (MTXM) of the patch; objects: the records that were added or removed, and the slots that changed (locations).
void Session::report(uint32_t who, uint8_t what, const Patch* p, const Bytes* before, const Bytes* after) {
    if (!ev_.changed || who == me()) return;
    Change c;
    c.who = who, c.what = what;
    if (p) {
        const uint32_t w = base_.width ? base_.width : 1;
        for (const Run& r : *p) {
            if (r.section != kMtxm) continue;
            c.count += uint32_t(r.values.size());
            for (size_t i = 0; i < r.values.size(); ++i) {
                const uint32_t cell = uint32_t(r.at + i), x = cell % w, y = cell / w;
                addChangeRect(c, int(x * 32), int(y * 32), int(x * 32 + 32), int(y * 32 + 32));
            }
        }
        if (!c.count) c.count = uint32_t(patchCells(*p));
    } else if (before && after && what >= 1 && what <= kObjSections) {
        const uint8_t s = uint8_t(what - 1);
        if (s == oStrings || s == oStringsX) return;  // (the locations' names: no place of their own)
        const ObjEdit e = diffObjects(s, *before, *after);
        const size_t rec = kObj[s].record;
        auto u16at = [](const Bytes& r, size_t at) { return at + 2 <= r.size() ? int(r[at] | r[at + 1] << 8) : 0; };
        auto u32at = [](const Bytes& r, size_t at) { return at + 4 <= r.size() ? int(r[at] | r[at + 1] << 8 | r[at + 2] << 16 | (r[at + 3] & 0x7F) << 24) : 0; };
        auto place = [&](const Bytes& r) {
            if (s == oLocations) {
                int l = u32at(r, 0), t = u32at(r, 4), rr = u32at(r, 8), b = u32at(r, 12);
                if (l > rr) std::swap(l, rr);
                if (t > b) std::swap(t, b);
                return addChangeRect(c, l, t, std::max(rr, l + 1), std::max(b, t + 1));
            }
            const size_t at = s == oUnits ? 4 : 2;  // (UNIT: serial, x, y; DD2, THG2: id, x, y)
            const int x = u16at(r, at), y = u16at(r, at + 2);
            addChangeRect(c, x - 16, y - 16, x + 16, y + 16);
        };
        for (const Bytes& r : e.added) place(r), ++c.count;
        for (const Bytes& r : e.removed) place(r), ++c.count;
        for (const auto& [i, r] : e.slots) {
            ++c.count;
            const bool empty = std::all_of(r.begin(), r.end(), [](uint8_t b) { return b == 0; });
            if (!empty) place(r);
            else if ((i + 1) * rec <= before->size()) place(Bytes(before->begin() + long(i * rec), before->begin() + long((i + 1) * rec)));
        }
    }
    if (c.count) ev_.changed(c);
}

void Session::sendPatch(const Patch& p) {
    if (p.empty()) return;
    if (host_) {
        Writer w;
        w.u8(mPatch), w.u32(0);
        writePatchTo(w, p);
        for (const Peer& o : peers_)
            if (o.approved) sendTo(o.id, w.b);
        return;
    }
    const uint32_t seq = nextSeq_++;
    for (const Run& r : p)
        for (size_t i = 0; i < r.values.size(); ++i) ++pending_[cellKey(r.section, uint32_t(r.at + i))];
    unconfirmed_[seq] = p;
    Writer w;
    w.u8(mPatch), w.u32(seq);
    writePatchTo(w, p);
    sendTo(0, w.b);
}

bool Session::flushLocal(bool force) {
    if (!ed_ || ed_->revision() == seenRevision_) return false;
    // (a member without the host: the edits stay in the editor; the next welcome sends them on top of the host's map)
    if (!host_ && (reconnecting_ || welcomeWaiting_)) return false;
    if (!force && now() - lastFlush_ < kFlushEvery) return false;
    if (role_ != Role::Edit && ed_->inStroke()) return false;  // (view only: put back after the stroke, which locks the map)
    lastFlush_ = now();
    seenRevision_ = ed_->revision();
    const bool undone = undoFilter_ || ed_->stateId() < maxState_;  // (an older state: the editor's undo or redo)
    undoFilter_ = false;
    maxState_ = std::max(maxState_, ed_->stateId());
    flushObjects(undone);
    if (ended_) return false;
    Terrain cur;
    std::string err;
    if (!readTerrain(ed_->chk(), cur, err)) {
        end("The shared map's terrain is not there any more. The session ended.");
        return false;
    }
    if (cur.width != base_.width || cur.height != base_.height || cur.era != base_.era || cur.hasTile != base_.hasTile) {
        end("The map's size or tileset changed. The session ended.");
        return false;
    }
    Patch p = diffTerrain(base_, cur);
    if (p.empty()) return false;
    if (role_ != Role::Edit) {  // view only: back as the host has it
        applyToEditor(diffTerrain(cur, base_));
        if (ev_.status) ev_.status("View only: edits are not sent");
        return false;
    }
    if (undone) {  // an undo or a redo: the cells that another user wrote last go back to the values they have
        Patch keep, back;
        for (const Run& r : p)
            for (size_t i = 0; i < r.values.size(); ++i) {
                const uint32_t cell = uint32_t(r.at + i);
                const bool mine = writer_[r.section][cell] == me();
                Patch& to = mine ? keep : back;
                if (to.empty() || to.back().section != r.section || to.back().at + to.back().values.size() != cell) to.push_back(Run{r.section, cell, {}});
                to.back().values.push_back(mine ? r.values[i] : base_.cells[r.section][cell]);
            }
        applyToEditor(back);
        p = std::move(keep);
        if (p.empty()) return false;
    }
    applyPatch(base_, p);
    for (const Run& r : p) std::fill_n(writer_[r.section].begin() + r.at, r.values.size(), me());
    sendPatch(p);
    return true;
}

// This user's records (the undo takes back only these): added ones, and removed ones (an undo may put them back).
void Session::noteMine(uint8_t section, const ObjEdit& e) {
    // (a record this user removed may come back with an undo or a redo; one this user added may go)
    for (const Bytes& r : e.removed) {
        auto it = mineAdded_[section].find(r);
        if (it != mineAdded_[section].end() && it->second > 0) --it->second;
        ++mineRemoved_[section][r];
    }
    for (const Bytes& r : e.added) {
        auto it = mineRemoved_[section].find(r);
        if (it != mineRemoved_[section].end() && it->second > 0) --it->second;
        ++mineAdded_[section][r];
    }
    for (const auto& [i, rec] : e.slots) {
        if (slotWriter_[section].size() <= i) slotWriter_[section].resize(i + 1, 0);
        slotWriter_[section][i] = me();
    }
    if (e.whole) objWriter_[section] = me();
}

// An undo's or a redo's edit of the objects: only what this user added, removed or wrote last.
ObjEdit Session::filterUndo(uint8_t section, const ObjEdit& e, const Bytes&) {
    ObjEdit keep;
    keep.section = section;
    std::map<Bytes, int> added = mineAdded_[section], removed = mineRemoved_[section];
    for (const Bytes& r : e.removed)
        if (auto it = added.find(r); it != added.end() && it->second > 0) --it->second, keep.removed.push_back(r);
    for (const Bytes& r : e.added)
        if (auto it = removed.find(r); it != removed.end() && it->second > 0) --it->second, keep.added.push_back(r);
    for (const auto& s : e.slots)
        if (s.first < slotWriter_[section].size() && slotWriter_[section][s.first] == me()) keep.slots.push_back(s);
    if (e.whole && objWriter_[section] == me()) keep.whole = true, keep.data = e.data;
    return keep;
}

// This program's edits of the objects: sent (host: the new state goes to the members).
void Session::flushObjects(bool undone) {
    for (uint8_t s = 0; s < kObjSections && !ended_; ++s) {
        const Bytes cur = readObjects(ed_->chk(), s);
        Bytes& shown = host_ ? objBase_[s] : objShown_[s];
        if (cur == shown) continue;
        ObjEdit e = diffObjects(s, shown, cur);
        if (e.empty()) continue;
        if (role_ != Role::Edit) {  // view only: back as the host has it
            applyObjectsToEditor(s, shown);
            if (ev_.status) ev_.status("View only: edits are not sent");
            continue;
        }
        if (undone) e = filterUndo(s, e, shown);
        const Bytes next = applyObjects(s, shown, e);
        if (!validObjects(s, next, ed_->unitTypes().size()) || (s == oLocations && next.size() > slotLimit_)) {
            applyObjectsToEditor(s, shown);
            if (ev_.status) ev_.status("The change is larger than collaborative editing takes: it was put back");
            continue;
        }
        if (next != cur) applyObjectsToEditor(s, next);  // (an undo's part that is not this user's: back)
        if (e.empty()) continue;
        noteMine(s, e);
        shown = next;
        if (host_) {
            objWriter_[s] = 0;
            objDirty_[s] = true;
            continue;
        }
        const uint32_t seq = nextSeq_++;
        objPending_[s].push_back({seq, e});
        Writer w;
        w.u8(mObjEdit), w.u32(seq);
        writeObjEdit(w, e);
        sendTo(0, w.b);
    }
}

void Session::applyObjectsToEditor(uint8_t section, const Bytes& data) {
    if (!ed_) return;
    const bool unsent = ed_->revision() != seenRevision_;
    if (!ed_->editSections([&](Chk& c) { writeObjects(c, section, data); })) return;
    if (!unsent) seenRevision_ = ed_->revision();
    maxState_ = std::max(maxState_, ed_->stateId());
}

// Host: a member's edit of the objects, merged into the map as it is now (this program's edits were sent first).
void Session::hostObjects(uint32_t from, uint32_t seq, ObjEdit e) {
    if (!ed_ || ended_) return;
    flushLocal(true);
    if (ended_) return;
    const uint8_t s = e.section;
    auto serialOf = [](const Bytes& r) { return uint32_t(r[0]) | uint32_t(r[1]) << 8 | uint32_t(r[2]) << 16 | uint32_t(r[3]) << 24; };
    auto setSerial = [](uint8_t* u, uint32_t v) {
        for (int b = 0; b < 4; ++b) u[b] = uint8_t(v >> (8 * b));
    };
    if (s == oUnits)  // (a unit this member placed that got another serial here: the member's later edits of it use the old one)
        for (auto* list : {&e.removed, &e.added})
            for (Bytes& r : *list)
                if (auto it = serialMap_.find({from, serialOf(r)}); it != serialMap_.end()) setSerial(r.data(), it->second);
    Bytes merged = applyObjects(s, objBase_[s], e);
    if (s == oUnits && merged.size() % 36 == 0) {  // (a unit added with a serial that another unit has: a new one)
        std::set<uint32_t> serials;
        uint32_t top = 0;
        const size_t kept = merged.size() / 36 - e.added.size();
        for (size_t k = 0; k < kept; ++k) {
            const uint8_t* u = merged.data() + k * 36;
            const uint32_t v = uint32_t(u[0]) | uint32_t(u[1]) << 8 | uint32_t(u[2]) << 16 | uint32_t(u[3]) << 24;
            serials.insert(v), top = std::max(top, v);
        }
        for (size_t k = kept; k < merged.size() / 36; ++k) {
            uint8_t* u = merged.data() + k * 36;
            const uint32_t was = uint32_t(u[0]) | uint32_t(u[1]) << 8 | uint32_t(u[2]) << 16 | uint32_t(u[3]) << 24;
            uint32_t v = was;
            if (serials.count(v)) {
                v = ++top;
                setSerial(u, v);
                if (serialMap_.size() < 4096) serialMap_[{from, was}] = v;
            }
            serials.insert(v), top = std::max(top, v);
        }
    }
    objAck_[from] = seq;
    noteApplied(from, seq);
    objDirty_[s] = true;  // (also when nothing changed: the member's state comes back)
    if (!validObjects(s, merged, ed_->unitTypes().size()) || (s == oLocations && merged.size() > slotLimit_)) return;
    if (merged == objBase_[s]) return;
    applyObjectsToEditor(s, merged);
    if (readObjects(ed_->chk(), s) != merged) return;  // (the editor did not take it: nothing changes, the member gets the state back)
    const Bytes old = std::move(objBase_[s]);
    objBase_[s] = merged;
    report(from, uint8_t(1 + s), nullptr, &old, &objBase_[s]);
    objWriter_[s] = from;
    for (const auto& [i, rec] : e.slots) {
        if (slotWriter_[s].size() <= i) slotWriter_[s].resize(i + 1, 0);
        slotWriter_[s][i] = from;
    }
    for (const Bytes& r : e.removed)  // (the host's own records that a member removed are not the host's to undo)
        if (auto it = mineAdded_[s].find(r); it != mineAdded_[s].end() && it->second > 0) --it->second;
}

// Guest: the host's state of a section. This program's edits that the host did not take yet stay on top of it.
void Session::objectsFromHost(uint8_t section, uint32_t ack, uint32_t writer, Bytes data) {
    if (!ed_) return;
    flushLocal(true);  // (this program's last edits go first, an undo's filtered: they become waiting edits)
    if (ended_) return;
    auto& pending = objPending_[section];
    std::map<uint32_t, Bytes> mineDone;  // (the slots of this user's edits that the host took now)
    for (const ObjPending& p : pending)
        if (p.seq <= ack)
            for (const auto& [i, rec] : p.edit.slots) mineDone[i] = rec;
    std::erase_if(pending, [&](const ObjPending& p) { return p.seq <= ack; });
    const Bytes old = objBase_[section];
    objBase_[section] = std::move(data);
    if (kObj[section].slots) {  // (the slots that the host changed: their last writer)
        const size_t rec = kObj[section].record, n = objBase_[section].size() / rec;
        if (slotWriter_[section].size() < n) slotWriter_[section].resize(n, 0);
        for (size_t i = 0; i < n; ++i)
            if (i * rec + rec > old.size() || !std::equal(old.begin() + long(i * rec), old.begin() + long(i * rec + rec), objBase_[section].begin() + long(i * rec))) {
                auto m = mineDone.find(uint32_t(i));
                const bool mine = m != mineDone.end() && std::equal(m->second.begin(), m->second.end(), objBase_[section].begin() + long(i * rec));
                slotWriter_[section][i] = mine ? me() : writer;
            }
    } else if (old != objBase_[section]) {
        objWriter_[section] = writer;
    }
    if (writer != me() && old != objBase_[section]) report(writer, uint8_t(1 + section), nullptr, &old, &objBase_[section]);
    if (section == oUnits && objBase_[section].size() % 36 == 0)  // (this user's units that the host gave a new serial: still this user's)
        for (auto it = mineAdded_[section].begin(); it != mineAdded_[section].end();) {
            const Bytes& r = it->first;
            bool there = false;
            for (size_t at = 0; at < objBase_[section].size() && !there; at += 36) there = std::equal(r.begin(), r.end(), objBase_[section].begin() + long(at));
            Bytes moved;
            if (!there && it->second > 0 && r.size() == 36)
                for (size_t at = 0; at < objBase_[section].size(); at += 36)
                    if (std::equal(r.begin() + 4, r.end(), objBase_[section].begin() + long(at + 4)) && !mineAdded_[section].count(Bytes(objBase_[section].begin() + long(at), objBase_[section].begin() + long(at + 36)))) {
                        moved.assign(objBase_[section].begin() + long(at), objBase_[section].begin() + long(at + 36));
                        break;
                    }
            if (moved.empty()) {
                ++it;
                continue;
            }
            const int n = it->second;
            it = mineAdded_[section].erase(it);
            mineAdded_[section][moved] += n;
        }
    Bytes shown = objBase_[section];
    for (const ObjPending& p : pending) shown = applyObjects(section, shown, p.edit);
    if (shown != readObjects(ed_->chk(), section)) applyObjectsToEditor(section, shown);
    objShown_[section] = std::move(shown);
}

void Session::afterUndo() {
    if (ended_ || !ed_) return;
    undoFilter_ = true;
    flushLocal(true);
}

void Session::tick() {
    if (ended_ || inTick_) return;
    inTick_ = true;
    if (ed_ && !ed_->isOpen()) {
        inTick_ = false;
        return end("The shared map was closed. The session ended.");
    }
    if (ed_) {
        flushLocal();
        if (!ended_ && !hold_ && !ed_->inStroke() && !later_.empty()) {
            std::vector<Waiting> batch = std::move(later_);
            later_.clear();
            Terrain cur;
            std::string err;
            const bool have = readTerrain(ed_->chk(), cur, err) && cur.width == base_.width && cur.height == base_.height && cur.hasTile == base_.hasTile;
            for (const Waiting& w : batch) {
                if (ended_) break;
                applyRemote(w, have ? &cur : nullptr);  // (it keeps `cur` as the editor has it)
            }
        }
    }
    if (!host_ && ed_ && !ended_ && welcomeWaiting_ && !hold_ && !ed_->inStroke()) {  // (back in the session: the host's map again)
        welcomeWaiting_ = false;
        welcomeAgain(welcomeTerrain_, welcomeObjs_, welcomeAck_);
        welcomeTerrain_ = Terrain{};
    }
    // The handover: after the old host's last edits are in this map (they came before it).
    if (!host_ && ed_ && !ended_ && handoverWaiting_ && !hold_ && !ed_->inStroke() && later_.empty() && objLater_.empty() && !welcomeWaiting_) {
        handoverWaiting_ = false;
        const Bytes h = std::move(handover_);
        handover_.clear();
        if (!becomeHost(h.data(), h.size())) end("The host sent data that is not valid. The session ended.");
    }
    if (host_ && ed_ && !ended_ && !hold_ && !ed_->inStroke() && !hostObjLater_.empty()) {
        std::vector<ObjEditWaiting> batch = std::move(hostObjLater_);
        hostObjLater_.clear();
        for (ObjEditWaiting& w : batch)
            if (!ended_) hostObjects(w.from, w.seq, std::move(w.edit));
    }
    if (ed_ && !ended_ && !hold_ && !ed_->inStroke() && !objLater_.empty()) {
        std::vector<ObjWaiting> batch = std::move(objLater_);
        objLater_.clear();
        for (ObjWaiting& w : batch) {
            if (ended_) break;
            Bytes data;
            if (w.delta) {  // (a change from the section as this program has it from the host)
                if (!undeltaObjects(w.section, objBase_[w.section], w.data, data) || objectsHash(data) != w.hash ||
                    !validObjects(w.section, data, ed_->unitTypes().size())) {
                    Writer rs;
                    rs.u8(mObjResync), rs.u8(w.section);
                    sendTo(0, rs.b);  // (the host sends the whole section next)
                    continue;
                }
            } else {
                data = std::move(w.data);
            }
            objectsFromHost(w.section, w.ack, w.writer, std::move(data));
        }
    }
    if (host_ && !ended_ && now() - objSent_ >= kObjectsEvery) {
        bool any = false;
        for (bool d : objDirty_) any |= d;
        if (any) {
            objSent_ = now();
            for (uint8_t s = 0; s < kObjSections; ++s) {
                if (!objDirty_[s]) continue;
                objDirty_[s] = false;
                const uint32_t hash = objectsHash(objBase_[s]);
                Bytes lastHad, lastDelta;  // (the members usually have the same copy: one delta for them)
                bool haveLast = false;
                for (const Peer& p : peers_) {
                    if (!p.approved) continue;
                    Bytes& had = objSentTo_[p.id][s];
                    bool& known = objKnown_[p.id][s];
                    if (known && !(haveLast && had == lastHad)) lastHad = had, lastDelta = deltaObjects(s, had, objBase_[s]), haveLast = true;
                    const Bytes delta = known ? lastDelta : Bytes{};
                    const bool useDelta = known && delta.size() < objBase_[s].size();
                    const Bytes& body = useDelta ? delta : objBase_[s];
                    Writer w;
                    w.u8(mObjState), w.u8(s), w.u32(objAck_[p.id]), w.u32(objWriter_[s]), w.u8(useDelta ? 1 : 0), w.u32(hash), w.u32(uint32_t(body.size()));
                    w.b.insert(w.b.end(), body.begin(), body.end());
                    sendTo(p.id, w.b);
                    had = objBase_[s], known = true;
                }
            }
        }
    }
    if (host_ && !ended_) {  // connections that did not make keys in time go
        std::vector<uint32_t> late;
        for (const auto& [id, at] : linkTime_)
            if (auto it = links_.find(id); it != links_.end() && !it->second->keyed && now() - at > kHelloWait) late.push_back(id);
        for (uint32_t id : late) {
            out_->text("{\"t\":\"kick\",\"id\":" + std::to_string(id) + "}");
            forget(id);
        }
    }
    if (!ended_ && !host_ && ed_ && cursorDirty_ && now() - cursorSent_ >= kCursorEvery) {
        Writer w;
        w.u8(mCursor), w.u8(cursorOn_), w.u16(uint16_t(std::clamp(cursorX_, 0, 65535))), w.u16(uint16_t(std::clamp(cursorY_, 0, 65535)));
        w.str(chat_);
        sendTo(0, w.b);
        cursorDirty_ = false, cursorSent_ = now();
    }
    if (!ended_ && host_ && (cursorDirty_ || cursorsDirty_) && now() - cursorsSent_ >= kCursorsEvery) sendCursors();
    if (!ended_ && !host_ && ed_ && presenceDirty_ && links_.count(0) && now() - presenceSent_ >= kPresenceEvery) {
        Writer w;
        w.u8(mPresence);
        writePresence(w, presence_);
        sendTo(0, w.b);
        presenceDirty_ = false, presenceSent_ = now();
    }
    if (!ended_ && host_ && (presenceDirty_ || presencesDirty_) && now() - presencesSent_ >= kPresenceEvery) sendPresences();
    inTick_ = false;
}

// Host: the presence of all, one message to each member (the member's own is not in it).
void Session::sendPresences() {
    presenceDirty_ = presencesDirty_ = false, presencesSent_ = now();
    std::vector<const Peer*> in;
    for (const Peer& p : peers_)
        if (p.approved) in.push_back(&p);
    for (const Peer* to : in) {
        Writer w;
        w.u8(mPresences), w.u8(uint8_t(in.size()));
        w.u32(0);
        writePresence(w, presence_);
        for (const Peer* p : in)
            if (p != to) w.u32(p->id), writePresence(w, p->presence);
        sendTo(to->id, w.b);
    }
}

void Session::presence(const Presence& p) {
    Presence q = p;
    if (q.units.size() > kMaxSelected) q.units.resize(kMaxSelected);
    if (q.locations.size() > kMaxSelected) q.locations.resize(kMaxSelected);
    if (q == presence_) return;
    presence_ = std::move(q), presenceDirty_ = true;
}

void Session::ping(int x, int y) {
    if (ended_ || reconnecting_) return;
    Writer w;
    if (host_) {
        w.u8(mPinged), w.u32(0), w.u16(clamp16(x)), w.u16(clamp16(y));
        for (const Peer& o : peers_)
            if (o.approved) sendTo(o.id, w.b);
    } else if (ed_) {
        w.u8(mPing), w.u16(clamp16(x)), w.u16(clamp16(y));
        sendTo(0, w.b);
    }
}

// Host: the cursors of all (the host's and the members'), one message to each member, at most 10 times a second.
void Session::sendCursors() {
    cursorDirty_ = cursorsDirty_ = false, cursorsSent_ = now();
    std::vector<const Peer*> in;
    for (const Peer& p : peers_)
        if (p.approved) in.push_back(&p);
    for (const Peer* to : in) {
        Writer w;
        w.u8(mCursors), w.u8(uint8_t(in.size()));  // (the host, and the others: the member itself is not in it)
        w.u32(0), w.u8(cursorOn_), w.u16(uint16_t(std::clamp(cursorX_, 0, 65535))), w.u16(uint16_t(std::clamp(cursorY_, 0, 65535)));
        w.str(chat_);
        for (const Peer* p : in)
            if (p != to) w.u32(p->id), w.u8(p->cursorOn), w.u16(uint16_t(p->x)), w.u16(uint16_t(p->y)), w.str(p->chat);
        sendTo(to->id, w.b);
    }
}

void Session::rename(const std::string& name) {
    const std::string n = cleanName(name);
    if (n == name_ || ended_) return;
    name_ = n;
    if (host_) {
        broadcastPeers();
    } else if (ed_) {  // (after the welcome: the host knows this member)
        Writer w;
        w.u8(mRename), w.str(name_);
        sendTo(0, w.b);
    }
    if (ev_.peersChanged) ev_.peersChanged();
}

void Session::chat(const std::string& text) {
    const std::string t = cleanText(text, kMaxChat);
    if (t == chat_) return;
    chat_ = t, cursorDirty_ = true;
}

void Session::cursor(int x, int y, bool on) {
    if (on == cursorOn_ && (!on || (x == cursorX_ && y == cursorY_))) return;
    cursorX_ = x, cursorY_ = y, cursorOn_ = on, cursorDirty_ = true;
}

void Session::setEditor(Editor* ed) {
    ed_ = ed;
    if (!ed_) return;
    if (tileGroups_ && ed_->tileset().groups.size() != tileGroups_) {  // (the host's map uses another tileset: a mod's, or the game's)
        ed_ = nullptr;
        return end("The host's map uses another tileset (a mod or the base game) than this program. Open StarDraft in the same mode as the host.");
    }
    // The map that welcome made has the host's terrain: what it has now is the start.
    Terrain cur;
    std::string err;
    if (!readTerrain(ed_->chk(), cur, err) || cur.width != base_.width || cur.height != base_.height) {
        ed_ = nullptr;
        return;
    }
    applyToEditor(diffTerrain(cur, base_));
    ed_->editSections([&](Chk& c) {
        for (uint8_t s = 0; s < kObjSections; ++s) writeObjects(c, s, objBase_[s]);
    });
    slotLimit_ = std::max<size_t>(objBase_[oLocations].size(), 64 * 20);
    for (uint8_t s = 0; s < kObjSections; ++s) {
        objShown_[s] = readObjects(ed_->chk(), s);
        slotWriter_[s].assign(kObj[s].slots ? objBase_[s].size() / kObj[s].record : 0, 0);
    }
    seenRevision_ = ed_->revision();
    maxState_ = std::max(maxState_, ed_->stateId());
}

// Guest: the host's map again (after a lost connection, or from a new host). The map in the tab becomes the host's;
// this program's terrain edits that the host may not have (not sent, or not confirmed: a patch has values, so it can go
// twice) and its object edits that were not sent go again on top of it.
void Session::welcomeAgain(const Terrain& t, Bytes* objs, uint32_t ack) {
    Terrain cur;
    std::string err;
    if (!readTerrain(ed_->chk(), cur, err) || cur.width != t.width || cur.height != t.height || cur.hasTile != t.hasTile)
        return end("The shared map changed while the connection was lost. The session ended.");
    // The edits the host applied (up to `ack`) are in its map: they do not go again (another person may have changed
    // those cells or records after them). The others go again on top of the host's map.
    for (auto it = unconfirmed_.begin(); it != unconfirmed_.end();) it = it->first <= ack ? unconfirmed_.erase(it) : std::next(it);
    std::set<uint64_t> again;
    for (const auto& [seq, patch] : unconfirmed_)
        for (const Run& r : patch)
            for (size_t i = 0; i < r.values.size(); ++i) again.insert(cellKey(r.section, uint32_t(r.at + i)));
    Patch mine;
    for (uint8_t s = 0; s < kSections; ++s)
        for (uint32_t c = 0; c < cur.cells[s].size() && c < t.cells[s].size(); ++c) {
            const bool local = cur.cells[s][c] != base_.cells[s][c] || again.count(cellKey(s, c));
            if (!local || cur.cells[s][c] == t.cells[s][c]) continue;
            if (mine.empty() || mine.back().section != s || mine.back().at + mine.back().values.size() != c) mine.push_back(Run{s, c, {}});
            mine.back().values.push_back(cur.cells[s][c]);
        }
    // Objects: the edits sent that the host did not apply, then the ones not sent yet (the editor's against what it showed).
    std::vector<ObjEdit> redo[kObjSections];
    for (uint8_t s = 0; s < kObjSections; ++s) {
        for (const ObjPending& op : objPending_[s])
            if (op.seq > ack) redo[s].push_back(op.edit);
        ObjEdit unsent = diffObjects(s, objShown_[s], readObjects(ed_->chk(), s));
        if (!unsent.empty()) redo[s].push_back(std::move(unsent));
    }
    base_ = t;
    for (uint8_t s = 0; s < kSections; ++s) writer_[s].assign(base_.cells[s].size(), 0);
    pending_.clear(), unconfirmed_.clear(), later_.clear(), objLater_.clear();
    applyToEditor(diffTerrain(cur, t));
    for (uint8_t s = 0; s < kObjSections; ++s) {
        objBase_[s] = std::move(objs[s]);
        objPending_[s].clear();
        objShown_[s] = objBase_[s];
        objWriter_[s] = 0;
        slotWriter_[s].assign(kObj[s].slots ? objBase_[s].size() / kObj[s].record : 0, 0);
        if (readObjects(ed_->chk(), s) != objBase_[s]) applyObjectsToEditor(s, objBase_[s]);
    }
    seenRevision_ = ed_->revision();
    // (this program's edits on top: the next flush sends them)
    if (!mine.empty()) ed_->editTerrainSections([&](Chk& c) { writePatch(c, mine); });
    for (uint8_t s = 0; s < kObjSections; ++s) {
        if (redo[s].empty()) continue;
        Bytes next = objBase_[s];
        for (const ObjEdit& e : redo[s]) next = applyObjects(s, next, e);
        if (next != objBase_[s] && validObjects(s, next, ed_->unitTypes().size())) ed_->editSections([&](Chk& c) { writeObjects(c, s, next); });
    }
    maxState_ = std::max(maxState_, ed_->stateId());
    reconnecting_ = false;
    presenceDirty_ = cursorDirty_ = true;
    if (ev_.status) ev_.status("Reconnected: the session is live again");
    if (ev_.connection) ev_.connection(false);
    if (ev_.peersChanged) ev_.peersChanged();
}

// Host: a member's edit was applied (its ticket keeps the number: after a reconnect, that edit does not go again).
void Session::noteApplied(uint32_t from, uint32_t seq) {
    auto t = ticketOf_.find(from);
    if (t == ticketOf_.end()) return;
    if (auto k = tickets_.find(t->second); k != tickets_.end()) k->second.lastSeq = std::max(k->second.lastSeq, seq);
}

// --- Handing the session over -------------------------------------------------------------------------------------------
// The host gives an editor the room's secrets (the host token's and the links') and the members' tickets, then leaves.
// The new host takes the room on the server with the host token (the server closes the others' connections), and the
// members come back with their tickets: in at once, and each gets the map from the new host.
bool Session::handOver(uint32_t id) {
    Peer* p = peer(id);
    if (!host_ || ended_ || reconnecting_ || !p || !p->approved || p->role != Role::Edit) return false;
    flushLocal(true);
    objSent_ = -1e9;  // (the members' edits that this program applied: their state goes now, before the handover)
    tick();
    if (ended_ || !(p = peer(id))) return false;
    Writer w;
    w.u8(mHandover);
    for (const Secret* s : {&hostSecret_, &secrets_[1], &secrets_[2], &memberSecret_}) w.b.insert(w.b.end(), s->begin(), s->end());
    w.u8(nextColor_);
    const std::string own = ticketOf_.count(id) ? ticketOf_[id] : std::string();
    std::vector<std::pair<const std::string*, const Ticket*>> give;
    for (const auto& [k, t] : tickets_)
        if (k != own && give.size() < 255) give.push_back({&k, &t});
    w.u8(uint8_t(give.size()));
    for (const auto& [k, t] : give) w.str(*k), w.str(t->name), w.u8(t->color), w.u8(uint8_t(t->role)), w.u32(t->lastSeq);
    sendTo(id, w.b);
    const std::string name = p->name;
    end("You handed the session to " + name + ". You keep your map.");
    return true;
}

bool Session::becomeHost(const uint8_t* p, size_t n) {
    Reader r{p, n};
    Secret s[4];
    for (Secret& k : s)
        if (r.need(k.size())) std::memcpy(k.data(), r.p + r.i, k.size()), r.i += k.size();
    const uint8_t color = r.u8(), count = r.u8();
    std::map<std::string, Ticket> tickets;
    for (int i = 0; i < count && r.ok; ++i) {
        const std::string k = r.str();
        Ticket t;
        t.name = cleanName(r.str()), t.color = uint8_t(r.u8() & 7);
        const uint8_t role = r.u8();
        if (k.size() != 16 || (role != uint8_t(Role::View) && role != uint8_t(Role::Edit))) return false;
        t.role = Role(role);
        t.lastSeq = r.u32();
        tickets[k] = t;
    }
    if (!r.done()) return false;
    hostSecret_ = s[0], secrets_[1] = s[1], secrets_[2] = s[2], memberSecret_ = s[3], hasMemberSecret_ = true;
    for (Secret& k : s) crypto_wipe(k.data(), k.size());
    crypto_wipe(secrets_[0].data(), secrets_[0].size());
    // The cells and records this program wrote are the host's now (0); the old host's are no one's here (it left).
    const uint32_t mine = myId_, gone = 0xFFFFFFF0u;
    auto remap = [&](uint32_t& w) { w = w == mine ? 0 : w == 0 ? gone : w; };
    for (auto& v : writer_)
        for (uint32_t& w : v) remap(w);
    for (auto& v : slotWriter_)
        for (uint32_t& w : v) remap(w);
    for (uint32_t& w : objWriter_) remap(w);
    // The map as this program shows it is the session's map from now on (its edits that the old host did not confirm too).
    for (uint8_t k = 0; k < kObjSections; ++k) objBase_[k] = objShown_[k], objPending_[k].clear(), objDirty_[k] = false;
    pending_.clear(), unconfirmed_.clear(), later_.clear(), objLater_.clear(), hostObjLater_.clear(), serialMap_.clear();
    host_ = true, role_ = Role::Edit, myId_ = 0, nextColor_ = uint8_t(std::max<uint8_t>(1, color & 7));
    hostName_ = name_;
    tickets_ = std::move(tickets), ticketOf_.clear();
    peers_.clear();
    slotLimit_ = std::max<size_t>(objBase_[oLocations].size(), slotLimit_);
    lost();  // (a new connection, as the host: it takes the room)
    if (ev_.becameHost) ev_.becameHost();
    if (ev_.status) ev_.status("You host the session now. Reconnecting as the host");
    return true;
}

// --- The host's controls ------------------------------------------------------------------------------------------------

std::string Session::safetyCode() const {
    auto it = links_.find(0);
    return !host_ && it != links_.end() && it->second->keyed ? it->second->code : std::string();
}

std::string Session::link(Role r) const {
    if (!host_ || room_.empty() || ended_) return {};
    return makeInviteLink(server_, room_, r, secrets_[int(r)]);
}

void Session::broadcastPeers() {
    if (!host_) return;
    Writer w;
    std::vector<const Peer*> in;
    for (const Peer& p : peers_)
        if (p.approved) in.push_back(&p);
    w.u8(mPeers), w.u8(uint8_t(in.size() + 1));
    w.u32(0), w.u8(uint8_t(Role::Edit)), w.u8(0), w.str(name_);
    for (const Peer* p : in) w.u32(p->id), w.u8(uint8_t(p->role)), w.u8(p->color), w.str(p->name);
    for (const Peer* p : in) sendTo(p->id, w.b);
}

void Session::approve(uint32_t id, bool allow) {
    Peer* p = peer(id);
    if (!host_ || ended_ || !p || p->approved) return;
    size_t in = 0;
    for (const Peer& o : peers_) in += o.approved;
    if (allow && in >= kMaxMembers) allow = false;
    if (!allow) {
        Writer w;
        w.u8(mDeny), w.str("no");
        sendTo(id, w.b);
        out_->text("{\"t\":\"kick\",\"id\":" + std::to_string(id) + "}");
        forget(id);
        return;
    }
    flushLocal(true);  // (the terrain as it is now)
    if (ended_) return;
    // Patches that wait (a stroke): the new member gets them after the map, as the others do.
    p->approved = true;
    // The member's ticket: with it, the member comes back in at once after a lost connection (one ticket each).
    Secret raw{};
    if (!randomBytes(raw.data(), raw.size())) return end("The system gave no random numbers.");
    const std::string ticket(reinterpret_cast<const char*>(raw.data()), raw.size());
    crypto_wipe(raw.data(), raw.size());
    if (auto old = ticketOf_.find(id); old != ticketOf_.end()) tickets_.erase(old->second);
    while (tickets_.size() >= kMaxTickets) tickets_.erase(tickets_.begin());
    tickets_[ticket] = {p->name, p->color, p->role, resumeAck_.count(id) ? resumeAck_[id] : 0};
    ticketOf_[id] = ticket;
    Writer w;
    w.u8(mWelcome), w.u8(uint8_t(p->role)), w.u32(id), w.str(name_), w.u32(uint32_t(ed_ ? ed_->tileset().groups.size() : 0)), w.str(ticket);
    w.str(std::string(reinterpret_cast<const char*>(memberSecret_.data()), memberSecret_.size()));
    uint32_t lastSeq = 0;
    if (auto a = resumeAck_.find(id); a != resumeAck_.end()) lastSeq = a->second, resumeAck_.erase(a);
    w.u32(lastSeq);
    writeTerrainTo(w, base_);
    for (uint8_t s = 0; s < kObjSections; ++s) {
        w.u32(uint32_t(objBase_[s].size()));
        w.b.insert(w.b.end(), objBase_[s].begin(), objBase_[s].end());
    }
    if (w.b.size() > 1000 * 1000) {  // (more than the relay takes in one message)
        Writer no;
        no.u8(mDeny), no.str("too large");
        sendTo(id, no.b);
        out_->text("{\"t\":\"kick\",\"id\":" + std::to_string(id) + "}");
        forget(id);
        if (ev_.status) ev_.status("The map is too large for collaborative editing (the preview takes at most 1 MB of terrain, units and strings)");
        return;
    }
    sendTo(id, w.b);
    for (uint8_t s = 0; s < kObjSections; ++s) objSentTo_[id][s] = objBase_[s], objKnown_[id][s] = true;  // (the welcome had them)
    broadcastPeers();
    if (ev_.peersChanged) ev_.peersChanged();
    if (ev_.status) ev_.status(p->name + " joined to " + roleName(p->role));
}

void Session::setRole(uint32_t id, Role r) {
    Peer* p = peer(id);
    if (!host_ || ended_ || !p || !p->approved || (r != Role::View && r != Role::Edit) || p->role == r) return;
    p->role = r;
    if (auto t = ticketOf_.find(id); t != ticketOf_.end())
        if (auto k = tickets_.find(t->second); k != tickets_.end()) k->second.role = r;
    Writer w;
    w.u8(mRole), w.u8(uint8_t(r));
    sendTo(id, w.b);
    broadcastPeers();
    if (ev_.peersChanged) ev_.peersChanged();
}

void Session::remove(uint32_t id) {
    if (!host_ || ended_ || !peer(id)) return;
    if (auto t = ticketOf_.find(id); t != ticketOf_.end()) tickets_.erase(t->second), ticketOf_.erase(t);  // (no coming back)
    Writer w;
    w.u8(mBye), w.str("The host removed you from the shared map.");
    sendTo(id, w.b);
    out_->text("{\"t\":\"kick\",\"id\":" + std::to_string(id) + "}");
    forget(id);
}

bool Session::newLinks() {
    if (!host_ || ended_ || room_.empty()) return false;
    Secret v, e;
    if (!randomBytes(v.data(), v.size()) || !randomBytes(e.data(), e.size())) return false;
    secrets_[1] = v, secrets_[2] = e;
    crypto_wipe(v.data(), v.size()), crypto_wipe(e.data(), e.size());
    out_->text("{\"t\":\"tokens\",\"view\":" + quote(relayToken(secrets_[1])) + ",\"edit\":" + quote(relayToken(secrets_[2])) + "}");
    if (ev_.linksChanged) ev_.linksChanged();
    return true;
}

void Session::editorClosed() {
    ed_ = nullptr;
    later_.clear();
    stop();
}

void Session::stop() {
    if (ended_) return;
    flushLocal(true);
    if (ended_) return;
    Writer w;
    w.u8(mBye), w.str(host_ ? "The host stopped sharing the map." : "");
    if (host_) {
        for (const Peer& p : peers_)
            if (p.approved) sendTo(p.id, w.b);
        out_->text("{\"t\":\"close\"}");
    } else {
        sendTo(0, w.b);
    }
    end(host_ ? "You stopped sharing the map." : "You left the shared map.");
}

}  // namespace stardraft::collab
