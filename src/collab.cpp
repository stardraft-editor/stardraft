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

constexpr uint8_t kProto = 1;
constexpr size_t kMaxName = 32;
constexpr size_t kMaxWaiting = 4;  // connections that have no keys yet
constexpr size_t kMaxMembers = 7;
constexpr double kCursorEvery = 0.05;  // seconds: at most 20 cursor messages a second
const char kKdfLabel[] = "stardraft-collab-1";

// Frames (the first byte of a binary message, inside the relay's).
enum Frame : uint8_t { kHello = 1, kHelloBack = 2, kSealed = 3 };
// Messages (the first byte of a sealed frame's text).
enum Msg : uint8_t { mJoin = 1, mWelcome, mDeny, mPatch, mAck, mReject, mCursor, mPeers, mRole, mBye, mCursors };
constexpr double kCursorsEvery = 0.1;  // seconds: the host sends the cursors of all at most 10 times a second
constexpr double kFlushEvery = 0.05;   // seconds: this program's edits go at most 20 times a second
constexpr double kHelloWait = 10;      // seconds: a connection with no keys after this goes
constexpr size_t kMaxAsking = 3;       // join requests that wait for the host's answer at one time

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
std::string cleanName(const std::string& in) {
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
        if (out.size() + len > kMaxName) break;
        const bool control = cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) || cp == 0x200E || cp == 0x200F || (cp >= 0x202A && cp <= 0x202E) ||
                             (cp >= 0x2066 && cp <= 0x2069) || cp == 0x2028 || cp == 0x2029 || cp == 0x061C || cp == 0xFEFF;
        if (!control) out.append(in, i, len);
        i += len;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out.empty() ? "Guest" : out;
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
    return s.size() == 22 && std::all_of(s.begin(), s.end(), [](char c) { return c && std::strchr(kB64, c); });
}

uint64_t cellKey(uint8_t section, uint32_t cell) { return uint64_t(section) << 32 | cell; }

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

std::string relayToken(const Key& secret) {
    uint8_t out[32];
    static const char label[] = "stardraft-collab relay token";
    crypto_blake2b_keyed(out, sizeof out, secret.data(), secret.size(), reinterpret_cast<const uint8_t*>(label), sizeof label - 1);
    return hex(out, sizeof out);
}

std::string makeInviteLink(const std::string& server, const std::string& room, Role role, const Key& secret) {
    return server + "/j/" + room + "#" + (role == Role::Edit ? "e." : "v.") + base64url(secret.data(), secret.size());
}

