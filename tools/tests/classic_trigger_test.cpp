// Classic triggers (roadmap L.2) on a map corpus: every map's TRIG (and MBRF, the same layout) reads and writes back
// the same bytes, every condition and action has a sentence, and what the corpus says about the flags of each type.
//   classic_trigger_test <install> <maps folder>... [--survey]
//   classic_trigger_test <install> --migrate <map> <scratch dir>   (L.4: the map made a project and built by euddraft)
// The last line says how many maps failed; the exit code is 1 when any did.
#include <StormLib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/classic_triggers.h"
#include "stardraft/editor.h"
#include "stardraft/eud_project.h"
#include "stardraft/game_data.h"
#include "stardraft/map_file.h"
#include "stardraft/strings.h"

using namespace stardraft;
namespace fs = std::filesystem;

// euddraft protects the maps it writes (the block table at the archive's start, one sector as large as the file), and
// StormLib does not read their scenario.chk then. The game reads them as this does: the hash table finds the file,
// the block table gives where it is; a file of one sector is decompressed whole.
namespace rawmpq {
uint32_t crypt[0x500];
void init() {
    uint32_t seed = 0x00100001;
    for (uint32_t i = 0; i < 0x100; ++i)
        for (uint32_t j = 0, idx = i; j < 5; ++j, idx += 0x100) {
            seed = (seed * 125 + 3) % 0x2AAAAB;
            const uint32_t a = (seed & 0xFFFF) << 16;
            seed = (seed * 125 + 3) % 0x2AAAAB;
            crypt[idx] = a | (seed & 0xFFFF);
        }
}
uint32_t hash(const char* s, uint32_t type) {
    uint32_t s1 = 0x7FED7FED, s2 = 0xEEEEEEEE;
    for (; *s; ++s) {
        const uint32_t c = uint32_t(std::toupper(static_cast<unsigned char>(*s)));
        s1 = crypt[(type << 8) + c] ^ (s1 + s2);
        s2 = c + s1 + s2 + (s2 << 5) + 3;
    }
    return s1;
}
std::vector<uint32_t> decrypt(const Bytes& d, size_t at, size_t words, uint32_t key) {
    std::vector<uint32_t> out;
    uint32_t s2 = 0xEEEEEEEE;
    for (size_t i = 0; i < words && at + 4 * i + 4 <= d.size(); ++i) {
        s2 += crypt[0x400 + (key & 0xFF)];
        uint32_t v = 0;
        std::memcpy(&v, &d[at + 4 * i], 4);
        const uint32_t c = v ^ (key + s2);
        key = ((~key << 0x15) + 0x11111111) | (key >> 0x0B);
        s2 = c + s2 + (s2 << 5) + 3;
        out.push_back(c);
    }
    return out;
}
bool readChk(const fs::path& file, Bytes& out) {
    init();
    std::ifstream f(file, std::ios::binary);
    const Bytes d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 32 || std::memcmp(d.data(), "MPQ\x1a", 4)) return false;
    uint32_t h[7];
    std::memcpy(h, &d[4], 28);
    const uint32_t hashPos = h[3], blockPos = h[4], hashN = h[5], blockN = h[6];
    const auto ht = decrypt(d, hashPos, size_t(hashN) * 4, hash("(hash table)", 3));
    const auto bt = decrypt(d, blockPos, size_t(blockN) * 4, hash("(block table)", 3));
    const char* name = "staredit\\scenario.chk";
    const uint32_t a = hash(name, 1), b = hash(name, 2), start = hashN ? hash(name, 0) % hashN : 0;
    for (uint32_t k = 0; k < hashN; ++k) {
        const size_t i = size_t((start + k) % hashN) * 4;
        if (i + 4 > ht.size() || ht[i + 3] == 0xFFFFFFFF) return false;
        if (ht[i] != a || ht[i + 1] != b) continue;
        const size_t bi = size_t(ht[i + 3]) * 4;
        if (bi + 4 > bt.size()) return false;
        const uint32_t at = bt[bi], csize = bt[bi + 1], fsize = bt[bi + 2], flags = bt[bi + 3];
        if ((flags & 0x00010000) || at + 8 > d.size()) return false;  // encrypted files: not needed here
        out.assign(fsize, 0);
        if (!(flags & 0x00000300)) return std::memcpy(out.data(), &d[at], std::min<size_t>(fsize, d.size() - at)), true;
        uint32_t s0 = 0, s1 = 0;  // one sector: its offsets
        std::memcpy(&s0, &d[at], 4), std::memcpy(&s1, &d[at + 4], 4);
        if (at + s1 > d.size() || s1 <= s0 || (uint64_t(512) << (h[2] >> 16)) < fsize) return false;
        Bytes in(d.begin() + at + s0, d.begin() + at + s1);
        int n = int(fsize);
        if (s1 - s0 == fsize) return std::memcpy(out.data(), in.data(), fsize), true;
        const bool ok = (flags & 0x100) ? SCompExplode(out.data(), &n, in.data(), int(in.size())) : SCompDecompress(out.data(), &n, in.data(), int(in.size()));
        (void)csize;
        return ok && n == int(fsize);
    }
    return false;
}
}  // namespace rawmpq

