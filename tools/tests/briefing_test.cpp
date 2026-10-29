// Reads a campaign map's mission briefing from the game's files, prints its timeline for player 1, and checks that
// writing it back gives the same briefing.
// usage: briefing_test <install dir> [campaign folder, e.g. SD\campaign\Terran\Terran01]
#include <algorithm>
#include <cstdio>
#include <string>

#include "stardraft/briefing.h"
#include "stardraft/game_data.h"
#include "stardraft/map_file.h"
#include "stardraft/strings.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: briefing_test <install dir> [campaign folder]\n"), 2;
    GameData data;
    std::string err;
    if (!data.open(argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const std::string folder = argc > 2 ? argv[2] : "SD\\campaign\\Terran\\Terran01";
    Bytes bytes;
    if (!data.readFile(folder + "\\staredit\\scenario.chk", bytes, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Chk chk = Chk::parse(bytes);
    StringTable strings = StringTable::read(chk);
    const auto b = readBriefings(chk, strings);
    std::printf("%zu briefing triggers\n", b.size());
    std::array<int, 8> forces{};
    uint32_t total = 0;
    const auto tl = briefingTimeline(b, 0, forces, &total);
    static const char* names[] = {"none", "wait", "play wav", "text", "objectives", "show portrait", "hide portrait", "speaking", "transmission", "skip tutorial"};
    for (const auto& e : tl) {
        const BriefingAction& a = *e.a;
        std::string text = a.text.substr(0, 50);
        for (char& c : text)
            if (static_cast<unsigned char>(c) < 0x20) c = '.';
        std::printf("%6.2fs +%5.2fs  %-14s slot %d unit %3d wav %-28s time %5u wavtime %5u mod %d  %s\n", e.at / 1000.0, e.length / 1000.0,
                    a.type < 10 ? names[a.type] : "?", a.slot, a.unit, a.wav.c_str(), a.time, a.wavTime, a.modifier, text.c_str());
        if (!a.wav.empty()) {
            Bytes w;
            std::string e2;
            if (!data.readFile(folder + "\\" + a.wav, w, e2)) std::printf("        (sound not found: %s)\n", (folder + "\\" + a.wav).c_str());
        }
    }
    std::printf("total %.2fs\n", total / 1000.0);
    // Round trip.
    Chk chk2 = chk;
    StringTable s2 = strings;
    writeBriefings(chk2, s2, b);
    const auto back = readBriefings(chk2, StringTable::read(chk2));
    const bool same = back == b;
    std::printf("written and read back: %s\n", same ? "same" : "DIFFERENT");
    // A text message's text shows for its time, then the box is empty; a transmission's stays until the next text.
    std::vector<BriefingTrigger> made(1);
    made[0].players[17] = true;
    BriefingAction msg, wait, tx;
    msg.type = BriefingAction::TextMessage, msg.text = "a", msg.time = 2000;
    wait.type = BriefingAction::Wait, wait.time = 3000;
    tx.type = BriefingAction::Transmission, tx.text = "b", tx.time = 1000;
    made[0].actions = {msg, wait, tx, wait};
    const auto mt = briefingTimeline(made, 0, forces);
    const bool textOk = briefingStateAt(mt, 1000).text == "a" && briefingStateAt(mt, 2500).text.empty() && briefingStateAt(mt, 3500).text == "b" &&
                        briefingStateAt(mt, 5000).text == "b";
    std::printf("text message for its time: %s\n", textOk ? "yes" : "NO");
    if (argc > 3) {  // the map with its sounds, to open in the editor
        std::vector<ArchiveFile> files;
        for (const auto& t : b)
            for (const auto& a : t.actions) {
                if (a.wav.empty() || std::any_of(files.begin(), files.end(), [&](const ArchiveFile& f) { return f.name == a.wav; })) continue;
                Bytes w;
                if (data.readFile(folder + "\\" + a.wav, w, err)) files.push_back({a.wav, std::move(w)});
            }
        const bool ok = writeMap(argv[3], bytes, files, err);
        std::printf("map written: %s (%zu sounds) %s\n", ok ? "yes" : "NO", files.size(), ok ? "" : err.c_str());
    }
    return same && textOk ? 0 : 1;
}