bool parseInviteLink(const std::string& text, InviteLink& out, std::string& error) {
    error = "This is not a StarDraft invite link. It looks like https://<server>/j/<room>#e.<code>.";
    std::string s = trimmed(text);
    static const char custom[] = "stardraft://join?link=";
    if (s.rfind(custom, 0) == 0) {
        bool ok = false;
        s = percentDecode(s.substr(sizeof custom - 1), ok);
        if (!ok || s.rfind("stardraft:", 0) == 0) return false;  // (one level only)
    }
    const size_t hash = s.find('#');
    if (hash == std::string::npos) return false;
    const std::string fragment = s.substr(hash + 1);
    s = s.substr(0, hash);
    const size_t j = s.find("/j/", s.find("://") == std::string::npos ? 0 : s.find("://") + 3);
    if (j == std::string::npos) return false;
    std::string server;
    if (!normalizeServer(s.substr(0, j), server, error)) return false;
    const std::string room = s.substr(j + 3);
    if (!roomId(room) || fragment.size() != 45 || fragment[1] != '.' || (fragment[0] != 'e' && fragment[0] != 'v')) {
        error = "This invite link is not complete. Ask for the link again.";
        return false;
    }
    if (!unbase64url(fragment.substr(2), out.secret.data(), out.secret.size())) {
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
    static void derive(const Key& secret, const uint8_t dh[32], const uint8_t* joinerPub, const uint8_t* hostPub, Key& j2h, Key& h2j) {
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
    if (!randomBytes(s->secrets_[1].data(), 32) || !randomBytes(s->secrets_[2].data(), 32)) {
        error = "The system gave no random numbers.";
        return nullptr;
    }
    s->host_ = true, s->role_ = Role::Edit, s->ed_ = &editor, s->out_ = &out, s->ev_ = std::move(ev);
    s->name_ = cleanName(name);
    s->seenRevision_ = editor.revision();
    s->maxState_ = editor.stateId();
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
    for (Key& k : secrets_) crypto_wipe(k.data(), k.size());
    if (notify && ev_.ended) ev_.ended(why);
}

void Session::opened() {
    if (ended_) return;
    if (host_) {
        out_->text("{\"t\":\"create\",\"v\":1,\"view\":" + quote(relayToken(secrets_[1])) + ",\"edit\":" + quote(relayToken(secrets_[2])) + "}");
        if (ev_.status) ev_.status("Connected to the server; making the invite links");
    } else {
        out_->text("{\"t\":\"join\",\"v\":1,\"room\":" + quote(room_) + ",\"token\":" + quote(relayToken(secrets_[0])) + "}");
        if (ev_.status) ev_.status("Connected to the server; waiting for the host");
    }
}

void Session::closed(const std::string& why) {
    if (!ended_) end(why.empty() ? "The connection to the server ended." : "The connection to the server ended (" + why + ").");
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
            if (ev_.status) ev_.status("Sharing the map: copy an invite link (File > Shared map)");
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
    linkTime_.erase(id);
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
        for (Role r : {Role::Edit, Role::View}) {
            Key j2h, h2j;
            Link::derive(secrets_[int(r)], l.dh, l.peerPub.data(), l.ePub.data(), j2h, h2j);
            if (Link::open(j2h, 0, p + 1, n - 1, plain)) {
                l.rx = j2h, l.tx = h2j, l.keyed = true, l.rxN = 1;
                l.code = Link::safetyCode(l.dh, l.peerPub.data(), l.ePub.data());
                Reader rd{plain.data(), plain.size()};
                if (rd.u8() != mJoin || rd.u8() != kProto) return drop();
                Peer np;
                np.id = from, np.name = cleanName(rd.str()), np.role = r, np.code = l.code;
                rd.str();  // (the program's version: not used yet)
                if (!rd.done()) return drop();
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
        w.u8(mJoin), w.u8(kProto), w.str(name_), w.str(version_);
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
    if (type == mBye) return forget(from);
    if (!pr->approved) return;  // (nothing but goodbye before the host's yes)
    if (type == mCursor) {
        const uint8_t on = r.u8();
        const uint16_t x = r.u16(), y = r.u16();
        if (!r.done() || on > 1) return drop("A message was not valid.");
        pr->cursorOn = on, pr->x = x, pr->y = y, pr->cursorTime = now();
        cursorsDirty_ = true;  // (sendCursors: all at once, at most 10 times a second)
        if (ev_.peersChanged) ev_.peersChanged();
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
        if (ed_) return bad();
        const uint8_t role = r.u8();
        myId_ = r.u32();
        hostName_ = cleanName(r.str());
        tileGroups_ = r.u32();
        Terrain t;
        if (!readTerrainFrom(r, t) || !r.done() || (role != uint8_t(Role::View) && role != uint8_t(Role::Edit))) return bad();
        role_ = Role(role);
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
        const std::string why = r.str();
        return end(why.empty() ? "The host ended the session." : "The host ended the session: " + cleanName(why));
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
            if (const Peer* old = peer(p.id)) p.cursorOn = old->cursorOn, p.x = old->x, p.y = old->y, p.cursorTime = old->cursorTime;
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
            if (on > 1) return bad();
            Peer* p = id == myId_ ? nullptr : peer(id);
            if (p && (p->cursorOn != bool(on) || p->x != x || p->y != y)) p->cursorOn = on, p->x = x, p->y = y, p->cursorTime = now();
        }
        if (!r.done()) return bad();
        if (ev_.peersChanged) ev_.peersChanged();
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
    default: return bad();
    }
}

bool Session::applyToEditor(const Patch& p) {
    if (!ed_ || p.empty()) return true;
    if (!ed_->editTerrainSections([&](Chk& c) { writePatch(c, p); })) return false;
    seenRevision_ = ed_->revision();
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
        sendTo(w.from, ack.b);
        Writer out;
        out.u8(mPatch), out.u32(w.from);
        writePatchTo(out, w.patch);
        for (const Peer& o : peers_)
            if (o.approved && o.id != w.from) sendTo(o.id, out.b);
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
    if (!force && now() - lastFlush_ < kFlushEvery) return false;
    if (role_ != Role::Edit && ed_->inStroke()) return false;  // (view only: put back after the stroke, which locks the map)
    lastFlush_ = now();
    seenRevision_ = ed_->revision();
    const bool undone = undoFilter_ || ed_->stateId() < maxState_;  // (an older state: the editor's undo or redo)
    undoFilter_ = false;
    maxState_ = std::max(maxState_, ed_->stateId());
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
        if (!ended_ && !ed_->inStroke() && !later_.empty()) {
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
        sendTo(0, w.b);
        cursorDirty_ = false, cursorSent_ = now();
    }
    if (!ended_ && host_ && (cursorDirty_ || cursorsDirty_) && now() - cursorsSent_ >= kCursorsEvery) sendCursors();
    inTick_ = false;
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
        for (const Peer* p : in)
            if (p != to) w.u32(p->id), w.u8(p->cursorOn), w.u16(uint16_t(p->x)), w.u16(uint16_t(p->y));
        sendTo(to->id, w.b);
    }
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
    seenRevision_ = ed_->revision();
}

// --- The host's controls ------------------------------------------------------------------------------------------------

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
    Writer w;
    w.u8(mWelcome), w.u8(uint8_t(p->role)), w.u32(id), w.str(name_), w.u32(uint32_t(ed_ ? ed_->tileset().groups.size() : 0));
    writeTerrainTo(w, base_);
    sendTo(id, w.b);
    broadcastPeers();
    if (ev_.peersChanged) ev_.peersChanged();
    if (ev_.status) ev_.status(p->name + " joined to " + roleName(p->role));
}

void Session::setRole(uint32_t id, Role r) {
    Peer* p = peer(id);
    if (!host_ || ended_ || !p || !p->approved || (r != Role::View && r != Role::Edit) || p->role == r) return;
    p->role = r;
    Writer w;
    w.u8(mRole), w.u8(uint8_t(r));
    sendTo(id, w.b);
    broadcastPeers();
    if (ev_.peersChanged) ev_.peersChanged();
}

void Session::remove(uint32_t id) {
    if (!host_ || ended_ || !peer(id)) return;
    Writer w;
    w.u8(mBye), w.str("The host removed you from the shared map.");
    sendTo(id, w.b);
    out_->text("{\"t\":\"kick\",\"id\":" + std::to_string(id) + "}");
    forget(id);
}

bool Session::newLinks() {
    if (!host_ || ended_ || room_.empty()) return false;
    Key v, e;
    if (!randomBytes(v.data(), 32) || !randomBytes(e.data(), 32)) return false;
    secrets_[1] = v, secrets_[2] = e;
    crypto_wipe(v.data(), 32), crypto_wipe(e.data(), 32);
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
