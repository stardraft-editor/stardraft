#include "stardraft/settings.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

#include "file_write.h"
#include "json.h"

namespace fs = std::filesystem;

namespace stardraft {

Settings::Settings(fs::path file) : file_(std::move(file)) { reload(); }

void Settings::reload() {
    std::ifstream in(file_, std::ios::binary);
    if (!in) return;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);  // a UTF-8 file's mark (Notepad's "UTF-8 with BOM")
    std::map<std::string, Value> values;
    std::map<std::string, std::string> foreign;
    if (fromText(text, values, &foreign)) {
        values_ = std::move(values), foreign_ = std::move(foreign);
        unreadable_ = false;
    } else if (!text.empty()) {
        // A file that is not JSON (a hand edit that left a comma): its values are not known, and the first save keeps
        // a copy of it (readError says where), so that the save does not lose them for good.
        unreadable_ = true;
        if (readError_.empty()) readError_ = file_.filename().string() + " is not a JSON object: StarDraft 2 uses the default settings";
    }
}

std::optional<int64_t> Settings::number(const std::string& name) const {
    const auto it = values_.find(name);
    if (it == values_.end() || !std::holds_alternative<int64_t>(it->second)) return std::nullopt;
    return std::get<int64_t>(it->second);
}

std::optional<std::string> Settings::text(const std::string& name) const {
    const auto it = values_.find(name);
    if (it == values_.end() || !std::holds_alternative<std::string>(it->second)) return std::nullopt;
    return std::get<std::string>(it->second);
}

std::optional<std::vector<std::string>> Settings::list(const std::string& name) const {
    const auto it = values_.find(name);
    if (it == values_.end() || !std::holds_alternative<std::vector<std::string>>(it->second)) return std::nullopt;
    return std::get<std::vector<std::string>>(it->second);
}

std::vector<std::string> Settings::names(const std::string& prefix) const {
    std::vector<std::string> out;
    for (auto it = values_.lower_bound(prefix); it != values_.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
        out.push_back(it->first);
    return out;
}

// A change reads the file again first: another StarDraft may have saved values since, and they stay.
bool Settings::set(const std::string& name, Value value) {
    reload();
    auto it = values_.find(name);
    if (it != values_.end() && it->second == value) return true;
    values_[name] = std::move(value);
    foreign_.erase(name);
    return save();
}

bool Settings::remove(const std::string& name) {
    reload();
    const bool had = values_.erase(name) + foreign_.erase(name) > 0;
    if (!had) return true;
    return save();
}

bool Settings::save() {
    error_.clear();
    std::error_code ec;
    if (file_.has_parent_path()) fs::create_directories(file_.parent_path(), ec);
    if (unreadable_) {  // the file that could not be read: a copy stays beside it (settings.json.unreadable, .unreadable2...)
        fs::path copy = file_;
        copy += ".unreadable";
        for (int n = 2; fs::exists(copy, ec) && n < 100; ++n) {
            copy = file_;
            copy += ".unreadable" + std::to_string(n);
        }
        fs::copy_file(file_, copy, ec);
        if (ec) return error_ = "cannot keep a copy of " + file_.string() + " (" + ec.message() + ")", false;
        readError_ = file_.filename().string() + " was not a JSON object: StarDraft 2 used the default settings. The file before is " +
                     copy.filename().string();
        unreadable_ = false;
    }
    // (writeFile: a new file beside it, then a rename: a crash or a full disk during the write leaves the old file)
    return writeFile(file_, toText(values_, &foreign_), error_);
}

std::string Settings::toText(const std::map<std::string, Value>& values, const std::map<std::string, std::string>* foreign) {
    std::map<std::string, std::string> all;  // each value's JSON text, by name (sorted)
    if (foreign) all = *foreign;
    for (const auto& [name, v] : values) {
        std::string t;
        if (const auto* i = std::get_if<int64_t>(&v)) t = std::to_string(*i);
        else if (const auto* s = std::get_if<std::string>(&v)) t = quote(*s);
        else {
            const auto& l = std::get<std::vector<std::string>>(v);
            t = "[";
            for (size_t k = 0; k < l.size(); ++k) t += (k ? ", " : "") + quote(l[k]);
            t += "]";
        }
        all[name] = std::move(t);
    }
    std::string out = "{\n";
    size_t n = 0;
    for (const auto& [name, t] : all) out += "  " + quote(name) + ": " + t + (++n < all.size() ? ",\n" : "\n");
    return out + "}\n";
}

bool Settings::fromText(const std::string& text, std::map<std::string, Value>& values, std::map<std::string, std::string>* foreign) {
    JsonParser p{text};
    const Json root = p.value();
    if (!p.ok || root.type != Json::Type::Object) return false;
    std::map<std::string, Value> out;
    std::map<std::string, std::string> other;
    for (const auto& [name, v] : root.fields) {
        if (v.type == Json::Type::Number && v.n == std::floor(v.n) && std::abs(v.n) < 9.0e15) {
            out[name] = int64_t(v.n);
        } else if (v.type == Json::Type::String) {
            out[name] = v.s;
        } else if (v.type == Json::Type::Array &&
                   std::all_of(v.items.begin(), v.items.end(), [](const Json& item) { return item.type == Json::Type::String; })) {
            std::vector<std::string> l;
            for (const Json& item : v.items) l.push_back(item.s);
            out[name] = std::move(l);
        } else {
            other[name] = jsonText(v);  // (a value of another kind, as a newer version writes it: kept as it is)
        }
    }
    values = std::move(out);
    if (foreign) *foreign = std::move(other);
    return true;
}

}  // namespace stardraft
