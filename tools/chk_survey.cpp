// Survey every .scm/.scx under a directory: parse scenario.chk, tally sections,
// check spec sizes, and verify byte-exact re-serialization and archive writing.
//
// usage: chk_survey <maps dir> [scratch dir for write tests / extracted chks]

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "stardraft/chk.h"
#include "stardraft/map_file.h"

namespace fs = std::filesystem;
using namespace stardraft;

namespace {

// Fixed sizes from the wiki spec (0 = variable).
const std::map<std::string, size_t> kFixedSize = {
    {"VER ", 2},    {"IVER", 2},    {"IVE2", 2},    {"VCOD", 1040}, {"IOWN", 12},   {"OWNR", 12},
    {"ERA ", 2},    {"DIM ", 4},    {"SIDE", 12},   {"PUNI", 5700}, {"UPGR", 1748}, {"PTEC", 912},
    {"UPRP", 1280}, {"UPUS", 64},   {"SPRP", 4},    {"WAV ", 2048}, {"UNIS", 4048}, {"UPGS", 598},
    {"TECS", 216},  {"SWNM", 1024}, {"COLR", 8},    {"CRGB", 32},   {"PUPx", 2318}, {"PTEx", 1672},
    {"UNIx", 4168}, {"UPGx", 794},  {"TECx", 396},  {"TYPE", 4},
};
const std::map<std::string, size_t> kMultipleOf = {
    {"UNIT", 36}, {"THG2", 10}, {"TRIG", 2400}, {"MBRF", 2400}, {"DD2 ", 8}, {"MRGN", 20},
};

std::string printable(const SectionName& n) {
    std::string s = n.str();
    for (char& c : s)
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e) c = '?';
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: chk_survey <maps dir> [scratch dir]\n");
        return 2;
    }
    fs::path root = argv[1];
    fs::path scratch = argc > 2 ? fs::path(argv[2]) : fs::path();
    if (!scratch.empty()) fs::create_directories(scratch / "chk");

    std::map<std::string, int> sectionMaps;  // name -> number of maps containing it
    std::map<int, int> versions;
    int total = 0, readFail = 0, roundTripFail = 0, writeFail = 0, odd = 0;

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".scm" && ext != ".scx") continue;
        ++total;
        std::string rel = fs::relative(entry.path(), root).string();

        Bytes raw;
        std::string err;
        if (!readMapChk(entry.path(), raw, err)) {
            ++readFail;
            std::printf("READ FAIL  %s: %s\n", rel.c_str(), err.c_str());
            continue;
        }

        ParseReport rep;
        Chk chk = Chk::parse(raw, &rep);
        if (rep.truncatedSection || rep.trailingBytes || rep.negativeSize || rep.loopDetected) {
            ++odd;
            std::printf("ODD        %s:%s%s%s%s\n", rel.c_str(), rep.truncatedSection ? " truncated" : "",
                        rep.trailingBytes ? " trailing" : "", rep.negativeSize ? " negative-size" : "",
                        rep.loopDetected ? " loop" : "");
        }

        std::map<std::string, int> seen;
        for (const auto& s : chk.sections) {
            std::string n = printable(s.name);
            if (seen[n]++ == 0) ++sectionMaps[n];
            if (auto f = kFixedSize.find(n); f != kFixedSize.end() && s.data.size() != f->second)
                std::printf("SIZE       %s: %s is %zu, spec %zu\n", rel.c_str(), n.c_str(), s.data.size(),
                            f->second);
            if (auto m = kMultipleOf.find(n); m != kMultipleOf.end() && s.data.size() % m->second)
                std::printf("SIZE       %s: %s is %zu, not a multiple of %zu\n", rel.c_str(), n.c_str(),
                            s.data.size(), m->second);
        }
        for (const auto& [n, c] : seen)
            if (c > 1 && n != "UNIT" && n != "THG2" && n != "TRIG" && n != "MBRF")
                std::printf("DUP        %s: %s x%d\n", rel.c_str(), n.c_str(), c);

        Bytes ver = chk.effective("VER ");
        ++versions[ver.size() == 2 ? ver[0] | (ver[1] << 8) : -1];

        if (chk.serialize() != raw) {
            ++roundTripFail;
            std::printf("ROUNDTRIP  %s: serialize() differs from original\n", rel.c_str());
        }

        if (!scratch.empty()) {
            std::ofstream(scratch / "chk" / (entry.path().stem().string() + ".chk"), std::ios::binary)
                .write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));

            // Rebuild the archive and read everything back.
            fs::path out = scratch / ("rt" + entry.path().extension().string());
            std::vector<ArchiveFile> files, filesBack;
            std::vector<std::string> unreadable;
            Bytes back;
            Bytes written = chk.serialize();
            bool ok = readMapFiles(entry.path(), files, err, {}, &unreadable) && writeMap(out, written, files, err) &&
                      readMapChk(out, back, err) && readMapFiles(out, filesBack, err);
            if (ok && back != written) err = "chk differs after write", ok = false;
            if (ok && filesBack.size() != files.size())
                err = std::to_string(files.size()) + " files in, " + std::to_string(filesBack.size()) + " out",
                ok = false;
            if (!ok) {
                ++writeFail;
                std::printf("WRITE FAIL %s: %s\n", rel.c_str(), err.c_str());
            }
            if (files.size() > 1 || !unreadable.empty()) {
                std::printf("FILES      %s:", rel.c_str());
                for (const auto& f : files) std::printf(" %s(%zu)", f.name.c_str(), f.data.size());
                for (const auto& n : unreadable) std::printf(" %s(UNREADABLE)", n.c_str());
                std::printf("\n");
            }
        }
    }

    std::printf("\n%d maps, %d read failures, %d with oddities, %d round-trip failures, %d write failures\n",
                total, readFail, odd, roundTripFail, writeFail);
    std::printf("\nVER: ");
    for (const auto& [v, c] : versions) std::printf("%d x%d  ", v, c);
    std::printf("\n\nSections (maps containing):\n");
    for (const auto& [n, c] : sectionMaps) std::printf("  %s  %d\n", n.c_str(), c);
    return readFail || roundTripFail || writeFail ? 1 : 0;
}
