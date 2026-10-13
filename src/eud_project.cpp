#include "stardraft/eud_project.h"
#include "stardraft/triggers.h"

#include "stardraft/dat_edit.h"
#include "file_write.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace stardraft {

namespace {

// euddraft's .eds syntax: "[section]" headers, "key : value" (or "key = value") lines; ':' '=' and '\'
// inside a key are escaped with '\'.
std::string escapeKey(const std::string& k) {
    std::string out;
    for (char c : k) {
        if (c == ':' || c == '=' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

// Splits "key : value" at the first unescaped ':' or '='; the key comes back unescaped.
bool splitLine(const std::string& line, std::string& key, std::string& value) {
    key.clear();
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            key += line[++i];
            continue;
        }
        if (line[i] == ':' || line[i] == '=') {
            value = line.substr(i + 1);
            auto trim = [](std::string& s) {
                s.erase(0, s.find_first_not_of(" \t"));
                s.erase(s.find_last_not_of(" \t\r") + 1);
            };
            trim(key), trim(value);
            return true;
        }
        key += line[i];
    }
    return false;
}

std::string generic(const fs::path& p) { return p.generic_string(); }

}  // namespace

bool EudProject::create(const fs::path& parent, const std::string& name, const fs::path& map, EudProject& out, std::string& error) {
    std::error_code ec;
    if (name.empty() || name.find_first_of("\\/:*?\"<>|") != std::string::npos) {
        error = "the name cannot be empty or hold \\ / : * ? \" < > |";
        return false;
    }
    const fs::path dir = parent / name;
    if (fs::exists(dir, ec) && !fs::is_empty(dir, ec)) {
        error = dir.string() + " already exists and is not empty";
        return false;
    }
    fs::create_directories(dir / "scripts", ec);
    if (ec) {
        error = "could not create " + dir.string() + ": " + ec.message();
        return false;
    }
    const fs::path mapRel = name + ".scx";
    if (!map.empty() && !fs::copy_file(map, dir / mapRel, fs::copy_options::overwrite_existing, ec)) {
        error = "could not copy the map: " + ec.message();
        return false;
    }
    if (!writeFile(dir / "scripts" / "main.eps",
                   "// " + name + ": your code. euddraft builds this file into the map.\n"
                   "// onPluginStart runs once when the map starts; beforeTriggerExec and afterTriggerExec run every\n"
                   "// trigger cycle.\n\n"
                   "function onPluginStart() {\n}\n\n"
                   "function beforeTriggerExec() {\n}\n\n"
                   "function afterTriggerExec() {\n}\n",
                   error))
        return false;
    out = {};
    out.file_ = dir / kFileName;
    out.name_ = name;
    out.sections_.push_back({"main", {"input: " + generic(mapRel), "output: " + name + "_EUD.scx"}});
    out.sections_.push_back({"eudTurbo", {}});  // EUD Turbo: on by default
    out.sections_.push_back({"scripts/main.eps", {}});
    return out.save(error);
}

bool EudProject::open(const fs::path& fileOrFolder, std::string& error) {
    std::error_code ec;
    const fs::path folder = fs::is_directory(fileOrFolder, ec) ? fileOrFolder : fileOrFolder.parent_path();
    if (fs::exists(folder / kFileName, ec)) return read(folder / kFileName, false, error);
    // No project.stardraft: a .eds (the one given, else the first in the folder) becomes the project.
    fs::path eds = fileOrFolder.extension() == ".eds" ? fileOrFolder : fs::path();
    for (const auto& e : fs::directory_iterator(folder, ec))
        if (eds.empty() && e.path().extension() == ".eds") eds = e.path();
    if (eds.empty()) {
        error = "no " + std::string(kFileName) + " or .eds file in " + folder.string();
        return false;
    }
    if (!read(eds, true, error)) return false;
    file_ = folder / kFileName;
    return save(error);
}

bool EudProject::read(const fs::path& file, bool eds, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "cannot read " + file.string();
        return false;
    }
    std::vector<Section> sections;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = line;
        t.erase(0, t.find_first_not_of(" \t"));
        if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
            sections.push_back({t.substr(1, t.size() - 2), {}});
            continue;
        }
        if (t.empty() || (sections.empty() && t.front() == '#')) continue;  // a comment before the first section
        if (sections.empty()) {
            error = "a setting before any [section] in " + file.string();
            return false;
        }
        sections.back().lines.push_back(line);
    }
    // project.stardraft's [project] is the .eds's [main], with name (not in the .eds) and map (input there).
    std::string name = file.stem().string();
    if (!eds && !sections.empty() && sections.front().name == "project") {
        sections.front().name = "main";
        std::vector<std::string> lines;
        for (const auto& l : sections.front().lines) {
            std::string k, v;
            if (splitLine(l, k, v) && k == "name") name = v;
            else if (splitLine(l, k, v) && k == "map") lines.push_back("input: " + v);
            else lines.push_back(l);
        }
        sections.front().lines = std::move(lines);
    }
    if (sections.empty() || sections.front().name != "main") {
        error = file.string() + (eds ? " has no [main] section" : " has no [project] section");
        return false;
    }
    if (!eds && name == "project") name = file.parent_path().filename().string();
    file_ = file;
    name_ = name;
    sections_ = std::move(sections);
    return true;
}

