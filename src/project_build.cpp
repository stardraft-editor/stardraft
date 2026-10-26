#include "stardraft/project_build.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/locations.h"
#include "stardraft/msqc.h"
#include "stardraft/sprites.h"
#include "stardraft/units.h"

namespace stardraft {

namespace {

// The .dat files of the project's edits (dat/<name>.dat) and the extras' files (dat/*.json).
std::filesystem::path extrasFileOf(const EudProject& pr, const char* name) { return pr.folder() / "dat" / name; }

const DatTable* tableNamed(const std::vector<std::unique_ptr<DatTable>>& tables, const char* name) {
    for (size_t i = 0; i < tables.size() && i < DatSchema::all().size(); ++i)
        if (DatSchema::all()[i].name == name) return tables[i].get();
    return nullptr;
}

// The game's tables with the project's edits on top (the Data workspace's load). A game table that does not load is null;
// `error` gets the first such error.
std::vector<std::unique_ptr<DatTable>> loadProjectTables(const GameData& data, const EudProject& pr, std::string& error) {
    std::vector<std::unique_ptr<DatTable>> out;
    std::string first;
    for (const DatSchema& sc : DatSchema::all()) {
        auto t = std::make_unique<DatTable>();
        std::string err;
        if (!t->load(data, sc, err)) {
            if (first.empty()) first = err;
            t.reset();
        } else if (!t->loadEdits(pr.datFile(sc.name), err)) {
            if (first.empty()) first = err;
        }
        out.push_back(std::move(t));
    }
    error = first;
    return out;
}

// The triggers as the build has them: the editor's (when given and loaded), else the file's (`report.triggersFromFile`).
bool writeTriggerCode(EudProject& pr, const Editor& ed, const TriggerSet* inMemory, BuildReport& report, std::string& err) {
    // The set lives in the report: the code's lines point into its nodes, so it is generated from here and never copied
    // afterwards (the editor's code map reads those pointers, with this set).
    TriggerSet& set = report.triggerSet;
    set = TriggerSet{};
    if (inMemory && !inMemory->loadFailed) set = *inMemory;
    else if (!set.load(triggersFileOf(pr), err)) return err += ". Repair the file, then build again.", false;
    else report.triggersFromFile = true;
    bool any = false;
    std::function<void(const TriggerNode&)> walk = [&](const TriggerNode& n) {
        any |= n.kind == TriggerNode::Kind::Trigger || n.kind == TriggerNode::Kind::Variable;
        for (const auto& c : n.children) walk(c);
    };
    for (const auto& r : set.roots) walk(r);
    // The [MSQC] section: the lines for the keys and buttons the triggers read; lines written by hand stay.
    auto setInputs = [&](const std::string& code) {
        std::vector<std::string> lines;
        for (const auto& l : pr.sectionLines("MSQC"))
            if (!msqcGeneratedLine(l)) lines.push_back(l);
        for (const auto& l : msqcLinesFor(code)) lines.push_back(l);
        const auto chat = triggerChatLines(code);
        if (lines == pr.sectionLines("MSQC") && chat == pr.sectionLines("chatEvent")) return true;
        pr.setSectionLines("MSQC", lines);
        pr.setSectionLines("chatEvent", chat);
        return pr.save(err);
    };
    if (!any) return pr.writeGeneratedScript("triggers.eps", "", err) && setInputs("");
    std::vector<TriggerIssue> issues;
    std::vector<TriggerCodeLine> lines;
    const std::string code = set.generate(triggerMapInfo(ed), issues, TriggerBuildOptions{pr.debugBuild()}, &lines);
    report.codeGenerated = true;
    report.triggerLines = lines;  // (the caller keeps the code map: a euddraft error in a line selects its block)
    int errors = 0;
    for (const auto& i : issues)
        if (i.level == TriggerIssue::Level::Error) {
            ++errors;
            report.triggerErrors.push_back("Trigger \"" + (i.node ? i.node->name : std::string("?")) + "\": " + i.message);
        }
    if (errors) {
        err = std::to_string(errors) + " problem(s) in the triggers (see the log; they are marked red in the Triggers workspace)";
        return false;
    }
    return pr.writeGeneratedScript("triggers.eps", code, err) && setInputs(code);
}

// The extras (strings, requirements, buttons, unit looks): their edits saved, and their code written.
bool writeExtrasEdits(EudProject& pr, const GameData& data, const BuildOpen& open, std::string& err) {
    StatTxt strings;
    Requirements reqs;
    ButtonSets buttons;
    UnitLooks looks;
    const bool given = open.strings && open.reqs && open.buttons && open.looks;
    if (!given) {
        if (!strings.load(data, err) || !strings.loadEdits(extrasFileOf(pr, "strings.json"), err) || !reqs.loadEdits(extrasFileOf(pr, "requirements.json"), err) ||
            !buttons.loadEdits(extrasFileOf(pr, "buttons.json"), err) || !looks.loadEdits(extrasFileOf(pr, "unit_display.json"), err))
            return false;
    }
    const StatTxt& s = given ? *open.strings : strings;
    const Requirements& r = given ? *open.reqs : reqs;
    const ButtonSets& b = given ? *open.buttons : buttons;
    const UnitLooks& lk = given ? *open.looks : looks;
    if (!s.saveEdits(extrasFileOf(pr, "strings.json"), err) || !r.saveEdits(extrasFileOf(pr, "requirements.json"), err) ||
        !b.saveEdits(extrasFileOf(pr, "buttons.json"), err) || !lk.saveEdits(extrasFileOf(pr, "unit_display.json"), err))
        return false;
    const std::string sCode = s.editsScript(err);
    if (!err.empty()) return false;
    const std::string rCode = r.editsScript(err);
    if (!err.empty()) return false;
    return pr.writeGeneratedScript("strings.eps", sCode, err) && pr.writeGeneratedScript("requirements.eps", rCode, err) &&
           pr.writeGeneratedScript("buttons.eps", b.editsScript(), err) && pr.writeGeneratedScript("unit_display.eps", lk.editsScript(), err);
}

// The .dat edits: every table's edits saved, and generated/<name>.eps written for each that has some; then the extras.
bool writeDatEdits(EudProject& pr, const GameData& data, const BuildOpen& open, BuildReport& report, std::string& err) {
    std::vector<std::unique_ptr<DatTable>> loaded;
    const std::vector<std::unique_ptr<DatTable>>* tables = open.tables;
    if (!tables) {
        std::string first;
        loaded = loadProjectTables(data, pr, first);
        tables = &loaded;
    }
    std::vector<const DatTable*> list;
    std::vector<std::unique_ptr<DatTable>> retried;
    for (size_t i = 0; i < tables->size(); ++i) {
        const DatTable* t = (*tables)[i].get();
        if (!t) {
            // The game table did not load. Without project edits for it, nothing is lost; with edits, the build stops
            // when it still does not load, as it would drop them.
            const DatSchema& sc = DatSchema::all()[i];
            std::error_code ec;
            if (!std::filesystem::exists(pr.datFile(sc.name), ec)) continue;
            auto r = std::make_unique<DatTable>();
            if (!r->load(data, sc, err) || !r->loadEdits(pr.datFile(sc.name), err)) return false;
            t = retried.emplace_back(std::move(r)).get();
        } else if (!t->saveEdits(pr.datFile(t->schema().name), err)) {
            return false;
        }
        for (std::string& m : t->unappliedSoundFiles()) report.datNotes.push_back(std::move(m));
        list.push_back(t);
    }
    return pr.writeGenerated(list, err) && writeExtrasEdits(pr, data, open, err);
}

// The new graphics of the preplaced units: the game's sprites and units with the project's edits on top.
bool projectGraphics(const GameData& data, const BuildOpen& open, const EudProject& pr, UnitSprites& game, UnitSprites& edited, std::string& err) {
    if (!game.load(data, err) || !edited.load(data, err)) return false;
    std::vector<std::unique_ptr<DatTable>> loaded;
    const std::vector<std::unique_ptr<DatTable>>* tables = open.tables;
    if (!tables) {
        std::string first;
        loaded = loadProjectTables(data, pr, first);
        tables = &loaded;
    }
    const DatTable* units = tableNamed(*tables, "units");
    const DatTable* flingy = tableNamed(*tables, "flingy");
    const DatTable* sprites = tableNamed(*tables, "sprites");
    const DatTable* images = tableNamed(*tables, "images");
    if (sprites && images) edited.applyEdits(sprites->bytes(), images->bytes());
    if (units && flingy) edited.applyUnitEdits(units->bytes(), flingy->bytes());
    return true;
}

// The remake code: the plan for the project's units (EudProject::planRemake), its counts, and the units the code makes.
bool writeRemakeCode(EudProject& pr, Editor& ed, const GameData& data, const BuildOpen& open, BuildReport& report, std::string& err) {
    EudProject::RemakePlan plan;
    report.remakeOn = pr.remakeUnits();
    if (pr.remakeUnits()) {
        const auto& types = ed.unitTypes();
        const auto& units = ed.units();
        report.freeProperties = freeUnitProperties(pr, ed);
        const std::vector<uint16_t> changed = projectRemakeUnitTypes(data, open, pr);
        plan = EudProject::planRemake(units, types, changed, report.freeProperties, ed.width(), ed.height());
        report.leaveOut = plan.made;
        // The game puts a unit only where it fits: a ground unit on walkable ground (a building on buildable ground), and
        // not on other ground units. Air units can be on top of each other.
        PlacementContext ctx = ed.placementContext();
        ctx.stacking = false;
        int blocked = 0;
        for (size_t i : plan.made) {
            const Unit& u = units[i];
            const UnitType& ut = types[u.type];
            if (ut.is(UnitFlag::Flyer) && !ut.is(UnitFlag::Building)) continue;
            const auto issues = checkPlacement(ctx, u, int(i));
            blocked += std::any_of(issues.begin(), issues.end(), [](PlacementIssue p) { return p != PlacementIssue::NearResources; });
        }
        report.remake = RemakeCounts{int(plan.made.size()), plan.loaded, plan.linked, plan.noProperty, plan.codeUnit, plan.edge, blocked};
    }
    return pr.writeRemakeCode(plan, err);
}

}  // namespace

TriggerMapInfo triggerMapInfo(const Editor& ed) {
    TriggerMapInfo m;
    if (!ed.isOpen()) return m;
    m.known = true;
    m.locations.resize(size_t(ed.locationCount()));
    for (int i = 0; i < ed.locationCount(); ++i)
        if (ed.locationUsed(i) || i == kAnywhereLocation) m.locations[size_t(i)] = ed.locationName(i);
    for (const auto& f : ed.archiveFiles()) {
        std::string lower = f.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.size() > 4 && (lower.ends_with(".wav") || lower.ends_with(".ogg"))) m.sounds.push_back(f.name);
    }
    for (uint16_t i = 0; i < ed.unitTypes().size(); ++i) m.unitNames.push_back(ed.unitTypes()[i].name);
    for (const auto& u : ed.units()) m.units.push_back({u.serial, u.type, u.owner, u.x, u.y});
    for (int p = 0; p < 8; ++p) {
        const auto pl = ed.player(p);
        m.playerForce[p] = pl.force;
        m.playerActive[p] = pl.controller == Editor::Controller::Human || pl.controller == Editor::Controller::Computer;
    }
    return m;
}

