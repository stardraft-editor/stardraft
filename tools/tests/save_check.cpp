// Open each map, save it (as StarDraft's File > Save does), open the copy again, and compare the map data: a map must
// come back as it was saved (no section lost or changed), and the copy must open. Lost work and maps that are not valid
// after a save are the worst defects (docs/project/FEEDBACK.md, order 1).
//
// usage: save_check [--edit] <install dir> <scratch dir> <map or folder>...
// --edit: before the save, a terrain stroke and a unit (as a user would), so the saved data is StarDraft's own.

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/map_file.h"

using namespace stardraft;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    bool edit = false;
    if (argc > 1 && std::string(argv[1]) == "--edit") edit = true, --argc, ++argv;
    if (argc < 4) {
        std::fprintf(stderr, "usage: save_check <install dir> <scratch dir> <map or folder>...\n");
        return 2;
    }
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const fs::path scratch = argv[2];
    std::error_code ec;
    fs::create_directories(scratch, ec);
    std::vector<fs::path> maps;
    for (int a = 3; a < argc; ++a) {
        const fs::path p = argv[a];
        if (fs::is_directory(p, ec)) {
            for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator();
                 it.increment(ec)) {
                if (ec) break;
                std::string ext = it->path().extension().string();
                for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
                if (it->is_regular_file(ec) && (ext == ".scm" || ext == ".scx")) maps.push_back(it->path());
            }
        } else {
            maps.push_back(p);
        }
    }
    int opened = 0, notOpened = 0, saveFailed = 0, reopenFailed = 0, differ = 0;
    std::map<std::string, int> sectionDiffs;
    for (const fs::path& m : maps) {
        Editor a(data);
        if (!a.open(m, err)) {
            ++notOpened;  // not a map StarDraft opens (a damaged or protected file): not this check's subject
            continue;
        }
        ++opened;
        if (edit) {  // a stroke of the second terrain across the middle, and a marine of player 1
            if (a.canPaintTerrain()) {
                const int cx = a.width() * 16, cy = a.height() * 16;
                a.beginTerrainStroke();
                for (int i = 0; i < 20; ++i) a.paintBrush(cx - 200 + i * 20, cy - 100 + i * 10, 2, Brush{Brush::Diamond, 2.0f});
                a.endTerrainStroke();
            }
            a.placeUnit(0, 0, a.width() * 16 + 64, a.height() * 16 + 64);
        }
        const fs::path out = scratch / ("copy" + m.extension().string());
        if (!a.save(out, err)) {
            ++saveFailed;
            std::printf("SAVE   %s: %s\n", m.filename().string().c_str(), err.c_str());
            continue;
        }
        Editor b(data);
        if (!b.open(out, err)) {
            ++reopenFailed;
            std::printf("REOPEN %s: %s\n", m.filename().string().c_str(), err.c_str());
            continue;
        }
        // What was saved: each section of the first editor's map against the copy's.
        const Chk& ca = a.chk();
        const Chk& cb = b.chk();
        std::string diff;
        for (const Section& s : ca.sections) {
            const std::string name = s.name.str();
            if (ca.effective(s.name) != cb.effective(s.name)) {
                if (diff.find(name) == std::string::npos) diff += (diff.empty() ? "" : ", ") + name;
            }
        }
        for (const Section& s : cb.sections)
            if (!ca.find(s.name)) diff += (diff.empty() ? "" : ", ") + std::string("+") + s.name.str();
        if (!diff.empty()) {
            ++differ;
            for (size_t i = 0, j; i < diff.size(); i = j + 2) {
                j = diff.find(", ", i);
                if (j == std::string::npos) j = diff.size();
                ++sectionDiffs[diff.substr(i, j - i)];
            }
            std::printf("DIFF   %s: %s\n", m.filename().string().c_str(), diff.c_str());
        }
    }
    fs::remove_all(scratch, ec);
    std::printf("%zu files: %d opened (%d not), %d saves failed, %d copies did not open, %d came back different\n", maps.size(), opened, notOpened,
                saveFailed, reopenFailed, differ);
    for (const auto& [name, n] : sectionDiffs) std::printf("  %s differs in %d\n", name.c_str(), n);
    return saveFailed || reopenFailed || differ ? 1 : 0;
}
