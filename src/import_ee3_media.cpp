#include "stardraft/import_ee3_extras.h"

// EUD Editor 3's dot images and BGM player (import_ee3_extras.h), as its build makes them:
//   - dotData.eps: BuildData.GetDotEps (Class/BulidData/WriteDotData.vb) with DotData.GetepsData and
//     DotPaint.GetColorIndex (Class/TriggerEditor/DotPainter). The project file holds each picture as the .NET Bitmap
//     that the user loaded: its "Data" member is the image file's bytes (PNG, BMP, GIF or JPEG). DotData's Pixels array
//     is never filled by EUD Editor 3 (all zeros), so the colors come from the picture, as GetepsData reads them.
//   - BGMFlexible.eps and the sound parts: BuildData.GetBGMEps (Class/BulidData/WriteBGMFile.vb) and
//     BGMPlayerConverter.BGMProcess (Window/BGMPlayerConverter.xaml.vb). EUD Editor 3 converts each sound with ffmpeg
//     to Ogg Vorbis and cuts it into parts of 2.28 seconds ("-f segment -segment_time 2.28 -c copy": the packets are
//     copied, not encoded again), st0.ogg, st1.ogg, ... Here an Ogg Vorbis sound is cut the same way, packet by packet,
//     with no encoder. A WAV or MP3 sound needs an Ogg Vorbis encoder, which StarDraft does not have: a problem line
//     says so.

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string_view>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STB_IMAGE_STATIC  // (the editor has its own stb_image: this copy is private to this file)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_JPEG
#define STBI_MAX_DIMENSIONS 4096
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fs = std::filesystem;

namespace stardraft {

namespace {

const char* const kNl = "\r\n";  // (StringBuilder.AppendLine, and the line ends in EUD Editor 3's multi-line strings)

// --- Dot images -------------------------------------------------------------------------------------------------------

// DotPaint.ColorHex: the 21 colors that dotTool.eps can draw, in its order.
constexpr uint32_t kDotColors[21] = {0xf70400, 0x3a57cc, 0x249824, 0x7c0200, 0x1d2c66, 0x124c12, 0x3e0100,
                                     0x0f1633, 0x092609, 0x982e66, 0x8d4e12, 0xffffff, 0x020b47, 0xfb8280,
                                     0xf94340, 0xbb4240, 0x7c0824, 0x8a1833, 0x808080, 0x404040, 0x202020};

// DotPaint.GetColorIndex: the nearest color (the squared RGB distance; the first of equal ones). Alpha has no effect.
int dotColorIndex(int r, int g, int b) {
    int best = 0;
    long long bestD = (1LL << 31) - 1;
    for (int i = 0; i < 21; ++i) {
        const int dr = int(kDotColors[i] >> 16) - r, dg = int((kDotColors[i] >> 8) & 0xFF) - g, db = int(kDotColors[i] & 0xFF) - b;
        const long long d = 1LL * dr * dr + 1LL * dg * dg + 1LL * db * db;
        if (d < bestD) bestD = d, best = i;
    }
    return best;
}

// The bytes of a byte[] member (the reader keeps a byte[] as one value).
std::string byteMember(const nrbf::Graph& g, const nrbf::Object* o, std::string_view name) {
    if (!o) return {};
    const nrbf::Value& v = g.value(o, name);
    if (v.kind == nrbf::Value::Kind::Bytes) return v.s;
    const nrbf::Object* a = g.get(v);
    if (a && a->array && a->values.size() == 1 && a->values[0].kind == nrbf::Value::Kind::Bytes) return a->values[0].s;
    return {};
}

// DotData.GetepsData: "EUDArray(list(1,<width>,<height>,<color index of each pixel, row by row>))". False with `why`
// when the picture cannot be read.
bool dotEps(const nrbf::Graph& g, const nrbf::Object* d, std::string& out, std::string& why) {
    const int64_t w = g.number(d, "imgWidth"), h = g.number(d, "imgHeight");
    if (w < 0 || h < 0 || w * h >= 1000) {  // (EUD Editor 3 refuses a picture of 1000 pixels or more)
        why = "its size (" + std::to_string(w) + " x " + std::to_string(h) + ") is not less than 1000 pixels";
        return false;
    }
    std::string list = "1," + std::to_string(w) + "," + std::to_string(h);
    if (w * h > 0) {
        const std::string data = byteMember(g, g.get(d, "PixelOrgData"), "Data");
        if (data.empty()) {
            why = "the project file holds no picture for it";
            return false;
        }
        int bw = 0, bh = 0, n = 0;
        stbi_uc* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()), int(std::min<size_t>(data.size(), 1u << 30)), &bw, &bh, &n, 4);
        if (!px) {
            why = "its picture is not a PNG, BMP, GIF or JPEG image that the import can read";
            return false;
        }
        if (bw < w || bh < h) {
            stbi_image_free(px);
            why = "its picture (" + std::to_string(bw) + " x " + std::to_string(bh) + ") is smaller than its size";
            return false;
        }
        for (int64_t y = 0; y < h; ++y)
            for (int64_t x = 0; x < w; ++x) {
                const stbi_uc* p = px + (size_t(y) * size_t(bw) + size_t(x)) * 4;
                list += "," + std::to_string(dotColorIndex(p[0], p[1], p[2]));
            }
        stbi_image_free(px);
    }
    out = "EUDArray(list(" + list + "))";
    return true;
}

