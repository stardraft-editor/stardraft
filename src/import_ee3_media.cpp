#include "stardraft/import_ee3_extras.h"

// EUD Editor 3's dot images and BGM player (import_ee3_extras.h), as its build makes them:
//   - dotData.eps: BuildData.GetDotEps (Class/BulidData/WriteDotData.vb) with DotData.GetepsData and
//     DotPaint.GetColorIndex (Class/TriggerEditor/DotPainter). The project file holds each picture as the .NET Bitmap
//     that the user loaded: its "Data" member is the image file's bytes (PNG, BMP, GIF or JPEG). DotData's Pixels array
//     is never filled by EUD Editor 3 (all zeros), so the colors come from the picture, as GetepsData reads them.
//   - BGMFlexible.eps and the sound parts: BuildData.GetBGMEps (Class/BulidData/WriteBGMFile.vb) and
//     BGMPlayerConverter.BGMProcess (Window/BGMPlayerConverter.xaml.vb). EUD Editor 3 converts each sound with ffmpeg
//     to Ogg Vorbis and cuts it into parts of 2.28 seconds ("-f segment -segment_time 2.28 -c copy": the packets are
//     copied, not encoded again), st0.ogg, st1.ogg, ... Here an Ogg Vorbis sound is cut the same way, packet by packet
//     (media.h: cutOggVorbis); a WAV or MP3 sound is encoded to Ogg Vorbis first (encodeOggVorbis).

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

#include "stardraft/media.h"

namespace fs = std::filesystem;

namespace stardraft {

namespace {

const char* const kNl = "\r\n";  // (StringBuilder.AppendLine, and the line ends in EUD Editor 3's multi-line strings)

// --- Dot images -------------------------------------------------------------------------------------------------------

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
        int bw = 0, bh = 0;
        std::vector<uint8_t> rgba;
        if (!decodePicture(data, bw, bh, rgba)) {
            why = "its picture is not a PNG, BMP, GIF or JPEG image that the import can read";
            return false;
        }
        if (bw < w || bh < h) {
            why = "its picture (" + std::to_string(bw) + " x " + std::to_string(bh) + ") is smaller than its size";
            return false;
        }
        for (int64_t y = 0; y < h; ++y)
            for (int64_t x = 0; x < w; ++x) {
                const uint8_t* p = rgba.data() + (size_t(y) * size_t(bw) + size_t(x)) * 4;
                list += "," + std::to_string(dotColorIndex(p[0], p[1], p[2]));
            }
    }
    out = "EUDArray(list(" + list + "))";
    return true;
}

// --- BGM player -------------------------------------------------------------------------------------------------------

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
        OggParts cut;
        if (!b) {
            why = "the project file holds no data for it";
        } else if (soundFile(g.text(b, "BGMPath"), g.text(b, "BGMRelativePath"), projectFolder, bytes, ext, path, why)) {
            // An OGG file is cut as it is; a WAV or MP3 file is encoded to Ogg Vorbis first, as EUD Editor 3's ffmpeg does.
            std::string ogg;
            if (!trackOggVorbis(bytes, ext, ogg, why) || !cutOggVorbis(ogg, cut, why)) why = "its file " + path + ": " + why;
        }
        if (why.empty()) {
            std::vector<std::string> names;
            for (size_t s = 0; s < cut.parts.size(); ++s) {
                const std::string file = "st" + std::to_string(s) + ".ogg";
                sounds.push_back({"sound/" + folder + "/" + file, std::move(cut.parts[s])});
                names.push_back(file);
            }
            last = int(cut.parts.size()) - 1;
            std::sort(names.begin(), names.end());  // (the folder's files in the order Windows lists them)
            for (const std::string& f : names)
                adds.push_back("    MPQAddFile('" + ee3SoundIndex(int(i), false) + ee3SoundIndex(std::stoi(f.substr(2)), false) +
                               "', pathlib.Path('sound/" + folder + "/" + f + "').read_bytes());");
            if (cut.otherStreams) problems.push_back(label + ": its file " + path + " holds more than one stream; only the first is used");
            const int64_t rate = g.number(b, "BGMSampleRate");
            if (rate > 0 && rate != int64_t(cut.rate))
                problems.push_back(label + ": its sample rate stays " + std::to_string(cut.rate) + " Hz. EUD Editor 3 converts it to " +
                                   std::to_string(rate) + " Hz with ffmpeg; the import keeps the file's rate");
        }
        if (!why.empty()) {
            all = false;
            problems.push_back(label + ": " + why);
        }
        lens.push_back(ee3SoundIndex(last, true));
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

