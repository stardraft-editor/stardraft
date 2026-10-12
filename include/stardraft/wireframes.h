#pragma once

// Unit wireframes: the pictures of a selected unit in the status panel (roadmap R13). Three sets:
//   single (unit\wirefram\wirefram.grp, 228 frames): one unit selected;
//   group (grpwire.grp, 131 frames): one of up to 12 selected units;
//   transport (tranwire.grp, 106 frames): a unit inside a transport.
// The frame a unit shows is a table of the game's code (by default the unit's own number), which eudplib's
// SetWirefram, SetGrpWire and SetTranWire change (UnitLooks in dat_extras.h).

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/hd_assets.h"
#include "stardraft/sprites.h"

namespace stardraft {

class GameData;

enum class WireKind { Single, Group, Transport };

class Wireframes {
public:
    // The file of a set: "wirefram", "grpwire" or "tranwire".
    static const char* fileName(WireKind kind);
    // SD: unit\wirefram\<name>.grp with the colors of game\twire.pcx. HD2: HD2\unit\wirefram\<name>.dds.grp
    // (`carbot`: HD2\Carbot\...).
    bool load(const GameData& data, WireKind kind, bool hd, bool carbot, std::string& error);
    int count() const { return hd_ ? int(tex_.size()) : int(grp_.frames.size()); }
    bool hd() const { return hd_; }
    // Frame `f` as the status panel shows it at full health: the parts of the unit in the green of twire.pcx,
    // the outlines in their own colors, or (`shields`: the unit has shields, all of them) in twire.pcx's blue.
    // nullptr when there is no such frame.
    const Sprite* picture(int f, bool shields);

    // The colors: twire.pcx's 24 palette indices and its palette.
    static constexpr int kTwireColors = 24;
    static constexpr int kFullHealth = 1;   // twire.pcx color 1: the green of a unit at full health
    static constexpr int kFullShields = 3;  // twire.pcx colors 3 and 4: the blue of full shields

private:
    bool hd_ = false;
    Grp grp_;                         // SD
    std::vector<DdsTexture> tex_;     // HD2: one texture each frame
    uint8_t twire_[kTwireColors] = {};
    uint8_t palette_[768] = {};
    std::map<int, std::unique_ptr<Sprite>> pictures_;
    Sprite decodeSd(int f, bool shields) const;
    Sprite decodeHd(int f, bool shields) const;
};

}  // namespace stardraft
