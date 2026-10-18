#include "stardraft/project_import.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include "stardraft/dat_edit.h"
#include "stardraft/dat_extras.h"
#include "stardraft/game_data.h"

namespace fs = std::filesystem;

namespace stardraft {

namespace {

std::string readAll(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}
std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}
std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) out.push_back(trim(l));
    return out;
}
fs::path pathOf(const std::string& s) { return fs::u8path(s); }
// "key : value" (EUD Editor 2's settings).
bool keyValue(const std::string& l, std::string& k, std::string& v) {
    const size_t c = l.find(" : ");
    if (c == std::string::npos) return false;
    k = trim(l.substr(0, c)), v = trim(l.substr(c + 3));
    return true;
}
// EUD Editor 2's text: <N> is the byte N (a color, 10 a line end). As StarDraft's (StatTxt::stock): a button text's
// hotkey and type, then the text up to its zero byte; the parts after that stay the game's.
std::string ee2Text(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '<') {
            const size_t e = s.find('>', i);
            if (e != std::string::npos && e > i + 1 && e - i <= 4 &&
                std::all_of(s.begin() + long(i) + 1, s.begin() + long(e), [](char ch) { return ch >= '0' && ch <= '9'; })) {
                const int n = std::stoi(s.substr(i + 1, e - i - 1));
                if (n < 256) {
                    out += char(n);
                    i = e;
                    continue;
                }
            }
        }
        out += s[i];
    }
    const size_t from = out.size() >= 2 && static_cast<unsigned char>(out[0]) >= 0x20 && static_cast<unsigned char>(out[1]) <= 5 ? 2 : 0;
    if (const size_t z = out.find('\0', from); z != std::string::npos) out.resize(z);
    return out;
}
// A script file into the project's scripts folder, as a script of the project. False when it cannot be copied.
bool copyScript(EudProject& pr, const fs::path& from, ImportReport& rep) {
    std::error_code ec;
    if (!fs::exists(from, ec)) return rep.notImported.push_back("the script " + from.filename().string() + ": the file is not there"), false;
    const fs::path to = pr.folder() / "scripts" / from.filename();
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) return rep.notImported.push_back("the script " + from.filename().string() + ": " + ec.message()), false;
    pr.addScript("scripts/" + from.filename().string());
    rep.imported.push_back("the script " + from.filename().string());
    // The modules it imports ("import name as x;" in epScript, "import name" in Python) that are beside it: copied
    // with it, not as scripts of their own.
    for (std::string l : lines(readAll(from))) {
        if (l.rfind("import ", 0) != 0 && l.rfind("from ", 0) != 0) continue;
        std::string mod = trim(l.substr(l.find(' ') + 1));
        mod = mod.substr(0, mod.find_first_of(" ;,\t"));
        if (mod.empty() || mod.find('.') != std::string::npos) continue;
        for (const char* ext : {".py", ".eps"}) {
            const fs::path m = from.parent_path() / (mod + ext), mt = to.parent_path() / (mod + ext);
            if (!fs::exists(m, ec) || fs::exists(mt, ec)) continue;
            fs::copy_file(m, mt, ec);
            if (!ec) rep.imported.push_back("the module " + m.filename().string() + " (that " + from.filename().string() + " imports)");
        }
    }
    return true;
}

// The plugin sections of an .eds (not [main], not the scripts): their lines as they are.
void importPluginSections(EudProject& pr, const std::vector<std::pair<std::string, std::vector<std::string>>>& sections, ImportReport& rep) {
    for (const auto& [name, body] : sections) {
        if (name == "main") continue;
        const fs::path p = pathOf(name);
        if (p.extension() == ".eps" || p.extension() == ".py") continue;
        if (name == "dataDumper") {  // (files of the other editor's own folder)
            rep.notImported.push_back("the plugin dataDumper: it copies files from the other editor's folder (texts, requirements); import "
                                      "EUD Editor 2's .e2s to get its texts");
            continue;
        }
        std::vector<std::string> keep;
        for (const std::string& l : body)
            if (!l.empty()) keep.push_back(l);
        if (keep.empty()) pr.addScript(name);  // (a plugin with no settings: the section alone)
        else pr.setSectionLines(name, keep);
        rep.imported.push_back("the plugin " + name);
    }
}

std::vector<std::pair<std::string, std::vector<std::string>>> edsSections(const std::string& text) {
    std::vector<std::pair<std::string, std::vector<std::string>>> out;
    for (const std::string& l : lines(text)) {
        if (l.size() >= 2 && l.front() == '[' && l.back() == ']') out.push_back({l.substr(1, l.size() - 2), {}});
        else if (!out.empty()) out.back().second.push_back(l);
    }
    return out;
}

