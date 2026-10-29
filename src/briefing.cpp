#include "stardraft/briefing.h"

#include <algorithm>

#include "stardraft/strings.h"

namespace stardraft {

namespace {
constexpr size_t kTrigger = 2400, kConditions = 16 * 20, kAction = 32, kActions = 64;
uint32_t u32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }
void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
}  // namespace

uint32_t BriefingAction::duration() const {
    if (disabled()) return 0;
    if (type == Wait) return time;
    if (type == Transmission) {
        if (modifier == Add) return wavTime + time;
        if (modifier == Subtract) return wavTime > time ? wavTime - time : 0;
        return time;
    }
    return 0;
}

std::vector<BriefingTrigger> readBriefings(const Chk& chk, const StringTable& strings) {
    std::vector<BriefingTrigger> out;
    const Bytes m = chk.effective("MBRF");
    auto str = [&](uint32_t id) { return id && id < strings.size() + 1 ? strings.get(id) : std::string(); };
    for (size_t t = 0; t + kTrigger <= m.size(); t += kTrigger) {
        BriefingTrigger bt;
        for (size_t a = 0; a < kActions; ++a) {
            const uint8_t* p = &m[t + kConditions + a * kAction];
            BriefingAction ba;
            ba.type = p[26];
            if (ba.type == BriefingAction::None) continue;
            ba.text = str(u32(p + 4)), ba.wav = str(u32(p + 8));
            ba.time = u32(p + 12);
            ba.slot = int(u32(p + 16));
            ba.wavTime = 0;
            if (ba.type == BriefingAction::Transmission) ba.wavTime = u32(p + 12), ba.time = u32(p + 20);  // the WAV's length; the amount
            ba.unit = uint16_t(p[24] | p[25] << 8);
            ba.modifier = p[27];
            ba.flags = p[28];
            if (ba.type == BriefingAction::PlayWav) ba.wavTime = ba.time;  // Play WAV keeps its length in the time field
            bt.actions.push_back(std::move(ba));
        }
        const uint8_t* pl = &m[t + kConditions + kActions * kAction + 4];
        for (size_t i = 0; i < 27; ++i) bt.players[i] = pl[i] != 0;
        out.push_back(std::move(bt));
    }
    return out;
}

void writeBriefings(Chk& chk, StringTable& strings, const std::vector<BriefingTrigger>& briefings) {
    auto id = [&](const std::string& s) -> uint32_t {
        if (s.empty()) return 0;
        for (uint32_t i = 1; i <= strings.size(); ++i)
            if (strings.get(i) == s) return i;
        return strings.add(s);
    };
    Bytes m(briefings.size() * kTrigger, 0);
    Bytes wav = chk.effective("WAV ");
    wav.resize(512 * 4, 0);
    auto registerWav = [&](uint32_t sid) {  // the WAV section lists every sound file the map holds
        if (!sid) return;
        for (size_t i = 0; i < 512; ++i)
            if (u32(&wav[4 * i]) == sid) return;
        for (size_t i = 0; i < 512; ++i)
            if (!u32(&wav[4 * i])) return put32(&wav[4 * i], sid);
    };
    for (size_t t = 0; t < briefings.size(); ++t) {
        uint8_t* base = &m[t * kTrigger];
        base[15] = 13;  // the one condition: Mission Briefing
        const auto& acts = briefings[t].actions;
        for (size_t a = 0; a < acts.size() && a < kActions; ++a) {
            const BriefingAction& ba = acts[a];
            uint8_t* p = base + kConditions + a * kAction;
            put32(p + 4, id(ba.text));
            const uint32_t w = id(ba.wav);
            registerWav(w);
            put32(p + 8, w);
            const bool sound = ba.type == BriefingAction::PlayWav || ba.type == BriefingAction::Transmission;
            put32(p + 12, sound ? ba.wavTime : ba.time);  // a sound's length goes in the time field
            put32(p + 16, uint32_t(ba.slot));
            put32(p + 20, ba.type == BriefingAction::Transmission ? ba.time : 0);  // the transmission's amount
            p[24] = uint8_t(ba.unit), p[25] = uint8_t(ba.unit >> 8);
            p[26] = ba.type;
            p[27] = ba.modifier;
            p[28] = ba.flags;
        }
        uint8_t* pl = base + kConditions + kActions * kAction + 4;
        for (size_t i = 0; i < 27; ++i) pl[i] = briefings[t].players[i] ? 1 : 0;
    }
    strings.write(chk);
    chk.set("MBRF", std::move(m));
    chk.set("WAV ", std::move(wav));
}

