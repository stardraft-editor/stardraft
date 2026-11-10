// Hyperion maps (src/mods/hyperion.cpp). For each .hxm map: the compile gives the bytes Hyperion's does (`Hyperion.exe
// --hxm-compile`), and the map converts to .hxm and back to the same CHK, which Hyperion's check passes as it passes
// the map itself. An unpacked .hxm (a folder) is read too (Hyperion's compile reads only the zip). For each classic
// map (.scm, .scx, .chk): the map converts to .hxm and back, the sections the game reads come back the same (the text
// ones with the same document), a second conversion gives the same CHK, and Hyperion's check passes it.
//
// usage: hyperion_test <Hyperion.exe, or "" without it> <maps...>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/map_file.h"
#include "stardraft/mods/hyperion.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

using namespace stardraft;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    std::fflush(stdout);
    if (!ok) ++failures;
}

static Bytes readBytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static bool writeBytes(const fs::path& p, const Bytes& b) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return bool(out);
}

// Runs Hyperion.exe with the arguments (each quoted); its exit code.
static int runHyperion(const fs::path& exe, const std::vector<fs::path>& args, const char* option) {
#ifdef _WIN32
    std::wstring cmd = L"\"" + exe.wstring() + L"\" " + std::wstring(option, option + std::strlen(option));
    for (const fs::path& a : args) cmd += L" \"" + a.wstring() + L"\"";
    cmd = L"\"" + cmd + L"\"";  // (cmd.exe /c takes the outer quotes off)
    return _wsystem(cmd.c_str());
#else
    std::string cmd = "\"" + exe.string() + "\" " + option;
    for (const fs::path& a : args) cmd += " \"" + a.string() + "\"";
    const int r = std::system(cmd.c_str());
    return r < 0 ? r : (r >> 8) & 0xFF;
#endif
}

// A settings section as the game reads it: the values of the entries that use the defaults (which StarEdit fills in)
// and the units' name ids (the string table's numbers) left out, and the weapons when no unit has settings of its own.
static Bytes settingsOnly(const std::string& tag, Bytes b) {
    auto clear = [&](size_t at, size_t n) { std::fill(b.begin() + std::ptrdiff_t(at), b.begin() + std::ptrdiff_t(at + n), uint8_t(0)); };
    if (tag == "UPGS" || tag == "UPGx" || tag == "TECS" || tag == "TECx") {
        const bool upg = tag[1] == 'P', bw = tag[3] == 'x';
        const size_t n = upg ? (bw ? 61 : 46) : (bw ? 44 : 24), base = upg && bw ? 62 : n, fields = upg ? 6 : 4;
        if (b.size() != base + n * fields * 2) return b;
        if (base > n) clear(n, base - n);
        for (size_t k = 0; k < n; ++k)
            if (b[k]) {
                b[k] = 1;
                for (size_t f = 0; f < fields; ++f) clear(base + f * n * 2 + k * 2, 2);
            }
    } else if (tag == "UNIS" || tag == "UNIx") {
        const size_t nWeap = tag == "UNIx" ? 130 : 100;
        if (b.size() != 0xE40 + nWeap * 4) return b;
        bool any = false;
        for (size_t k = 0; k < 228; ++k) {
            clear(0xC78 + k * 2, 2);
            if (!b[k]) {
                any = true;
                continue;
            }
            b[k] = 1;
            clear(0xE4 + k * 4, 4), clear(0x474 + k * 2, 2), clear(0x63C + k, 1), clear(0x720 + k * 2, 2);
            clear(0x8E8 + k * 2, 2), clear(0xAB0 + k * 2, 2);
        }
        if (!any) clear(0xE40, nWeap * 4);
    }
    return b;
}

