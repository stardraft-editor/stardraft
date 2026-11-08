#include "stardraft/media.h"

// The project's music tracks and dot images (media.h). The Ogg Vorbis cut follows EUD Editor 3's BGM player
// (BGMPlayerConverter.BGMProcess: ffmpeg "-f segment -segment_time 2.28 -c copy"), the dot colors its DotPainter, and
// the generated modules its BuildData.GetBGMEps, BuildData.GetDotEps and TriggerEditor/dotTool.eps (MIT, LeeJungHun).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>

#include <ogg/ogg.h>
#include <vorbis/vorbisenc.h>

#include "file_write.h"
#include "json.h"
#include "stardraft/eud_project.h"
#include "stardraft/png_writer.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
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
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include <dr_wav.h>
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include <dr_mp3.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fs = std::filesystem;

namespace stardraft {

namespace {

std::string u8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
fs::path pathOf(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool readBytes(const fs::path& f, std::string& out) {
    std::ifstream in(f, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

std::string freeName(const std::string& base, const std::function<bool(const std::string&)>& taken) {
    const std::string b = base.empty() ? std::string("Untitled") : base;
    std::string n = b;
    for (int k = 2; taken(n); ++k) n = b + " " + std::to_string(k);
    return n;
}

}  // namespace

// --- The lists ---------------------------------------------------------------------------------------------------------

fs::path mediaFileOf(const EudProject& pr) { return pr.folder() / "media.json"; }

namespace {
bool safePart(const std::string& s) {
    if (s.empty() || s == "." || s == "..") return false;
    for (char c : s)
        if (uint8_t(c) < 0x20 || c == '\\' || c == ':' || c == '/' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
    return s.back() != '.' && s.back() != ' ';
}
}  // namespace

bool mediaFileSafe(const std::string& file, const char* dir) {
    const std::string pre = std::string(dir) + "/";
    if (file.rfind(pre, 0) != 0 || file.size() == pre.size() || file.size() > 400) return false;
    size_t at = pre.size();
    while (at <= file.size()) {
        const size_t end = std::min(file.find('/', at), file.size());
        if (!safePart(file.substr(at, end - at))) return false;
        at = end + 1;
    }
    return true;
}
bool mediaFolderSafe(const std::string& folder) { return folder.size() <= 100 && safePart(folder); }

bool ProjectMedia::load(const fs::path& file, std::string& error) {
    *this = {};
    std::error_code ec;
    if (!fs::exists(file, ec)) return true;
    std::string text;
    if (!readBytes(file, text)) return error = "cannot read " + u8(file), false;
    JsonParser p{text};
    const Json j = p.value();
    if (!p.ok || j.type != Json::Type::Object) return error = u8(file.filename()) + " is not valid", false;
    if (j.integer("version", 1) > 1) return error = u8(file.filename()) + " is from a newer version", false;
    ee3Folder = j.str("ee3Folder");
    const std::string bad = u8(file.filename()) + " is not valid: ";
    if (!ee3Folder.empty() && !mediaFolderSafe(ee3Folder)) return *this = {}, error = bad + "\"" + ee3Folder + "\" is not a folder name", false;
    if (const Json* t = j.get("tracks"))
        for (const Json& e : t->items) {
            MusicTrack m;
            m.name = e.str("name"), m.file = e.str("file");
            const Json* ms = e.get("ms");
            m.ms = ms && ms->type == Json::Type::Number && ms->n > 0 ? uint32_t(std::min(ms->n, 4.0e9)) : 0;
            if (m.file.empty()) continue;
            if (!mediaFileSafe(m.file, "music")) return *this = {}, error = bad + "the file \"" + m.file + "\" is not in the folder music", false;
            tracks.push_back(std::move(m));
        }
    if (const Json* t = j.get("images"))
        for (const Json& e : t->items) {
            DotImage d;
            d.name = e.str("name"), d.file = e.str("file");
            d.width = std::max(0, e.integer("width", 0)), d.height = std::max(0, e.integer("height", 0));
            if (d.file.empty()) continue;
            if (!mediaFileSafe(d.file, "dots")) return *this = {}, error = bad + "the file \"" + d.file + "\" is not in the folder dots", false;
            images.push_back(std::move(d));
        }
    return true;
}

bool ProjectMedia::save(const fs::path& file, std::string& error) const {
    std::error_code ec;
    if (empty() && ee3Folder.empty()) {
        fs::remove(file, ec);
        return true;
    }
    std::string o = "{\n  \"version\": 1";
    if (!ee3Folder.empty()) o += ",\n  \"ee3Folder\": " + quote(ee3Folder);
    o += ",\n  \"tracks\": [";
    for (size_t i = 0; i < tracks.size(); ++i)
        o += std::string(i ? "," : "") + "\n    {\"name\": " + quote(tracks[i].name) + ", \"file\": " + quote(tracks[i].file) +
             ", \"ms\": " + std::to_string(tracks[i].ms) + "}";
    o += std::string(tracks.empty() ? "" : "\n  ") + "],\n  \"images\": [";
    for (size_t i = 0; i < images.size(); ++i)
        o += std::string(i ? "," : "") + "\n    {\"name\": " + quote(images[i].name) + ", \"file\": " + quote(images[i].file) +
             ", \"width\": " + std::to_string(images[i].width) + ", \"height\": " + std::to_string(images[i].height) + "}";
    o += std::string(images.empty() ? "" : "\n  ") + "]\n}\n";
    fs::create_directories(file.parent_path(), ec);
    return writeFile(file, o, error);
}

std::string ProjectMedia::freeTrackName(const std::string& base) const {
    return freeName(base, [&](const std::string& n) {
        return std::any_of(tracks.begin(), tracks.end(), [&](const MusicTrack& t) { return lowerAscii(t.name) == lowerAscii(n); });
    });
}
std::string ProjectMedia::freeImageName(const std::string& base) const {
    return freeName(base, [&](const std::string& n) {
        return std::any_of(images.begin(), images.end(), [&](const DotImage& t) { return lowerAscii(t.name) == lowerAscii(n); });
    });
}

// --- Dot images ----------------------------------------------------------------------------------------------------------

const uint32_t kDotColors[kDotColorCount] = {0xf70400, 0x3a57cc, 0x249824, 0x7c0200, 0x1d2c66, 0x124c12, 0x3e0100,
                                             0x0f1633, 0x092609, 0x982e66, 0x8d4e12, 0xffffff, 0x020b47, 0xfb8280,
                                             0xf94340, 0xbb4240, 0x7c0824, 0x8a1833, 0x808080, 0x404040, 0x202020};

int dotColorIndex(int r, int g, int b) {
    int best = 0;
    long long bestD = (1LL << 31) - 1;
    for (int i = 0; i < kDotColorCount; ++i) {
        const int dr = int(kDotColors[i] >> 16) - r, dg = int((kDotColors[i] >> 8) & 0xFF) - g, db = int(kDotColors[i] & 0xFF) - b;
        const long long d = 1LL * dr * dr + 1LL * dg * dg + 1LL * db * db;
        if (d < bestD) bestD = d, best = i;
    }
    return best;
}

bool decodePicture(const std::string& bytes, int& width, int& height, std::vector<uint8_t>& rgba) {
    int n = 0;
    width = height = 0;
    stbi_uc* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), int(std::min<size_t>(bytes.size(), 1u << 30)), &width, &height, &n, 4);
    if (!px) return false;
    rgba.assign(px, px + size_t(width) * size_t(height) * 4);
    stbi_image_free(px);
    return true;
}

bool dotPixels(const std::string& bytes, int& width, int& height, std::vector<uint8_t>& colors, std::string& why) {
    std::vector<uint8_t> rgba;
    colors.clear();
    if (!decodePicture(bytes, width, height, rgba)) return why = "it is not a PNG, BMP, GIF or JPEG picture", false;
    if (int64_t(width) * height >= kDotMaxPixels) {
        why = "its size (" + std::to_string(width) + " x " + std::to_string(height) + ") is not less than " + std::to_string(kDotMaxPixels) + " pixels";
        return false;
    }
    colors.reserve(size_t(width) * size_t(height));
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) colors.push_back(uint8_t(dotColorIndex(rgba[i], rgba[i + 1], rgba[i + 2])));
    return true;
}

bool writePngFile(const fs::path& file, int width, int height, const std::vector<uint8_t>& rgba) {
    if (width <= 0 || height <= 0 || rgba.size() < size_t(width) * size_t(height) * 4) return false;
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    PngWriter w;
    return w.open(file, width, height) && w.addRows(rgba.data(), height) && w.finish();
}

// --- Sounds ------------------------------------------------------------------------------------------------------------

namespace {

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

}  // namespace

SoundKind soundKind(const std::string& bytes, const std::string& extension) {
    if (bytes.size() >= 4 && bytes.compare(0, 4, "OggS") == 0) return SoundKind::Ogg;
    if (bytes.size() >= 12 && (bytes.compare(0, 4, "RIFF") == 0 || bytes.compare(0, 4, "RIFX") == 0) && bytes.compare(8, 4, "WAVE") == 0) return SoundKind::Wav;
    if (bytes.size() >= 3 && bytes.compare(0, 3, "ID3") == 0) return SoundKind::Mp3;
    const std::string e = lowerAscii(extension);
    if (e == ".mp3" && bytes.size() >= 2 && uint8_t(bytes[0]) == 0xFF && (uint8_t(bytes[1]) & 0xE0) == 0xE0) return SoundKind::Mp3;
    return SoundKind::Unknown;
}

namespace {
bool decodeSoundImpl(const std::string& bytes, const std::string& extension, Pcm& out, std::string& why);
bool encodeImpl(const Pcm& pcm, std::string& ogg, std::string& why, float quality);
}  // namespace

bool decodeSound(const std::string& bytes, const std::string& extension, Pcm& out, std::string& why) {
    try {
        return decodeSoundImpl(bytes, extension, out, why);
    } catch (const std::bad_alloc&) {
        out = {};
        return why = "there is not enough memory for it", false;
    }
}
bool encodeOggVorbis(const Pcm& pcm, std::string& ogg, std::string& why, float quality) {
    try {
        return encodeImpl(pcm, ogg, why, quality);
    } catch (const std::bad_alloc&) {
        ogg.clear();
        return why = "there is not enough memory for it", false;
    }
}

namespace {
bool decodeSoundImpl(const std::string& bytes, const std::string& extension, Pcm& out, std::string& why) {
    out = {};
    const SoundKind k = soundKind(bytes, extension);
    if (k == SoundKind::Wav) {
        unsigned int channels = 0, rate = 0;
        drwav_uint64 frames = 0;
        drwav_int16* s = drwav_open_memory_and_read_pcm_frames_s16(bytes.data(), bytes.size(), &channels, &rate, &frames, nullptr);
        if (!s || !channels || !rate) return drwav_free(s, nullptr), why = "it is a WAV file that cannot be read", false;
        out.channels = int(channels), out.rate = rate, out.samples.assign(s, s + size_t(frames) * channels);
        drwav_free(s, nullptr);
        return true;
    }
    if (k == SoundKind::Mp3) {
        drmp3_config cfg{};
        drmp3_uint64 frames = 0;
        drmp3_int16* s = drmp3_open_memory_and_read_pcm_frames_s16(bytes.data(), bytes.size(), &cfg, &frames, nullptr);
        if (!s || !cfg.channels || !cfg.sampleRate) return drmp3_free(s, nullptr), why = "it is an MP3 file that cannot be read", false;
        out.channels = int(cfg.channels), out.rate = cfg.sampleRate, out.samples.assign(s, s + size_t(frames) * cfg.channels);
        drmp3_free(s, nullptr);
        return true;
    }
    why = k == SoundKind::Ogg ? "it is an OGG file" : "it is not an OGG, WAV or MP3 file";
    return false;
}
}  // namespace

namespace {
// Samples at a rate as milliseconds, with no overflow; at most about 49 days (the most a uint32_t holds).
uint32_t framesToMs(uint64_t frames, uint32_t rate) {
    if (rate == 0) return 0;
    const uint64_t ms = frames / rate * 1000 + frames % rate * 1000 / rate;
    return uint32_t(std::min<uint64_t>(ms, 0xFFFFFFFFu));
}
// An Ogg Vorbis file's length from its headers: the rate from the first page's identification header, the samples
// from the last page's granule position (found from the end). No packet is read.
bool oggLength(const std::string& b, uint32_t& ms, std::string& why) {
    const auto* p = reinterpret_cast<const uint8_t*>(b.data());
    if (b.size() < 28 || b.compare(0, 4, "OggS") != 0) return why = "it is not an Ogg file", false;
    const size_t body = 27 + size_t(p[26]);
    if (b.size() < body + 30 || b.compare(body, 7, "\x01vorbis") != 0) return why = "it is not an Ogg Vorbis sound", false;
    const uint32_t rate = le32(p + body + 12);
    if (rate == 0) return why = "its Vorbis headers are damaged", false;
    for (size_t at = b.size() >= 27 ? b.size() - 27 : 0;; --at) {
        if (std::memcmp(p + at, "OggS", 4) == 0 && p[at + 4] == 0) {
            uint64_t granule = 0;
            for (int k = 7; k >= 0; --k) granule = granule << 8 | p[at + 6 + k];
            if (granule != ~0ull) return ms = framesToMs(granule, rate), true;
        }
        if (at == 0) break;
    }
    return ms = 0, true;
}
}  // namespace

bool soundLength(const std::string& bytes, const std::string& extension, uint32_t& ms, std::string& why) {
    ms = 0;
    try {
        switch (soundKind(bytes, extension)) {
        case SoundKind::Ogg: return oggLength(bytes, ms, why);
        case SoundKind::Wav: {  // (the header's frame count: nothing is decoded)
            drwav w;
            if (!drwav_init_memory(&w, bytes.data(), bytes.size(), nullptr)) return why = "it is a WAV file that cannot be read", false;
            ms = framesToMs(w.totalPCMFrameCount, w.sampleRate);
            drwav_uninit(&w);
            return true;
        }
        case SoundKind::Mp3: {  // (the frames' headers are counted: nothing is kept)
            drmp3 m;
            if (!drmp3_init_memory(&m, bytes.data(), bytes.size(), nullptr)) return why = "it is an MP3 file that cannot be read", false;
            ms = framesToMs(drmp3_get_pcm_frame_count(&m), m.sampleRate);
            drmp3_uninit(&m);
            return true;
        }
        default: return why = "it is not an OGG, WAV or MP3 file", false;
        }
    } catch (const std::bad_alloc&) {
        return why = "there is not enough memory for it", false;
    }
}

namespace {
bool encodeImpl(const Pcm& pcm, std::string& ogg, std::string& why, float quality) {
    ogg.clear();
    if (pcm.channels <= 0 || pcm.rate == 0) return why = "the sound has no channels", false;
    const int ch = std::min(pcm.channels, 2);
    vorbis_info vi;
    vorbis_info_init(&vi);
    if (vorbis_encode_init_vbr(&vi, ch, long(pcm.rate), std::clamp(quality, 0.0f, 1.0f)) != 0) {
        vorbis_info_clear(&vi);
        return why = "the Ogg Vorbis encoder does not take its sample rate (" + std::to_string(pcm.rate) + " Hz)", false;
    }
    vorbis_comment vc;
    vorbis_comment_init(&vc);
    vorbis_dsp_state vd;
    vorbis_block vb;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);
    ogg_stream_state os;
    ogg_stream_init(&os, 0x53445241);  // (a fixed serial number: the same sound gives the same file)
    ogg_packet h0, h1, h2;
    vorbis_analysis_headerout(&vd, &vc, &h0, &h1, &h2);
    ogg_stream_packetin(&os, &h0), ogg_stream_packetin(&os, &h1), ogg_stream_packetin(&os, &h2);
    ogg_page og;
    auto put = [&] { ogg.append(reinterpret_cast<const char*>(og.header), size_t(og.header_len)), ogg.append(reinterpret_cast<const char*>(og.body), size_t(og.body_len)); };
    while (ogg_stream_flush(&os, &og)) put();
    const uint64_t frames = pcm.frames();
    auto drain = [&] {
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            ogg_packet op;
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                while (ogg_stream_pageout(&os, &og)) put();
            }
        }
    };
    constexpr uint64_t kChunk = 4096;
    for (uint64_t at = 0; at < frames; at += kChunk) {
        const int n = int(std::min(kChunk, frames - at));
        float** buf = vorbis_analysis_buffer(&vd, n);
        for (int i = 0; i < n; ++i) {
            const int16_t* f = &pcm.samples[size_t(at + uint64_t(i)) * size_t(pcm.channels)];
            if (pcm.channels <= 2) {
                for (int c = 0; c < ch; ++c) buf[c][i] = float(f[c]) / 32768.0f;
            } else {  // (more channels: the even ones left, the odd ones right)
                float l = 0, r = 0;
                for (int c = 0; c < pcm.channels; ++c) (c % 2 ? r : l) += float(f[c]);
                buf[0][i] = l / float((pcm.channels + 1) / 2) / 32768.0f, buf[1][i] = r / float(pcm.channels / 2) / 32768.0f;
            }
        }
        vorbis_analysis_wrote(&vd, n);
        drain();
    }
    vorbis_analysis_wrote(&vd, 0);
    drain();
    while (ogg_stream_flush(&os, &og)) put();
    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    return true;
}
}  // namespace