// --- Ogg Vorbis: read the packets, cut them, write them again ---------------------------------------------------------

uint32_t oggCrc(const uint8_t* p, size_t n) {
    static const auto table = [] {
        std::vector<uint32_t> t(256);
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t r = i << 24;
            for (int k = 0; k < 8; ++k) r = r & 0x80000000u ? (r << 1) ^ 0x04c11db7u : r << 1;
            t[i] = r;
        }
        return t;
    }();
    uint32_t crc = 0;
    for (size_t i = 0; i < n; ++i) crc = (crc << 8) ^ table[((crc >> 24) ^ p[i]) & 0xFF];
    return crc;
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// Vorbis reads its packets bit by bit, the low bit of each byte first.
struct Bits {
    const uint8_t* p;
    size_t n;
    size_t pos = 0;  // (in bits)
    bool bad = false;
    uint32_t get(int k) {
        uint32_t v = 0;
        for (int i = 0; i < k; ++i, ++pos) {
            if ((pos >> 3) >= n) {
                bad = true;
                return 0;
            }
            v |= uint32_t((p[pos >> 3] >> (pos & 7)) & 1) << i;
        }
        return v;
    }
    void skip(uint64_t k) {
        if (k > uint64_t(n) * 8 - std::min<uint64_t>(pos, uint64_t(n) * 8)) bad = true;
        else pos += size_t(k);
    }
};

int ilog(uint32_t x) {
    int r = 0;
    while (x) ++r, x >>= 1;
    return r;
}