// A section as the game reads it: MTXM and MASK copy over copy, the others as Chk::effective gives them.
static Bytes gameReads(const Chk& chk, SectionName n) {
    if (n.str() != "MTXM" && n.str() != "MASK") return chk.effective(n);
    Bytes out;
    for (const Section& s : chk.sections)
        if (s.name == n) {
            if (out.size() < s.data.size()) out.resize(s.data.size());
            std::copy(s.data.begin(), s.data.end(), out.begin());
        }
    return out;
}

// The first section that differs between two CHKs, as the game reads them, among `tags`; "" when none.
static std::string differs(const Chk& a, const Chk& b, std::initializer_list<const char*> tags) {
    for (const char* t : tags) {
        SectionName n;
        std::copy(t, t + 4, n.c.begin());
        if (a.has(n) && settingsOnly(t, gameReads(a, n)) != settingsOnly(t, gameReads(b, n))) return t;
    }
    return {};
}

// The document without its compat block (string counts and versions change with the string table).
static std::string withoutCompat(const std::string& json) {
    const size_t at = json.find("\n \"compat\": {");
    if (at == std::string::npos) return json;
    const size_t end = json.find("\n }", at);
    return json.substr(0, at) + json.substr(end + 3);
}

static int run(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: hyperion_test <Hyperion.exe, or \"\"> <maps...>\n"), 2;
    std::vector<fs::path> args(argv, argv + argc);
#ifdef _WIN32
    if (wchar_t** w = CommandLineToArgvW(GetCommandLineW(), &argc)) {  // (map names in any language)
        args.assign(w, w + argc);
        LocalFree(w);
    }
