// The game's string tables and tilesets on an install: every table loads with no bad entries, and every tileset file
// holds whole records only (the checks in parseTbl and Tileset::load). Prints one line per check.
//
// Fails (exit 1) on the first check that does not pass.
//
// usage: tbl_probe <install dir>

#include <cstdio>

#include "../../src/tbl.h"
#include "stardraft/dat_edit.h"
#include "stardraft/game_data.h"
#include "stardraft/sprites.h"
#include "stardraft/tileset.h"
#include "stardraft/units.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        failures += !ok;
    };
    for (const char* name : {"rez\\stat_txt.tbl", "arr\\images.tbl", "arr\\sfxdata.tbl"}) {
        Bytes tbl;
        if (!data.readFile(name, tbl, err)) {
            check(false, std::string(name) + ": " + err);
            continue;
        }
        TblStrings t = parseTbl(tbl);
        check(t.bad == 0, std::string(name) + ": " + std::to_string(t.strings.size()) + " strings, " +
                              std::to_string(t.bad) + " bad");
    }
    for (const auto& sc : DatSchema::all()) {
        DatTable t;
        err.clear();
        const bool ok = t.load(data, sc, err);
        check(ok, sc.name + ".dat" + (sc.name == "sfxdata" ? " + sfxdata.tbl (" + std::to_string(t.fileNames().size()) + " names)" : "") +
                      (ok ? "" : ": " + err));
    }
    UnitTypes types;
    err.clear();
    const bool typesOk = types.load(data, err);
    check(typesOk, "UnitTypes " + err);
    UnitSprites sprites;
    err.clear();
    const bool spritesOk = sprites.load(data, err);
    check(spritesOk, "UnitSprites " + err);
    for (uint16_t era = 0; era < 8; ++era) {
        Tileset ts;
        err.clear();
        const bool ok = ts.load(data, era, err);
        check(ok, std::string("tileset ") + tilesetName(era) + (ok ? "" : ": " + err));
    }
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