std::filesystem::path triggersFileOf(const EudProject& pr) { return pr.folder() / "triggers.json"; }

int freeUnitProperties(const EudProject& pr, const Editor& ed) {
    int used = 0;
    if (const auto* upus = ed.chk().find("UPUS"))
        for (size_t i = 0; i < upus->data.size() && i < size_t(EudProject::kUnitProperties); ++i) used += upus->data[i] != 0;
    auto occurrences = [](const std::string& text, const char* word) {
        int n = 0;
        for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) ++n;
        return n;
    };
    std::set<std::filesystem::path> files;
    for (const std::string& s : pr.scripts()) files.insert(pr.scriptFile(s).lexically_normal());
    // The files beside each script (an import names a module in its folder), and all of the project's scripts folder.
    std::set<std::filesystem::path> folders = {pr.folder() / "scripts"};
    for (const std::filesystem::path& f : files) folders.insert(f.parent_path());
    std::error_code ec;
    auto add = [&](const std::filesystem::path& f) {
        if (f.extension() == ".eps" || f.extension() == ".py") files.insert(f.lexically_normal());
    };
    for (const std::filesystem::path& dir : folders) {
        const bool all = dir == pr.folder() / "scripts";
        if (all) {
            for (auto it = std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                const std::string name = it->path().filename().string();
                if (it->is_directory(ec) && (name == "__epspy__" || name == "__pycache__")) it.disable_recursion_pending();
                else add(it->path());
            }
        } else {
            for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) add(it->path());
        }
        ec.clear();
    }
    const std::filesystem::path remake = (pr.folder() / "generated" / "remake.eps").lexically_normal();
    for (const std::filesystem::path& file : files) {
        if (file == remake || (file.extension() != ".eps" && file.extension() != ".py")) continue;
        std::ifstream f(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        used += std::max(occurrences(text, "CreateUnitWithProperties"), occurrences(text, "UnitProperty("));
    }
    return std::max(0, EudProject::kUnitProperties - used);
}