bool briefingRunsFor(const BriefingTrigger& t, int player, const std::array<int, 8>& forces) {
    if (player < 0 || player >= 8) return false;
    if (t.players[size_t(player)] || t.players[17]) return true;
    const int f = forces[size_t(player)];
    return f >= 0 && f < 4 && t.players[size_t(18 + f)];
}

std::vector<BriefingEvent> briefingTimeline(const std::vector<BriefingTrigger>& briefings, int player, const std::array<int, 8>& forces,
                                            uint32_t* total) {
    std::vector<BriefingEvent> out;
    uint32_t t = 0;
    for (size_t ti = 0; ti < briefings.size(); ++ti) {
        if (!briefingRunsFor(briefings[ti], player, forces)) continue;
        const auto& acts = briefings[ti].actions;
        for (size_t ai = 0; ai < acts.size(); ++ai) {
            const BriefingAction& a = acts[ai];
            if (a.disabled()) continue;
            uint32_t len = a.duration();
            if (a.type == BriefingAction::PlayWav) len = a.wavTime;
            if (a.type == BriefingAction::SpeakingPortrait || a.type == BriefingAction::TextMessage) len = a.time;  // (a text shows for its time)
            out.push_back({t, len, int(ti), int(ai), &a});
            t += a.duration();
        }
    }
    uint32_t end = t;
    for (const auto& e : out) end = std::max(end, e.at + e.length);
    if (total) *total = end;
    return out;
}

BriefingState briefingStateAt(const std::vector<BriefingEvent>& timeline, uint32_t t) {
    BriefingState s;
    for (const BriefingEvent& e : timeline) {
        if (e.at > t) break;
        const BriefingAction& a = *e.a;
        const bool now = t < e.at + e.length;
        const int slot = std::clamp(a.slot, 0, 3);
        switch (a.type) {
        case BriefingAction::Objectives: s.objectives = a.text; break;
        case BriefingAction::TextMessage:  // (shown for its time, then the box is empty; a time of 0: until the next text)
            if (a.time && t >= e.at + a.time) s.text.clear(), s.textSlot = -1;
            else s.text = a.text, s.textSlot = -1, s.textSince = e.at, s.textFor = a.time;
            break;
        case BriefingAction::ShowPortrait: s.portraits[size_t(slot)] = {true, a.unit, false, e.at}; break;
        case BriefingAction::HidePortrait: s.portraits[size_t(slot)] = {}; break;
        case BriefingAction::SpeakingPortrait:
            if (now) s.portraits[size_t(slot)].speaking = true, s.portraits[size_t(slot)].talkSince = e.at;
            break;
        case BriefingAction::Transmission:
            s.text = a.text, s.textSlot = slot, s.textSince = e.at, s.textFor = e.length;
            if (now) s.portraits[size_t(slot)].speaking = true, s.portraits[size_t(slot)].talkSince = e.at;
            if (now && !a.wav.empty()) s.sounds.push_back(&e);
            break;
        case BriefingAction::PlayWav:
            if (now && !a.wav.empty()) s.sounds.push_back(&e);
            break;
        }
    }
    // A speaking state ends when its event does: take back the ones that ended.
    for (auto& p : s.portraits) p.speaking = p.speaking && p.shown;
    return s;
}

}  // namespace stardraft
