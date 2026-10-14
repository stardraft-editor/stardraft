#include "stardraft/settings.h"

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
    fromText(ss.str(), values_);  // (a file that cannot be read changes nothing)
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
    return save();
}

bool Settings::remove(const std::string& name) {
    reload();
    if (!values_.erase(name)) return true;
    return save();
}

bool Settings::save() {
    error_.clear();
    std::error_code ec;
    if (file_.has_parent_path()) fs::create_directories(file_.parent_path(), ec);
    // A new file beside it, then a rename: a crash or a full disk during the write leaves the old file.
    // Its name is this process's own, so two programs that save at once do not write into one file.
    static const std::string kPart = ".new" + std::to_string(std::random_device{}());
    fs::path part = file_;
    part += kPart;
    if (!writeFile(part, toText(values_), error_)) return false;
    fs::rename(part, file_, ec);
    if (ec) return error_ = "cannot replace " + file_.string() + " (" + ec.message() + ")", false;
    return true;
}

std::string Settings::toText(const std::map<std::string, Value>& values) {
    std::string out = "{\n";
    size_t n = 0;
    for (const auto& [name, v] : values) {
        out += "  " + quote(name) + ": ";
        if (const auto* i = std::get_if<int64_t>(&v)) out += std::to_string(*i);
        else if (const auto* s = std::get_if<std::string>(&v)) out += quote(*s);
        else {
            const auto& l = std::get<std::vector<std::string>>(v);
            out += "[";
            for (size_t k = 0; k < l.size(); ++k) out += (k ? ", " : "") + quote(l[k]);
            out += "]";
        }
        out += ++n < values.size() ? ",\n" : "\n";
    }
    return out + "}\n";
}

bool Settings::fromText(const std::string& text, std::map<std::string, Value>& values) {
    JsonParser p{text};
    const Json root = p.value();
    if (!p.ok || root.type != Json::Type::Object) return false;
    std::map<std::string, Value> out;
    for (const auto& [name, v] : root.fields) {
        if (v.type == Json::Type::Number && v.n == std::floor(v.n) && std::abs(v.n) < 9.0e15) {
            out[name] = int64_t(v.n);
        } else if (v.type == Json::Type::String) {
            out[name] = v.s;
        } else if (v.type == Json::Type::Array) {
            std::vector<std::string> l;
            for (const Json& item : v.items)
                if (item.type == Json::Type::String) l.push_back(item.s);
            out[name] = std::move(l);
        }  // (other values: skipped, as if not there)
    }
    values = std::move(out);
    return true;
}

}  // namespace stardraft