// The file's text: project.stardraft, or (eds) euddraft's .eds. UTF-8 without BOM, CRLF (as euddraft's own examples
// and EUD Editor write it).
std::string EudProject::text(bool eds) const {
    std::ostringstream o;
    if (!eds) o << "# StarDraft project. Build writes " << name_ << ".eds for euddraft from this file.\r\n\r\n";
    for (size_t i = 0; i < sections_.size(); ++i) {
        if (i) o << "\r\n";
        if (i == 0 && !eds) {
            o << "[project]\r\nname: " << name_ << "\r\n";
            for (const auto& l : sections_[0].lines) {
                std::string k, v;
                o << (splitLine(l, k, v) && k == "input" ? "map: " + v : l) << "\r\n";
            }
            continue;
        }
        o << '[' << sections_[i].name << "]\r\n";
        for (const auto& l : sections_[i].lines) {
            std::string k, v;
            if (eds && i == 0 && splitLine(l, k, v) && (k == "debug" || k == "trace")) continue;  // StarDraft's own settings: not for euddraft
            o << l << "\r\n";
        }
        // A trace build: euddraft's debug mode (any "debug" key turns it on, so it is written only when on).
        if (eds && i == 0 && traceBuild()) o << "debug: 1\r\n";
    }
    return o.str();
}

bool EudProject::save(std::string& error) const { return writeFile(file_, text(false), error); }

bool EudProject::writeEds(std::string& error) const { return writeFile(edsFile(), text(true), error); }

std::string EudProject::mainValue(const std::string& key) const {
    std::string k, v;
    for (const auto& l : sections_.front().lines)
        if (splitLine(l, k, v) && k == key) return v;
    return {};
}

fs::path EudProject::inputMap() const {
    const fs::path p = mainValue("input");
    return p.is_absolute() ? p : folder() / p;
}

fs::path EudProject::outputMap() const {
    const fs::path p = mainValue("output");
    return p.is_absolute() ? p : folder() / p;
}

void EudProject::setOutputMap(const fs::path& file) {
    std::error_code ec;
    fs::path rel = fs::relative(file, folder(), ec);
    const bool inside = !ec && !rel.empty() && rel.native().rfind(fs::path("..").native(), 0) != 0;
    const std::string value = inside ? generic(rel) : fs::path(file).make_preferred().string();
    auto& lines = sections_.front().lines;
    std::string k, v;
    for (auto& l : lines)
        if (splitLine(l, k, v) && k == "output") {
            l = "output: " + value;
            return;
        }
    lines.push_back("output: " + value);
}

std::vector<std::string> EudProject::scripts() const {
    std::vector<std::string> out;
    for (const auto& s : sections_) {
        const std::string ext = fs::path(s.name).extension().string();
        if (ext == ".eps" || ext == ".py") out.push_back(s.name);
    }
    return out;
}

void EudProject::addScript(const std::string& relativePath) {
    for (const auto& s : sections_)
        if (s.name == relativePath) return;
    sections_.push_back({relativePath, {}});
}

fs::path EudProject::scriptFile(const std::string& section) const {
    const fs::path p = section;
    return p.is_absolute() ? p : (folder() / p).lexically_normal();
}

std::string EudProject::linkScript(const fs::path& file) {
    std::error_code ec;
    const fs::path full = fs::absolute(file, ec).lexically_normal();
    // Relative when the file is on the folder's drive ("../shared/main.eps"); else the full path.
    const fs::path rel = full.root_name() == folder().root_name() ? full.lexically_relative(folder()) : fs::path();
    const std::string name = !rel.empty() ? rel.generic_string() : fs::path(full).make_preferred().string();
    addScript(name);
    return name;
}

bool EudProject::isLinked(const std::string& section) {
    const fs::path p = section;
    return p.is_absolute() || section.rfind("../", 0) == 0 || section.rfind("..\\", 0) == 0;
}

std::vector<std::string> EudProject::missingScripts() const {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& s : scripts())
        if (!isGenerated(s) && !fs::is_regular_file(scriptFile(s), ec)) out.push_back(s);  // generated code: Build writes it
    return out;
}

std::string EudProject::missingScriptText(const std::string& section) const {
    return std::string("Build stopped: ") + (isLinked(section) ? "the linked file " : "the script ") + section + " is not found (" +
           fs::path(scriptFile(section)).make_preferred().string() + "). Put the file back, or remove it from the build on the Project page.";
}

