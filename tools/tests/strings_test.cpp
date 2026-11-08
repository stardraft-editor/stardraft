// The map string table's editor (Data > Strings of a classic map): each kind of use of a string follows its string when
// the strings move (Editor::moveMapString, strings.h: renumberMapStrings), a used string cannot be deleted, each edit is
// one undo step, and the map saves and opens again with the same texts and uses.
//
// usage: strings_test <install dir> <scratch dir> [map [the map saved after moves in the program]]

#include <cstdio>
#include <filesystem>
#include <map>
#include <random>
#include <set>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/strings.h"

using namespace stardraft;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond, msg)                                     \
    do {                                                     \
        bool ok_ = (cond);                                   \
        std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", msg); \
        std::fflush(stdout);                                 \
        if (!ok_) ++failures;                                \
    } while (0)

// What each use reads now: the use (without its id) and its string's text.
static std::map<std::string, std::string> useTexts(const Editor& ed) {
    std::map<std::string, std::string> out;
    for (const StringUse& u : ed.mapStringUses()) {
        const std::string key = std::to_string(int(u.kind)) + ":" + std::to_string(u.index) + ":" + std::to_string(u.action);
        out[key] = ed.strings().get(u.id);
    }
    return out;
}

// The texts as the editor's own readers give them (the names, the triggers, the briefing).
static std::vector<std::string> readerTexts(const Editor& ed) {
    std::vector<std::string> out{ed.scenarioName(), ed.scenarioDescription(), ed.force(0).name, ed.force(1).name, ed.unitSettingName(0),
                                 ed.locationName(0), ed.switchName(0)};
    for (const std::string& s : ed.soundReferences()) out.push_back(s);
    const ClassicTriggers t = ed.classicTriggers();
    for (const ClassicTrigger& tr : t.triggers)
        for (int a = 0; a < tr.actionCount(); ++a) {
            const ClassicAction& x = tr.actions[size_t(a)];
            if (x.type == 9 || x.type == 8) out.push_back(ed.strings().get(x.type == 9 ? x.text : x.wav));
            if (x.type == 44) out.push_back("create unit text field " + std::to_string(x.text));  // (not a string: it stays)
        }
    for (const BriefingTrigger& b : ed.briefings())
        for (const BriefingAction& a : b.actions) out.push_back(a.text + "|" + a.wav);
    return out;
}

static void moveTest(const GameData& data, const fs::path& scratch) {
    std::string err;
    Editor ed(data);
    const IsomTerrainTable* table = IsomTerrainTable::forEra(0);
    uint16_t terrain = 1;
    while (table && terrain < 64 && !table->isTerrain(terrain)) ++terrain;
    if (!ed.newMap(64, 64, 0, terrain, err)) {
        CHECK(false, ("strings: a new map (" + err + ")").c_str());
        return;
    }
    // One use of each kind.
    ed.setScenarioText("Scenario name", "Scenario description");
    ed.setForce(0, {"Force one", 0});
    ed.setForce(1, {"Force two", 0});
    ed.setUnitSettings(ed.unitSettings(), {{0, "Unit name"}});
    const int loc = ed.createLocation({32, 32, 96, 96}, false, 3, "Location A");
    ed.setSwitchName(0, "Switch name");
    ed.addSoundReference("sound\\trigger.wav");
    StringTable strings = ed.strings();
    ClassicTrigger t = ClassicTrigger::make();
    t.players[0] = 1;
    ClassicAction text = makeClassicAction(9);
    text.text = classicStringId(strings, "Trigger text");
    t.addAction(text);
    ClassicAction wav = makeClassicAction(8);
    wav.wav = classicStringId(strings, "sound\\trigger.wav");
    t.addAction(wav);
    ClassicAction create = makeClassicAction(44);  // Create unit: its text field is not a string
    create.text = 3;
    t.addAction(create);
    ed.addClassicTrigger(t, -1, &strings);
    BriefingTrigger b;
    b.players[0] = true;
    BriefingAction bt;
    bt.type = BriefingAction::TextMessage, bt.text = "Briefing text", bt.time = 1000;
    BriefingAction bw;
    bw.type = BriefingAction::PlayWav, bw.wav = "sound\\briefing.wav", bw.wavTime = 1000;
    b.actions = {bt, bw};
    ed.setBriefings({b});
    // Unused strings between them, and one at the end.
    const uint32_t spare1 = ed.addMapString("Spare one");
    const uint32_t spare2 = ed.addMapString("Spare two");

    std::set<StringUseKind> kinds;
    for (const StringUse& u : ed.mapStringUses()) kinds.insert(u.kind);
    CHECK(kinds.size() == 11, ("strings: the map has every kind of use (" + std::to_string(kinds.size()) + " of 11)").c_str());
    CHECK(loc >= 0 && ed.locationName(loc) == "Location A", "strings: the location has its name");

    const auto uses0 = useTexts(ed);
    const auto readers0 = readerTexts(ed);
    const Chk original = ed.chk();
    const size_t steps0 = ed.undoSteps();
    const uint32_t n = uint32_t(ed.strings().size());
    std::printf("      %zu uses of %u strings\n", ed.mapStringUses().size(), n);

    // The ids that the uses must have after each move: string `from` to `to`, the ones between one place over.
    std::mt19937 rng(7);
    int moves = 0, wrongIds = 0, wrongTexts = 0;
    uint32_t last = 0;
    for (uint32_t i = 1; i <= n; ++i)
        if (!ed.strings().get(i).empty()) last = i;
    const uint32_t span = std::min<uint32_t>(n, last + 3);  // (the strings and a few empty slots after them)
    for (int k = 0; k < 40; ++k) {
        const uint32_t from = 1 + rng() % span, to = 1 + rng() % span;
        std::vector<StringUse> before = ed.mapStringUses();
        if (!ed.moveMapString(from, to)) {
            if (from != to) ++wrongIds;
            continue;
        }
        ++moves;
        auto expect = [&](uint32_t id) -> uint32_t {
            if (id == from) return to;
            if (from < to && id > from && id <= to) return id - 1;
            if (to < from && id >= to && id < from) return id + 1;
            return id;
        };
        const std::vector<StringUse> after = ed.mapStringUses();
        if (after.size() != before.size()) ++wrongIds;
        for (size_t i = 0; i < std::min(before.size(), after.size()); ++i)
            if (after[i].id != expect(before[i].id) || after[i].kind != before[i].kind) ++wrongIds;
        if (useTexts(ed) != uses0 || readerTexts(ed) != readers0) ++wrongTexts;
    }
    CHECK(moves > 30, ("strings: " + std::to_string(moves) + " moves").c_str());
    CHECK(wrongIds == 0, ("strings: every use has the moved id (" + std::to_string(wrongIds) + " wrong)").c_str());
    CHECK(wrongTexts == 0, ("strings: every use reads its own text after each move (" + std::to_string(wrongTexts) + " wrong)").c_str());
    CHECK(ed.undoSteps() == steps0 + size_t(moves), "strings: each move is one undo step");
    CHECK(ed.chk().find("STR ") && !ed.chk().find("STRx"), "strings: the table stays STR");

    // A move to the first id: the scenario name's string.
    {
        uint32_t nameId = 0;
        for (const StringUse& u : ed.mapStringUses())
            if (u.kind == StringUseKind::ScenarioName) nameId = u.id;
        CHECK(nameId == 1 || ed.moveMapString(nameId, 1), "strings: move the scenario name's string to id 1");
        uint32_t now = 0;
        for (const StringUse& u : ed.mapStringUses())
            if (u.kind == StringUseKind::ScenarioName) now = u.id;
        CHECK(now == 1 && ed.scenarioName() == "Scenario name", "strings: the scenario name is string 1 and reads the same");
    }
    CHECK(!ed.moveMapString(0, 2) && !ed.moveMapString(2, 0) && !ed.moveMapString(3, 3) && !ed.moveMapString(1, n + 1),
          "strings: string 0, the same place and a place past the table do not move");

    // Delete: a used string stays; an unused one goes (its slot is empty), and undo brings it back.
    uint32_t used = 0, spare = 0;
    for (const StringUse& u : ed.mapStringUses()) used = u.id;
    for (uint32_t i = 1; i <= ed.strings().size(); ++i)
        if (ed.strings().get(i) == "Spare one") spare = i;
    CHECK(spare1 && spare2 && spare, "strings: the spare strings are there");
    CHECK(!ed.deleteMapString(used), "strings: a used string is not deleted");
    CHECK(ed.deleteMapString(spare) && ed.strings().get(spare).empty(), "strings: an unused string is deleted");
    ed.undo();
    CHECK(ed.strings().get(spare) == "Spare one", "strings: undo brings the deleted string back");
    CHECK(ed.setMapString(spare, "Spare one, changed") && ed.strings().get(spare) == "Spare one, changed", "strings: a string's text changes");
    ed.undo();

    // Undo every move: the map as before them (each section's bytes).
    ed.undo(ed.undoSteps() - steps0);
    bool same = ed.chk().sections.size() == original.sections.size();
    for (size_t i = 0; same && i < original.sections.size(); ++i) same = ed.chk().sections[i].data == original.sections[i].data;
    CHECK(same && useTexts(ed) == uses0, "strings: undo of the moves gives the map as it was");
    ed.redo(ed.redoSteps());
    CHECK(useTexts(ed) == uses0 && readerTexts(ed) == readers0, "strings: redo of the moves keeps the texts");

    // Saved and opened again: the same texts, the same uses.
    const fs::path file = scratch / "strings_test.scx";
    const auto usesNow = ed.mapStringUses();
    Editor back(data);
    CHECK(ed.save(file, err) && back.open(file, err), ("strings: save and open again " + err).c_str());
    CHECK(back.mapStringUses() == usesNow && useTexts(back) == uses0 && readerTexts(back) == readers0,
          "strings: the opened map has the same uses and texts");
}

// renumberMapStrings on a u16 field: a new id past 65535 is refused, and nothing changes.
static void wideTest() {
    Chk chk;
    Bytes sprp{5, 0, 6, 0};
    chk.set("SPRP", sprp);
    Bytes swnm(256 * 4, 0);
    swnm[0] = 6;
    chk.set("SWNM", swnm);
    std::vector<uint32_t> to(7, 0);
    for (uint32_t i = 0; i < 7; ++i) to[i] = i;
    to[5] = 70000, to[6] = 2;
    const Chk before = chk;
    CHECK(!renumberMapStrings(chk, to) && chk.effective("SPRP") == before.effective("SPRP") && chk.effective("SWNM") == before.effective("SWNM"),
          "strings: a new id past 65535 in a u16 field changes nothing");
    to[5] = 1;
    CHECK(renumberMapStrings(chk, to) && chk.effective("SPRP") == Bytes({1, 0, 2, 0}) && chk.effective("SWNM")[0] == 2,
          "strings: the scenario's ids and a switch's id take the new ids");
}

// A real map: each string moved to id 1 and back keeps the map's bytes and its uses.
static void mapTest(const GameData& data, const char* map) {
    std::string err;
    Editor ed(data);
    if (!ed.open(map, err)) {
        CHECK(false, ("strings: the map opens " + err).c_str());
        return;
    }
    const auto uses0 = useTexts(ed);
    const Chk original = ed.chk();
    int moved = 0, wrong = 0;
    for (uint32_t i = 2; i <= ed.strings().size() && i < 64; ++i) {
        if (!ed.moveMapString(i, 1)) continue;
        ++moved;
        if (useTexts(ed) != uses0) ++wrong;
        ed.moveMapString(1, i);
    }
    CHECK(wrong == 0, ("strings: " + fs::path(map).filename().string() + ": " + std::to_string(moved) + " strings moved first and back, uses keep their texts").c_str());
    bool same = true;
    for (const SectionName n : {SectionName("SPRP"), SectionName("FORC"), SectionName("MRGN"), SectionName("TRIG"), SectionName("MBRF"),
                                SectionName("WAV "), SectionName("SWNM"), SectionName("UNIx"), SectionName("UNIS")})
        same = same && ed.chk().effective(n) == original.effective(n);
    CHECK(same, "strings: the fields have their first ids again");
}

// The map and a copy saved after string moves (the program's Data > Strings): each use reads the same text.
static void sameUsesTest(const GameData& data, const char* map, const char* moved) {
    std::string err;
    Editor a(data), b(data);
    if (!a.open(map, err) || !b.open(moved, err)) {
        CHECK(false, ("strings: the two maps open " + err).c_str());
        return;
    }
    const auto ua = useTexts(a), ub = useTexts(b);
    CHECK(!ua.empty() && ua == ub && readerTexts(a) == readerTexts(b),
          ("strings: " + fs::path(moved).filename().string() + " has the uses and texts of " + fs::path(map).filename().string() + " (" +
           std::to_string(ua.size()) + " uses)").c_str());
}

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: strings_test <install dir> <scratch dir> [map [moved map]]\n"), 2;
    GameData data;
    std::string err;
    if (!data.open(argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    wideTest();
    moveTest(data, fs::path(argv[2]));
    if (argc > 3) mapTest(data, argv[3]);
    if (argc > 4) sameUsesTest(data, argv[3], argv[4]);
    std::printf("\n%d failures\n", failures);
    return failures ? 1 : 0;
}
