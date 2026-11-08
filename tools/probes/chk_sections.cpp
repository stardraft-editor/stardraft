// The sections of a map's scenario.chk in file order: name, size and a hash; with two maps, the sections that differ.
// For reports of maps that do not open in the game: compare a broken map with a copy another editor repaired.
//
// usage: chk_sections <map> [<other map>] [--dump NAME]   (--dump: the section's bytes in hex, from each map)

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/map_file.h"

using namespace stardraft;

static uint32_t fnv(const Bytes& b) {
    uint32_t h = 2166136261u;
    for (uint8_t c : b) h = (h ^ c) * 16777619u;
    return h;
}

int main(int argc, char** argv) {
    std::vector<std::string> maps;
    std::string dump;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dump" && i + 1 < argc) dump = argv[++i];
        else maps.push_back(a);
    }
    if (maps.empty()) return std::fprintf(stderr, "usage: chk_sections <map> [<other map>] [--dump NAME]\n"), 2;
    std::vector<Chk> chks;
    for (const std::string& m : maps) {
        Bytes raw;
        std::string err;
        if (!readMapChk(m, raw, err)) return std::fprintf(stderr, "%s: %s\n", m.c_str(), err.c_str()), 1;
        ParseReport rep;
        chks.push_back(Chk::parse(raw, &rep));
        std::printf("== %s: %zu bytes, %zu sections%s%s%s\n", m.c_str(), raw.size(), chks.back().sections.size(),
                    rep.truncatedSection ? ", truncated section" : "", rep.trailingBytes ? ", trailing bytes" : "",
                    rep.negativeSize ? ", negative size" : "");
        for (const Section& s : chks.back().sections)
            std::printf("  %s %8zu %08x\n", s.name.str().c_str(), s.data.size(), fnv(s.data));
    }
    if (chks.size() == 2) {
        std::map<std::string, int> names;
        for (const Chk& c : chks)
            for (const Section& s : c.sections) names[s.name.str()] = 1;
        std::printf("== differences (effective data)\n");
        for (const auto& [n, _] : names) {
            SectionName sn;
            for (int k = 0; k < 4; ++k) sn.c[size_t(k)] = n[size_t(k)];
            Bytes a = chks[0].effective(sn), b = chks[1].effective(sn);
            if (a == b) continue;
            size_t first = 0;
            while (first < a.size() && first < b.size() && a[first] == b[first]) ++first;
            std::printf("  %s: %zu vs %zu bytes, first difference at %zu\n", n.c_str(), a.size(), b.size(), first);
        }
    }
    if (!dump.empty()) {
        SectionName sn;
        for (int k = 0; k < 4 && k < int(dump.size()); ++k) sn.c[size_t(k)] = dump[size_t(k)];
        for (int k = int(dump.size()); k < 4; ++k) sn.c[size_t(k)] = ' ';
        for (size_t i = 0; i < chks.size(); ++i) {
            Bytes d = chks[i].effective(sn);
            std::printf("== %s of %s (%zu bytes)\n", dump.c_str(), maps[i].c_str(), d.size());
            for (size_t o = 0; o < d.size(); o += 32) {
                std::printf("  %6zu ", o);
                for (size_t k = o; k < o + 32 && k < d.size(); ++k) std::printf("%02x", d[k]);
                std::printf("\n");
            }
        }
    }
    return 0;
}