// L.4: a classic map made a project (as File > Project > Make this map a project does: the map saved as the project's
// map), then built by euddraft. The project's map keeps TRIG, MBRF and the settings sections byte-identical; the built
// map keeps MBRF (euddraft writes the other sections again, see below).
static int migrate(const GameData& data, const fs::path& map, const fs::path& dir) {
    int failed = 0;
    auto check = [&](bool ok, const std::string& what) {
        if (!ok) ++failed;
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    };
    std::string err;
    Editor ed(data);
    if (!ed.open(map, err)) {
        check(false, "the map opens: " + err);
        return 1;
    }
    const Chk original = ed.chk();
    const size_t triggers = readClassicTriggers(original).triggers.size();
    std::printf("      %zu classic triggers, %zu briefing triggers\n", triggers, original.effective("MBRF").size() / kClassicTriggerSize);
    const char* const kept[] = {"TRIG", "MBRF", "UNIx", "UNIS", "PUNI", "UPGx", "UPGS", "PUPx", "UPGR", "TECx", "TECS", "PTEx", "PTEC", "SWNM", "MRGN", "WAV "};
    auto sameSections = [&](const Chk& c, bool trig, std::string& differ) {
        for (const char* n : kept) {
            SectionName s;
            std::copy(n, n + 4, s.c.begin());
            if (!trig && std::string(n) == "TRIG") continue;
            const Bytes x = original.effective(s), y = c.effective(s);
            if (x != y) {
                size_t first = 0;
                while (first < x.size() && first < y.size() && x[first] == y[first]) ++first;
                differ += std::string(" ") + n + " (" + std::to_string(x.size()) + " -> " + std::to_string(y.size()) + " bytes, from byte " +
                          std::to_string(first) + ")";
            }
        }
        return differ.empty();
    };
    std::error_code ec;
    fs::remove_all(dir / "migrated", ec);
    EudProject pr;
    check(EudProject::create(dir, "migrated", {}, pr, err) && ed.save(pr.inputMap(), err), "the project is made from the map " + err);
    Bytes in, out;
    std::string differ;
    const bool inSame = readMapChk(pr.inputMap(), in, err) && sameSections(Chk::parse(in), true, differ);
    check(inSame, "the project's map keeps TRIG, MBRF and the settings sections byte-identical" + differ);
    std::string log;
    const auto r = pr.compile({}, [&](const std::string& line) { log += line + "\n"; });
    if (!r.ok) std::printf("%s\n", log.c_str());
    check(r.ok, "euddraft builds the project");
    if (!r.ok || !(readMapChk(pr.outputMap(), out, err) || rawmpq::readChk(pr.outputMap(), out))) {
        check(false, "the built map reads " + err);
        return failed;
    }
    const Chk built = Chk::parse(out);
    // euddraft writes the map again: it moves the classic triggers into its own trigger list, builds a new string
    // table, and writes MRGN, the settings and other sections in a form that it puts back when the game starts (the
    // bytes in the file are not the values: location 1 of Premonition_1 has 3720554324 for its left side). So only
    // MBRF can be compared here; the rest only the game can check.
    check(original.effective("MBRF") == built.effective("MBRF"), "the built map keeps MBRF byte-identical");
    differ.clear();
    sameSections(built, false, differ);
    std::printf("      euddraft wrote again (to check in the game):%s\n", differ.empty() ? " nothing" : differ.c_str());
    std::printf("      built TRIG: %zu triggers (the map had %zu)\n", readClassicTriggers(built).triggers.size(), triggers);
    return failed;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: classic_trigger_test <install> <maps folder>... [--survey]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    GameData data;
    std::string err;
    const bool haveData = data.open(argv[1], err);
    if (argc >= 5 && std::string(argv[2]) == "--migrate") {
        if (!haveData) return std::printf("FAIL game data: %s\n", err.c_str()), 1;
        const int f = migrate(data, fs::u8path(argv[3]), fs::u8path(argv[4]));
        std::printf("%d failures\n", f);
        return f ? 1 : 0;
    }
    std::vector<ClassicAiScript> ai;
    if (haveData) ai = loadClassicAiScripts(data);
    std::printf("AI scripts from aiscript.bin: %zu\n", ai.size());
    bool survey = false;
    std::vector<fs::path> files;
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--survey") {
            survey = true;
            continue;
        }
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(fs::u8path(argv[i]), ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (it->is_regular_file(ec) && (ext == ".scm" || ext == ".scx" || ext == ".chk")) files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());

    // The checks of the list operations, on a made trigger.
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) ++failed;
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    };
    {
        ClassicTrigger t = ClassicTrigger::make();
        check(t.conditionCount() == 1 && t.actionCount() == 0 && t.conditions[0].type == 22, "a new trigger has Always and no actions");
        for (uint8_t a : {44, 9, 4}) t.addAction(makeClassicAction(a));
        t.moveAction(2, 0);
        check(t.actionCount() == 3 && t.actions[0].type == 4 && t.actions[1].type == 44 && t.actions[2].type == 9, "add and move actions");
        t.removeAction(1);
        check(t.actionCount() == 2 && t.actions[0].type == 4 && t.actions[1].type == 9 && t.actions[2].type == 0, "remove an action");
        ClassicAction create = makeClassicAction(44);
        create.unit = 37, create.player = 1, create.location = 3, create.modifier = 5;
        const ClassicAction kill = retypeClassicAction(create, 23);
        check(kill.unit == 37 && kill.player == 1 && kill.location == 3 && kill.modifier == 5, "a new type keeps the values both types have");
        bool texts = true;
        for (const ClassicDef* d : classicConditionDefs()) texts &= classicConditionText(makeClassicCondition(d->type), {}).find('{') == std::string::npos;
        for (const ClassicDef* d : classicActionDefs()) texts &= classicActionText(makeClassicAction(d->type), {}).find('{') == std::string::npos;
        check(texts && classicConditionDefs().size() == 23 && classicActionDefs().size() == 59, "every condition and action has a sentence with its values");
    }

    // Batches (R23): a block is a row of the action list with its children. The file has the block's frame (markers, the
    // template and the expansion); the working copy has the block again after a read.
    {
        auto valid = [](const ClassicTriggers& t) {  // every entry before a list's end has a type; nothing after the end
            for (const ClassicTrigger& tr : t.triggers) {
                const int nc = tr.conditionCount(), na = tr.actionCount();
                for (int i = 0; i < nc; ++i) if (!tr.conditions[size_t(i)].type) return false;
                for (int i = 0; i < na; ++i) if (!tr.actions[size_t(i)].type) return false;
                for (int i = nc; i < kClassicConditions; ++i) if (tr.conditions[size_t(i)] != ClassicCondition{}) return false;
                for (int i = na; i < kClassicActions; ++i) if (tr.actions[size_t(i)] != ClassicAction{}) return false;
            }
            return true;
        };
        auto block = [](const ClassicBatch& b, int depth) {  // a block row
            ClassicAction a;
            a.type = kClassicBlockType, a.depth = uint8_t(depth), a.batch = std::make_shared<ClassicBatch>(b);
            return a;
        };
        auto child = [](uint8_t type, int depth) {
            ClassicAction a = makeClassicAction(type);
            a.depth = uint8_t(depth);
            return a;
        };
        const ClassicBatch units{"unit", ClassicBatchKind::Unit, {0, 1, 36}};
        // Block "unit": Create unit (the variable), then Display text. Then one more trigger.
        ClassicTrigger tpl = ClassicTrigger::make();
        tpl.addAction(block(units, 0));
        ClassicAction create = child(44, 1);
        create.unit = 0, create.var[2] = 1, create.player = 0, create.location = 64;
        tpl.addAction(create);
        tpl.addAction(child(9, 1));
        ClassicTriggers work;
        work.triggers = {tpl, ClassicTrigger::make()};
        work.tail = {1, 2, 3};
        StringTable strings;
        const ClassicTriggers out = classicExpandBatches(work, strings);
        check(out.triggers.size() == 2 && out.tail == work.tail && valid(out), "a block of 3 values expands to one trigger, then the other trigger");
        const ClassicTrigger& head = out.triggers[0];
        const uint32_t mark = head.actions[0].text;
        check(head.actionCount() == 12 && head.actions[0].type == 47 && mark && strings.get(mark) == "Batch Trigger marker - do not remove" && head.actions[11].type == 47,
              "the frame has a begin, its header, the template, the expansion and an end marker");
        check((head.actions[3].flags & 2) && (head.actions[4].flags & 2) && !(head.actions[5].flags & 2), "the template is disabled, the expansion is enabled");
        check(head.actions[5].unit == 0 && head.actions[7].unit == 1 && head.actions[9].unit == 36 && head.actions[6].type == 9 && head.actions[10].type == 9,
              "each value gives its copy of the children, in order, with the value in the slot");
        Chk saved;
        writeClassicTriggers(saved, out);
        ClassicTriggers back = classicCollapseBatches(readClassicTriggers(saved), strings);
        check(back.triggers.size() == 2 && back.triggers[0] == work.triggers[0] && back.tail == work.tail,
              "a saved block is read back as the block with its children");
        StringTable again = strings;
        check(encodeClassicTriggers(classicExpandBatches(back, again)) == encodeClassicTriggers(out), "read back and expanded again, the bytes are the same");

        // Nested: the outer block's variable is a unit, the inner block's is a player. The inner frame is in the template.
        ClassicTrigger nest = ClassicTrigger::make();
        nest.addAction(block(units, 0));
        nest.addAction(block({"player", ClassicBatchKind::Player, {0, 1}}, 1));
        ClassicAction make = child(44, 2);
        make.var[2] = 1, make.var[1] = 2, make.unit = 0, make.player = 0;
        nest.addAction(make);
        ClassicTriggers nw;
        nw.triggers = {nest};
        StringTable ns;
        const ClassicTriggers nout = classicExpandBatches(nw, ns);
        check(nout.triggers.size() == 1 && nout.triggers[0].actionCount() > 0 && nout.triggers[0].actions[0].type == 47,
              "nested blocks write one trigger");
        // Expansion: 3 units x 2 players = 6 copies, each with its unit and player.
        int copies = 0, bad = 0;
        const ClassicTrigger& nt = nout.triggers[0];
        const int na = nt.actionCount();
        for (int i = 0; i < na; ++i)
            if (nt.actions[size_t(i)].type == 44 && !(nt.actions[size_t(i)].flags & 2)) ++copies, bad += nt.actions[size_t(i)].player > 1;
        check(copies == 6 && bad == 0, "nested: six copies, one for each unit and player");
        Chk nsaved;
        writeClassicTriggers(nsaved, nout);
        ClassicTriggers nback = classicCollapseBatches(readClassicTriggers(nsaved), ns);
        check(nback.triggers.size() == 1 && nback.triggers[0] == nw.triggers[0], "nested blocks are read back the same");
        // Moving a row out of its block drops the variable it cannot hold.
        ClassicTrigger outRow = nback.triggers[0];
        const ClassicAction& moved = outRow.actions[size_t(outRow.actionCount() - 1)];
        check(outRow.outdentAction(outRow.actionCount() - 1) && moved.depth == 1 && moved.var[1] == 0 && moved.var[2] == 1,
              "a row that moves out of the player block keeps the unit variable of the outer block and loses the player one");

        // Editing rows: a child is added inside the block, and a block with its children is removed as one.
        ClassicTrigger edit = ClassicTrigger::make();
        edit.addAction(block(units, 0));
        edit.addAction(child(9, 1));
        check(edit.addAction(child(9, 1), edit.blockEnd(0)) && edit.actionCount() == 3 && edit.actions[2].depth == 1,
              "a row added after the children is a child of the block");
        edit.addAction(child(9, 0));
        edit.removeAction(0);
        check(edit.actionCount() == 1 && edit.actions[0].type == 9 && edit.actions[0].depth == 0, "removing a block removes its children with it");

        // A split: 40 rows in a block of 3 values make more than 64 slots. The parts are linked by a switch that no other
        // trigger uses; the list still shows one trigger.
        ClassicTrigger big = ClassicTrigger::make();
        big.addAction(block(units, 0));
        for (int i = 0; i < 40; ++i) big.addAction(child(9, 1));
        big.flags = 4;  // preserve
        ClassicTrigger other = ClassicTrigger::make();
        other.conditions[0] = makeClassicCondition(11), other.conditions[0].resource = 0, other.conditions[0].comparison = 2;
        other.addAction(makeClassicAction(13));  // Set switch 0, so switch 0 is used
        ClassicTriggers bw;
        bw.triggers = {big, other};
        StringTable bs;
        const ClassicTriggers bout = classicExpandBatches(bw, bs);
        int texts = 0, parts = 0, handoff = 0;
        for (size_t i = 0; i < bout.triggers.size(); ++i) {
            const ClassicTrigger& t = bout.triggers[i];
            if (t.conditions[0].type == 11) ++handoff;
        }
        for (const ClassicTrigger& t : bout.triggers) {
            if (t.flags & 4) ++parts;
            for (int i = 0; i < t.actionCount(); ++i) texts += t.actions[size_t(i)].type == 9 && !(t.actions[size_t(i)].flags & 2);
        }
        check(bout.triggers.size() >= 3 && texts == 120 && handoff >= 1 && parts >= 2, "a long block is split into parts that make all its copies");
        const int linkedSwitch = bout.triggers[1].conditions[0].resource;
        check(linkedSwitch == 1 && bout.triggers[2].actions[0].type == 13 && bout.triggers[2].actions[0].modifier == 5, "the parts use the first free switch (switch 1 here)");
        Chk bsaved;
        writeClassicTriggers(bsaved, bout);
        ClassicTriggers bback = classicCollapseBatches(readClassicTriggers(bsaved), bs);
        check(bback.triggers.size() == 2 && bback.triggers[0].actionCount() == 41 && bback.triggers[0].splitSwitch == 1 && bback.triggers[1] == other,
              "split parts read back as the one trigger, with its switch");
        check(encodeClassicTriggers(classicExpandBatches(bback, bs)) == encodeClassicTriggers(bout), "a split trigger expands to the same bytes again");

        // A copy that another editor changed: the block is not taken back, the triggers stay as they are.
        ClassicTriggers altered = out;
        altered.triggers[0].actions[5].unit = 7;
        Chk asaved;
        writeClassicTriggers(asaved, altered);
        const ClassicTriggers alteredBack = classicCollapseBatches(readClassicTriggers(asaved), strings);
        check(alteredBack.triggers.size() == 2 && alteredBack.triggers[0].actionCount() == 12 && alteredBack.triggers[0].actions[0].type == 47,
              "a changed copy is not taken back: the actions stay as they are");

        // No block: the marker string is blanked, and the trigger is written as it is.
        ClassicTriggers plain;
        ClassicTrigger one = ClassicTrigger::make();
        one.addAction(child(9, 0));
        plain.triggers = {one};
        StringTable ps = strings;
        const ClassicTriggers plainOut = classicExpandBatches(plain, ps);
        check(plainOut.triggers.size() == 1 && plainOut.triggers[0] == one && ps.get(mark).empty(), "a trigger without a block is written as it is, and the marker string is blank");
        check(classicCollapseBatches(readClassicTriggers(saved), ps).triggers.size() == 2, "a map with blocks reads back without the marker string as the triggers they are");

        // A block's text.
        ClassicNames names;
        names.unit = [](uint32_t v) { return v == 0 ? std::string("Terran Marine") : v == 1 ? std::string("Terran Firebat") : std::string("Zergling"); };
        check(classicBatchText(units, names) == "For each unit in Terran Marine, Terran Firebat, Zergling", "a block shows its variable and its values");
        ClassicAction withVar = child(44, 1);
        withVar.var[2] = 1;
        const std::vector<const ClassicBatch*> scope{&units};
        check(classicActionText(withVar, names, &scope).find("variable \"unit\"") != std::string::npos, "a field that holds a variable shows its name");
        check(classicBatchName("  a;b=c_1  ", ClassicBatchKind::Unit) == "abc_1" && classicBatchName("", ClassicBatchKind::Player) == "player",
              "a variable's name keeps letters, digits, spaces and underscores; an empty name is the kind's name");
        check(encodeClassicTriggers(classicExpandBatches(work, strings)) == encodeClassicTriggers(out), "the expanded bytes are the same each time");
    }


    int maps = 0, openFail = 0, trigFail = 0, mbrfFail = 0, withTrig = 0, eudMaps = 0, batchFail = 0;
    size_t triggers = 0, conditions = 0, actions = 0, eudEntries = 0, unknownTypes = 0, hidden = 0, tails = 0;
    std::map<int, std::map<int, int>> condFlags, actFlags;  // type -> flags -> count
    std::map<int, int> condTypes, actTypes;
    for (const fs::path& f : files) {
        ++maps;
        Bytes bytes;
        if (!readMapChk(f, bytes, err)) {
            ++openFail;
            continue;
        }
        const Chk chk = Chk::parse(bytes);
        const std::u8string u8 = f.filename().u8string();
        const std::string name(u8.begin(), u8.end());  // (a Korean name: string() would throw)
        const ClassicTriggers t = readClassicTriggers(chk);
        if (!t.triggers.empty()) ++withTrig;
        if (encodeClassicTriggers(t) != chk.effective("TRIG")) ++trigFail, std::printf("FAIL TRIG %s\n", name.c_str());
        Chk back = chk;
        writeClassicTriggers(back, t);
        if (back.serialize() != chk.serialize()) ++trigFail, std::printf("FAIL TRIG written back changes the map %s\n", name.c_str());
        // MBRF has the same layout.
        Chk m;
        m.sections.push_back({"TRIG", chk.effective("MBRF")});
        if (encodeClassicTriggers(readClassicTriggers(m)) != chk.effective("MBRF")) ++mbrfFail, std::printf("FAIL MBRF %s\n", name.c_str());
        if (!t.tail.empty()) ++tails;
        const StringTable strings = StringTable::read(chk);
        // Batches (R23): a map without batches is the same when it is read as the editor's working copy and written
        // again; a map's TRIG is not changed by the batch code.
        {
            StringTable copy = strings;
            const ClassicTriggers working = classicCollapseBatches(t, strings);
            if (working.triggers != t.triggers) ++batchFail, std::printf("FAIL batch: a map without batches changes %s\n", name.c_str());
            if (encodeClassicTriggers(classicExpandBatches(working, copy)) != chk.effective("TRIG")) ++batchFail, std::printf("FAIL batch: TRIG changes %s\n", name.c_str());
        }
        ClassicNames names;
        names.string = [&](uint32_t id) { return id <= strings.size() ? strings.get(id) : std::string(); };
        names.ai = &ai;
        bool eud = false;
        for (const ClassicTrigger& tr : t.triggers) {
            ++triggers;
            const int nc = tr.conditionCount(), na = tr.actionCount();
            for (int i = nc; i < kClassicConditions; ++i) hidden += tr.conditions[size_t(i)] != ClassicCondition{};
            for (int i = na; i < kClassicActions; ++i) hidden += tr.actions[size_t(i)] != ClassicAction{};
            for (int i = 0; i < nc; ++i) {
                const ClassicCondition& c = tr.conditions[size_t(i)];
                ++conditions, ++condTypes[c.type], ++condFlags[c.type][c.flags];
                if (!classicConditionDef(c.type)) ++unknownTypes;
                if (classicEud(c)) ++eudEntries, eud = true;
                (void)classicConditionText(c, names);
            }
            for (int i = 0; i < na; ++i) {
                const ClassicAction& a = tr.actions[size_t(i)];
                ++actions, ++actTypes[a.type], ++actFlags[a.type][a.flags];
                if (!classicActionDef(a.type)) ++unknownTypes;
                if (classicEud(a)) ++eudEntries, eud = true;
                (void)classicActionText(a, names);
            }
        }
        eudMaps += eud;
    }
    if (survey) {
        auto show = [](const char* what, const std::map<int, std::map<int, int>>& flags, const std::map<int, int>& types, bool cond) {
            for (const auto& [type, byFlags] : flags) {
                const ClassicDef* d = cond ? classicConditionDef(uint8_t(type)) : classicActionDef(uint8_t(type));
                std::printf("%s %2d %-38s %6d:", what, type, d ? d->name : "?", types.at(type));
                std::vector<std::pair<int, int>> v(byFlags.begin(), byFlags.end());
                std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
                for (size_t i = 0; i < v.size() && i < 5; ++i) std::printf(" flags %d x%d", v[i].first, v[i].second);
                std::printf("\n");
            }
        };
        show("condition", condFlags, condTypes, true);
        show("action", actFlags, actTypes, false);
    }
    std::printf("%d maps (%d with classic triggers): %d did not open; %zu triggers, %zu conditions, %zu actions; %zu EUD entries in %d maps, "
                "%zu entries of types the game does not have, %zu entries after the end of a list, %zu maps with bytes after the last trigger\n",
                maps, withTrig, openFail, triggers, conditions, actions, eudEntries, eudMaps, unknownTypes, hidden, tails);
    std::printf("round trip: TRIG %d maps differ, MBRF %d maps differ; list checks %d failed; batch checks on the maps %d failed\n", trigFail, mbrfFail,
                failed, batchFail);
    return trigFail || mbrfFail || failed || batchFail ? 1 : 0;
}