namespace {
constexpr size_t kMaxParts = 3200;                  // (2 hours of parts of 2.28 seconds)
constexpr size_t kMaxPartBytes = size_t(512) << 20;  // (all the parts of a track)
constexpr size_t kMaxSetup = size_t(64) << 10;       // (the setup header, which each part has again)
bool cutOggVorbisImpl(const std::string& ogg, OggParts& out, std::string& why);
}  // namespace

bool cutOggVorbis(const std::string& ogg, OggParts& out, std::string& why) {
    try {
        return cutOggVorbisImpl(ogg, out, why);
    } catch (const std::bad_alloc&) {
        out = {};
        return why = "there is not enough memory for it", false;
    }
}

namespace {
bool cutOggVorbisImpl(const std::string& ogg, OggParts& out, std::string& why) {
    out = {};
    Vorbis v;
    if (!readVorbis(ogg, v, why)) return false;
    if (v.id.size() + v.comment.size() + v.setup.size() > kMaxSetup) return why = "its Vorbis headers are larger than 64 KB", false;
    // A packet's length in samples: a quarter of the block before it and a quarter of its own (the first gives none).
    // Empty or unknown packets give no sound: they are left out.
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
    if (v.audio.empty()) return why = "it holds no sound", false;
    // The cuts (ffmpeg's segment muxer): a new part at the first packet at or after each 2.28 seconds.
    std::vector<size_t> starts = {0};
    int64_t total = 0;  // (the samples before each packet's)
    for (size_t k = 0; k < v.audio.size(); ++k) {
        if (k > 0 && total >= int64_t(starts.size()) * 228 * v.rate / 100) starts.push_back(k);
        if (k > 0) total += (blocks[k - 1] + blocks[k]) / 4;
    }
    if (starts.size() > kMaxParts) return why = "it is longer than 2 hours", false;
    const int64_t trim = v.lastGranule >= 0 && v.lastGranule < total ? total - v.lastGranule : 0;
    size_t bytes = 0;
    for (size_t s = 0; s < starts.size(); ++s) {
        const size_t to = s + 1 < starts.size() ? starts[s + 1] : v.audio.size();
        out.parts.push_back(vorbisPart(v, blocks, starts[s], to, s + 1 == starts.size() ? trim : 0));
        bytes += out.parts.back().size();
        if (bytes > kMaxPartBytes) return out = {}, why = "its parts are larger than 512 MB", false;
    }
    out.rate = v.rate, out.channels = v.channels, out.otherStreams = v.otherStreams;
    out.samples = v.lastGranule >= 0 ? v.lastGranule : total;
    return true;
}
}  // namespace

