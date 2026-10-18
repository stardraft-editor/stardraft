#include "stardraft/campaign.h"

#include <fstream>
#include <sstream>

#include "json.h"

namespace fs = std::filesystem;

namespace stardraft {

namespace {
std::string readAll(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}
}  // namespace

bool Campaign::create(const fs::path& dir, const std::string& campaignName, std::string& error) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (fs::exists(dir / kFileName, ec)) return error = (dir / kFileName).string() + " is there already", false;
    folder = dir, name = campaignName, maps.clear();
    return save(error);
}

bool Campaign::load(const fs::path& folderOrFile, std::string& error) {
    std::error_code ec;
    const fs::path file = fs::is_directory(folderOrFile, ec) ? folderOrFile / kFileName : folderOrFile;
    if (!fs::exists(file, ec)) return error = file.string() + " does not exist", false;
    const std::string text = readAll(file);
    JsonParser p{text};
    const Json j = p.value();
    if (!p.ok || j.type != Json::Type::Object) return error = file.string() + " is not a campaign", false;
    if (j.integer("version", 1) > 1) return error = file.string() + " is from a newer StarDraft", false;
    folder = file.parent_path(), name = j.str("name"), maps.clear();
    if (const Json* m = j.get("maps"); m && m->type == Json::Type::Array)
        for (const Json& item : m->items)
            if (item.type == Json::Type::String && !item.s.empty()) maps.push_back(fs::u8path(item.s));
    return true;
}

bool Campaign::save(std::string& error) const {
    std::string text = "{\n  \"version\": 1,\n  \"name\": " + quote(name) + ",\n  \"maps\": [";
    for (size_t i = 0; i < maps.size(); ++i) {
        const auto u8 = maps[i].generic_u8string();  // (the path in UTF-8)
        text += std::string(i ? ",\n" : "\n") + "    " + quote(std::string(u8.begin(), u8.end()));
    }
    text += maps.empty() ? "]\n}\n" : "\n  ]\n}\n";
    std::ofstream out(folder / kFileName, std::ios::binary);
    out << text;
    out.close();
    if (!out) return error = "cannot write " + (folder / kFileName).string(), false;
    return true;
}

fs::path Campaign::mapFile(size_t i) const {
    if (i >= maps.size()) return {};
    return maps[i].is_absolute() ? maps[i] : folder / maps[i];
}

bool Campaign::add(const fs::path& file) {
    std::error_code ec;
    fs::path rel = fs::relative(file, folder, ec);
    const bool inside = !ec && !rel.empty() && rel.native()[0] != '.';
    const fs::path keep = inside ? rel : file;
    const fs::path wanted = fs::weakly_canonical(file, ec);
    for (size_t i = 0; i < maps.size(); ++i) {
        std::error_code e2;
        if (fs::weakly_canonical(mapFile(i), e2) == wanted || fs::equivalent(mapFile(i), file, e2)) return false;  // (a file not made yet: its path)
    }
    maps.push_back(keep);
    return true;
}

}  // namespace stardraft
