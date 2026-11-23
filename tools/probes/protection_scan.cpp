// Which maps in folders are protected (map_protection.h: Chkdraft's rules), and why: a check that the rules do not
// call ordinary maps protected.
//
// usage: protection_scan <folder or map>...   (prints each protected map and the counts)

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "stardraft/map_file.h"
#include "stardraft/map_protection.h"

using namespace stardraft;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: protection_scan <folder or map>...\n"), 2;
    std::vector<fs::path> maps;
    for (int i = 1; i < argc; ++i) {
        std::error_code ec;
        if (fs::is_directory(argv[i], ec)) {
            for (auto it = fs::recursive_directory_iterator(argv[i], fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec)) {
                std::string x = it->path().extension().string();
                for (char& c : x) c = char(tolower(c));
                if (it->is_regular_file(ec) && (x == ".scm" || x == ".scx")) maps.push_back(it->path());
            }
        } else maps.push_back(argv[i]);
    }
    int unreadable = 0, prot = 0;
    std::map<std::string, int> why;
    for (const fs::path& p : maps) {
        Bytes raw;
        std::string err;
        bool other = false;
        if (!readMapChk(p, raw, err, &other)) {
            ++unreadable;
            std::printf("UNREADABLE %s: %s\n", p.u8string().c_str(), err.c_str());
            continue;
        }
        ParseReport r;
        const Chk chk = Chk::parse(raw, &r);
        const std::string w = mapProtection(chk, r, other);
        if (w.empty()) continue;
        ++prot, ++why[w.substr(0, w.find(" ("))];
        std::printf("PROTECTED %s: %s\n", p.u8string().c_str(), w.c_str());
    }
    std::printf("%zu maps: %d protected, %d unreadable\n", maps.size(), prot, unreadable);
    for (const auto& [w, n] : why) std::printf("  %5d  %s\n", n, w.c_str());
    return 0;
}
