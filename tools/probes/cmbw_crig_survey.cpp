// CRIGs as condition and action rows over a folder of Cosmonarchy maps: for each map, its triggers collapsed into rows
// and expanded again (cmbw::crigCollapse, crigExpand) must give the same TRIG bytes and strings; counts the Comments that
// became rows, those that stay Comments (comment mode), and the lines that are not commands (the most common first words).
// usage: cmbw_crig_survey <maps folder>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/classic_triggers.h"
#include "stardraft/map_file.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/strings.h"

namespace fs = std::filesystem;
using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: cmbw_crig_survey <maps folder>\n"), 2;
    int maps = 0, broken = 0, comments = 0, rows = 0, rowConds = 0, triggersCollapsed = 0, triggersKept = 0;
    std::map<std::string, int> unknown;  // a line's first word that is not a command
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(argv[1], ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        if (p.extension() != ".scx" && p.extension() != ".scm") continue;
        Bytes raw;
        std::string err;
        if (!readMapFile(p, raw, err)) continue;
        const Chk chk = Chk::parse(raw);
        const ClassicTriggers t = readClassicTriggers(chk);
        const StringTable strings = StringTable::read(chk);
        ++maps;
        const ClassicTriggers c = cmbw::crigCollapse(t, strings);
        StringTable after = strings;
        ClassicTriggers reread;
        const ClassicTriggers e = cmbw::crigExpand(c, after, nullptr, &reread);
        if (!(reread.triggers == c.triggers)) {
            ++broken;
            std::printf("the rows read again after writing are not the rows read: %s\n", p.filename().string().c_str());
        }
        if (encodeClassicTriggers(e) != encodeClassicTriggers(t) || after.size() != strings.size()) {
            ++broken;
            std::printf("not the same after the round trip: %s\n", p.filename().string().c_str());
        }
        for (size_t i = 0; i < t.triggers.size(); ++i) {
            bool has = false, became = false;
            for (int k = 0; k < t.triggers[i].actionCount(); ++k) {
                const ClassicAction& a = t.triggers[i].actions[size_t(k)];
                if (a.type != 47 || !a.text) continue;
                ++comments;
                std::string text = strings.get(a.text), cur;
                std::vector<std::string> lines;
                for (char ch : text)
                    if (ch == '\n') lines.push_back(cur), cur.clear();
                    else if (ch != '\r') cur += ch;
                lines.push_back(cur);
                for (const std::string& l : lines) {
                    const auto pl = cmbw::parseCrigLine(l);
                    if (pl.command) has = true;
                    else if (!l.empty()) ++unknown[cmbw::crigWords(l).empty() ? l : cmbw::crigWords(l).front()];
                }
            }
            for (int k = 0; k < c.triggers[i].actionCount(); ++k) became |= c.triggers[i].actions[size_t(k)].type == kClassicCrigType, rows += c.triggers[i].actions[size_t(k)].type == kClassicCrigType;
            for (int k = 0; k < c.triggers[i].conditionCount(); ++k) {
                const bool r = c.triggers[i].conditions[size_t(k)].type == kClassicCrigType;
                became |= r, rows += r, rowConds += r;
            }
            if (became) ++triggersCollapsed;
            else if (has && ++triggersKept <= 6) {
                std::printf("kept (%s, trigger %zu):", p.filename().string().c_str(), i);
                for (int k = 0; k < t.triggers[i].actionCount(); ++k) {
                    const ClassicAction& a = t.triggers[i].actions[size_t(k)];
                    if (a.type == 47 && a.text) {
                        std::string x = strings.get(a.text);
                        for (char& ch : x)
                            if (ch == '\r') ch = '|';
                            else if (ch == '\n') ch = '/';
                        std::printf(" [flags %u] \"%s\"", unsigned(a.flags), x.substr(0, 160).c_str());
                    }
                }
                std::printf("\n");
            }
        }
    }
    std::printf("%d maps (%d not the same after the round trip); %d comments; %d triggers with command rows (%d rows, %d of them "
                "conditions); %d triggers with commands kept as comments (a line that is not a command)\n",
                maps, broken, comments, triggersCollapsed, rows, rowConds, triggersKept);
    std::vector<std::pair<int, std::string>> top;
    for (const auto& [w, n] : unknown) top.push_back({n, w});
    std::sort(top.rbegin(), top.rend());
    std::printf("lines that are not commands, by their first word:");
    for (size_t i = 0; i < top.size() && i < 25; ++i) std::printf(" %s (%d)", top[i].second.substr(0, 24).c_str(), top[i].first);
    std::printf("\n");
    return broken ? 1 : 0;
}
