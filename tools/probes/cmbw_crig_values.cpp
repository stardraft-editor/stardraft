// The value problems (cmbw::crigValueProblem) in the trigger comment commands of every map under a folder: what the
// marks of the Classic triggers page would show on the mod's own maps (they should be few: the maps work).
// usage: cmbw_crig_values <CMBW-maps folder> [--all]
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

#include "stardraft/chk.h"
#include "stardraft/classic_triggers.h"
#include "stardraft/map_file.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/strings.h"

using namespace stardraft;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const bool all = argc > 2 && std::string(argv[2]) == "--all";
    int maps = 0, lines = 0, values = 0;
    std::map<std::string, int> kinds;
    for (const auto& e : fs::recursive_directory_iterator(argv[1])) {
        const std::string ext = e.path().extension().string();
        if (ext != ".scx" && ext != ".scm") continue;
        Bytes bytes;
        std::string err;
        if (!readMapChk(e.path(), bytes, err)) continue;
        ++maps;
        const Chk chk = Chk::parse(bytes);
        const cmbw::CrigMapInfo info = cmbw::crigMapInfo(chk, 1000);
        const ClassicTriggers t = readClassicTriggers(chk);
        const StringTable strings = StringTable::read(chk);
        for (size_t ti = 0; ti < t.triggers.size(); ++ti)
            for (size_t ai = 0; ai < t.triggers[ti].actions.size(); ++ai) {
                const ClassicAction& a = t.triggers[ti].actions[ai];
                if (a.type != 47 || !a.text || (a.flags & 2) || a.text > strings.size()) continue;
                const std::string& text = strings.get(a.text);
                size_t start = 0;
                while (start <= text.size()) {
                    size_t end = start;
                    while (end < text.size() && text[end] != char(10) && text[end] != char(13)) ++end;
                    const std::string line = text.substr(start, end - start);
                    start = end + 1;
                    const cmbw::CrigLine l = cmbw::parseCrigLine(line);
                    if (!l.command || cmbw::crigLooksLikeNote(line)) continue;
                    ++lines;
                    for (size_t k = 0; k < l.command->args.size(); ++k) {
                        ++values;
                        const std::string p = cmbw::crigValueProblem(l, k, info);
                        if (p.empty()) continue;
                        const std::string kind = l.command->names.front() + "." + l.command->args[k].name + " (" + l.command->args[k].kind + ")";
                        if (all || ++kinds[kind] <= 2) std::printf("%s (trigger %zu, action %zu): \"%s\": %s%c", e.path().filename().string().c_str(), ti, ai, line.c_str(), p.c_str(), 10);
                        if (all) ++kinds[kind];
                    }
                }
            }
    }
    std::printf("%d maps, %d command lines, %d values%c", maps, lines, values, 10);
    for (const auto& [k, n] : kinds) std::printf("  %5d %s%c", n, k.c_str(), 10);
}