bool trackOggVorbis(const std::string& bytes, const std::string& extension, std::string& ogg, std::string& why) {
    if (soundKind(bytes, extension) == SoundKind::Ogg) return ogg = bytes, true;
    Pcm p;
    return decodeSound(bytes, extension, p, why) && encodeOggVorbis(p, ogg, why);
}

std::string ee3SoundIndex(int index, bool value) {
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

// --- The generated modules ---------------------------------------------------------------------------------------------

std::string bgmModuleText(const std::vector<int>& lastPart, const std::vector<std::vector<std::string>>& partFiles, bool relativeHelper) {
    std::string eps = "// Generated by StarDraft from the project's music tracks (media.json). Do not edit: it is rewritten on every\n"
                      "// build. EUD Editor 3's BGM data (its BuildData.GetBGMEps, MIT): for each track, the sound name of its last part;\n"
                      "// PlayOGG plays one part; loadSound puts the parts into the map.\n";
    eps += relativeHelper ? "import .sd_mpqadd as sdmpq;\n" : "import sd_mpqadd as sdmpq;\n";
    eps += "const bgmlen = [";
    for (size_t k = 0; k < lastPart.size(); ++k) eps += (k ? ", " : "") + ee3SoundIndex(lastPart[k], true);
    eps += "];\nconst str = StringBuffer();\n";
    eps += "function PlayOGG(bgmindex, bgmcode, track) {\n"
           "    SetMemoryEPD(str.epd, SetTo, bgmcode);\n"
           "    SetMemoryEPD(str.epd + 1, SetTo, track);\n"
           "    str.Play();\n"
           "    if (MemoryEPD(EPD(bgmlen) + bgmindex, Exactly, track)) {\n"
           "        return 0;\n"
           "    } else {\n"
           "        return 1;\n"
           "    }\n"
           "}\n"
           "function loadSound() {\n"
           "    EUDPlayerLoop()();\n"
           "    str.insert(0, '');\n"
           "    str.append('@@@@@@@@');\n"
           "    EUDEndPlayerLoop();\n";
    for (size_t t = 0; t < partFiles.size(); ++t)
        for (size_t k = 0; k < partFiles[t].size(); ++k)
            eps += "    sdmpq.add('" + ee3SoundIndex(int(t), false) + ee3SoundIndex(int(k), false) + "', '" + partFiles[t][k] + "');\n";
    eps += "}\n";
    return eps;
}

std::string dotModuleText(const std::vector<std::vector<uint8_t>>& colors, const std::vector<DotImage>& images) {
    std::string eps = "// Generated by StarDraft from the project's dot images (media.json). Do not edit: it is rewritten on every build.\n"
                      "// EUD Editor 3's dot data (its BuildData.GetDotEps, MIT): for each picture, 1 (white is the background), its\n"
                      "// width and height, then the dot color of each pixel, row by row.\n"
                      "const arrdata = EUDArray(list(\n";
    for (size_t i = 0; i < images.size(); ++i) {
        eps += "    EUDArray(list(1, " + std::to_string(images[i].width) + ", " + std::to_string(images[i].height);
        if (i < colors.size())
            for (uint8_t c : colors[i]) eps += "," + std::to_string(c);
        eps += std::string("))") + (i + 1 < images.size() ? "," : "") + "  // " + images[i].name + "\n";
    }
    eps += "));\n\nfunction GetData(id) {\n    return arrdata[id];\n}\n";
    return eps;
}

const char* mpqAddText() {
    return "# Generated by StarDraft. Do not edit: it is rewritten on every build.\n"
           "# Puts a file into the map one time: two copies of the BGM data (generated/ and the folder of an imported EUD Editor 3\n"
           "# project's scripts) can name the same sound part.\n"
           "# The names added so far are on builtins, which every module of the build shares.\n"
           "import builtins\n"
           "\n"
           "from eudplib import *\n"
           "\n"
           "\n"
           "def f_add(name, path):  # (epScript calls a function of a module by its name with f_ before it)\n"
           "    added = builtins.__dict__.setdefault('_stardraft_mpq_added', set())\n"
           "    if name in added:\n"
           "        return\n"
           "    with open(path, 'rb') as f:\n"
           "        MPQAddFile(name, f.read())\n"
           "    added.add(name)\n";
}

const char* musicPlayerText() {
    return R"eps(// Generated by StarDraft. Do not edit: it is rewritten on every build.
// The player of the project's music tracks (Assets > Music) for the blocks Play music track, Stop the music track,
// Pause the music track and Resume the music track. As EUD Editor 3's BGMPlayer.eps (MIT), each computer plays the track
// of its own player: a sound is that computer's own, so the game does not desync. The blocks set the values of each
// player on every computer; the playing itself (sd_musicCycle) reads and changes only this computer's values.
import .BGMFlexible as bgm;

const sd_mIndex = EUDArray(8);  // the track of each player (its place in the list)
const sd_mCode = EUDArray(8);   // the track's part of the sound name ("@@@A" as a number)
const sd_mOn = EUDArray(8);     // 1: plays; 0: paused or stopped
const sd_mSeq = EUDArray(8);    // one more on each Play and Stop: the track starts from its first part
var sd_mPart = 0x40404040;      // this computer: the next part's name ("@@@@" as a number)
var sd_mLast = 0;               // this computer: the time when the last part started (0x51CE8C counts down)
var sd_mSeen = 0;               // this computer: the sd_mSeq that it plays

function sd_musicLoad() {
    bgm.loadSound();
}

function sd_musicPlay(index, code, mask) {
    for (var p = 0; p < 8; p++) {
        if (mask & (1 << p)) {
            sd_mIndex[p] = index;
            sd_mCode[p] = code;
            sd_mOn[p] = 1;
            sd_mSeq[p] += 1;
        }
    }
}

function sd_musicStop(mask) {
    for (var p = 0; p < 8; p++) {
        if (mask & (1 << p)) {
            sd_mOn[p] = 0;
            sd_mSeq[p] += 1;
        }
    }
}

function sd_musicPause(mask, on) {
    for (var p = 0; p < 8; p++) {
        if (mask & (1 << p)) {
            if (sd_mCode[p] != 0) sd_mOn[p] = on;
        }
    }
}

// On each trigger cycle: the next part of this computer's player's track, when the last part has played (2.22 seconds:
// a part is 2.28 seconds long).
function sd_musicCycle() {
    const p = getuserplayerid();
    if (p >= 8) return;
    const now = dwread_epd(EPD(0x51CE8C));
    if (sd_mSeq[p] != sd_mSeen) {
        sd_mSeen = sd_mSeq[p];
        sd_mPart = 0x40404040;
        sd_mLast = now + 2221;
    }
    if (sd_mOn[p] == 0) return;
    if (sd_mLast - now > 2220) {
        const cp = getcurpl();
        setcurpl(p);
        const r = bgm.PlayOGG(sd_mIndex[p], sd_mCode[p], sd_mPart);
        setcurpl(cp);
        sd_mLast = now;
        sd_mPart += 0x1000000;
        if (sd_mPart.AtLeastX(1, 0x10000000)) {
            DoActions(sd_mPart.SetNumberX(0, 0x10000000));
            sd_mPart += 0x10000;
            if (sd_mPart.AtLeastX(1, 0x100000)) {
                DoActions(sd_mPart.SetNumberX(0, 0x100000));
                sd_mPart += 0x100;
                if (sd_mPart.AtLeastX(1, 0x1000)) {
                    DoActions(sd_mPart.SetNumberX(0, 0x1000));
                    sd_mPart += 0x1;
                }
            }
        }
        if (r == 0) sd_mPart = 0x40404040;  // the last part: the track plays again from its start
    }
}
)eps";
}