#endif
    const fs::path exe = args[1];
    const bool haveExe = !exe.empty();
    const fs::path tmp = fs::temp_directory_path() / "stardraft_hyperion_test";
    fs::create_directories(tmp);
    std::string err;

    check(hyperion::isHxm("a/b.HXM") && hyperion::isHxm("x.hxm") && !hyperion::isHxm("x.scx"), "isHxm");

    for (int i = 2; i < argc; ++i) {
        const fs::path map = args[size_t(i)];
        const std::u8string u8name = map.filename().u8string();
        const std::string name(u8name.begin(), u8name.end());
        hyperion::HxmMap first;
        Bytes original;  // a classic map's own CHK
        const bool folder = fs::is_directory(map);  // (an unpacked .hxm: Hyperion reads only the zip)
        if (hyperion::isHxm(map) || folder) {
            if (!hyperion::readHxmFile(map, first, err)) {
                check(false, name + ": reads (" + err + ")");
                continue;
            }
            if (haveExe && !folder) {
                const fs::path out = tmp / "hyperion.chk";
                fs::remove(out);
                const int code = runHyperion(exe, {map, out}, "--hxm-compile");
                const Bytes theirs = readBytes(out);
                check(code == 0 && theirs == first.chk,
                      name + ": the compile equals Hyperion's (" + std::to_string(first.chk.size()) + " bytes, Hyperion's " +
                          std::to_string(theirs.size()) + ", exit " + std::to_string(code) + ")");
                if (theirs != first.chk && !theirs.empty()) {
                    const Chk a = Chk::parse(first.chk), b = Chk::parse(theirs);
                    for (size_t k = 0; k < std::max(a.sections.size(), b.sections.size()); ++k) {
                        const std::string ta = k < a.sections.size() ? a.sections[k].name.str() : "-";
                        const std::string tb = k < b.sections.size() ? b.sections[k].name.str() : "-";
                        const size_t sa = k < a.sections.size() ? a.sections[k].data.size() : 0, sb = k < b.sections.size() ? b.sections[k].data.size() : 0;
                        if (ta != tb || sa != sb || (k < a.sections.size() && k < b.sections.size() && a.sections[k].data != b.sections[k].data))
                            std::printf("      section %zu: ours %s %zu, Hyperion's %s %zu\n", k, ta.c_str(), sa, tb.c_str(), sb);
                    }
                }
            }
        } else {
            std::vector<ArchiveFile> files;
            if (map.extension() == ".chk") {
                original = readBytes(map);
            } else if (!readMapFile(map, original, err) || !readMapFiles(map, files, err)) {
                check(false, name + ": reads (" + err + ")");
                continue;
            }
            first.chk = original;
            first.files = files;
        }

        // to .hxm and back, twice
        Bytes zip1, zip2;
        hyperion::HxmMap back1, back2;
        std::vector<std::string> lost;
        const bool ok1 = hyperion::writeHxm(first, zip1, err, &lost) && hyperion::readHxm(zip1, back1, err);
        for (const std::string& l : lost) std::printf("note  %s: %s\n", name.c_str(), l.c_str());
        check(ok1, name + ": converts to .hxm and back" + (ok1 ? "" : " (" + err + ")"));
        if (!ok1) continue;
        const bool ok2 = hyperion::writeHxm(back1, zip2, err) && hyperion::readHxm(zip2, back2, err);
        check(ok2, name + ": converts again" + (ok2 ? "" : " (" + err + ")"));
        if (!ok2) continue;
        if (original.empty()) {
            check(back1.chk == first.chk, name + ": hxm -> CHK -> hxm -> CHK gives the same CHK");
        } else {
            const Chk a = Chk::parse(original), b = Chk::parse(back1.chk);
            const std::string d = differs(a, b, {"TYPE", "IVER", "IVE2", "VCOD", "IOWN", "OWNR", "ERA ", "DIM ", "SIDE", "MTXM",
                                                 "PUNI", "UPGR", "PTEC", "UNIT", "ISOM", "TILE", "DD2 ", "THG2", "MASK", "UPRP",
                                                 "UPUS", "COLR", "CRGB", "PUPx", "PTEx", "UPGS", "TECS", "UPGx", "TECx", "UNIS", "UNIx"});
            check(d.empty(), name + ": the game's sections come back the same" + (d.empty() ? "" : " (" + d + " differs)"));
            std::string j1, j2;
            std::vector<std::pair<std::string, Bytes>> l1, l2;
            hyperion::chkToDocument(original, {}, j1, l1, err);
            hyperion::chkToDocument(back1.chk, {}, j2, l2, err);
            if (lost.empty())
                check(withoutCompat(j1) == withoutCompat(j2) && l1 == l2, name + ": the text sections come back with the same document");
        }
        // (the document may differ in compat: a map whose string table grew past STR's gets the VER that STRx needs)
        check(back2.chk == back1.chk, name + ": a second conversion gives the same CHK");
        if (back2.chk != back1.chk) writeBytes(tmp / "differs1.hxm", zip1), writeBytes(tmp / "differs2.hxm", zip2);  // (to compare)
        check(back1.files.size() == first.files.size(), name + ": the map's files come back (" + std::to_string(first.files.size()) + ")");

        if (haveExe) {
            const fs::path written = tmp / "written.hxm";
            writeBytes(written, zip1);
            const int code = runHyperion(exe, {written, tmp / "check.txt"}, "--hxm-check");
            const int want = hyperion::isHxm(map) && !folder ? runHyperion(exe, {map, tmp / "check0.txt"}, "--hxm-check") : 0;
            check(code == want, name + ": Hyperion's check of the written .hxm: exit " + std::to_string(code) + " (want " + std::to_string(want) + ")");
            if (code != want) {
                std::ifstream rep(tmp / "check.txt");
                for (std::string line; std::getline(rep, line);) std::printf("      %s\n", line.c_str());
            }
            const fs::path out = tmp / "hyperion2.chk";
            fs::remove(out);
            const int c2 = runHyperion(exe, {written, out}, "--hxm-compile");
            check(c2 == 0 && readBytes(out) == back1.chk, name + ": Hyperion compiles the written .hxm to the same CHK");
        }
    }
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::printf("FAIL  an exception: %s\n", e.what());
        return 1;
    }
}