bool ee3Media(const nrbf::Graph& g, const nrbf::Object* te, const std::string& projectFolder, ProjectMedia& media, std::vector<ImportFile>& files,
              std::vector<std::string>& problems) {
    media = {};
    files.clear();
    bool all = true;
    std::set<std::string> usedNames;
    const std::vector<nrbf::Value> list = g.items(g.get(g.get(te, "BGMData"), "BGMList"));
    for (size_t i = 0; i < list.size(); ++i) {
        const nrbf::Object* b = g.get(list[i]);
        const std::string name = b ? g.text(b, "BGMName") : std::string();
        const std::string label = "the sound " + std::to_string(i) + (name.empty() ? "" : " (\"" + name + "\")");
        std::string bytes, ext, path, why;
        uint32_t ms = 0;
        if (!b) why = "the project file holds no data for it";
        else if (soundFile(g.text(b, "BGMPath"), g.text(b, "BGMRelativePath"), projectFolder, bytes, ext, path, why) && !soundLength(bytes, ext, ms, why))
            why = "its file " + path + ": " + why;
        if (!why.empty()) {
            all = false;
            problems.push_back(label + ": " + why);
            continue;
        }
        std::string stem = name;  // (a name with the file's extension: once in the file's name)
        if (lowerAscii(stem).ends_with(ext)) stem.resize(stem.size() - ext.size());
        const std::string file = "music/" + folderName(stem, i, usedNames) + ext;
        media.tracks.push_back({media.freeTrackName(name.empty() ? "Track " + std::to_string(i + 1) : name), file, ms});
        files.push_back({file, {}, 0, 0, path});  // (the bytes go: the import copies the file)
    }
    usedNames.clear();
    const std::vector<nrbf::Value> dots = g.items(g.get(te, "DotDatas"));
    for (size_t i = 0; i < dots.size(); ++i) {
        const nrbf::Object* d = g.get(dots[i]);
        const std::string name = d ? g.text(d, "ItemName") : std::string();
        const std::string label = "the dot image " + std::to_string(i) + (name.empty() ? "" : " (\"" + name + "\")");
        std::string why;
        const int64_t w = d ? g.number(d, "imgWidth") : 0, h = d ? g.number(d, "imgHeight") : 0;
        int bw = 0, bh = 0;
        std::vector<uint8_t> rgba;
        if (!d) why = "the project file holds no data for it";
        else if (w <= 0 || h <= 0 || w * h >= kDotMaxPixels) why = "its size (" + std::to_string(w) + " x " + std::to_string(h) + ") is not 1 to 999 pixels";
        else if (!decodePicture(byteMember(g, g.get(d, "PixelOrgData"), "Data"), bw, bh, rgba)) why = "the project file holds no picture for it that the import can read";
        else if (bw < w || bh < h) why = "its picture (" + std::to_string(bw) + " x " + std::to_string(bh) + ") is smaller than its size";
        if (!why.empty()) {
            all = false;
            problems.push_back(label + ": " + why);
            continue;
        }
        std::string px;  // (the picture cut to the image's size, as EUD Editor 3 reads it)
        for (int64_t y = 0; y < h; ++y) px.append(reinterpret_cast<const char*>(rgba.data()) + size_t(y) * size_t(bw) * 4, size_t(w) * 4);
        const std::string file = "dots/" + folderName(name.empty() ? "dot" + std::to_string(i + 1) : name, i, usedNames) + ".png";
        media.images.push_back({media.freeImageName(name.empty() ? "Dot image " + std::to_string(i + 1) : name), file, int(w), int(h)});
        files.push_back({file, std::move(px), int(w), int(h)});
    }
    return all;
}

}  // namespace stardraft