const char* dotDrawText() {
    return R"eps(// Generated by StarDraft. Do not edit: it is rewritten on every build.
// Draw dot image: EUD Editor 3's dotTool.eps (MIT, Copyright (c) 2019 LeeJungHun), unchanged but for its import, and
// sd_dotDraw, the block's function. Each dot is a unit with the color of a nuke dot: the unit's image takes the color
// of images 232 and 233. Every computer makes the same units, so it is not a local block.
import .dotData as dD;


const nukebeam = 232;
const nukedot = 233;

const dark = 10;
const red = 0;
const blue = 16;
const green = 13;
const white = 17;
const deepblue = 12;

const clock = 6;

var temploc = 0;
var player = 0;
function SetLoc(loc){
	temploc = loc;
}
function SetPlayer(p){
	player = p;
}
var drawUnit = 0;
function SetDrawUnit(unit){
	drawUnit = unit;
}

var IsBack = 0;
function dotred(){
	SetMemoryXEPD(EPD(0x669E28 + 232 * 1), 7, 0, 0xFF);
	SetMemoryXEPD(EPD(0x669E28 + 233 * 1), 7, 0, 0xFF00);
	bwrite(0x663150 + drawUnit * 1 ,0);
	CreateUnit(1, drawUnit, temploc, player);
}
function dotlightblue(){
	SetMemoryXEPD(EPD(0x669E28 + 232 * 1), 7, 16, 0xFF);
	SetMemoryXEPD(EPD(0x669E28 + 233 * 1), 7, 4096, 0xFF00);
	bwrite(0x663150 + drawUnit * 1 ,0);
	CreateUnit(1, drawUnit, temploc, player);
}
function dotlightgreen(){
	SetMemoryXEPD(EPD(0x669E28 + 232 * 1), 7, 13, 0xFF);
	SetMemoryXEPD(EPD(0x669E28 + 233 * 1), 7, 3328, 0xFF00);
	bwrite(0x663150 + drawUnit * 1 ,0);
	CreateUnit(1, drawUnit, temploc, player);
}
function dotclock(){
	SetMemoryXEPD(EPD(0x669E28 + 232 * 1), 7, 6, 0xFF);
	SetMemoryXEPD(EPD(0x669E28 + 233 * 1), 7, 1536, 0xFF00);
	bwrite(0x663150 + drawUnit * 1 ,1);
	CreateUnit(1, drawUnit, temploc, player);
}
function dotdark(){
	SetMemoryXEPD(EPD(0x669E28 + 232 * 1), 7, 10, 0xFF);
	SetMemoryXEPD(EPD(0x669E28 + 233 * 1), 7, 2560, 0xFF00);
	bwrite(0x663150 + drawUnit * 1 ,2);
	CreateUnit(1, drawUnit, temploc, player);
}
function dotwhite(){
	if(IsBack == 0){
		SetMemoryXEPD(EPD(0x669E28 + 232 * 1), 7, 17, 0xFF);
		SetMemoryXEPD(EPD(0x669E28 + 233 * 1), 7, 4352, 0xFF00);
		bwrite(0x663150 + drawUnit * 1 ,0);
		CreateUnit(1, drawUnit, temploc, player);
	}

}
function dotdeepblue(){
	SetMemoryXEPD(EPD(0x669E28 + 232 * 1), 7, 12, 0xFF);
	SetMemoryXEPD(EPD(0x669E28 + 233 * 1), 7, 3072, 0xFF00);
	bwrite(0x663150 + drawUnit * 1 ,0);
	CreateUnit(1, drawUnit, temploc, player);
}
function dotdarkred(){
	dotdark();
	dotred();
}
function dotblue(){
	dotdark();
	dotlightblue();
}
function dotgreen(){
	dotdark();
	dotlightgreen();
}

function dotbrown(){
	dotdark();
	dotdark();
	dotred();
}
function dotindigoblue(){
	dotdark();
	dotdark();
	dotlightblue();
}
function dotdarkgreen(){
	dotdark();
	dotdark();
	dotlightgreen();
}
function dotpink(){
	dotclock();
	dotlightblue();
}
function dotorange(){
	dotclock();
	dotlightgreen();
}
function dotlightpink(){
	dotclock();
	dotwhite();
}
function dotlightred(){
	dotclock();
	dotclock();
	dotwhite();
}
function dotpalered(){
	dotclock();
	dotdark();
	dotwhite();
}
function dotmargenta(){
	dotclock();
	dotdeepblue();
}
function dotdarkpink(){
	dotpink();
	dotdark();
}
function dotlightgray(){
	dotdark();
	dotwhite();
}
function dotgray(){
	dotdark();
	dotdark();
	dotwhite();
}
function dotdarkgray(){
	dotdark();
	dotdark();
	dotdark();
	dotwhite();
}

function drawStart(unit : TrgUnit, player : TrgPlayer, loc : TrgLocation){
	RemoveUnit(unit, player);
	SetDrawUnit(unit);
	SetPlayer(player);
	SetLoc(loc);
}


function draw(id,size){
	const arr = EUDArray.cast(dD.GetData(id));
	IsBack = arr[0];
	const width = arr[1];
	const height = arr[2];

	const arrepd = EPD(arr)+3;

	var index = 0;
	for(var y = 0 ; y <  height ; y++){
		for(var x = 0 ; x <  width ; x++){
			const epd = arrepd + index;
			if(MemoryEPD(epd, Exactly, 0)){
				dotred();
			}else if(MemoryEPD(epd, Exactly, 1)){
				dotlightblue();
			}else if(MemoryEPD(epd, Exactly, 2)){
				dotlightgreen();
			}else if(MemoryEPD(epd, Exactly, 3)){
				dotdarkred();
			}else if(MemoryEPD(epd, Exactly, 4)){
				dotblue();
			}else if(MemoryEPD(epd, Exactly, 5)){
				dotgreen();
			}else if(MemoryEPD(epd, Exactly, 6)){
				dotbrown();
			}else if(MemoryEPD(epd, Exactly, 7)){
				dotindigoblue();
			}else if(MemoryEPD(epd, Exactly, 8)){
				dotdarkgreen();
			}else if(MemoryEPD(epd, Exactly, 9)){
				dotpink();
			}else if(MemoryEPD(epd, Exactly, 10)){
				dotorange();
			}else if(MemoryEPD(epd, Exactly, 11)){
				dotwhite();
			}else if(MemoryEPD(epd, Exactly, 12)){
				dotdeepblue();
			}else if(MemoryEPD(epd, Exactly, 13)){
				dotlightpink();
			}else if(MemoryEPD(epd, Exactly, 14)){
				dotlightred();
			}else if(MemoryEPD(epd, Exactly, 15)){
				dotpalered();
			}else if(MemoryEPD(epd, Exactly, 16)){
				dotmargenta();
			}else if(MemoryEPD(epd, Exactly, 17)){
				dotdarkpink();
			}else if(MemoryEPD(epd, Exactly, 18)){
				dotlightgray();
			}else if(MemoryEPD(epd, Exactly, 19)){
				dotgray();
			}else if(MemoryEPD(epd, Exactly, 20)){
				dotdarkgray();
			}

			addloc(temploc, size, 0);
			index += 1;
		}
		addloc(temploc, -size * width, size);
	}
}

// The block Draw dot image: the picture's dots from the location's top left, `size` pixels apart. The location goes
// back to where it was.
function sd_dotDraw(id, unit, player, loc, size) {
	drawStart(unit, player, loc);
	draw(id, size);
	const arr = EUDArray.cast(dD.GetData(id));
	addloc(loc, 0, -size * arr[2]);
}
)eps";
}

