// The trigger comment command problems (cmbw::crigProblems) of every map under a folder: to see that the check finds
// mistakes and not lines the game reads.
// usage: cmbw_crig_check <CMBW-maps folder> [--all]   (--all: each problem; else a count by kind and a few)
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

#include "stardraft/chk.h"
#include "stardraft/map_file.h"
#include "stardraft/mods/cmbw.h"

using namespace stardraft;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const bool all = argc > 2 && std::string(argv[2]) == "--all";
    int maps = 0, withProblems = 0;
    std::map<std::string, int> kinds;
    for (const auto& e : fs::recursive_directory_iterator(argv[1])) {
        const std::string ext = e.path().extension().string();
        if (ext != ".scx" && ext != ".scm") continue;
        Bytes bytes;
        std::string err;
        if (!readMapChk(e.path(), bytes, err)) continue;
        ++maps;
        const auto problems = cmbw::crigProblems(Chk::parse(bytes));
        withProblems += !problems.empty();
        for (const auto& p : problems) {
            const size_t k = p.rfind("\": ");
            std::string kind = k == std::string::npos ? p : p.substr(k + 3);
            if (kind.rfind("no command", 0) == 0) kind = "no command";
            else if (kind.rfind("the map has no location", 0) == 0) kind = "no location";
            else if (kind.find(" is no ") != std::string::npos) kind = "not a word of " + kind.substr(kind.find(" is no ") + 7);
            else if (kind.find("missing") != std::string::npos) kind = "values missing";
            if (all || ++kinds[kind] <= 3) std::printf("%s: %s%c", e.path().filename().string().c_str(), p.c_str(), 10);
            if (all) ++kinds[kind];
        }
    }
    std::printf("%d maps, %d with problems%c", maps, withProblems, 10);
    for (const auto& [k, n] : kinds) std::printf("  %5d %s%c", n, k.c_str(), 10);
}