// The setup header (Vorbis I, section 4.2.4) up to its modes: for each mode, whether it uses the long block. All that a
// cut needs, as a packet's length in samples comes from its mode.
bool vorbisModes(const std::string& setup, int channels, std::vector<bool>& modeLong) {
    if (setup.size() < 7 || setup.compare(0, 7, "\x05vorbis") != 0) return false;
    Bits b{reinterpret_cast<const uint8_t*>(setup.data()) + 7, setup.size() - 7};
    const uint32_t books = b.get(8) + 1;
    for (uint32_t i = 0; i < books && !b.bad; ++i) {
        if (b.get(24) != 0x564342) return false;
        const uint32_t dims = b.get(16), entries = b.get(24);
        if (b.get(1)) {  // (ordered)
            uint32_t at = 0;
            b.get(5);
            while (at < entries && !b.bad) {
                at += b.get(ilog(entries - at));
                if (at > entries) return false;
            }
        } else {
            const bool sparse = b.get(1);
            for (uint32_t e = 0; e < entries && !b.bad; ++e)
                if (!sparse || b.get(1)) b.get(5);
        }
        const uint32_t lookup = b.get(4);
        if (lookup == 1 || lookup == 2) {
            b.get(32), b.get(32);
            const uint32_t valueBits = b.get(4) + 1;
            b.get(1);
            uint64_t values = 0;
            if (lookup == 1) {  // (lookup1_values: the largest r with r^dims <= entries)
                if (dims == 0) return false;
                auto fits = [&](uint64_t r) {
                    uint64_t pw = 1;
                    for (uint32_t k = 0; k < dims && pw <= entries; ++k) pw *= r;
                    return pw <= entries;
                };
                values = uint64_t(std::pow(double(entries), 1.0 / dims));
                while (values > 0 && !fits(values)) --values;
                while (fits(values + 1)) ++values;
            } else {
                values = uint64_t(entries) * dims;
            }
            b.skip(values * valueBits);
        } else if (lookup != 0) {
            return false;
        }
    }
    const uint32_t times = b.get(6) + 1;
    for (uint32_t i = 0; i < times; ++i)
        if (b.get(16) != 0) return false;
    const uint32_t floors = b.get(6) + 1;
    for (uint32_t i = 0; i < floors && !b.bad; ++i) {
        const uint32_t type = b.get(16);
        if (type == 0) {
            b.get(8), b.get(16), b.get(16), b.get(6), b.get(8);
            const uint32_t nb = b.get(4) + 1;
            for (uint32_t k = 0; k < nb; ++k) b.get(8);
        } else if (type == 1) {
            const uint32_t parts = b.get(5);
            std::vector<uint32_t> cls(parts);
            uint32_t maxClass = 0;
            for (uint32_t& c : cls) c = b.get(4), maxClass = std::max(maxClass, c);
            std::vector<uint32_t> dimsOf(parts ? maxClass + 1 : 0);
            for (uint32_t& dm : dimsOf) {
                dm = b.get(3) + 1;
                const uint32_t sub = b.get(2);
                if (sub) b.get(8);
                for (uint32_t k = 0; k < (1u << sub); ++k) b.get(8);
            }
            b.get(2);
            const int rangeBits = int(b.get(4));
            for (uint32_t c : cls)
                for (uint32_t k = 0; k < dimsOf[c]; ++k) b.get(rangeBits);
        } else {
            return false;
        }
    }
    const uint32_t residues = b.get(6) + 1;
    for (uint32_t i = 0; i < residues && !b.bad; ++i) {
        if (b.get(16) > 2) return false;
        b.get(24), b.get(24), b.get(24);
        const uint32_t classes = b.get(6) + 1;
        b.get(8);
        std::vector<uint32_t> cascade(classes);
        for (uint32_t& c : cascade) {
            const uint32_t low = b.get(3);
            c = b.get(1) ? b.get(5) * 8 + low : low;
        }
        for (uint32_t c : cascade)
            for (int k = 0; k < 8; ++k)
                if (c & (1u << k)) b.get(8);
    }
    const uint32_t mappings = b.get(6) + 1;
    const int chanBits = ilog(uint32_t(std::max(channels - 1, 0)));
    for (uint32_t i = 0; i < mappings && !b.bad; ++i) {
        if (b.get(16) != 0) return false;
        const uint32_t submaps = b.get(1) ? b.get(4) + 1 : 1;
        if (b.get(1)) {
            const uint32_t steps = b.get(8) + 1;
            for (uint32_t k = 0; k < steps; ++k) b.get(chanBits), b.get(chanBits);
        }
        if (b.get(2) != 0) return false;
        if (submaps > 1)
            for (int c = 0; c < channels; ++c) b.get(4);
        for (uint32_t k = 0; k < submaps; ++k) b.get(8), b.get(8), b.get(8);
    }
    const uint32_t modes = b.get(6) + 1;
    modeLong.clear();
    for (uint32_t i = 0; i < modes; ++i) {
        modeLong.push_back(b.get(1) != 0);
        b.get(16), b.get(16), b.get(8);
    }
    return !b.bad && b.get(1) == 1;  // (the framing bit)
}

struct Vorbis {
    std::string id, comment, setup;
    std::vector<std::string> audio;
    int channels = 0;
    uint32_t rate = 0;
    int block0 = 0, block1 = 0;
    std::vector<bool> modeLong;
    int64_t lastGranule = -1;  // (the last page's granule position: the stream's length in samples)
    bool otherStreams = false;
};

