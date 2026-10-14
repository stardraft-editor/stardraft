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

    // Batches (R23): the expanded TRIG is what the game reads; the working copy has the template with its batch.
    {
        auto header = [](const StringTable& s, const ClassicTrigger& t) { return t.actionCount() && t.actions[0].type == 47 ? s.get(t.actions[0].text) : std::string(); };
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
        StringTable strings;
        ClassicTrigger tpl = ClassicTrigger::make();
        tpl.addAction(makeClassicAction(44)), tpl.addAction(makeClassicAction(9));  // Create unit; Display text message
        tpl.actions[0].unit = 0, tpl.actions[0].player = 0;
        ClassicBatch b;
        b.name = "unit", b.kind = ClassicBatchKind::Unit, b.values = {0, 1, 36};
        classicBatchSetSlot(b, false, 0, ClassicField::Unit, true);
        tpl.batch = b;
        ClassicTriggers work;
        work.triggers = {tpl, ClassicTrigger::make()};
        work.tail = {1, 2, 3};
        const ClassicTriggers out = classicExpandBatches(work, strings);
        check(out.triggers.size() == 5 && out.tail == work.tail, "a batch of 3 expands to its template and 3 triggers, then the other trigger");
        const bool head = out.triggers.size() == 5 && (out.triggers[0].flags & 8) && header(strings, out.triggers[0]).rfind("StarDraft batch of 3 triggers: ", 0) == 0 &&
                          out.triggers[0].actionCount() == 3 && out.triggers[0].actions[1].type == 44;
        check(head, "the template is disabled, its first action is the comment that names the batch, its values follow it");
        bool made = out.triggers.size() == 5;
        const uint16_t units[3] = {0, 1, 36};
        for (int i = 0; made && i < 3; ++i) {
            const ClassicTrigger& g = out.triggers[size_t(i + 1)];
            made = !(g.flags & 8) && g.actionCount() == 2 && g.actions[0].type == 44 && g.actions[0].unit == units[i] && g.actions[1].type == 9 &&
                   g.conditionCount() == 1 && g.conditions[0].type == 22 && header(strings, g).empty();
        }
        check(made, "each value makes one enabled trigger with its unit in the slot, and no comment");
        check(valid(out) && out.triggers[4] == work.triggers[1], "the expanded TRIG is valid, and the other trigger stays as it was");
        const Bytes bytes = encodeClassicTriggers(out);
        Chk saved;
        writeClassicTriggers(saved, out);
        const StringTable reread = strings;
        ClassicTriggers back = classicCollapseBatches(readClassicTriggers(saved), reread);
        check(back.triggers.size() == 2 && back.triggers[0].batch == b && back.triggers[0].actionCount() == 2 && back.triggers[0] == work.triggers[0],
              "a saved batch is read back as its template with the same batch");
        StringTable again = reread;
        check(encodeClassicTriggers(classicExpandBatches(back, again)) == bytes, "read back and expanded again, the bytes are the same");
        // Adding an action before the slot moves the slot; a slot whose field the new type lacks goes.
        ClassicTrigger moved = back.triggers[0];
        moved.addAction(makeClassicAction(9), 0);
        check(classicBatchHasSlot(*moved.batch, false, 1, ClassicField::Unit) && !classicBatchHasSlot(*moved.batch, false, 0, ClassicField::Unit),
              "a slot moves with its action when an action is added before it");
        moved.actions[1] = retypeClassicAction(moved.actions[1], 9);
        classicBatchPrune(moved);
        check(moved.batch->slots.empty(), "a slot whose field the new type does not have is dropped");
        // A batch's comment takes the last place: a template with 64 actions cannot take one more action.
        ClassicTrigger full = ClassicTrigger::make();
        for (int i = 0; i < kClassicActions - 1; ++i) full.addAction(makeClassicAction(9));
        full.batch = b;
        check(!full.addAction(makeClassicAction(9)) && full.actionCount() == kClassicActions - 1, "a batch's template has room for 63 actions");
        check(classicBatchName("  a;b=c_1  ", ClassicBatchKind::Unit) == "abc_1" && classicBatchName("", ClassicBatchKind::Player) == "player",
              "a variable's name keeps letters, digits, spaces and underscores; an empty name is the kind's name");
        // A comment that names a batch with the wrong count is not a batch: it stays as it is.
        ClassicTriggers bad;
        ClassicTrigger fake = ClassicTrigger::make();
        fake.flags = 8, fake.addAction(makeClassicAction(47), 0), fake.actions[0].text = strings.add("StarDraft batch of 2 triggers: name=unit; kind=unit; values=0; slots=a1.unit");
        bad.triggers = {fake, ClassicTrigger::make()};
        check(classicCollapseBatches(bad, strings).triggers.size() == 2 && !classicCollapseBatches(bad, strings).triggers[0].batch, "a comment with the wrong count is not a batch");
        // Two batches in a row, and a batch whose slot is a player.
        ClassicTrigger pl = ClassicTrigger::make();
        pl.addAction(makeClassicAction(44));  // Create unit for a player: the player is the variable
        ClassicBatch pb;
        pb.kind = ClassicBatchKind::Player, pb.name = "player", pb.values = {0, 1};
        classicBatchSetSlot(pb, false, 0, ClassicField::Player, true);
        pl.batch = pb;
        ClassicTriggers two;
        two.triggers = {tpl, pl};
        const ClassicTriggers expanded2 = classicExpandBatches(two, strings);
        Chk saved2;
        writeClassicTriggers(saved2, expanded2);
        const ClassicTriggers collapsed2 = classicCollapseBatches(readClassicTriggers(saved2), strings);
        check(expanded2.triggers.size() == 4 + 3 && collapsed2.triggers.size() == 2 && collapsed2.triggers[1].batch == pb &&
                  expanded2.triggers[5].actions[0].player == 0 && expanded2.triggers[6].actions[0].player == 1,
              "two batches in a row expand and read back as two batches");
        // A number field (Give units to player: to) is a slot too, and it reads back.
        ClassicTrigger give = ClassicTrigger::make();
        give.addAction(makeClassicAction(48));
        ClassicBatch gb;
        gb.kind = ClassicBatchKind::Player, gb.name = "player", gb.values = {0, 1};
        classicBatchSetSlot(gb, false, 0, ClassicField::Number, true);
        give.batch = gb;
        ClassicTriggers giveWork;
        giveWork.triggers = {give};
        const ClassicTriggers giveOut = classicExpandBatches(giveWork, strings);
        Chk giveSaved;
        writeClassicTriggers(giveSaved, giveOut);
        const ClassicTriggers giveBack = classicCollapseBatches(readClassicTriggers(giveSaved), strings);
        check(giveOut.triggers.size() == 3 && giveOut.triggers[2].actions[0].number == 1 && giveBack.triggers.size() == 1 && giveBack.triggers[0].batch == gb,
              "a batch whose variable is a number field is read back with the same slot");
        // A copy that another editor changed: the batch is not read (the triggers stay as they are).
        ClassicTriggers altered = out;
        altered.triggers[2].conditions[0].amount = 99;
        Chk alteredSaved;
        writeClassicTriggers(alteredSaved, altered);
        const ClassicTriggers alteredBack = classicCollapseBatches(readClassicTriggers(alteredSaved), strings);
        check(alteredBack.triggers.size() == 5 && !alteredBack.triggers[0].batch, "a changed copy is not taken into the batch");
        // A template from another editor, with bytes after its last action, and turned off: read back as the same batch,
        // and its copies stay off.
        ClassicTrigger odd = tpl;
        odd.actions[size_t(odd.actionCount())].flags = 4, odd.actions[size_t(odd.actionCount()) + 1].type = 9;
        odd.flags |= 8;
        ClassicTriggers oddWork;
        oddWork.triggers = {odd};
        const ClassicTriggers oddOut = classicExpandBatches(oddWork, strings);
        Chk oddSaved;
        writeClassicTriggers(oddSaved, oddOut);
        const ClassicTriggers oddBack = classicCollapseBatches(readClassicTriggers(oddSaved), strings);
        bool oddOff = oddOut.triggers.size() == 4;
        for (size_t i = 1; oddOff && i < 4; ++i) oddOff = (oddOut.triggers[i].flags & 8) != 0;
        check(oddBack.triggers.size() == 1 && oddBack.triggers[0].batch == b && valid(oddOut), "a template with bytes after its last action is read back as the batch");
        check(oddOff && oddBack.triggers.size() == 1 && (oddBack.triggers[0].flags & 8), "a batch that is turned off makes copies that are off, and stays off");
        // Turned into a trigger: no comment, enabled, one trigger.
        ClassicTriggers plain;
        ClassicTrigger one = tpl;
        one.batch.reset();
        plain.triggers = {one};
        const ClassicTriggers plainOut = classicExpandBatches(plain, strings);
        check(plainOut.triggers.size() == 1 && !(plainOut.triggers[0].flags & 8) && plainOut.triggers[0].actionCount() == 2, "a trigger without a batch is written as it is, enabled");
        check(encodeClassicTriggers(out) == bytes && !bytes.empty(), "the expanded bytes are the same each time");
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
