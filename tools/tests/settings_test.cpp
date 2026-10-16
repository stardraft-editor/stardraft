// The settings file (stardraft/settings.h): values of each kind are kept, written and read again, names by prefix, a
// bad file reads as empty. Needs no game data.
//
// usage: settings_test <scratch dir>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

#include "stardraft/settings.h"

using namespace stardraft;
namespace fs = std::filesystem;

namespace {

int failures = 0;
const std::string kMap = "C:\\Maps\\\xEB\xA7\xB5 \"one\".scx";  // (a Korean letter in UTF-8)

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok   " : "FAIL ", what.c_str());
    if (!ok) ++failures;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: settings_test <scratch dir>\n"), 2;
    const fs::path dir = fs::path(argv[1]) / "settings test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path file = dir / "StarDraft" / "settings.json";
    {
        Settings s(file);
        check(!s.has("Volume") && !fs::exists(file), "no file: no values, and nothing is written");
        check(s.set("Layout/Volume", int64_t(80)) && fs::exists(file), "a number is written (the folder is made)");
        s.set("Language", std::string("ko"));
        s.set("RecentMaps", std::vector<std::string>{kMap, "D:\\two.scm"});
        s.set("Layout/LeftWidth", int64_t(-5));
        s.set("Big", int64_t(1) << 40);
        s.set("DataPins/units", std::string("0,7,12"));
        s.set("DataPins/weapons", std::string(""));
        check(s.lastError().empty(), "every write succeeds");
    }
    {
        Settings s(file);
        check(s.number("Layout/Volume") == 80 && s.number("Layout/LeftWidth") == -5 && s.number("Big") == int64_t(1) << 40,
              "numbers read back");
        check(s.text("Language") == std::string("ko") && s.text("DataPins/weapons") == std::string(), "texts read back");
        const auto l = s.list("RecentMaps");
        check(l && l->size() == 2 && (*l)[0] == kMap && (*l)[1] == "D:\\two.scm",
              "a list reads back (non-ASCII, quotes and backslashes)");
        check(!s.number("Language") && !s.text("Layout/Volume") && !s.list("Language"), "a value of another kind is not given");
        const auto pins = s.names("DataPins/");
        check(pins.size() == 2 && pins[0] == "DataPins/units" && pins[1] == "DataPins/weapons", "names by prefix");
        check(s.remove("Language") && !s.has("Language"), "a value is removed");
        const auto before = fs::last_write_time(file);
        check(s.set("Layout/Volume", int64_t(80)) && fs::last_write_time(file) == before, "the same value writes nothing");
    }
    {
        Settings s(file);
        check(!s.has("Language") && s.has("Layout/Volume"), "the removal is in the file");
    }
    {  // two programs with the file: each change keeps what the other saved
        Settings a(file), b(file);
        a.set("Theme", int64_t(1));
        b.set("Pins", std::string("3"));
        a.set("Volume", int64_t(5));
        b.remove("Nothing");
        Settings c(file);
        check(c.number("Theme") == 1 && c.text("Pins") == std::string("3") && c.number("Volume") == 5, "two programs' changes both stay");
    }
    {
        std::ofstream(file, std::ios::binary) << "{ not json";
        Settings s(file);
        check(s.names().empty() && !s.readError().empty(), "a file that cannot be read has no values, and says so");
        check(s.set("Layout/Volume", int64_t(10)) && Settings(file).number("Layout/Volume") == 10, "and is written again on a change");
        std::ifstream kept(fs::path(file) += ".unreadable", std::ios::binary);
        const std::string before((std::istreambuf_iterator<char>(kept)), {});
        check(before == "{ not json" && s.readError().find("settings.json.unreadable") != std::string::npos,
              "the change keeps a copy of the file that could not be read (its values are not lost for good)");
    }
    {  // a hand edit saved with Notepad's "UTF-8 with BOM": the file's values are read
        std::ofstream(file, std::ios::binary) << "\xEF\xBB\xBF{\n  \"Language\": \"ko\",\n  \"RecentMaps\": [\"a.scx\"]\n}\n";
        Settings s(file);
        check(s.text("Language") == std::string("ko") && s.list("RecentMaps") && s.readError().empty(), "a file with a UTF-8 mark at its start is read");
        s.set("Theme", int64_t(2));
        check(Settings(file).text("Language") == std::string("ko"), "a change keeps the values of a file with a UTF-8 mark");
    }
    {  // values of other kinds (as a newer version writes them) stay in the file after a change
        std::ofstream(file, std::ios::binary) << "{\"Volume\": 0.75, \"Layout/Window\": {\"x\": 10, \"y\": 20}, \"Sound\": true, "
                                                 "\"Mixed\": [\"a\", 2], \"Language\": \"es\"}";
        Settings s(file);
        check(s.set("Theme", int64_t(1)), "a change in a file with values of other kinds");
        std::map<std::string, Settings::Value> values;
        std::map<std::string, std::string> other;
        std::string text;
        {
            std::ifstream in(file, std::ios::binary);  // (closed before the next change: Windows replaces no open file)
            text.assign(std::istreambuf_iterator<char>(in), {});
        }
        check(Settings::fromText(text, values, &other) && other.size() == 4 && other["Volume"] == "0.75" && other["Sound"] == "true" &&
                  other["Layout/Window"] == "{\"x\": 10, \"y\": 20}" && other["Mixed"] == "[\"a\", 2]" && values.size() == 2,
              "the values of other kinds stay as they were: " + text);
        check(s.set("Volume", int64_t(5)) && Settings(file).number("Volume") == 5, "a value of another kind that this version sets takes the new value");
    }
    fs::remove_all(dir, ec);
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