// An Ogg file's first logical stream, as Vorbis packets. False with `why`.
bool readVorbis(const std::string& file, Vorbis& v, std::string& why) {
    const auto* p = reinterpret_cast<const uint8_t*>(file.data());
    const size_t n = file.size();
    size_t at = 0;
    bool haveSerial = false;
    uint32_t serial = 0;
    std::string packet;
    std::vector<std::string> packets;
    while (at < n) {
        if (n - at < 27 || std::memcmp(p + at, "OggS", 4) != 0 || p[at + 4] != 0) {
            why = packets.empty() && at == 0 ? "it is not an Ogg file" : "the file is damaged (a page at byte " + std::to_string(at) + ")";
            return false;
        }
        const uint8_t flags = p[at + 5];
        int64_t granule = 0;
        for (int k = 7; k >= 0; --k) granule = granule << 8 | p[at + 6 + k];
        const uint32_t pageSerial = le32(p + at + 14), crc = le32(p + at + 22);
        const size_t segs = p[at + 26];
        if (n - at - 27 < segs) {
            why = "the file is damaged (its end is missing)";
            return false;
        }
        size_t body = 0;
        for (size_t k = 0; k < segs; ++k) body += p[at + 27 + k];
        const size_t size = 27 + segs + body;
        if (n - at < size) {
            why = "the file is damaged (its end is missing)";
            return false;
        }
        std::string page(file, at, size);
        std::memset(page.data() + 22, 0, 4);
        if (oggCrc(reinterpret_cast<const uint8_t*>(page.data()), size) != crc) {
            why = "the file is damaged (a page at byte " + std::to_string(at) + " has a wrong checksum)";
            return false;
        }
        if (!haveSerial) haveSerial = true, serial = pageSerial;
        if (pageSerial != serial) {
            v.otherStreams = true;
        } else {
            if (!(flags & 1)) packet.clear();  // (a page that does not continue a packet: a packet cut short is lost)
            size_t data = at + 27 + segs;
            for (size_t k = 0; k < segs; ++k) {
                const size_t len = p[at + 27 + k];
                packet.append(file, data, len), data += len;
                if (len < 255) packets.push_back(std::move(packet)), packet.clear();
            }
            if (granule != -1) v.lastGranule = granule;
            if (flags & 4) {  // (the stream's end: a chained stream after it is left out)
                v.otherStreams = v.otherStreams || at + size < n;
                break;
            }
        }
        at += size;
    }
    if (packets.size() < 3 || packets[0].size() < 30 || packets[0].compare(0, 7, "\x01vorbis") != 0) {
        why = packets.size() && packets[0].compare(0, 8, "OpusHead") == 0 ? "it is an Opus sound, not Ogg Vorbis" : "it is not an Ogg Vorbis sound";
        return false;
    }
    const auto* id = reinterpret_cast<const uint8_t*>(packets[0].data());
    v.channels = id[11], v.rate = le32(id + 12), v.block0 = 1 << (id[28] & 15), v.block1 = 1 << (id[28] >> 4);
    if (v.channels == 0 || v.rate == 0 || packets[1].compare(0, 7, "\x03vorbis") != 0 || !vorbisModes(packets[2], v.channels, v.modeLong)) {
        why = "its Vorbis headers are damaged";
        return false;
    }
    v.id = std::move(packets[0]), v.comment = std::move(packets[1]), v.setup = std::move(packets[2]);
    v.audio.assign(std::make_move_iterator(packets.begin() + 3), std::make_move_iterator(packets.end()));
    return true;
}

// Writes Ogg pages, as libogg does: a page when about 4 KB of packets are in it.
struct OggWriter {
    std::string out;
    uint32_t serial = 0x45453353;  // (any number: the file has one stream)
    uint32_t seq = 0;
    std::string lacing, body;
    int64_t granule = -1;    // (the granule position of the last packet that ends on this page)
    bool continued = false;  // (the page starts in a packet)
    void packet(const std::string& p, int64_t granuleAfter) {
        const size_t count = p.size() / 255 + 1;
        for (size_t i = 0, at = 0; i < count; ++i) {
            if (lacing.size() == 255) page(false);
            const size_t len = i + 1 < count ? 255 : p.size() % 255;
            lacing += char(len), body.append(p, at, len), at += len;
        }
        granule = granuleAfter;
    }
    void page(bool last) {
        std::string h = "OggS";
        h += char(0);
        h += char((continued ? 1 : 0) | (seq == 0 ? 2 : 0) | (last ? 4 : 0));
        for (int k = 0; k < 8; ++k) h += char(uint64_t(granule) >> (8 * k));
        for (uint32_t v : {serial, seq, 0u})
            for (int k = 0; k < 4; ++k) h += char(v >> (8 * k));
        h += char(lacing.size());
        h += lacing + body;
        const uint32_t crc = oggCrc(reinterpret_cast<const uint8_t*>(h.data()), h.size());
        for (int k = 0; k < 4; ++k) h[22 + k] = char(crc >> (8 * k));
        out += h;
        continued = !lacing.empty() && uint8_t(lacing.back()) == 255;
        lacing.clear(), body.clear(), granule = -1, ++seq;
    }
};