std::vector<uint16_t> projectRemakeUnitTypes(const GameData& data, const BuildOpen& open, const EudProject& pr) {
    auto game = std::make_unique<UnitSprites>();
    auto edited = std::make_unique<UnitSprites>();
    std::string err;
    if (!projectGraphics(data, open, pr, *game, *edited, err)) return {};
    return UnitSprites::changedUnitGraphics(*game, *edited);
}

bool prepareBuild(EudProject& pr, Editor& ed, const GameData& data, const BuildOpen& open, BuildReport& report, std::string& error) {
    report = BuildReport{};
    if (!writeTriggerCode(pr, ed, open.triggers, report, error)) return report.failedStep = "triggers", false;
    if (!writeDatEdits(pr, data, open, report, error)) return report.failedStep = "dat", false;
    if (!writeRemakeCode(pr, ed, data, open, report, error)) return report.failedStep = "remake", false;
    if ((ed.modified() || ed.path() != pr.inputMap()) && !ed.save(pr.inputMap(), error)) return report.failedStep = "save", false;
    // The map that euddraft gets: the project's map without the units that the remake code makes.
    if (!pr.writeBuildMap(report.leaveOut, error)) return report.failedStep = "map", false;
    return true;
}

euddraft::BuildResult compileBuild(const EudProject& pr, const std::function<void(const std::string&)>& onLine) {
    return pr.compile({}, onLine);
}

}  // namespace stardraft
