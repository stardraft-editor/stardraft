// .dat editing end to end: the generated field layout against the game's files, edits to memory
// writes, saving and loading, and a project compiled with the edits by euddraft.
//
// usage: dat_test <install dir> <a Brood War map (.scx)> <scratch dir>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "stardraft/dat_edit.h"
#include "stardraft/dat_extras.h"
#include "stardraft/eud_project.h"
#include "stardraft/game_data.h"
#include "stardraft/units.h"
#include "stardraft/wireframes.h"

using namespace stardraft;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    if (argc < 4) return std::fprintf(stderr, "usage: dat_test <install dir> <map.scx> <scratch dir>\n"), 2;
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;

    // Every .dat the editor knows loads at the size its layout adds up to.
    for (const auto& s : DatSchema::all()) {
        DatTable t;
        check(t.load(data, s, err), s.name + ".dat loads (" + std::to_string(s.fileSize) + " bytes, " + std::to_string(s.fields.size()) +
                                        " fields)" + (err.empty() ? "" : ": " + err));
        err.clear();
    }

    // units.dat read through the layout agrees with the map editor's own units.dat reader.
    const DatSchema& us = *DatSchema::find("units");
    DatTable units;
    units.load(data, us, err);
    UnitTypes types;
    types.load(data, err);
    auto field = [&](const char* name) -> const DatField& {
        for (const auto& f : us.fields)
            if (f.name == name) return f;
        std::fprintf(stderr, "no field %s\n", name);
        std::exit(1);
    };
    const DatField &hp = field("Hit Points"), &flags = field("Special Ability Flags"), &pw = field("StarEdit Placement Box Width"),
                   &ph = field("StarEdit Placement Box Height"), &sl = field("Unit Size Left"), &sd = field("Unit Size Down"),
                   &size = field("Unit Size");
    int mismatches = 0;
    for (int u = 0; u < 228; ++u) {
        const UnitType& t = types[uint16_t(u)];
        mismatches += units.value(hp, u) != t.hitPoints || units.value(flags, u) != t.flags || units.value(pw, u) != t.placementWidth ||
                      units.value(ph, u) != t.placementHeight || units.value(sl, u) != t.sizeLeft || units.value(sd, u) != t.sizeDown;
    }
    check(mismatches == 0, "all 228 units: hit points, flags, placement box and collision box match (" + std::to_string(mismatches) + " differ)");
    check(hp.memory == 0x662350 && pw.memory + 2 == ph.memory && pw.stride() == 4, "memory layout: hit points at 0x662350, placement box interleaved");

    // Edits: Marine hit points 40 -> 60, Marine unit size Small -> Large; one changed byte inside a dword.
    check(units.value(hp, 0) == 40 * 256 && units.value(size, 0) == 1, "marine: 40 hit points, Small");
    units.set(hp, 0, 60 * 256);
    units.set(size, 0, 3);
    check(units.changedCount() == 2 && units.entryChanged(0) && !units.entryChanged(1), "two values changed, on the marine only");
    const std::string script = units.editsScript();
    std::printf("%s", script.c_str());
    check(script.find("SetMemory(0x662350, SetTo, 0x00003C00);") != std::string::npos, "hit points: a full dword write");
    const uint32_t sizeAddr = size.memory;
    char want[80];
    std::snprintf(want, sizeof want, "SetMemoryX(0x%06X, SetTo, 0x%08X, 0x%08X);", sizeAddr & ~3u, 3u << (8 * (sizeAddr & 3)), 0xFFu << (8 * (sizeAddr & 3)));
    check(script.find(want) != std::string::npos, std::string("unit size: one masked byte (") + want + ")");

    // Saved as a standard .dat and read back; no edits, no file.
    const fs::path scratch = argv[3];
    fs::create_directories(scratch);
    const fs::path edits = scratch / "units.dat";
    check(units.saveEdits(edits, err) && fs::file_size(edits) == us.fileSize, "edits saved as a complete units.dat");
    DatTable back;
    back.load(data, us, err);
    check(back.loadEdits(edits, err) && back.value(hp, 0) == 60 * 256 && back.changedCount() == 2, "and read back");
    units.reset(hp, 0), units.reset(size, 0);
    check(units.saveEdits(edits, err) && !fs::exists(edits), "with no edits left the file is removed");
    units.set(hp, 0, 60 * 256), units.set(size, 0, 3);

    // Strings, requirements and button sets.
    StatTxt strings;
    check(strings.load(data, err) && strings.count() > 1500, "stat_txt.tbl: " + std::to_string(strings.count()) + " strings");
    check(strings.build().size() <= strings.capacity(), "unchanged strings rebuild to no more than the game's size (" +
                                                          std::to_string(strings.build().size()) + " of " + std::to_string(strings.capacity()) + ")");
    check(strings.stock(0) == "Terran Marine", "string 0 is the Marine's name");
    check(StatTxt::unescape(StatTxt::escape(std::string("a<b\\x\x01\x05c\nd"))) == std::string("a<b\\x\x01\x05c\nd"), "escape and unescape agree");
    strings.set(0, "Space Marine");
    Requirements reqs;
    check(reqs.used() <= Requirements::capacity(), "the game's requirements fit (" + std::to_string(reqs.used()) + " of " +
                                                    std::to_string(Requirements::capacity()) + " bytes)");
    check(reqs.list(0, 0).used && reqs.list(0, 0).ops.size() == 3 && reqs.list(0, 0).ops[0] == RequirementOp{0xFF02, 111},
          "Marine: the current unit is a Barracks, then two more steps");
    {
        std::string e;
        check(reqs.editsScript(e).empty() && e.empty(), "no requirement edits: no code");
    }
    RequirementList marine = reqs.list(0, 0);
    marine.ops.push_back({0xFF03, 112});  // must have an Academy
    reqs.set(0, 0, marine);
    ButtonSets buttons;
    check(buttons.set(7).size() == 9 && buttons.set(7)[0].pos == 1 && ButtonSets::action(buttons.set(7)[0].action), "SCV: nine buttons, functions known");
    std::vector<Button> scv = buttons.set(7);
    scv.pop_back();
    buttons.set(7, scv);
    buttons.setUnitSet(0, 7);  // the Marine shows the SCV's (changed) card
    UnitLooks looks;
    check(looks.stockStatus(0) == 0x424980 && looks.stockDisplay(0) == 0x426F50 && UnitLooks::kindOf(looks.status(106), looks.display(106)) &&
              std::string(UnitLooks::kindOf(looks.status(106), looks.display(106))->name) == "Building",
          "status panels: a Marine's is a unit's, a Command Center's a building's");
    // R13: every unit's status panel has a name; the rank titles are stat_txt.tbl strings.
    {
        int unnamed = 0;
        for (int u = 0; u < UnitLooks::kUnits; ++u) unnamed += !UnitLooks::kindOf(looks.stockStatus(u), looks.stockDisplay(u));
        check(unnamed == 0, "status panels: every unit's has a name (" + std::to_string(unnamed) + " have none)");
        const DatField* rank = units.field("Rank/Sublabel");
        auto title = [&](int v) { return strings.stock(UnitLooks::kRankString - 1 + v); };
        check(rank && units.value(*rank, 0) == 2 && title(2) == "Private" && units.value(*rank, 32) == 4 && title(4) == "Corporal" &&
                  units.value(*rank, 1) == 5 && title(5) == "Specialist" && units.value(*rank, 20) == 17 && title(17) == "Marshall" &&
                  title(1) == "Private" && title(UnitLooks::kRanks - 1) == "Admiral",
              "ranks: Marine Private, Firebat Corporal, Ghost Specialist, Jim Raynor Marshall; 1 to 22 are Private to Admiral (" +
                  (rank ? title(2) + ", " + title(4) + ", " + title(5) + ", " + title(17) + ", " + title(22) : "") + ")");
        int maxRank = 0;
        for (int u = 0; u < UnitLooks::kUnits && rank; ++u) maxRank = std::max(maxRank, int(units.value(*rank, u)));
        check(maxRank < UnitLooks::kRanks, "ranks: the game's units use only named titles (largest " + std::to_string(maxRank) + ")");
    }
    // R13: the wireframes decode (SD and HD2), each a plausible picture.
    for (bool hd : {false, true}) {
        const char* q = hd ? "HD2" : "SD";
        Wireframes single, group, transport;
        std::string e1, e2, e3;
        const bool loaded = single.load(data, WireKind::Single, hd, false, e1) && group.load(data, WireKind::Group, hd, false, e2) &&
                            transport.load(data, WireKind::Transport, hd, false, e3);
        check(loaded && single.count() == UnitLooks::kWireframes && group.count() == UnitLooks::kGroupWireframes &&
                  transport.count() == UnitLooks::kTransportWireframes,
              std::string(q) + " wireframes: 228 single, 131 group, 106 transport (" + e1 + e2 + e3 + ")");
        if (!loaded) continue;
        const int box = hd ? 128 : 64;
        for (auto [u, name] : {std::pair{0, "Marine"}, {5, "Siege Tank"}, {72, "Carrier"}}) {
            const Sprite* s = single.picture(u, u == 72);
            int opaque = 0, green = 0, blue = 0;
            for (size_t i = 0; s && i + 3 < s->rgba.size(); i += 4)
                if (s->rgba[i + 3] > 128) {
                    ++opaque;
                    green += s->rgba[i + 1] > 2 * std::max(s->rgba[i], s->rgba[i + 2]) && s->rgba[i + 1] > 100;
                    blue += s->rgba[i + 2] > 2 * std::max(s->rgba[i], s->rgba[i + 1]) && s->rgba[i + 2] > 100;
                }
            const int area = s ? s->width * s->height : 0;
            check(s && s->width == box && s->height == box && opaque > area / 10 && opaque < area * 3 / 4 && green > opaque / 4 &&
                      (u == 72) == (blue > 0),
                  std::string(q) + " wireframe of the " + name + ": " + (s ? std::to_string(s->width) + "x" + std::to_string(s->height) : "none") +
                      ", " + std::to_string(opaque) + " pixels, " + std::to_string(green) + " green, " + std::to_string(blue) + " blue (shields)");
        }
        const Sprite* g = group.picture(5, false);
        const Sprite* t = transport.picture(0, false);
        check(g && g->width >= box / 4 && g->width <= box / 2 && t && t->width >= box / 4 && t->width <= box,
              std::string(q) + " group and transport wireframes: Siege Tank " + (g ? std::to_string(g->width) + "x" + std::to_string(g->height) : "none") +
                  ", Marine " + (t ? std::to_string(t->width) + "x" + std::to_string(t->height) : "none"));
    }
    {
        UnitLooks::Unit m;
        m.status = 0x425180, m.display = 0x427890, m.wireframe = 37, m.groupWireframe = 5, m.transportWireframe = 7;
        looks.set(0, m);
    }

    // D.4: portdata.dat (idle and talking portraits in two arrays each) and sfxdata.dat Sound File (a pointer to the
    // file name in the game).
    DatTable port, sfx;
    check(port.load(data, *DatSchema::find("portdata"), err) && sfx.load(data, *DatSchema::find("sfxdata"), err), "portdata.dat and sfxdata.dat load");
    const DatField *portFile = port.field("Portrait File"), *smkChange = port.field("SMK Change"), *idleUnknown = port.field("Unknown1");
    const DatField *soundFile = sfx.field("Sound File"), *sfxFlags = sfx.field("Flags");
    check(portFile && smkChange && idleUnknown && portFile->address(0) == 0x655C58 && portFile->address(109) == 0x655C58 + 4 * 109 &&
              portFile->address(110) == 0x655E80 && smkChange->address(0) == 0x655E10 && smkChange->address(115) == 0x6560A8 + 5 &&
              idleUnknown->address(110) == 0x656118,
          "portdata.dat: idle values at 0x655C58 / 0x655E10 / 0x656038, talking values at 0x655E80 / 0x6560A8 / 0x656118");
    check(portFile && portFile->memoryRanges().size() == 2 && portFile->memoryRanges()[1] == std::pair<uint32_t, uint32_t>{0x655E80, 0x655E80 + 440},
          "portdata.dat Portrait File: two memory ranges");
    check(soundFile && sfxFlags && !sfxFlags->flags.empty() && sfxFlags->flags[0] == "Preload" && sfx.pointerField(*soundFile) &&
              !port.pointerField(*portFile),
          "sfxdata.dat: Flags has its bit names; Sound File is a pointer in the game");
    check(sfx.fileNames().size() > 1000, "sfxdata.tbl: " + std::to_string(sfx.fileNames().size()) + " file names");
    int preloaded = 0, plain = -1, plain2 = -1, preloadedOne = -1;
    for (int s = 1; s < 1144; ++s) {
        if (sfx.soundPreloaded(s)) preloaded++, preloadedOne = preloadedOne < 0 ? s : preloadedOne;
        else if (sfx.stock(*soundFile, s) && plain < 0) plain = s;
        else if (sfx.stock(*soundFile, s) && plain2 < 0) plain2 = s;
    }
    std::printf("      %d sounds have the Preload flag\n", preloaded);
    check(preloadedOne > 0 && plain > 0 && plain2 > 0, "sounds with and without the Preload flag");
    if (preloadedOne > 0 && plain > 0 && plain2 > 0 && portFile) {
        port.set(*portFile, 0, port.stock(*portFile, 1));        // idle: the Marine's portrait shows the Ghost's
        port.set(*portFile, 110, port.stock(*portFile, 111));    // talking
        port.set(*smkChange, 115, port.stock(*smkChange, 115) ^ 1);
        const std::string pCode = port.editsScript();
        std::printf("%s", pCode.c_str());
        char w1[64], w2[64];
        std::snprintf(w1, sizeof w1, "SetMemory(0x655C58, SetTo, 0x%08X);", unsigned(port.stock(*portFile, 1)));
        std::snprintf(w2, sizeof w2, "SetMemory(0x655E80, SetTo, 0x%08X);", unsigned(port.stock(*portFile, 111)));
        check(pCode.find(w1) != std::string::npos && pCode.find(w2) != std::string::npos && pCode.find("SetMemoryX(0x6560AC, SetTo, ") != std::string::npos,
              "portdata edits: idle and talking portraits and SMK Change written at their own arrays");
        const uint32_t newFile = sfx.stock(*soundFile, plain2);
        sfx.set(*soundFile, plain, newFile);                // a plain sound plays another file
        sfx.set(*soundFile, plain2, 0);                     // and another plays no file
        sfx.set(*soundFile, preloadedOne, newFile);         // a preloaded sound cannot change
        const std::string fCode = sfx.editsScript();
        std::printf("%s", fCode.c_str());
        char w3[96], w4[96], w5[64], w6[64];
        std::snprintf(w3, sizeof w3, "const sfxFile%u = EUDArray(list(", unsigned(newFile));
        std::snprintf(w4, sizeof w4, "SetMemory(0x%06X, SetTo, 0x58A364 + 4 * sfxFile%u);", unsigned(0x68DAA0 + 4 * plain), unsigned(newFile));
        std::snprintf(w5, sizeof w5, "SetMemory(0x%06X, SetTo, sfxNoFile);", unsigned(0x68DAA0 + 4 * plain2));
        std::snprintf(w6, sizeof w6, "SetMemory(0x%06X,", unsigned(0x68DAA0 + 4 * preloadedOne));
        check(fCode.find(w3) != std::string::npos && fCode.find(w4) != std::string::npos, "Sound File: points to a copy of the file name in the map");
        check(fCode.find("const sfxNoFile = dwread(0x68DAA0);") != std::string::npos && fCode.find(w5) != std::string::npos,
              "Sound File 0: points to sound 0's name");
        check(fCode.find(w6) == std::string::npos, "a sound with the Preload flag: no write");
        check(fCode.find(sfx.fileNames()[newFile - 1]) != std::string::npos, "the file name is in the code (" + sfx.fileNames()[newFile - 1] + ")");
    }

    // A project with the edits, compiled by euddraft.
    const fs::path projParent = scratch / "projects";
    std::error_code ec;
    fs::remove_all(projParent, ec);
    EudProject pr;
    check(EudProject::create(projParent, "DatTest", argv[2], pr, err), "project created" + (err.empty() ? "" : ": " + err));
    check(units.saveEdits(pr.datFile("units"), err) && pr.writeGenerated({&units, &port, &sfx}, err), "edits saved and code generated in the project");
    check(fs::exists(pr.folder() / "generated" / "portdata.eps") && fs::exists(pr.folder() / "generated" / "sfxdata.eps"),
          "portdata.eps and sfxdata.eps written");
    {
        std::string e1, e2;
        const std::string sCode = strings.editsScript(e1), rCode = reqs.editsScript(e2), bCode = buttons.editsScript();
        check(!sCode.empty() && e1.empty(), "string edits: code" + e1);
        check(!rCode.empty() && e2.empty() && rCode.find("0x660A70") != std::string::npos, "requirement edits: code, with the Marine's offset" + e2);
        check(bCode.find("SetMemory(0x51883C, SetTo, 8);") != std::string::npos, "button edits: SCV set count 8");
        check(bCode.find("const unitSetCount0 = dwread(0x51883C);") != std::string::npos && bCode.find("SetMemory(0x5187E8, SetTo, unitSetCount0);") != std::string::npos,
              "the Marine uses set 7: its entry gets set 7's count and pointer");
        const std::string lCode = looks.editsScript();
        check(lCode.find("SetMemory(0x5193A4, SetTo, 0x425180);") != std::string::npos && lCode.find("SetWirefram(0, 37);") != std::string::npos &&
                  lCode.find("SetGrpWire(0, 5);") != std::string::npos && lCode.find("SetTranWire(0, 7);") != std::string::npos,
              "unit display: the status panel written, the wireframes through eudplib");
        check(pr.writeGeneratedScript("strings.eps", sCode, err) && pr.writeGeneratedScript("requirements.eps", rCode, err) &&
                  pr.writeGeneratedScript("buttons.eps", bCode, err) && pr.writeGeneratedScript("unit_display.eps", lCode, err),
              "strings, requirements, buttons and unit display code written");
        check(looks.saveEdits(pr.folder() / "dat" / "unit_display.json", err), "unit display saved");
        UnitLooks l2;
        check(l2.loadEdits(pr.folder() / "dat" / "unit_display.json", err) && l2.unit(0) == looks.unit(0) && l2.changedCount() == 1, "and read back");
        check(strings.saveEdits(pr.folder() / "dat" / "strings.json", err) && reqs.saveEdits(pr.folder() / "dat" / "requirements.json", err) &&
                  buttons.saveEdits(pr.folder() / "dat" / "buttons.json", err),
              "and saved");
        StatTxt s2;
        Requirements r2;
        ButtonSets b2;
        s2.load(data, err);
        check(s2.loadEdits(pr.folder() / "dat" / "strings.json", err) && s2.text(0) == "Space Marine" && s2.changedCount() == 1, "strings read back");
        check(r2.loadEdits(pr.folder() / "dat" / "requirements.json", err) && r2.list(0, 0) == marine && r2.changedCount() == 1, "requirements read back");
        check(b2.loadEdits(pr.folder() / "dat" / "buttons.json", err) && b2.set(7) == scv && b2.unitSet(0) == 7 && b2.changedCount() == 2, "button sets read back");
    }
    std::ifstream eds(pr.file());
    std::stringstream edsText;
    edsText << eds.rdbuf();
    const std::string e = edsText.str();
    check(e.find("[generated/units.eps]") != std::string::npos && e.find("[generated/units.eps]") < e.find("[scripts/main.eps]"),
          "the .eds runs the generated code before the scripts");
    auto r = pr.compile({}, [](const std::string& l) {
        if (l.find("rror") != std::string::npos || l.find("Saving") != std::string::npos) std::printf("      | %s\n", l.c_str());
    });
    check(r.ok && fs::exists(pr.outputMap()), "euddraft builds the map with the edits");
    // Removing the edits takes the generated code out again.
    units.reset(hp, 0), units.reset(size, 0);
    EudProject again;
    again.open(pr.file(), err);
    check(units.saveEdits(again.datFile("units"), err) && again.writeGenerated({&units}, err) &&
              !fs::exists(again.folder() / "generated" / "units.eps"),
          "no edits: no generated code");

    std::printf("\n%d failures\n", failures);
    return failures ? 1 : 0;
}
