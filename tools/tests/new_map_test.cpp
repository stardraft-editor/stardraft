// Create a default map, save it as .scx, reopen it and check its sections against the CHK spec.
//
// usage: new_map_test <install dir> <out.scx>

#include <cstdio>
#include <map>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/map_file.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: new_map_test <install dir> <out.scx>\n");
        return 2;
    }
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    Editor ed(data);
    if (!ed.newMap(128, 128, 0, 1, err) || !ed.save(argv[2], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    Bytes raw;
    if (!readMapChk(argv[2], raw, err)) {
        std::fprintf(stderr, "reread: %s\n", err.c_str());
        return 1;
    }
    Chk chk = Chk::parse(raw);
    // Expected sizes for a 128x128 Brood War map (0 = any).
    const std::map<std::string, size_t> expect = {
        {"TYPE", 4},    {"VER ", 2},    {"IVE2", 2},    {"VCOD", 1040}, {"IOWN", 12},   {"OWNR", 12},
        {"ERA ", 2},    {"DIM ", 4},    {"SIDE", 12},   {"MTXM", 32768}, {"PUNI", 5700}, {"UNIT", 0},
        {"ISOM", 67080}, {"TILE", 32768}, {"DD2 ", 0},  {"THG2", 0},    {"MASK", 16384}, {"STR ", 0},
        {"UPRP", 1280}, {"UPUS", 64},   {"MRGN", 5100}, {"TRIG", 0},    {"MBRF", 0},    {"SPRP", 4},
        {"FORC", 20},   {"WAV ", 2048}, {"SWNM", 1024}, {"COLR", 8},    {"PUPx", 2318}, {"PTEx", 1672},
        {"UNIx", 4168}, {"UPGx", 794},  {"TECx", 396},
    };
    int failures = 0;
    for (const auto& [name, size] : expect) {
        SectionName n{{name[0], name[1], name[2], name[3]}};
        const Section* s = chk.find(n);
        bool ok = s && (size == 0 || s->data.size() == size);
        if (!ok) ++failures;
        std::printf("%s %s %zu\n", ok ? "ok  " : "FAIL", name.c_str(), s ? s->data.size() : 0);
    }
    Editor re(data);
    bool reopened = re.open(argv[2], err);
    std::printf("%s reopen in editor (%ux%u, %s, Anywhere = \"%s\")\n", reopened ? "ok  " : "FAIL", re.width(), re.height(),
                tilesetName(re.era()), reopened ? re.locationName(kAnywhereLocation).c_str() : err.c_str());
    failures += !reopened;
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