bool importEds(const fs::path& file, const fs::path& parent, const std::string& name, EudProject& pr, ImportReport& rep, std::string& error) {
    const auto sections = edsSections(readAll(file));
    fs::path input;
    for (const auto& [sec, body] : sections)
        if (sec == "main")
            for (const std::string& l : body)
                if (l.rfind("input", 0) == 0 && l.find(':') != std::string::npos) input = pathOf(trim(l.substr(l.find(':') + 1)));
    if (input.empty()) return error = "the .eds has no input map ([main] input)", false;
    if (input.is_relative()) input = file.parent_path() / input;
    if (!EudProject::create(parent, name, input, pr, error)) return false;
    rep.imported.push_back("the map " + input.filename().string());
    for (const auto& [sec, body] : sections) {
        const fs::path p = pathOf(sec);
        if (p.extension() != ".eps" && p.extension() != ".py") continue;
        copyScript(pr, p.is_absolute() ? p : file.parent_path() / p, rep);
    }
    importPluginSections(pr, sections, rep);
    return pr.save(error) && pr.writeEds(error);
}

// EUD Editor 2's sections: S_<name> ... E_<name>.
std::map<std::string, std::vector<std::string>> e2sSections(const std::string& text) {
    std::map<std::string, std::vector<std::string>> out;
    std::string cur;
    for (const std::string& l : lines(text)) {
        if (cur.empty()) {
            if (l.rfind("S_", 0) == 0) cur = l.substr(2), out[cur];
            continue;
        }
        if (l == "E_" + cur) {
            cur.clear();
            continue;
        }
        out[cur].push_back(l);
    }
    return out;
}