// The packets of a cut, with the stream's three headers before them, as one Ogg file. The first packet gives no sound
// (a Vorbis stream starts with it); `trim`: samples to leave out at the end (the source's last page says how many).
std::string vorbisPart(const Vorbis& v, const std::vector<int>& blocks, size_t from, size_t to, int64_t trim) {
    OggWriter w;
    w.packet(v.id, 0), w.page(false);
    w.packet(v.comment, 0), w.packet(v.setup, 0), w.page(false);
    int64_t samples = 0;
    std::vector<int64_t> after(to - from);
    for (size_t k = from; k < to; ++k) {
        if (k > from) samples += (blocks[k - 1] + blocks[k]) / 4;
        after[k - from] = samples;
    }
    if (trim > 0) after.back() = std::max(after.size() > 1 ? after[after.size() - 2] : 0, after.back() - trim);
    for (size_t k = from; k < to; ++k) {
        if (w.body.size() >= 4096) w.page(false);
        w.packet(v.audio[k], after[k - from]);
    }
    w.page(true);
    return w.out;
}

// --- BGM player -------------------------------------------------------------------------------------------------------

// BuildData.GetSoundIndex: a number as 4 characters, '@' + each hex digit ("@@@E" for 5); `value`: those characters
// as the dword that holds them ("0x45404040").
std::string soundIndex(int index, bool value) {
    char hex[8];
    std::snprintf(hex, sizeof hex, "%04X", unsigned(index) & 0xFFFF);
    auto digit = [](char c) { return c >= 'A' ? c - 'A' + 10 : c - '0'; };
    std::string rv = value ? "0x" : "";
    for (int i = 0; i < 4; ++i) {
        if (!value) {
            rv += char('@' + digit(hex[i]));
        } else {
            char b[4];
            std::snprintf(b, sizeof b, "%02X", unsigned(0x40 + digit(hex[3 - i])));
            rv += b;
        }
    }
    return rv;
}

std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

// A BGM's name as a folder name in the project: no folder parts, no characters that Windows refuses, no quote (the
// name goes into a Python string), not "." or "..", not a device name; each name once. The file is not trusted.
std::string folderName(const std::string& name, size_t index, std::set<std::string>& used) {
    std::string s;
    for (char c : name) s += uint8_t(c) < 0x20 || std::string_view("<>:\"/\\|?*'`").find(c) != std::string_view::npos ? '_' : c;
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    if (s.size() > 100) s.resize(100);
    while (!s.empty() && (uint8_t(s.back()) & 0xC0) == 0x80) s.pop_back();  // (a UTF-8 character cut in two)
    if (!s.empty() && (uint8_t(s.back()) & 0xC0) == 0xC0) s.pop_back();
    const std::string base = lowerAscii(s.substr(0, s.find('.')));
    static const std::set<std::string> devices = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
                                                  "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    if (s.empty()) s = "bgm" + std::to_string(index);
    else if (devices.count(base)) s = "_" + s;
    std::string out = s;
    for (int k = 2; used.count(lowerAscii(out)); ++k) out = s + "_" + std::to_string(k);
    used.insert(lowerAscii(out));
    return out;
}

bool networkPath(const std::string& s) { return s.size() >= 2 && (s[0] == '\\' || s[0] == '/') && (s[1] == '\\' || s[1] == '/'); }

