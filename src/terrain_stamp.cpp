#include "stardraft/terrain_stamp.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

#include "file_write.h"
#include "json.h"
#include "stardraft/tileset.h"

namespace stardraft {

namespace fs = std::filesystem;

int TerrainStamp::count() const { return int(std::count(mask.begin(), mask.end(), uint8_t(1))); }

namespace {

// A path as UTF-8 text (path::string() can throw on Windows for a letter outside the system's code page).
std::string pathText(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// The code points of UTF-8 text; false when it is not UTF-8.
bool codePoints(const std::string& s, std::vector<char32_t>& out) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + size_t(n) >= s.size() + (n ? 0 : 1)) return false;
        char32_t cp = n == 0 ? c : n == 1 ? (c & 0x1F) : n == 2 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k <= n; ++k) {
            const unsigned char d = static_cast<unsigned char>(s[i + size_t(k)]);
            if ((d >> 6) != 2) return false;
            cp = cp << 6 | (d & 0x3F);
        }
        out.push_back(cp);
        i += size_t(n) + 1;
    }
    return true;
}

// A letter outside ASCII: not a space, a mark of punctuation or a symbol of the common blocks (a name stays a name).
bool otherLetter(char32_t c) {
    if (c < 0xC0 || c == 0xD7 || c == 0xF7) return false;                 // Latin-1's signs
    if (c >= 0x2000 && c <= 0x2BFF) return false;                          // punctuation, arrows, symbols, boxes
    if (c >= 0x3000 && c <= 0x303F) return false;                          // CJK punctuation and the wide space
    if (c >= 0xD800 && c <= 0xDFFF) return false;                          // (not a character)
    if (c >= 0xE000 && c <= 0xF8FF) return false;                          // private use
    if (c >= 0xFE30 && c <= 0xFE4F) return false;                          // CJK forms
    if ((c >= 0xFF00 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) || (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65))
        return false;                                                      // full-width signs
    if (c >= 0xFFF0 && c <= 0xFFFF) return false;                          // specials
    if (c >= 0x1F000) return false;                                        // emoji and other pictures
    return true;
}

}  // namespace

bool terrainStampNameOk(const std::string& name, std::string& why) {
    std::vector<char32_t> cps;
    if (!codePoints(name, cps)) return why = "The name is not valid text.", false;
    if (cps.empty()) return why = "Type a name.", false;
    if (cps.size() > kTerrainStampNameMax) return why = "The name is too long (" + std::to_string(kTerrainStampNameMax) + " characters at most).", false;
    if (cps.front() == ' ' || cps.back() == ' ') return why = "The name cannot start or end with a space.", false;
    for (char32_t c : cps) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-' || otherLetter(c);
        if (!ok) return why = "Use only letters, digits, spaces, \"_\" and \"-\" in the name.", false;
    }
    // (a letter outside ASCII is 6 to 12 characters of the file's name: the name's file must fit in a file system's
    // names and in Windows' paths)
    if (terrainStampFileName(name).size() > kTerrainStampFileNameMax) return why = "The name is too long. Use fewer letters.", false;
    return true;
}

std::string terrainStampFileName(const std::string& name) {
    static const char* const kHex = "0123456789ABCDEF";
    std::string out;
    for (const char ch : name) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') out += char(c);
        else if (c >= 'A' && c <= 'Z') out += char(c - 'A' + 'a');  // (one file for names that differ in case only)
        else out += '%', out += kHex[c >> 4], out += kHex[c & 15];
    }
    // Windows' device names (with any extension): CON, PRN, AUX, NUL, COM0 to COM9, LPT0 to LPT9.
    const std::string base = out.substr(0, out.find('%'));
    const bool device = base == out && (out == "con" || out == "prn" || out == "aux" || out == "nul" ||
                                        (out.size() == 4 && (out.compare(0, 3, "com") == 0 || out.compare(0, 3, "lpt") == 0) && out[3] >= '0' && out[3] <= '9'));
    if (device) out = std::string("%") + kHex[(out[0] & 0xDF) >> 4] + kHex[(out[0] & 0xDF) & 15] + out.substr(1);
    return out;
}

bool terrainStampSameName(const std::string& a, const std::string& b) { return terrainStampFileName(a) == terrainStampFileName(b); }

fs::path terrainStampPath(const fs::path& folder, uint16_t era, const std::string& name) {
    return folder / tilesetName(era) / fs::path(terrainStampFileName(name) + ".sdstamp");
}