bool importE2s(const fs::path& file, const fs::path& parent, const std::string& name, const GameData& data, EudProject& pr, ImportReport& rep,
               std::string& error) {
    auto sec = e2sSections(readAll(file));
    std::string k, v;
    fs::path input;
    for (const std::string& l : sec["ProjectSET"])
        if (keyValue(l, k, v) && k == "InputMap") input = pathOf(v);
    std::error_code ec;
    if (input.empty()) return error = "the .e2s has no input map", false;
    if (!fs::exists(input, ec) && fs::exists(file.parent_path() / input.filename(), ec)) input = file.parent_path() / input.filename();  // (moved with it)
    if (!EudProject::create(parent, name, input, pr, error)) return false;
    rep.imported.push_back("the map " + input.filename().string());

    // The .dat edits: "dat,field,entry - the field's first entry,difference from the game's value" (EUD Editor 2's
    // order of the .dat files and of their fields: the files' own, as StarDraft's).
    static const char* const kDats[] = {"units", "weapons", "flingy", "sprites", "images", "upgrades", "techdata", "orders", "sfxdata", "portdata"};
    std::map<int, std::vector<std::array<long long, 4>>> rows;
    for (const std::string& l : sec["DatEditSET"]) {
        std::array<long long, 4> r{};
        if (std::sscanf(l.c_str(), "%lld,%lld,%lld,%lld", &r[0], &r[1], &r[2], &r[3]) == 4) rows[int(r[0])].push_back(r);
    }
    for (const auto& [dat, list] : rows) {
        if (dat < 0 || dat >= int(std::size(kDats))) {
            rep.notImported.push_back(std::to_string(list.size()) + " values of an unknown .dat (" + std::to_string(dat) + ")");
            continue;
        }
        const DatSchema* schema = DatSchema::find(kDats[dat]);
        DatTable t;
        std::string err;
        if (!schema || !t.load(data, *schema, err)) {
            rep.notImported.push_back(std::to_string(list.size()) + " values of " + kDats[dat] + ".dat: " + err);
            continue;
        }
        int n = 0, skipped = 0;
        for (const auto& r : list) {
            if (r[1] < 0 || r[1] >= long(schema->fields.size())) {
                ++skipped;
                continue;
            }
            const DatField& f = schema->fields[size_t(r[1])];
            const int entry = f.first + int(r[2]);
            if (!f.covers(entry)) {
                ++skipped;
                continue;
            }
            const long long most = f.size == 1 ? 0xFF : f.size == 2 ? 0xFFFF : 0xFFFFFFFFLL;
            t.set(f, entry, uint32_t(std::clamp<long long>(static_cast<long long>(t.stock(f, entry)) + r[3], 0, most)));
            ++n;
        }
        if (n && !t.saveEdits(pr.datFile(kDats[dat]), err)) return error = err, false;
        if (n) rep.imported.push_back(std::to_string(n) + " values of " + kDats[dat] + ".dat");
        if (skipped) rep.notImported.push_back(std::to_string(skipped) + " values of " + kDats[dat] + ".dat: fields or entries that the file does not have");
    }

    // stat_txt.tbl's texts (FileManagerSET: stattextdickey<i> : string, stattextdicvalue<i> : text).
    std::map<std::string, std::string> fm;
    for (const std::string& l : sec["FileManagerSET"])
        if (keyValue(l, k, v)) fm[k] = v;
    if (const int count = fm.count("stattextdicCount") ? std::atoi(fm["stattextdicCount"].c_str()) : 0; count > 0) {
        StatTxt strings;
        std::string err;
        if (strings.load(data, err)) {
            int n = 0;
            for (int i = 0; i < count; ++i) {
                const auto key = fm.find("stattextdickey" + std::to_string(i)), val = fm.find("stattextdicvalue" + std::to_string(i));
                if (key == fm.end() || val == fm.end()) continue;
                const int s = std::atoi(key->second.c_str());
                if (s < 0 || s >= strings.count()) continue;
                strings.set(s, ee2Text(val->second));
                ++n;
            }
            if (n && !strings.saveEdits(pr.folder() / "dat" / "strings.json", err)) return error = err, false;
            if (n) rep.imported.push_back(std::to_string(n) + " texts of stat_txt.tbl");
        } else {
            rep.notImported.push_back("the texts of stat_txt.tbl: " + err);
        }
    }

    // The plugins: EUD Editor 2's check boxes, and the sections of the .eds it wrote (beside the .e2s), as MSQC's.
    static const std::pair<const char*, const char*> kPlugins[] = {
        {"unlimiter", "unlimiter"}, {"noAirColsion", "noAirCollision"}, {"eudTurbo", "eudTurbo"}, {"soundstopper", "soundstopper"}, {"keepSTR", "keepSTR"}};
    std::map<std::string, std::string> plugins;
    for (const std::string& l : sec["PluginSET"])
        if (keyValue(l, k, v)) plugins[k] = v;
    for (const auto& [key, section] : kPlugins)
        if (plugins[key] == "True") pr.addScript(section), rep.imported.push_back(std::string("the plugin ") + section);
    for (const fs::path& eds : {file.parent_path() / "EUDEditor.eds", file.parent_path() / ".reference-files" / "EUDEditor.eds"})
        if (fs::exists(eds, ec)) {
            for (const auto& [s, body] : edsSections(readAll(eds)))
                if (s == "MSQC") {
                    pr.setSectionLines("MSQC", body);
                    rep.imported.push_back("the plugin MSQC (keys and mouse)");
                }
            break;
        }
    // The trigger editor's code: EUD Editor 2 writes TriggerEditor.eps when it builds.
    bool trig = false;
    for (const fs::path& eps : {file.parent_path() / "TriggerEditor.eps", file.parent_path() / ".reference-files" / "TriggerEditor.eps"})
        if (!trig && fs::exists(eps, ec)) trig = copyScript(pr, eps, rep);
    if (trig)
        rep.notImported.push_back("TriggerEditor.eps is EUD Editor 2's code for its own euddraft: a newer euddraft can find errors in it (a name "
                                  "such as Gas is now eudplib's). Build, then correct them in the script");
    if (!trig && sec.count("TriggerEditorSET") && sec["TriggerEditorSET"].size() > 2)
        rep.notImported.push_back("the triggers of EUD Editor 2's trigger editor: build the map once in EUD Editor 2, then import again with "
                                  "TriggerEditor.eps beside the .e2s");
    // What StarDraft does not read yet.
    auto count = [&](const char* s) { return sec.count(s) ? sec[s].size() : 0; };
    if (count("BtnSET")) rep.notImported.push_back("the button sets (" + std::to_string(count("BtnSET") / 2) + "): make them again in the Data workspace, Buttons");
    if (count("ReqSET")) rep.notImported.push_back("the requirements: make them again in the Data workspace");
    if (count("FireGraftSET")) rep.notImported.push_back("FireGraft's settings (unit status functions)");
    if (count("GRPEditorSET") > 1 || count("TileSET") > 1) rep.notImported.push_back("the graphics and tileset settings");
    return pr.save(error) && pr.writeEds(error);
}

}  // namespace

std::string importKind(const fs::path& file) {
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == ".e2s") return "e2s";
    if (ext == ".eds" || ext == ".edd") return "eds";
    return "";
}

bool importProject(const fs::path& file, const fs::path& parent, const std::string& name, const GameData& data, EudProject& out, ImportReport& report,
                   std::string& error) {
    const std::string kind = importKind(file);
    if (kind == "e2s") return importE2s(file, parent, name, data, out, report, error);
    if (kind == "eds") return importEds(file, parent, name, out, report, error);
    return error = file.filename().string() + " is not a project that StarDraft imports (.e2s, .eds, .edd)", false;
}

}  // namespace stardraft