std::string u8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
fs::path pathOf(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// A BGM's sound file: its path, else the project folder and its relative path (BGMFile.BGMPath). Only a local, regular
// file with an extension that EUD Editor 3 takes (.ogg, .mp3, .wav). False with a problem line.
bool soundFile(const std::string& path, const std::string& relative, const std::string& projectFolder, std::string& bytes,
               std::string& ext, std::string& used, std::string& why) {
    std::vector<std::string> tried;
    std::vector<std::string> candidates;
    if (!path.empty()) candidates.push_back(path);
    if (!relative.empty()) {
        const fs::path r = pathOf(relative);
        candidates.push_back(r.is_absolute() || networkPath(relative) ? relative : u8(pathOf(projectFolder) / r));
    }
    std::string network;
    for (const std::string& c : candidates) {
        if (networkPath(c)) {  // (not opened: a file that the project names must not open a connection by itself)
            network = c;
            continue;
        }
        const fs::path f = pathOf(c).lexically_normal();
        std::error_code ec;
        if (!fs::is_regular_file(fs::status(f, ec))) {
            tried.push_back(u8(f));
            continue;
        }
        ext = lowerAscii(u8(f.extension()));
        if (ext != ".ogg" && ext != ".mp3" && ext != ".wav") {
            why = "its file " + u8(f) + " is not an OGG, MP3 or WAV file";
            return false;
        }
        const uintmax_t size = fs::file_size(f, ec);
        if (ec || size > (256u << 20)) {
            why = "its file " + u8(f) + " is larger than 256 MB";
            return false;
        }
        std::ifstream in(f, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (!in && !in.eof()) {
            why = "its file " + u8(f) + " cannot be read";
            return false;
        }
        used = u8(f);
        return true;
    }
    if (!network.empty()) why = "its file " + network + " is on a network path, which the import does not open";
    else if (tried.empty()) why = "the project file gives no path for its sound file";
    else {
        why = "its sound file is not found (";
        for (size_t k = 0; k < tried.size(); ++k) why += (k ? "; " : "") + tried[k];
        why += ")";
    }
    return false;
}

}  // namespace

bool ee3DotImages(const nrbf::Graph& g, const nrbf::Object* te, std::vector<ImportFile>& files, std::vector<std::string>& problems) {
    files.clear();
    const std::vector<nrbf::Value> dots = g.items(g.get(te, "DotDatas"));
    if (dots.empty()) return true;
    // BuildData.GetDotEps.
    std::string eps = std::string(kNl) + "const arrdata = EUDArray(list(" + kNl;
    bool all = true;
    for (size_t i = 0; i < dots.size(); ++i) {
        if (i != 0) eps += std::string(",") + kNl;
        const nrbf::Object* d = g.get(dots[i]);
        std::string one, why;
        if (!d) why = "the project file holds no data for it";
        if (d && dotEps(g, d, one, why)) {
            eps += one;
        } else {
            all = false;
            const std::string name = d ? g.text(d, "ItemName") : std::string();
            problems.push_back("the dot image " + std::to_string(i) + (name.empty() ? "" : " (\"" + name + "\")") + ": " + why +
                               "; its data is empty in dotData.eps");
            eps += "EUDArray(list(1,0,0))";  // (an empty image keeps the numbers of the others)
        }
    }
    eps += std::string(")") + kNl + ");" + kNl + kNl + "function GetData(id){" + kNl + "\treturn arrdata[id];" + kNl + "}" + kNl;
    files.push_back({"TriggerEditor/dotData.eps", eps});
    return all;
}

bool ee3BgmPlayer(const nrbf::Graph& g, const nrbf::Object* te, const std::string& projectFolder, std::vector<ImportFile>& files,
                  std::vector<std::string>& problems) {
    files.clear();
    const std::vector<nrbf::Value> list = g.items(g.get(g.get(te, "BGMData"), "BGMList"));
    if (list.empty()) return true;
    bool all = true;
    std::set<std::string> usedNames;
    std::vector<std::string> lens, adds;
    std::vector<ImportFile> sounds;
    for (size_t i = 0; i < list.size(); ++i) {
        const nrbf::Object* b = g.get(list[i]);
        const std::string name = b ? g.text(b, "BGMName") : std::string();
        const std::string label = "the sound " + std::to_string(i) + (name.empty() ? "" : " (\"" + name + "\")");
        const std::string folder = folderName(name, i, usedNames);
        int last = 0;  // (BGMBlockCount: the number of the last part)
        std::string bytes, ext, path, why;
        Vorbis v;
        if (!b) {
            why = "the project file holds no data for it";
        } else if (soundFile(g.text(b, "BGMPath"), g.text(b, "BGMRelativePath"), projectFolder, bytes, ext, path, why)) {
            if (ext != ".ogg" && bytes.compare(0, 4, "OggS") != 0) {
                why = "its file " + path + " is " + (ext == ".mp3" ? "an MP3" : "a WAV") +
                      " file. EUD Editor 3 converts it to Ogg Vorbis with ffmpeg; the import has no Ogg Vorbis encoder. Convert the file "
                      "to .ogg and import again, or use EUD Editor 3's build folder";
            } else if (!readVorbis(bytes, v, why)) {
                why = "its file " + path + ": " + why;
            }
        }
        if (why.empty()) {
            // A packet's length in samples: a quarter of the block before it and a quarter of its own (the first gives
            // none). Empty or unknown packets give no sound: they are left out.
            std::vector<int> blocks;
            std::vector<std::string> audio;
            const int modeBits = ilog(uint32_t(v.modeLong.size() - 1));
            for (std::string& pk : v.audio) {
                Bits bits{reinterpret_cast<const uint8_t*>(pk.data()), pk.size()};
                if (pk.empty() || bits.get(1) != 0) continue;
                const uint32_t mode = bits.get(modeBits);
                if (bits.bad || mode >= v.modeLong.size()) continue;
                blocks.push_back(v.modeLong[mode] ? v.block1 : v.block0);
                audio.push_back(std::move(pk));
            }
            v.audio = std::move(audio);
            if (v.audio.empty()) why = "its file " + path + " holds no sound";
            else {
                // The cuts (ffmpeg's segment muxer): a new part at the first packet at or after each 2.28 seconds.
                std::vector<size_t> starts = {0};
                int64_t total = 0;  // (the samples before each packet's)
                for (size_t k = 0; k < v.audio.size(); ++k) {
                    if (k > 0 && total >= int64_t(starts.size()) * 228 * v.rate / 100) starts.push_back(k);
                    if (k > 0) total += (blocks[k - 1] + blocks[k]) / 4;
                }
                if (starts.size() > 0x10000) {
                    why = "its file " + path + " is too long (more than 65536 parts of 2.28 seconds)";
                } else {
                    const int64_t trim = v.lastGranule >= 0 && v.lastGranule < total ? total - v.lastGranule : 0;
                    std::vector<std::string> names;
                    for (size_t s = 0; s < starts.size(); ++s) {
                        const size_t to = s + 1 < starts.size() ? starts[s + 1] : v.audio.size();
                        const std::string file = "st" + std::to_string(s) + ".ogg";
                        sounds.push_back({"sound/" + folder + "/" + file, vorbisPart(v, blocks, starts[s], to, s + 1 == starts.size() ? trim : 0)});
                        names.push_back(file);
                    }
                    last = int(starts.size()) - 1;
                    std::sort(names.begin(), names.end());  // (the folder's files in the order Windows lists them)
                    for (const std::string& f : names)
                        adds.push_back("    MPQAddFile('" + soundIndex(int(i), false) + soundIndex(std::stoi(f.substr(2)), false) +
                                       "', pathlib.Path('sound/" + folder + "/" + f + "').read_bytes());");
                    if (v.otherStreams) problems.push_back(label + ": its file " + path + " holds more than one stream; only the first is used");
                    const int64_t rate = g.number(b, "BGMSampleRate");
                    if (rate > 0 && rate != int64_t(v.rate))
                        problems.push_back(label + ": its sample rate stays " + std::to_string(v.rate) + " Hz. EUD Editor 3 converts it to " +
                                           std::to_string(rate) + " Hz with ffmpeg; the import has no Ogg Vorbis encoder");
                }
            }
        }
        if (!why.empty()) {
            all = false;
            problems.push_back(label + ": " + why);
        }
        lens.push_back(soundIndex(last, true));
    }
    // BuildData.GetBGMEps; the parts' paths are relative to the project folder (euddraft runs there).
    std::string eps = "import py_pathlib;const bgmlen = [";
    for (size_t k = 0; k < lens.size(); ++k) eps += (k ? ", " : "") + lens[k];
    eps += std::string("];") + kNl;
    eps += std::string("const str = StringBuffer();") + kNl;
    for (const char* l : {"function PlayOGG(bgmindex ,bgmcode, track){", "    SetMemoryEPD(str.epd, SetTo, bgmcode);",
                          "    SetMemoryEPD(str.epd + 1, SetTo, track);", "    str.Play();", "", "    if (MemoryEPD(EPD(bgmlen) + bgmindex, Exactly, track)){",
                          "        return 0;", "    }else{", "        return 1;", "    }", "}", "function loadSound(){", "EUDPlayerLoop()();",
                          "    str.insert(0, '');", "    str.append('@@@@@@@@');", "EUDEndPlayerLoop();"})
        eps += std::string(l) + kNl;
    for (const std::string& a : adds) eps += a + kNl;
    eps += std::string("}") + kNl;
    files.push_back({"TriggerEditor/BGMFlexible.eps", eps});
    files.insert(files.end(), std::make_move_iterator(sounds.begin()), std::make_move_iterator(sounds.end()));
    return all;
}

}  // namespace stardraft