bool EudProject::scriptError(const std::string& log, ScriptError& out) const {
    // Each "[epScript] Compiling "<path>"..." line: the module's file (the path is relative to the .eds's folder).
    // euddraft writes the errors (stderr) before the "Compiling" lines (stdout) in the log: read those first. A "Compiled
    // failed for <full path>" line names a file too.
    out = {};
    std::vector<fs::path> compiled;
    {
        std::istringstream in(log);
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            static const std::string kCompiling = "[epScript] Compiling \"", kFailed = "Compiled failed for ";
            if (const size_t c = line.find(kCompiling); c != std::string::npos) {
                const size_t start = c + kCompiling.size(), end = line.find('"', start);
                if (end != std::string::npos) {
                    const fs::path p = line.substr(start, end - start);
                    compiled.push_back(p.is_absolute() ? p.lexically_normal() : (folder() / p).lexically_normal());
                }
            } else if (const size_t f = line.find(kFailed); f != std::string::npos) {
                const fs::path p = line.substr(f + kFailed.size());
                if (p.is_absolute()) compiled.push_back(p.lexically_normal());
            }
        }
    }
    std::istringstream in(log);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        static const std::string kModule = "Module \"";
        const size_t m = line.find(kModule);
        if (m == std::string::npos || line.find("[Error", 0) == std::string::npos) continue;
        const size_t nameEnd = line.find('"', m + kModule.size());
        if (nameEnd == std::string::npos || line.compare(nameEnd, 7, "\" Line ") != 0) continue;
        const std::string module = line.substr(m + kModule.size(), nameEnd - m - kModule.size());
        const int at = std::atoi(line.c_str() + nameEnd + 7);
        const size_t colon = line.find(" : ", nameEnd);
        const std::string message = colon == std::string::npos ? std::string() : line.substr(colon + 3);
        if (out.line > 0) {
            // A second line for the same place often says more ("Incomplete statement..." after "General syntax error").
            if (module == out.module && at == out.line && out.message == "General syntax error" && !message.empty()) out.message = message;
            continue;
        }
        out = {};
        out.module = module, out.line = at, out.message = message;
        // The module's file: the last file compiled with its name (a dotted module: its last part), else a script.
        const std::string stem = out.module.substr(out.module.rfind('.') == std::string::npos ? 0 : out.module.rfind('.') + 1);
        for (auto it = compiled.rbegin(); it != compiled.rend() && out.file.empty(); ++it)
            if (it->stem().string() == stem) out.file = *it;
        for (const auto& s : scripts())
            if (out.file.empty() && fs::path(s).stem().string() == stem) out.file = scriptFile(s);
    }
    return out.line > 0;
}

bool EudProject::writeGenerated(const std::vector<const DatTable*>& tables, std::string& error) {
    std::error_code ec;
    bool changedEds = false;
    std::string writeError;
    for (const DatTable* t : tables) {
        const std::string section = "generated/" + t->schema().name + ".eps";
        const fs::path file = folder() / section;
        auto it = std::find_if(sections_.begin(), sections_.end(), [&](const Section& s) { return s.name == section; });
        if (t->changedCount() == 0) {
            fs::remove(file, ec);
            if (it != sections_.end()) sections_.erase(it), changedEds = true;
            continue;
        }
        fs::create_directories(file.parent_path(), ec);
        if (!writeFile(file, t->editsScript(), writeError)) break;  // the sections changed until now are still saved
        if (it == sections_.end()) {
            // Right after [main]: before the scripts, so they run with the edited values in place.
            sections_.insert(sections_.begin() + 1, {section, {}});
            changedEds = true;
        }
    }
    if (changedEds && !save(error)) return false;
    return writeError.empty() || (error = writeError, false);
}

bool EudProject::writeGeneratedScript(const std::string& name, const std::string& code, std::string& error) {
    std::error_code ec;
    const std::string section = "generated/" + name;
    const fs::path file = folder() / section;
    auto it = std::find_if(sections_.begin(), sections_.end(), [&](const Section& s) { return s.name == section; });
    bool changedEds = false;
    if (code.empty()) {
        fs::remove(file, ec);
        if (it != sections_.end()) sections_.erase(it), changedEds = true;
    } else {
        fs::create_directories(file.parent_path(), ec);
        if (!writeFile(file, code, error)) return false;
        // The triggers import their helpers from a file of their own next to them (not a plugin: no .eds section).
        if (name == "triggers.eps" && !writeFile(file.parent_path() / "sdlib.eps", TriggerSet::helperLibrary(), error)) return false;
        if (it == sections_.end()) {
            // After [main] and the generated .dat code, before the scripts.
            auto at = sections_.begin() + 1;
            while (at != sections_.end() && isGenerated(at->name)) ++at;
            sections_.insert(at, {section, {}});
            changedEds = true;
        }
    }
    return !changedEds || save(error);
}

