// EUD Editor 3's dot images and BGM player (stardraft/import_ee3_extras.h).
//
// usage: import_ee3_media_test [project.e3s]
//        import_ee3_media_test --cut sound.ogg folder   (writes the parts of a sound, for a check with a real decoder)
//
// No real project has dot images, so the inputs are made here (tools/tests/nrbf_writer.h) with EUD Editor 3's class and
// member names, with a small PNG for the picture and a small Ogg Vorbis stream (written here, in a new temporary folder)
// for the sounds. The expected texts are EUD Editor 3's GetDotEps and GetBGMEps (Class/BulidData/WriteDotData.vb,
// WriteBGMFile.vb) for the same data, traced by hand. With a project file, the test also prints what the BGM player of
// that project gives (its problems), for a check by eye.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "nrbf_writer.h"
#include "stardraft/import_ee3_extras.h"

using namespace stardraft;
namespace fs = std::filesystem;

namespace {

int failed = 0;

void check(bool ok, const std::string& what) {
    if (!ok) ++failed, std::printf("FAILED: %s\n", what.c_str());
}

std::string crlf(const std::vector<std::string>& lines) {
    std::string s;
    for (const std::string& l : lines) s += l + "\r\n";
    return s;
}

void checkText(const std::string& got, const std::string& want, const std::string& what) {
    if (got == want) return;
    ++failed;
    std::printf("FAILED: %s\n--- got:\n%s\n--- want:\n%s\n---\n", what.c_str(), got.c_str(), want.c_str());
}

void checkProblems(const std::vector<std::string>& got, const std::vector<std::string>& want, const std::string& what) {
    if (got == want) return;
    ++failed;
    std::printf("FAILED: %s\n  got:\n", what.c_str());
    for (const std::string& p : got) std::printf("    %s\n", p.c_str());
    std::printf("  want:\n");
    for (const std::string& p : want) std::printf("    %s\n", p.c_str());
}

std::string u8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
fs::path pathOf(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

void writeFile(const fs::path& f, const std::string& bytes) {
    std::ofstream out(f, std::ios::binary);
    out << bytes;
}

// --- A small PNG (RGB, 8 bits, stored deflate blocks) ----------------------------------------------------------------

uint32_t zcrc(const std::string& s) {
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : s) {
        c ^= b;
        for (int k = 0; k < 8; ++k) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}
void be32(std::string& s, uint32_t v) {
    for (int k = 3; k >= 0; --k) s += char(v >> (8 * k));
}
std::string png(int w, int h, const std::vector<uint32_t>& rgb) {
    std::string raw;
    for (int y = 0; y < h; ++y) {
        raw += char(0);
        for (int x = 0; x < w; ++x) {
            const uint32_t c = rgb[size_t(y * w + x)];
            raw += char(c >> 16), raw += char(c >> 8), raw += char(c);
        }
    }
    uint32_t a = 1, b = 0;
    for (unsigned char ch : raw) a = (a + ch) % 65521, b = (b + a) % 65521;
    std::string z = "\x78\x01";
    z += char(1), z += char(raw.size() & 0xFF), z += char(raw.size() >> 8), z += char(~raw.size() & 0xFF), z += char((~raw.size() >> 8) & 0xFF);
    z += raw;
    be32(z, b << 16 | a);
    auto chunk = [](std::string& out, const std::string& type, const std::string& data) {
        be32(out, uint32_t(data.size()));
        out += type + data;
        be32(out, zcrc(type + data));
    };
    std::string ihdr;
    be32(ihdr, uint32_t(w)), be32(ihdr, uint32_t(h));
    ihdr += std::string("\x08\x02\x00\x00\x00", 5);
    std::string out = "\x89PNG\r\n\x1a\n";
    chunk(out, "IHDR", ihdr), chunk(out, "IDAT", z), chunk(out, "IEND", "");
    return out;
}

N bitmap(const std::string& data) {
    N b = obj("", {{"Data", bytes(std::vector<uint8_t>(data.begin(), data.end()))}});
    b.s = "System.Drawing.Bitmap";
    return b;
}
N dotData(const std::string& name, int w, int h, const std::string& picture) {
    return obj("DotData", {{"ItemName", str(name)},
                           {"PixelOrgData", bitmap(picture)},
                           {"ImagePallet", null()},
                           {"ColorFlag", null()},
                           {"LoadSuccess", boolean(true)},
                           {"Pixels", null()},  // (EUD Editor 3 never fills it)
                           {"imgWidth", i32(w)},
                           {"imgHeight", i32(h)}});
}

// --- A small Ogg Vorbis stream (headers that parse; its audio packets are not real sound) ----------------------------

struct BitWriter {
    std::string out;
    int bit = 0;
    void put(uint32_t v, int n) {
        for (int i = 0; i < n; ++i, ++bit) {
            if ((bit & 7) == 0) out += char(0);
            if (v >> i & 1) out.back() = char(out.back() | (1 << (bit & 7)));
        }
    }
};

std::string vorbisSetup() {
    BitWriter b;
    b.put(0, 8);                                        // one codebook
    b.put(0x564342, 24), b.put(1, 16), b.put(2, 24);    // dimensions 1, entries 2
    b.put(0, 1), b.put(0, 1), b.put(0, 5), b.put(0, 5); // not ordered, not sparse, two lengths
    b.put(0, 4);                                        // no lookup
    b.put(0, 6), b.put(0, 16);                          // one time
    b.put(0, 6), b.put(1, 16), b.put(0, 5), b.put(1, 2), b.put(7, 4);  // one floor 1, no partitions
    b.put(0, 6), b.put(0, 16), b.put(0, 24), b.put(0, 24), b.put(0, 24), b.put(0, 6), b.put(0, 8), b.put(0, 3), b.put(0, 1);  // one residue
    b.put(0, 6), b.put(0, 16), b.put(0, 1), b.put(0, 1), b.put(0, 2), b.put(0, 8), b.put(0, 8), b.put(0, 8);  // one mapping
    b.put(1, 6);                                        // two modes: short, long
    b.put(0, 1), b.put(0, 16), b.put(0, 16), b.put(0, 8);
    b.put(1, 1), b.put(0, 16), b.put(0, 16), b.put(0, 8);
    b.put(1, 1);                                        // framing
    return "\x05vorbis" + b.out;
}
std::string vorbisId() {
    std::string s = "\x01vorbis";
    s += std::string(4, '\0');                              // version
    s += char(1);                                           // channels
    s += char(0x40), s += char(0x1F), s += std::string(2, '\0');  // 8000 Hz
    s += std::string(12, '\0');                             // bit rates
    s += char(0xB8);                                        // blocks of 256 and 2048 samples
    s += char(1);
    return s;
}
const std::string kComment = std::string("\x03vorbis\x04\0\0\0test\0\0\0\0\x01", 20);

uint32_t oggCrc(const std::string& s) {
    uint32_t crc = 0;
    for (unsigned char ch : s) {
        crc ^= uint32_t(ch) << 24;
        for (int k = 0; k < 8; ++k) crc = crc & 0x80000000u ? (crc << 1) ^ 0x04c11db7u : crc << 1;
    }
    return crc;
}
// One page for each packet.
std::string oggPage(const std::string& packet, uint8_t flags, int64_t granule, uint32_t seq) {
    std::string lacing;
    for (size_t left = packet.size();; left -= 255) {
        lacing += char(left >= 255 ? 255 : left);
        if (left < 255) break;
    }
    std::string h = "OggS";
    h += char(0), h += char(flags);
    for (int k = 0; k < 8; ++k) h += char(uint64_t(granule) >> (8 * k));
    for (uint32_t v : {0x1234u, seq, 0u})
        for (int k = 0; k < 4; ++k) h += char(v >> (8 * k));
    h += char(lacing.size()) + lacing + packet;
    const uint32_t crc = oggCrc(h);
    for (int k = 0; k < 4; ++k) h[22 + k] = char(crc >> (8 * k));
    return h;
}

// The audio packets: 50, all long blocks but number 20 (short). Each packet's samples (a quarter of its block and of
// the one before it): 1024, but 576 for numbers 20 and 21. All of them: 49280.
std::vector<std::string> audioPackets() {
    std::vector<std::string> p;
    for (int k = 0; k < 50; ++k) {
        std::string s(1, char(k == 20 ? 0 : 2));  // (bit 0: audio; bit 1: the mode)
        for (int j = 0; j < 300 + 7 * k; ++j) s += char((k + j) & 0xFF);
        p.push_back(s);
    }
    return p;
}
const int64_t kTrim = 300;  // (the last page's granule position says that the last 300 samples are not sound)

std::string oggFile() {
    std::string f = oggPage(vorbisId(), 2, 0, 0) + oggPage(kComment, 0, 0, 1) + oggPage(vorbisSetup(), 0, 0, 2);
    const std::vector<std::string> a = audioPackets();
    int64_t g = 0;
    for (size_t k = 0; k < a.size(); ++k) {
        if (k > 0) g += (k == 20 || k == 21) ? 576 : 1024;
        const bool last = k + 1 == a.size();
        f += oggPage(a[k], last ? 4 : 0, last ? g - kTrim : g, uint32_t(3 + k));
    }
    return f;
}

// Reads a part back: its pages (each checksum, BOS on the first, EOS on the last only), its packets and its last
// granule position.
bool readOgg(const std::string& f, std::vector<std::string>& packets, int64_t& lastGranule, std::string& why) {
    size_t at = 0;
    std::string packet;
    uint32_t seq = 0;
    while (at < f.size()) {
        if (f.compare(at, 4, "OggS") != 0) return why = "no page at " + std::to_string(at), false;
        const size_t segs = uint8_t(f[at + 26]);
        size_t body = 0;
        for (size_t k = 0; k < segs; ++k) body += uint8_t(f[at + 27 + k]);
        std::string page = f.substr(at, 27 + segs + body);
        uint32_t crc = 0, pseq = 0;
        for (int k = 3; k >= 0; --k) crc = crc << 8 | uint8_t(page[22 + k]), pseq = pseq << 8 | uint8_t(page[18 + k]);
        for (int k = 0; k < 4; ++k) page[22 + k] = 0;
        if (oggCrc(page) != crc) return why = "a wrong checksum", false;
        if (pseq != seq++) return why = "a wrong page number", false;
        const uint8_t flags = uint8_t(page[5]);
        const bool last = at + page.size() == f.size();
        if (bool(flags & 2) != (at == 0) || bool(flags & 4) != last) return why = "wrong page flags", false;
        int64_t g = 0;
        for (int k = 7; k >= 0; --k) g = g << 8 | uint8_t(page[6 + k]);
        if (g != -1) lastGranule = g;
        size_t d = 27 + segs;
        for (size_t k = 0; k < segs; ++k) {
            const size_t len = uint8_t(page[27 + k]);
            packet += page.substr(d, len), d += len;
            if (len < 255) packets.push_back(packet), packet.clear();
        }
        at += page.size();
    }
    return true;
}

N bgmFile(const std::string& name, const std::string& path, const std::string& rel, int rate = -1) {
    return obj("BGMData+BGMFile", {{"_BGMFile", null()},
                                   {"_BGMName", str(name)},
                                   {"_BGMPath", str(path)},
                                   {"_BGMRelativePath", str(rel)},
                                   {"_BGMLen", i32(10)},
                                   {"_BGMCount", i32(0)},
                                   {"_BGMSampleRate", i32(rate)},
                                   {"_BGMBitRate", i32(-1)},
                                   {"BGMorgSize", i64(1000)},
                                   {"BGMCompressionSize", i64(-1)},
                                   {"BGMBlockCount", i32(7)}});  // (not used: the import counts its own parts)
}

N triggerEditor(std::vector<N> dots, std::vector<N> bgms) {
    return obj("TriggerEditorData", {{"_DotDatas", list(std::move(dots))}, {"_BGMData", obj("BGMData", {{"BGMList", list(std::move(bgms))}})}});
}

bool graphOf(const N& te, nrbf::Graph& g) {
    Writer w;
    const std::string s = w.stream(te);
    std::string error;
    const bool ok = g.read(reinterpret_cast<const uint8_t*>(s.data()), s.size(), error);
    check(ok, "the test graph reads: " + error);
    return ok;
}

const std::string kDotTail = ")\r\n);\r\n\r\nfunction GetData(id){\r\n\treturn arrdata[id];\r\n}\r\n";

void testDots() {
    // 3 x 2 pixels: red, white, black (nearest: #092609, number 8), gray (#808080, 18), #3c0000 (nearest #3e0100, 6),
    // #fa8280 (nearest #fb8280, 13).
    const std::string pic = png(3, 2, {0xf70400, 0xffffff, 0x000000, 0x808080, 0x3c0000, 0xfa8280});
    {
        nrbf::Graph g;
        if (!graphOf(triggerEditor({dotData("heart", 3, 2, pic)}, {}), g)) return;
        std::vector<ImportFile> files;
        std::vector<std::string> problems;
        check(ee3DotImages(g, g.root(), files, problems), "dot images: made");
        check(files.size() == 1 && files[0].path == "TriggerEditor/dotData.eps", "dot images: one file, dotData.eps");
        if (files.size() == 1)
            checkText(files[0].bytes, "\r\nconst arrdata = EUDArray(list(\r\nEUDArray(list(1,3,2,0,11,8,18,6,13))" + kDotTail, "dot images: the text");
        checkProblems(problems, {}, "dot images: no problems");
    }
    {  // A picture that cannot be read keeps its place (the numbers of the others do not change).
        nrbf::Graph g;
        if (!graphOf(triggerEditor({dotData("bad", 2, 2, "not a picture"), dotData("heart", 3, 2, pic), dotData("big", 40, 25, pic)}, {}), g)) return;
        std::vector<ImportFile> files;
        std::vector<std::string> problems;
        check(!ee3DotImages(g, g.root(), files, problems), "dot images (one bad): not all made");
        if (files.size() == 1)
            checkText(files[0].bytes,
                      "\r\nconst arrdata = EUDArray(list(\r\nEUDArray(list(1,0,0)),\r\nEUDArray(list(1,3,2,0,11,8,18,6,13)),\r\nEUDArray(list(1,0,0))" + kDotTail,
                      "dot images (one bad): the text");
        else
            check(false, "dot images (one bad): one file");
        checkProblems(problems,
                      {"the dot image 0 (\"bad\"): its picture is not a PNG, BMP, GIF or JPEG image that the import can read; its data is empty in dotData.eps",
                       "the dot image 2 (\"big\"): its size (40 x 25) is not less than 1000 pixels; its data is empty in dotData.eps"},
                      "dot images (one bad): the problems");
    }
    {
        nrbf::Graph g;
        if (!graphOf(triggerEditor({}, {}), g)) return;
        std::vector<ImportFile> files;
        std::vector<std::string> problems;
        check(ee3DotImages(g, g.root(), files, problems) && files.empty() && problems.empty(), "dot images (none): no file");
    }
}

// The player's text for these bgmlen values and MPQAddFile lines.
std::string bgmEps(const std::string& lens, const std::vector<std::string>& adds) {
    std::vector<std::string> l = {"import py_pathlib;const bgmlen = [" + lens + "];",
                                  "const str = StringBuffer();",
                                  "function PlayOGG(bgmindex ,bgmcode, track){",
                                  "    SetMemoryEPD(str.epd, SetTo, bgmcode);",
                                  "    SetMemoryEPD(str.epd + 1, SetTo, track);",
                                  "    str.Play();",
                                  "",
                                  "    if (MemoryEPD(EPD(bgmlen) + bgmindex, Exactly, track)){",
                                  "        return 0;",
                                  "    }else{",
                                  "        return 1;",
                                  "    }",
                                  "}",
                                  "function loadSound(){",
                                  "EUDPlayerLoop()();",
                                  "    str.insert(0, '');",
                                  "    str.append('@@@@@@@@');",
                                  "EUDEndPlayerLoop();"};
    l.insert(l.end(), adds.begin(), adds.end());
    l.push_back("}");
    return crlf(l);
}
std::string add(const std::string& code, const std::string& path) {
    return "    MPQAddFile('" + code + "', pathlib.Path('" + path + "').read_bytes());";
}

void testBgm(const fs::path& dir) {
    fs::create_directories(dir / "music");
    const std::string ogg = oggFile();
    writeFile(dir / "music" / "theme.ogg", ogg);
    writeFile(dir / "music" / "battle.ogg", ogg);
    writeFile(dir / "music" / "wave.wav", std::string("RIFF\x24\0\0\0WAVEfmt ", 16));
    const std::string folder = u8(dir);
    {  // Two sounds: one by its path, one by its relative path (its path is not there).
        nrbf::Graph g;
        if (!graphOf(triggerEditor({}, {bgmFile("Theme", u8(dir / "music" / "theme.ogg"), "", 22050),
                                        bgmFile("Battle", u8(dir / "gone" / "battle.ogg"), "music/battle.ogg")}),
                     g))
            return;
        std::vector<ImportFile> files;
        std::vector<std::string> problems;
        check(ee3BgmPlayer(g, g.root(), folder, files, problems), "BGM: made");
        checkProblems(problems,
                      {"the sound 0 (\"Theme\"): its sample rate stays 8000 Hz. EUD Editor 3 converts it to 22050 Hz with ffmpeg; the import keeps the "
                       "file's rate"},
                      "BGM: the problems");
        // 2.28 seconds at 8000 Hz: 18240 samples. The parts start at the packets with 18432 and 36992 samples before
        // them (packets 19 and 38): 3 parts, the last number 2 ("@@@B": 0x42404040).
        const std::vector<std::string> want = {"TriggerEditor/BGMFlexible.eps", "sound/Theme/st0.ogg", "sound/Theme/st1.ogg", "sound/Theme/st2.ogg",
                                               "sound/Battle/st0.ogg", "sound/Battle/st1.ogg", "sound/Battle/st2.ogg"};
        std::vector<std::string> got;
        for (const ImportFile& f : files) got.push_back(f.path);
        checkProblems(got, want, "BGM: the files");
        if (files.size() != want.size()) return;
        checkText(files[0].bytes,
                  bgmEps("0x42404040, 0x42404040", {add("@@@@@@@@", "sound/Theme/st0.ogg"), add("@@@@@@@A", "sound/Theme/st1.ogg"),
                                                    add("@@@@@@@B", "sound/Theme/st2.ogg"), add("@@@A@@@@", "sound/Battle/st0.ogg"),
                                                    add("@@@A@@@A", "sound/Battle/st1.ogg"), add("@@@A@@@B", "sound/Battle/st2.ogg")}),
                  "BGM: BGMFlexible.eps");
        // Each part: the three headers, then its packets; its last granule position: the samples of its packets after
        // the first (that one gives none), less the trim on the last part.
        const std::vector<std::string> audio = audioPackets();
        const size_t starts[] = {0, 19, 38, 50};
        const int64_t lengths[] = {18 * 1024, 576 + 576 + 16 * 1024, 11 * 1024 - kTrim};
        for (int s = 0; s < 3; ++s) {
            std::vector<std::string> packets;
            int64_t last = -2;
            std::string why;
            const std::string what = "BGM: part " + std::to_string(s);
            if (!readOgg(files[size_t(1 + s)].bytes, packets, last, why)) {
                check(false, what + ": " + why);
                continue;
            }
            std::vector<std::string> wantPackets = {vorbisId(), kComment, vorbisSetup()};
            wantPackets.insert(wantPackets.end(), audio.begin() + starts[s], audio.begin() + starts[s + 1]);
            check(packets == wantPackets, what + ": its packets");
            check(last == lengths[s], what + ": its length (" + std::to_string(last) + ", not " + std::to_string(lengths[s]) + ")");
            check(files[size_t(4 + s)].bytes == files[size_t(1 + s)].bytes, what + ": the same for the second sound");
        }
    }
    {  // Sounds that cannot be made: the player is made with the others (their bgmlen: 0x40404040, no parts).
        nrbf::Graph g;
        if (!graphOf(triggerEditor({}, {bgmFile("Missing", u8(dir / "none.ogg"), "music/none.ogg"),
                                        bgmFile("Net", "\\\\server\\share\\a.ogg", ""),
                                        bgmFile("Net2", "", "//server/share/a.ogg"),
                                        bgmFile("Wave", u8(dir / "music" / "wave.wav"), ""),
                                        bgmFile("Text", u8(dir / "music" / "notes.txt"), ""),
                                        bgmFile("../x:'y", u8(dir / "music" / "theme.ogg"), "")}),
                     g))
            return;
        writeFile(dir / "music" / "notes.txt", "text");
        std::vector<ImportFile> files;
        std::vector<std::string> problems;
        check(!ee3BgmPlayer(g, g.root(), folder, files, problems), "BGM (bad): not all made");
        const std::string wav = u8((dir / "music" / "wave.wav").lexically_normal());
        checkProblems(problems,
                      {"the sound 0 (\"Missing\"): its sound file is not found (" + u8((dir / "none.ogg").lexically_normal()) + "; " +
                           u8((dir / "music/none.ogg").lexically_normal()) + ")",
                       "the sound 1 (\"Net\"): its file \\\\server\\share\\a.ogg is on a network path, which the import does not open",
                       "the sound 2 (\"Net2\"): its file //server/share/a.ogg is on a network path, which the import does not open",
                       "the sound 3 (\"Wave\"): its file " + wav + ": it is a WAV file that cannot be read",
                       "the sound 4 (\"Text\"): its file " + u8((dir / "music" / "notes.txt").lexically_normal()) + " is not an OGG, MP3 or WAV file"},
                      "BGM (bad): the problems");
        check(files.size() == 4, "BGM (bad): BGMFlexible.eps and the 3 parts of the last sound");
        if (!files.empty())
            checkText(files[0].bytes,
                      bgmEps("0x40404040, 0x40404040, 0x40404040, 0x40404040, 0x40404040, 0x42404040",
                             {add("@@@E@@@@", "sound/.._x__y/st0.ogg"), add("@@@E@@@A", "sound/.._x__y/st1.ogg"), add("@@@E@@@B", "sound/.._x__y/st2.ogg")}),
                      "BGM (bad): BGMFlexible.eps");
    }
    {  // A WAV sound: encoded to Ogg Vorbis (media.h), then cut (1 second: one part).
        std::string pcm;
        for (int i = 0; i < 8000; ++i) pcm += char(i * 7), pcm += char((i * 13) % 64);
        auto u32 = [](uint32_t v) { return std::string{char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; };
        writeFile(dir / "music" / "good.wav", "RIFF" + u32(uint32_t(36 + pcm.size())) + "WAVEfmt " + u32(16) + std::string("\1\0\1\0", 4) + u32(8000) +
                                                   u32(16000) + std::string("\2\0\x10\0", 4) + "data" + u32(uint32_t(pcm.size())) + pcm);
        nrbf::Graph g;
        if (!graphOf(triggerEditor({}, {bgmFile("Good", u8(dir / "music" / "good.wav"), "")}), g)) return;
        std::vector<ImportFile> files;
        std::vector<std::string> problems;
        check(ee3BgmPlayer(g, g.root(), folder, files, problems) && files.size() == 2 && files[1].path == "sound/Good/st0.ogg" &&
                  files[1].bytes.compare(0, 4, "OggS") == 0,
              "BGM (WAV): encoded to Ogg Vorbis, one part");
        checkProblems(problems, {}, "BGM (WAV): no problems");
    }
    {  // The lists of the Assets workspace (ee3Media): the sounds' files as they are, the pictures cut to their size.
        const std::string pic = png(3, 2, {0xf70400, 0xffffff, 0x000000, 0x808080, 0x3c0000, 0xfa8280});
        nrbf::Graph g;
        if (!graphOf(triggerEditor({dotData("heart", 2, 1, pic), dotData("big", 40, 25, pic)},
                                   {bgmFile("Theme", u8(dir / "music" / "theme.ogg"), ""), bgmFile("Good.wav", u8(dir / "music" / "good.wav"), ""),
                                    bgmFile("Missing", u8(dir / "none.ogg"), "")}),
                     g))
            return;
        ProjectMedia media;
        std::vector<ImportFile> files;
        std::vector<std::string> problems;
        check(!ee3Media(g, g.root(), folder, media, files, problems), "media: not all came over");
        check(media.tracks.size() == 2 && media.tracks[0].name == "Theme" && media.tracks[0].file == "music/Theme.ogg" && media.tracks[0].ms > 0 &&
                  media.tracks[1].name == "Good.wav" && media.tracks[1].file == "music/Good.wav" && media.tracks[1].ms == 1000,
              "media: the music tracks (their names, files and lengths)");
        check(media.images.size() == 1 && media.images[0].name == "heart" && media.images[0].file == "dots/heart.png" && media.images[0].width == 2 &&
                  media.images[0].height == 1,
              "media: the dot image");
        check(files.size() == 3 && files[0].bytes.empty() && pathOf(files[0].from) == (dir / "music" / "theme.ogg").lexically_normal() && files[2].path == "dots/heart.png" && files[2].width == 2 && files[2].height == 1 &&
                  files[2].bytes == std::string("\xf7\x04\x00\xff\xff\xff\xff\xff", 8),
              "media: the files (a sound by its path, copied at import time; a picture cut to its size)");
        check(problems.size() == 2 && problems[0].find("Missing") != std::string::npos && problems[1].find("big") != std::string::npos,
              "media: a problem line for the missing sound and the picture that is too large");
    }
    {  // A damaged sound file.
        std::string bad = ogg;
        bad[200] = char(bad[200] ^ 0x55);
        writeFile(dir / "music" / "damaged.ogg", bad);
        nrbf::Graph g;
        if (!graphOf(triggerEditor({}, {bgmFile("Damaged", u8(dir / "music" / "damaged.ogg"), "")}), g)) return;
        std::vector<ImportFile> files;
        std::vector<std::string> problems;
        check(!ee3BgmPlayer(g, g.root(), folder, files, problems), "BGM (damaged): not made");
        check(problems.size() == 1 && problems[0].find("is damaged") != std::string::npos, "BGM (damaged): a problem line");
    }
}

// A real project: what its BGM player gives.
void showProject(const std::string& file) {
    std::ifstream in(pathOf(file), std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    nrbf::Graph g;
    std::string error;
    if (!g.read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), error)) {
        check(false, file + ": " + error);
        return;
    }
    const nrbf::Object* te = g.get(g.root(), "TEData");
    std::vector<ImportFile> files;
    std::vector<std::string> problems;
    const bool bgm = ee3BgmPlayer(g, te, u8(pathOf(file).parent_path()), files, problems);
    std::printf("%s: the BGM player %s, %zu files\n", file.c_str(), bgm ? "made" : "not all made", files.size());
    for (const std::string& p : problems) std::printf("  problem: %s\n", p.c_str());
    if (!files.empty()) std::printf("%s", files[0].bytes.c_str());
    problems.clear();
    const bool dots = ee3DotImages(g, te, files, problems);
    std::printf("the dot images %s, %zu files\n", dots ? "made" : "not all made", files.size());
    for (const std::string& p : problems) std::printf("  problem: %s\n", p.c_str());
}

}  // namespace

// The parts of one sound, written into a folder.
int cutSound(const std::string& sound, const std::string& out) {
    nrbf::Graph g;
    if (!graphOf(triggerEditor({}, {bgmFile("cut", sound, "")}), g)) return 1;
    std::vector<ImportFile> files;
    std::vector<std::string> problems;
    const bool ok = ee3BgmPlayer(g, g.root(), u8(fs::current_path()), files, problems);
    for (const std::string& p : problems) std::printf("problem: %s\n", p.c_str());
    for (const ImportFile& f : files) {
        const fs::path to = pathOf(out) / pathOf(f.path).filename();
        writeFile(to, f.bytes);
        std::printf("%s: %zu bytes\n", u8(to).c_str(), f.bytes.size());
    }
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--cut") return cutSound(argv[2], argv[3]);
    testDots();
    const fs::path dir = fs::temp_directory_path() /
                         ("stardraft-ee3-media-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    testBgm(dir);
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (argc > 1) showProject(argv[1]);
    std::printf("%s (%d failed)\n", failed ? "FAILED" : "PASSED", failed);
    return failed ? 1 : 0;
}
