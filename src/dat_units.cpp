#include "stardraft/dat_units.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace stardraft {

namespace {

constexpr double kFastest = 24.0;  // frames each second at Fastest

struct UnitRow {
    const char* dat;
    const char* field;
    DatUnit unit;
};

// The fields with a conversion. A change here: the field's help page (docs/help/<dat>-dat.md) says the same.
const UnitRow kRows[] = {
    {"units", "Hit Points", {"1/256 hit points", "hit points", 1.0 / 256}},
    {"units", "Build Time", {"frames", "s (fastest)", 1.0 / kFastest}},
    {"units", "Sight Range", {"tiles", "pixels", 32}},
    {"units", "Target Acquisition Range", {"tiles", "pixels", 32}},
    {"units", "Supply Provided", {"half supply", "supply", 0.5}},
    {"units", "Supply Required", {"half supply", "supply", 0.5}},
    {"weapons", "Minimum Range", {"pixels", "tiles", 1.0 / 32}},
    {"weapons", "Maximum Range", {"pixels", "tiles", 1.0 / 32}},
    {"weapons", "Inner Splash Range", {"pixels", "tiles", 1.0 / 32}},
    {"weapons", "Medium Splash Range", {"pixels", "tiles", 1.0 / 32}},
    {"weapons", "Outer Splash Range", {"pixels", "tiles", 1.0 / 32}},
    {"weapons", "Weapon Cooldown", {"frames", "s (fastest)", 1.0 / kFastest}},
    {"weapons", "Remove After", {"frames", "s (fastest)", 1.0 / kFastest}},
    {"weapons", "Attack Angle", {"1/256 of a full turn", "degrees", 360.0 / 256}},
    {"weapons", "Launch Spin", {"1/256 of a full turn", "degrees", 360.0 / 256}},
    {"flingy", "Speed", {"1/256 pixels per frame", "pixels/s (fastest)", kFastest / 256}},
    {"flingy", "Acceleration", {"1/256 pixels per frame, each frame", "pixels/s² (fastest)", kFastest * kFastest / 256}},
    {"flingy", "Halt Distance", {"1/256 pixels", "pixels", 1.0 / 256}},
    {"flingy", "Turn Radius", {"1/256 of a full turn per frame", "degrees/s (fastest)", 360.0 / 256 * kFastest}},
    {"upgrades", "Research Time Base", {"frames", "s (fastest)", 1.0 / kFastest}},
    {"upgrades", "Research Time Factor", {"frames", "s (fastest)", 1.0 / kFastest}},
    {"techdata", "Resarch Time", {"frames", "s (fastest)", 1.0 / kFastest}},
};

}  // namespace

DatUnit datFieldUnit(const std::string& dat, const std::string& field) {
    for (const UnitRow& r : kRows)
        if (dat == r.dat && field == r.field) return r.unit;
    return {};
}

std::string datConvertedText(const std::string& dat, const std::string& field, uint32_t raw) {
    const DatUnit u = datFieldUnit(dat, field);
    if (!*u.unit) return "";
    const double v = double(raw) * u.factor;
    char b[64];
    std::snprintf(b, sizeof b, "%.3f", std::round(v * 1000) / 1000);
    std::string s = b;  // (no zeros at the end, and no point when it is a whole number)
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return "= " + s + " " + u.unit;
}

}  // namespace stardraft
