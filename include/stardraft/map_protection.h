#pragma once

#include <string>

#include "stardraft/chk.h"

namespace stardraft {

// Map protection: the tricks that a map's author uses so that editors do not open the map, or do not save it. A
// protected map opens, but it is view only: the editor does not save it, export it or copy from it (Editor::save).
//
// The rules are Chkdraft's (https://github.com/TheNitesWhoSay/Chkdraft, MIT license, by TheNitesWhoSay: Scenario::parse
// in src/mapping_core/scenario.cpp and MapFile's scenario read in src/mapping_core/map_file.cpp), used with permission:
//   - the scenario is only in the copy of another locale (language) in the map's archive (the default one is missing or
//     does not read);
//   - a section's negative size (a jump) goes to before the start of the scenario;
//   - the scenario ends in part of a section header (1 to 7 bytes);
//   - a section of a fixed size is larger than that size (MRGN: not a whole number of locations);
//   - an MTXM section is larger than an MTXM before it;
//   - the scenario's version (VER) is newer than StarCraft: Remastered's (206).
// Also: a jump that comes back to a place read before (a loop).
//
// The return value: empty when the map is not protected, else the first rule that it breaks (English, for the log).
std::string mapProtection(const Chk& chk, const ParseReport& report, bool otherLocale);

}  // namespace stardraft
