#include "stardraft/map_protection.h"

#include <cstring>

namespace stardraft {

namespace {

// The sections of a fixed size, as Chkdraft reads them (its MapData structs): it reads this many bytes and skips the rest.
struct FixedSize {
    const char* name;
    size_t size;
};
constexpr FixedSize kFixed[] = {
    {"VER ", 2},    {"IVER", 2},    {"IVE2", 2},    {"VCOD", 1040}, {"IOWN", 12},   {"OWNR", 12},   {"ERA ", 2},
    {"DIM ", 4},    {"SIDE", 12},   {"PUNI", 5700}, {"UPGR", 1748}, {"PTEC", 912},  {"UNIS", 4048}, {"UPGS", 598},
    {"TECS", 216},  {"UNIx", 4168}, {"UPGx", 794},  {"TECx", 396},  {"PUPx", 2318}, {"PTEx", 1672}, {"COLR", 8},
    {"CRGB", 32},   {"SPRP", 4},    {"FORC", 20},   {"WAV ", 2048}, {"SWNM", 1024}, {"UPRP", 1280}, {"UPUS", 64},
    {"TYPE", 4},
};

constexpr uint16_t kRemasteredVersion = 206;
constexpr size_t kLocationSize = 20;  // (MRGN)

}  // namespace

std::string mapProtection(const Chk& chk, const ParseReport& report, bool otherLocale) {
    if (otherLocale) return "the scenario is only in the copy of another locale";
    if (report.jumpBeforeStart) return "a section jumps to before the start of the scenario";
    if (report.loopDetected) return "a section jumps back to a place read before";
    if (report.trailingBytes) return "the scenario ends in part of a section header";
    size_t mtxm = 0;
    bool hadMtxm = false;
    for (const Section& s : chk.sections) {
        for (const FixedSize& f : kFixed)
            if (std::memcmp(s.name.c.data(), f.name, 4) == 0 && s.data.size() > f.size)
                return "the " + s.name.str() + " section is larger than its size (" + std::to_string(s.data.size()) + " bytes, not " +
                       std::to_string(f.size) + ")";
        if (s.name == SectionName("MRGN") && s.data.size() % kLocationSize)
            return "the MRGN section is not a whole number of locations";
        if (s.name == SectionName("MTXM")) {
            const size_t tiles = (s.data.size() + 1) / 2;
            if (hadMtxm && tiles > mtxm) return "an MTXM section is larger than the MTXM before it";
            if (!hadMtxm) mtxm = tiles, hadMtxm = true;
        }
    }
    if (const Section* v = chk.find(SectionName("VER ")); v && v->data.size() >= 2) {
        uint16_t version;
        std::memcpy(&version, v->data.data(), 2);
        if (version > kRemasteredVersion) return "the scenario's version (" + std::to_string(version) + ") is newer than StarCraft: Remastered's";
    }
    return {};
}

}  // namespace stardraft
