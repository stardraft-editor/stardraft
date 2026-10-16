#pragma once

// The program's settings in a file (a JSON object), for every system: numbers, texts and lists of texts by name.
// Names may have parts ("Layout/Volume"). Each change reads the file again (another program may have saved values),
// then writes it at once (it is small); a file that cannot be read is the same as no file. Texts are UTF-8.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace stardraft {

class Settings {
public:
    using Value = std::variant<int64_t, std::string, std::vector<std::string>>;

    // Reads the file (if it is there). Nothing is written until a value changes.
    explicit Settings(std::filesystem::path file);

    const std::filesystem::path& file() const { return file_; }

    std::optional<int64_t> number(const std::string& name) const;
    std::optional<std::string> text(const std::string& name) const;
    std::optional<std::vector<std::string>> list(const std::string& name) const;
    bool has(const std::string& name) const { return values_.count(name) != 0; }
    // The names that start with `prefix` (all names when it is empty), in order.
    std::vector<std::string> names(const std::string& prefix = {}) const;

    // Each set or remove writes the file; false (and `lastError`) when the write failed. A value the same as before
    // writes nothing.
    bool set(const std::string& name, Value value);
    bool remove(const std::string& name);
    const std::string& lastError() const { return error_; }

    // The file's text for these values (sorted by name), and the values in a text; false for a text that is not a
    // JSON object. `foreign`: the values of other kinds (a fraction, true, an object, a list with other items, as a
    // newer version can write them), as their JSON text: fromText gives them, toText writes them back unchanged.
    static std::string toText(const std::map<std::string, Value>& values, const std::map<std::string, std::string>* foreign = nullptr);
    static bool fromText(const std::string& text, std::map<std::string, Value>& values, std::map<std::string, std::string>* foreign = nullptr);

    // The values as the file has them now. A file that is not JSON gives no values; the first save then keeps a copy
    // of it beside it (settings.json.unreadable), and readError says so (empty: the file was read).
    void reload();
    const std::string& readError() const { return readError_; }

private:
    bool save();
    std::filesystem::path file_;
    std::map<std::string, Value> values_;
    std::map<std::string, std::string> foreign_;  // the values of other kinds, as their JSON text (kept unchanged)
    bool unreadable_ = false;
    std::string error_, readError_;
};

}  // namespace stardraft