std::string terrainStampText(const TerrainStamp& s) {
    std::string tiles, mask;
    const size_t n = size_t(s.width) * size_t(s.height);
    for (size_t i = 0; i < n; ++i) {
        const bool in = i < s.mask.size() && s.mask[i];
        tiles += (i ? ", " : "") + std::to_string(in && i < s.tiles.size() ? s.tiles[i] : 0);
        mask += in ? '1' : '0';
    }
    return "{\"format\": \"StarDraft terrain stamp\", \"version\": " + std::to_string(kTerrainStampVersion) + ", \"name\": " + quote(s.name) +
           ", \"tileset\": " + quote(tilesetName(s.era)) + ", \"era\": " + std::to_string(s.era & 7) + ", \"created\": " + std::to_string(s.created) +
           ", \"width\": " + std::to_string(s.width) + ", \"height\": " + std::to_string(s.height) + ",\n \"tiles\": [" + tiles + "],\n \"mask\": \"" + mask +
           "\"}\n";
}

bool readTerrainStamp(const fs::path& file, TerrainStamp& out, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return error = "cannot read " + pathText(file), false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    JsonParser p{text};
    const Json j = p.value();
    if (!p.ok || j.type != Json::Type::Object || j.str("format") != "StarDraft terrain stamp") return error = pathText(file) + " is not a terrain stamp", false;
    TerrainStamp s;
    s.name = j.str("name");
    s.era = uint16_t(std::clamp(j.integer("era", -1), -1, 7));
    s.width = j.integer("width", 0), s.height = j.integer("height", 0);
    const Json* created = j.get("created");
    s.created = created && created->type == Json::Type::Number ? int64_t(created->n) : 0;
    const Json* tiles = j.get("tiles");
    const std::string mask = j.str("mask");
    std::string why;
    if (j.integer("era", -1) < 0 || !terrainStampNameOk(s.name, why) || s.width < 1 || s.height < 1 || s.width > kTerrainStampSizeMax ||
        s.height > kTerrainStampSizeMax || !tiles || tiles->type != Json::Type::Array || tiles->items.size() != size_t(s.width) * size_t(s.height) ||
        mask.size() != tiles->items.size())
        return error = pathText(file) + ": the stamp's fields are not complete", false;
    for (size_t i = 0; i < mask.size(); ++i) {
        const Json& t = tiles->items[i];
        s.tiles.push_back(t.type == Json::Type::Number && t.n >= 0 && t.n <= 0xFFFF ? uint16_t(t.n) : 0);
        s.mask.push_back(mask[i] == '1' ? 1 : 0);
    }
    s.file = file;
    out = std::move(s);
    return true;
}

bool writeTerrainStamp(const fs::path& folder, TerrainStamp& s, std::string& error) {
    std::string why;
    if (!terrainStampNameOk(s.name, why)) return error = why, false;
    const fs::path file = terrainStampPath(folder, s.era, s.name);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec) return error = "cannot make the folder " + pathText(file.parent_path()) + " (" + ec.message() + ")", false;
    try {  // (writeFile's error text: path::string(), which can throw on Windows)
        if (!writeFile(file, terrainStampText(s), error)) return false;
    } catch (const std::exception&) {
        return error = "cannot write " + pathText(file), false;
    }
    s.file = file;
    return true;
}

std::vector<TerrainStamp> terrainStamps(const fs::path& folder, uint16_t era) {
    std::vector<TerrainStamp> out;
    std::error_code ec;
    const fs::path dir = folder / tilesetName(era);
    if (folder.empty() || !fs::is_directory(dir, ec)) return out;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".sdstamp" || !it->is_regular_file(ec)) continue;
        TerrainStamp s;
        std::string err;
        if (readTerrainStamp(it->path(), s, err) && s.era == (era & 7)) out.push_back(std::move(s));  // (one that cannot be read: left out)
    }
    std::sort(out.begin(), out.end(), [](const TerrainStamp& a, const TerrainStamp& b) { return a.name < b.name; });
    return out;
}

std::vector<std::string> terrainStampNameParts(const std::string& name) {
    std::vector<std::string> parts;
    std::stringstream in(name);
    for (std::string part; std::getline(in, part, '_');)
        if (!part.empty()) parts.push_back(part);
    if (parts.empty()) parts.push_back(name);
    return parts;
}

}  // namespace stardraft
