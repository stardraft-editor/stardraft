#pragma once
// Mission briefings (MBRF): triggers in the TRIG layout whose only condition is "Mission Briefing" (13) and whose
// actions are the briefing's own (wait, sound, text, objectives, portraits, transmission). The briefing window plays
// them from the start for each player whose triggers they are. GPU-free.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

class StringTable;

struct BriefingAction {
    enum Type : uint8_t {
        None = 0,
        Wait = 1,              // time
        PlayWav = 2,           // wav, wavTime
        TextMessage = 3,       // text, time
        Objectives = 4,        // text
        ShowPortrait = 5,      // unit, slot
        HidePortrait = 6,      // slot
        SpeakingPortrait = 7,  // slot, time
        Transmission = 8,      // text, slot, wav, wavTime; its length: wavTime set to, plus or minus `time` (modifier)
        SkipTutorial = 9,
    };
    enum Modifier : uint8_t { SetTo = 7, Add = 8, Subtract = 9 };
    uint8_t type = None;
    std::string text, wav;  // the strings themselves (text with the game's control bytes; wav: a path in the map)
    uint32_t time = 0, wavTime = 0;  // milliseconds
    int slot = 0;                    // portrait slot 0-3
    uint16_t unit = 0;
    uint8_t modifier = SetTo;
    uint8_t flags = 0;  // the action's flags byte (bit 1: disabled)
    bool disabled() const { return flags & 2; }
    // How long it holds the briefing (the next action waits): Wait and Transmission; 0 for the others.
    uint32_t duration() const;
    bool operator==(const BriefingAction&) const = default;
};

struct BriefingTrigger {
    std::array<bool, 27> players{};  // the TRIG player list: 0-7 players, 17 all players, 18-21 forces 1-4, ...
    std::vector<BriefingAction> actions;  // up to 64
    bool operator==(const BriefingTrigger&) const = default;
};

// Reads MBRF; strings and WAV paths come from the map's string table.
std::vector<BriefingTrigger> readBriefings(const Chk& chk, const StringTable& strings);
// Writes MBRF (and keeps the strings it uses in the table, and the WAV paths in the WAV section).
void writeBriefings(Chk& chk, StringTable& strings, const std::vector<BriefingTrigger>& briefings);

// Whether a trigger runs for player p (0-7), given each player's force (0-3).
bool briefingRunsFor(const BriefingTrigger& t, int player, const std::array<int, 8>& forces);

// The briefing as player p sees it, in time: what each action does and when. The triggers run one after another
// (a wait in one holds the others, as in the game).
struct BriefingEvent {
    uint32_t at = 0, length = 0;  // ms
    int trigger = 0, action = 0;
    const BriefingAction* a = nullptr;
};
std::vector<BriefingEvent> briefingTimeline(const std::vector<BriefingTrigger>& briefings, int player, const std::array<int, 8>& forces,
                                            uint32_t* total = nullptr);

// What the briefing window shows at time t (ms).
struct BriefingState {
    std::string objectives;
    struct Portrait {
        bool shown = false;
        uint16_t unit = 0;
        bool speaking = false;   // a speaking portrait or a transmission on this slot now
        uint32_t since = 0;      // when it was shown (its animation starts there)
        uint32_t talkSince = 0;  // when it started to talk (the talking clips start there)
    };
    std::array<Portrait, 4> portraits{};
    std::string text;            // the text in the middle (the last text message or transmission)
    int textSlot = -1;           // the slot whose transmission it is
    uint32_t textSince = 0;      // when the text appeared (the box scrolls from then)
    uint32_t textFor = 0;        // how long it shows: the box scrolls through all of it in that time
    std::vector<const BriefingEvent*> sounds;  // the sounds playing at t
};
BriefingState briefingStateAt(const std::vector<BriefingEvent>& timeline, uint32_t t);

}  // namespace stardraft
