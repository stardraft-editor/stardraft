#pragma once

// Map string table (STR / STRx sections). String ids are 1-based; 0 means "no string".

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

class StringTable {
public:
    // Reads STRx if present, else STR. Text bytes are kept as-is (encoding is unspecified).
    static StringTable read(const Chk& chk);
    // Writes STR when everything fits its 16-bit offsets, else STRx; removes the other section.
    void write(Chk& chk) const;

    size_t size() const { return strings_.size(); }
    const std::string& get(uint32_t id) const;
    void set(uint32_t id, std::string text);
    // Store `text` in the first empty slot (or a new one); returns its id.
    uint32_t add(std::string_view text);
    // The strings in a new order: string `order[k]` gets id k + 1 (each id of 1 to size() once; else nothing changes).
    bool reorder(const std::vector<uint32_t>& order);
    // The size of the section that write() makes: STR, or STRx (a table that is STRx, or more than 65535 bytes in STR).
    size_t strBytes() const;
    bool extended() const { return extended_; }

private:
    std::vector<std::string> strings_;  // strings_[i] is string id i + 1
    bool extended_ = false;
};

// --- Where the map uses its strings -------------------------------------------------------------------------------
// Each field of the map that holds a string id (mapStringFields): the one list that "Used by" (mapStringUses) and the
// new ids of a reorder (renumberMapStrings) both read, so they always agree.
//   SPRP: the scenario's name and description (u16). FORC: the 4 force names (u16 at 8). UNIx and UNIS: the unit names
//   (u16 at 3192; both sections get the new ids, the uses come from the one the game reads). MRGN: the location names
//   (u16 at 16 of each 20 bytes). SWNM: the 256 switch names (u32). WAV: the 512 sound files (u32). TRIG: the text and
//   the sound (u32 at 4 and 8) of each action whose type has them (classicActionDef). MBRF: the text and the sound of
//   each briefing action that is not None.
enum class StringUseKind : uint8_t {
    ScenarioName, ScenarioDescription, ForceName, UnitName, LocationName, SwitchName, SoundFile, TriggerText, TriggerSound,
    BriefingText, BriefingSound,
};
struct StringUse {
    StringUseKind kind = StringUseKind::ScenarioName;
    uint32_t id = 0;   // the string (1 and up)
    int index = 0;     // the force, unit type, location, switch, WAV slot, trigger or briefing trigger (0-based)
    int action = -1;   // a trigger's or a briefing's action (0-based); -1 for the others
    friend bool operator==(const StringUse&, const StringUse&) = default;
};
// The uses of every string, in the map's order of the fields above (string 0, "none", is no use).
std::vector<StringUse> mapStringUses(const Chk& chk);
// Every string id in the map's fields goes to newId[id] (an id that is 0 or not in the vector stays as it is). False,
// with nothing changed, when a new id does not fit its field (more than 65535 in a u16 field).
bool renumberMapStrings(Chk& chk, const std::vector<uint32_t>& newId);

}  // namespace stardraft