bool EudProject::turbo() const {
    return std::any_of(sections_.begin(), sections_.end(), [](const Section& s) { return s.name == "eudTurbo"; });
}
bool EudProject::debugBuild() const { return mainValue("debug") == "1"; }
void EudProject::setDebugBuild(bool on) {
    if (on == debugBuild() || sections_.empty()) return;
    auto& lines = sections_.front().lines;
    std::string k, v;
    lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const std::string& l) { return splitLine(l, k, v) && k == "debug"; }), lines.end());
    if (on) lines.push_back("debug: 1");
}

bool EudProject::traceBuild() const { return mainValue("trace") == "1"; }
void EudProject::setTraceBuild(bool on) {
    if (on == traceBuild() || sections_.empty()) return;
    auto& lines = sections_.front().lines;
    std::string k, v;
    lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const std::string& l) { return splitLine(l, k, v) && k == "trace"; }), lines.end());
    if (on) lines.push_back("trace: 1");
}

bool EudProject::plugin(const std::string& name) const {
    return std::any_of(sections_.begin(), sections_.end(), [&](const Section& s) { return s.name == name; });
}
void EudProject::setPlugin(const std::string& name, bool on, std::vector<std::string> lines) {
    if (!on) return void(removeSection(name));
    if (plugin(name)) return setSectionLines(name, std::move(lines));
    sections_.insert(sections_.begin() + (sections_.empty() ? 0 : 1), {name, std::move(lines)});  // a plugin: right after [main]
}

std::vector<std::string> EudProject::sectionLines(const std::string& name) const {
    for (const auto& s : sections_)
        if (s.name == name) return s.lines;
    return {};
}

void EudProject::setSectionLines(const std::string& name, std::vector<std::string> lines) {
    auto it = std::find_if(sections_.begin() + (sections_.empty() ? 0 : 1), sections_.end(), [&](const Section& s) { return s.name == name; });
    if (lines.empty()) {
        if (it != sections_.end()) sections_.erase(it);
        return;
    }
    if (it != sections_.end()) return void(it->lines = std::move(lines));
    auto at = sections_.begin() + (sections_.empty() ? 0 : 1);
    while (at != sections_.end() && isGenerated(at->name)) ++at;
    sections_.insert(at, {name, std::move(lines)});
}

bool EudProject::removeSection(const std::string& name) {
    auto it = std::find_if(sections_.begin() + 1, sections_.end(), [&](const Section& s) { return s.name == name; });
    if (it == sections_.end()) return false;
    sections_.erase(it);
    return true;
}

bool EudProject::moveScript(const std::string& name, int delta) {
    for (size_t i = 1; i < sections_.size(); ++i) {
        if (sections_[i].name != name || isGenerated(name)) continue;
        const size_t j = size_t(int(i) + delta);
        if (j < 1 || j >= sections_.size() || isGenerated(sections_[j].name)) return false;
        std::swap(sections_[i], sections_[j]);
        return true;
    }
    return false;
}

euddraft::BuildResult EudProject::compile(const fs::path& chosen, const std::function<void(const std::string&)>& onLine) const {
    euddraft::BuildResult r;
    // A missing script (a linked file moved or deleted): stop before euddraft, with the file's name.
    if (const auto missing = missingScripts(); !missing.empty()) {
        for (const auto& s : missing) r.errors.push_back(missingScriptText(s));
        r.log = r.errors.front() + "\n";
        if (onLine) onLine(r.errors.front());
        return r;
    }
    auto use = euddraft::find(chosen);
    if (!use) {
        // StarDraft keeps its own copy: fetch the newest on the first build.
        if (onLine) onLine("No euddraft yet: fetching the newest release...");
        euddraft::Release rel;
        EuddraftInstall in;
        std::string err;
        if (!euddraft::latestRelease(rel, err) || !euddraft::install(rel, in, err, onLine)) {
            r.log = "Could not get euddraft: " + err;
            r.errors.push_back(r.log);
            if (onLine) onLine(r.log);
            return r;
        }
        use = in;
    }
    std::error_code ec;
    fs::create_directories(outputMap().parent_path(), ec);
    if (!traceBuild()) fs::remove(traceMap(), ec);  // the new map has no trace code: an old trace file does not fit it
    std::string err;
    if (!writeEds(err)) {
        r.log = err;
        r.errors.push_back(err);
        if (onLine) onLine(err);
        return r;
    }
    if (onLine) onLine("euddraft " + use->version + ": building " + edsFile().filename().string());
    return euddraft::build(use->exe, edsFile(), onLine);
}

}  // namespace stardraft