// --- Build -------------------------------------------------------------------------------------------------------------

namespace {
bool writeMediaCodeImpl(const EudProject& pr, const ProjectMedia& media, MediaBuildReport& report, std::string& error);
}  // namespace

bool writeMediaCode(const EudProject& pr, const ProjectMedia& media, MediaBuildReport& report, std::string& error) {
    // (the lists come from a file: their paths are checked here again, before any file is read, written or removed)
    if (!media.ee3Folder.empty() && !mediaFolderSafe(media.ee3Folder)) return error = "Could not use media.json: \"" + media.ee3Folder + "\" is not a folder name", false;
    for (const MusicTrack& t : media.tracks)
        if (!mediaFileSafe(t.file, "music")) return error = "Could not use the music track \"" + t.name + "\": its file is not in the folder music", false;
    for (const DotImage& d : media.images)
        if (!mediaFileSafe(d.file, "dots")) return error = "Could not use the dot image \"" + d.name + "\": its file is not in the folder dots", false;
    try {
        return writeMediaCodeImpl(pr, media, report, error);
    } catch (const std::bad_alloc&) {
        return error = "Could not make the music tracks' parts: there is not enough memory", false;
    }
}

namespace {
bool writeMediaCodeImpl(const EudProject& pr, const ProjectMedia& media, MediaBuildReport& report, std::string& error) {
    report = {};
    const fs::path gen = pr.folder() / "generated";
    std::error_code ec;
    auto put = [&](const fs::path& f, const std::string& text) {
        fs::create_directories(f.parent_path(), ec);
        std::string old;
        if (readBytes(f, old) && old == text) return true;  // (unchanged: the file keeps its time)
        return writeFile(f, text, error);
    };
    auto drop = [&](const fs::path& f) { fs::remove(f, ec); };
    const fs::path ee3 = media.ee3Folder.empty() ? fs::path() : pr.folder() / pathOf(media.ee3Folder);

    // Music: the parts of each track, then the modules.
    if (media.tracks.empty()) {
        for (const char* f : {"BGMFlexible.eps", "sdmusic.eps"}) drop(gen / f);
        fs::remove_all(gen / "music", ec);
        if (!ee3.empty()) drop(ee3 / "BGMFlexible.eps");
    } else {
        std::vector<int> last;
        std::vector<std::vector<std::string>> files;
        for (size_t t = 0; t < media.tracks.size(); ++t) {
            const MusicTrack& tr = media.tracks[t];
            const fs::path src = pr.folder() / pathOf(tr.file), dir = gen / "music" / std::to_string(t);
            const std::string what = "the music track \"" + tr.name + "\" (" + tr.file + ")";
            const auto size = fs::file_size(src, ec);
            if (ec) return error = "Could not read " + what + ": the file is not found", false;
            const auto time = fs::last_write_time(src, ec).time_since_epoch().count();
            // The parts stay while the source is the same file (its name, size and time): encoding takes a few seconds.
            const std::string key = tr.file + "\n" + std::to_string(size) + "\n" + std::to_string(time) + "\n";
            std::string oldKey, count;
            std::vector<std::string> names;
            int parts = -1;
            if (readBytes(dir / "source.txt", oldKey) && oldKey.rfind(key, 0) == 0) {
                parts = std::atoi(oldKey.c_str() + key.size());
                for (int k = 0; k < parts; ++k)
                    if (!fs::exists(dir / ("st" + std::to_string(k) + ".ogg"), ec)) parts = -1;
            }
            if (parts <= 0) {
                std::string bytes, ogg, why;
                if (!readBytes(src, bytes)) return error = "Could not read " + what, false;
                const bool encode = soundKind(bytes, u8(src.extension())) != SoundKind::Ogg;
                if (!trackOggVorbis(bytes, u8(src.extension()), ogg, why)) return error = "Could not use " + what + ": " + why, false;
                OggParts cut;
                if (!cutOggVorbis(ogg, cut, why)) return error = "Could not use " + what + ": " + why, false;
                fs::remove_all(dir, ec);
                fs::create_directories(dir, ec);
                for (size_t k = 0; k < cut.parts.size(); ++k)
                    if (!writeFile(dir / ("st" + std::to_string(k) + ".ogg"), cut.parts[k], error)) return false;
                if (!writeFile(dir / "source.txt", key + std::to_string(cut.parts.size()) + "\n", error)) return false;
                parts = int(cut.parts.size());
                report.encoded += encode;
            }
            for (int k = 0; k < parts; ++k) names.push_back("generated/music/" + std::to_string(t) + "/st" + std::to_string(k) + ".ogg");
            report.parts += parts;
            last.push_back(parts - 1);
            files.push_back(std::move(names));
        }
        for (size_t t = media.tracks.size();; ++t) {  // (the parts of tracks that were removed)
            if (!fs::exists(gen / "music" / std::to_string(t), ec)) break;
            fs::remove_all(gen / "music" / std::to_string(t), ec);
        }
        if (!put(gen / "BGMFlexible.eps", bgmModuleText(last, files, true)) || !put(gen / "sdmusic.eps", musicPlayerText())) return false;
        if (!ee3.empty() && !put(ee3 / "BGMFlexible.eps", bgmModuleText(last, files, false))) return false;
    }

    // Dot images: each picture's colors, then the modules.
    if (media.images.empty()) {
        for (const char* f : {"dotData.eps", "sddots.eps"}) drop(gen / f);
        if (!ee3.empty()) drop(ee3 / "dotData.eps");
    } else {
        std::vector<std::vector<uint8_t>> colors;
        std::vector<DotImage> images = media.images;
        for (DotImage& d : images) {
            std::string bytes, why;
            std::vector<uint8_t> c;
            const std::string what = "the dot image \"" + d.name + "\" (" + d.file + ")";
            if (!readBytes(pr.folder() / pathOf(d.file), bytes)) return error = "Could not read " + what + ": the file is not found", false;
            if (!dotPixels(bytes, d.width, d.height, c, why)) return error = "Could not use " + what + ": " + why, false;
            colors.push_back(std::move(c));
        }
        const std::string text = dotModuleText(colors, images);
        if (!put(gen / "dotData.eps", text) || !put(gen / "sddots.eps", dotDrawText())) return false;
        if (!ee3.empty() && !put(ee3 / "dotData.eps", text)) return false;
    }

    // The helper that both copies of the BGM data use.
    if (media.tracks.empty()) {
        drop(gen / "sd_mpqadd.py");
        if (!ee3.empty()) drop(ee3 / "sd_mpqadd.py");
    } else {
        if (!put(gen / "sd_mpqadd.py", mpqAddText())) return false;
        if (!ee3.empty() && !put(ee3 / "sd_mpqadd.py", mpqAddText())) return false;
    }
    return true;
}

}  // namespace

}  // namespace stardraft
