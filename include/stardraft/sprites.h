#pragma once

// Unit graphics: classic GRP sprites resolved through units.dat -> flingy.dat -> sprites.dat ->
// images.dat -> images.tbl, colored with the tileset palette and tunit.pcx player colors.

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/tileset.h"

namespace stardraft {

class GameData;
struct Tileset;
struct Unit;
struct Image;

// What an image's iscript does when it starts (its Init animation, and Built for buildings, read
// straight through to the first jump): the frame it comes to rest on and the overlays it creates
// underneath (shadows) and on top of itself (glows, lights, smoke). Sprite overlays are given as
// their image.
struct ImageScript {
    struct Overlay {
        int image = -1;
        int x = 0, y = 0;  // offset from the parent's origin, map pixels
    };
    int initFrame = 0;
    // The frame Init shows first (at its first wait), as the game starts a map's sprites (THG2). With
    // playframtile the tileset's index (ERA) is added.
    int startFrame = 0;
    bool startFrameTile = false;
    int startFrameOn(int era) const { return startFrame + (startFrameTile ? (era & 7) : 0); }
    std::vector<Overlay> under, over;
    // The direction (0 to 31) Init turns its sprite to (setfldirect: the Archon's energy faces 15), or -1.
    int direction = -1;
};

// Classic StarCraft GRP: palette-indexed frames inside a fixed box (index 0 = transparent).
struct Grp {
    struct Frame {
        uint8_t x = 0, y = 0, width = 0, height = 0;  // position inside the box
        std::vector<uint8_t> pixels;                  // width * height palette indices
    };
    uint16_t width = 0, height = 0;  // frame box; the sprite's origin is its center
    std::vector<Frame> frames;

    bool parse(std::span<const uint8_t> data);
};

// An RGBA picture of one unit, drawn with its origin at (originX, originY).
struct Sprite {
    int width = 0, height = 0;
    int originX = 0, originY = 0;
    std::vector<uint8_t> rgba;  // straight alpha; transparent pixels have alpha 0
};

class UnitSprites;

// An image's Init animation (scripts\iscript.bin) stepped one game tick at a time, as a preplaced
// One line of an iscript listing: its offset in iscript.bin, a label (where a jump lands), the opcode and its
// arguments ("playfram 0x22", "goto L_1A2B"). Each argument also on its own, with what it points at: an image,
// a sprite, a sound (sfxdata), a weapon, or a place in the script (a jump or call).
struct IscriptArg {
    std::string text;
    std::string ref;  // "images", "sprites", "sfxdata", "weapons", "label", or "" (a number)
    int value = 0;
};
struct IscriptLine {
    size_t offset = 0;
    bool label = false;
    std::string op, args;
    uint8_t opcode = 0;
    std::vector<IscriptArg> argv;
};
// The code of an animation from `start`: what it runs straight through, and every place its jumps and calls
// lead to, in file order. At most `maxLines` instructions.
std::vector<IscriptLine> disassembleIscript(const Bytes& iscript, size_t start, size_t maxLines = 200);

// unit plays it: frames, waits, random waits and branches, jumps, calls. Sounds, overlays, orders and
// turning are skipped. 24 ticks per second at the game's fastest speed.
class ImageAnim {
public:
    ImageAnim() = default;
    // Plays animation `anim` of the image's script (0 Init; 16 Built, which finished buildings play).
    ImageAnim(const UnitSprites& sprites, int image, uint32_t seed, int anim = 0);
    // One game tick; true if the frame changed.
    bool tick();
    // Goes on with animation `anim` of the image's script now (its frame, place and turns stay), if the script has it.
    void play(const UnitSprites& sprites, int image, int anim);
    // The frame the script shows (for turning images the base of a 17-frame facing set).
    int frame() const { return frame_; }
    bool running() const { return !stopped_; }
    // The sounds (sfxdata.dat entries) the script asked for since the last call: playsnd, playsndrand,
    // playsndbtwn, attackmelee.
    std::vector<int> takeSounds() { return std::exchange(sounds_, {}); }
    // Whether the script attacked since the last call (domissiledmg, attackmelee, attackwith, attack, castspell,
    // useweapon).
    bool takeAttack() { return std::exchange(attacked_, false); }
    // The image's place from its sprite (sethorpos, setvertpos, setpos: an air unit's hover).
    int x() const { return ox_; }
    int y() const { return oy_; }
    // The direction (0 to 31) of a unit that faced `start` after the script's turns (turnccwise, turncwise, turn1cwise,
    // turnrand, setfldirect: an idle that looks around).
    int direction(int start) const { return ((setDir_ >= 0 ? setDir_ : start) + turn_) & 31; }
    // Whether the script hides the image now (tmprmgraphicstart .. tmprmgraphicend: the Archon's flicker).
    bool hidden() const { return hidden_; }
private:
    std::vector<int> sounds_;
    bool attacked_ = false;
    const Bytes* code_ = nullptr;
    size_t pc_ = 0, ret_ = 0;
    int wait_ = 0, frame_ = 0;
    int ox_ = 0, oy_ = 0, turn_ = 0, setDir_ = -1;
    bool hidden_ = false;
    uint32_t rng_ = 1;
    bool stopped_ = true;
};

// A sprite as the game runs it (OpenBW iscript_execute): its main image and the images its scripts add above and
// below (shadows, flashes, glows, turrets), each with its own script, offsets, flip and facing; the sprites the
// scripts make (dust, smoke); sounds and attacks. Orders, weapons and movement are not run: a "move" or an attack
// is noted, nothing more. 24 ticks per second at the fastest speed.
class SpriteSim {
public:
    struct Draw {
        int image = 0, frame = 0;
        bool flip = false;
        int x = 0, y = 0;  // from the sprite's origin
        int drawFunction = 0;  // the image's draw function now (cloaking changes it as it goes)
        int level = 0;         // the draw function's state: 0..8 cloaking, 48..63 warp flash
    };
    SpriteSim() = default;
    // The sprite of `image`, playing animation `anim` (0 Init), facing `dir32` (0 up, 8 right, clockwise).
    SpriteSim(UnitSprites& sprites, int image, int anim, int dir32, uint32_t seed, int tilesetIndex = 0);
    void tick();
    void setDirection(int dir32);
    int direction() const { return ((heading_ + 4) / 8) & 31; }
    // The heading in 256ths of a turn (0 up, clockwise), as the game keeps it.
    int heading() const { return heading_; }
    bool running() const;
    // The main image runs, or a sprite it made does (a death's remnants).
    bool anyRunning() const { return running() || !children_.empty(); }
    int mainFrame() const;
    // What to draw, back to front.
    std::vector<Draw> draws() const;
    std::vector<int> takeSounds() { return std::exchange(sounds_, {}); }
    bool takeAttack() { return std::exchange(attacked_, false); }
    // The main image's instructions run in the last tick (offsets in iscript.bin), the last one where it stopped.
    const std::vector<size_t>& executed() const { return executed_; }
    // Animation `anim` on every image that has it (the game gives a sprite's animation to all its images).
    void playAnim(int anim);
    // Where the main image gave control back to its unit's order since the last call: 1 gotorepeatattk (attack
    // again after the cooldown), 2 ignorerest (waits for an order), 3 a long wait (idle); 0 none.
    int takeHandoff() { return std::exchange(handoff_, 0); }
    // The unit's order, as far as the script reads it (OpenBW's iscript_unit). A target in direction `dir256`
    // (256ths, 0 up, clockwise), `distance` pixels away; -1: no target. The sprite turns toward the target by
    // `turnRate` (flingy.dat Turn Radius, 256ths per tick; 0: at once), as a turret does.
    void setTarget(int dir256, int distance = 96);
    int target() const { return target_; }
    void setTurnRate(int turnRate) { turnRate_ = turnRate; }
    // Where `move` took the sprite (pixels from where it started), and a place to put it again (the player keeps
    // a walking sprite on its stage).
    std::pair<double, double> position() const { return {px_, py_}; }
    void setPosition(double x, double y) { px_ = x, py_ = y; }
    // A Protoss building warping in, as order_IncompleteWarping does it: the building's shape filled with the warp
    // texture as script 193 plays it (draw function 12), and at that script's order signal, the images again with
    // the Warp in animation (its warp flash).
    void warpIn();
    // 0 no warp-in; 1 the warp texture plays; 2 the warp flash plays; 3 done.
    int warpState() const { return warp_; }

private:
    int handoff_ = 0;
    struct Image {
        int id = 0, base = 0, dirIndex = 0;
        bool flipped = false, hidden = false, special = false, directional = false, alive = true;
        bool queued = false;  // playAnim: the image is still to get the animation
        int ox = 0, oy = 0;
        size_t pc = 0, ret = 0;
        const Bytes* code = nullptr;  // the iscript file that pc is in
        int wait = 0;
        int mod = 0, mod1 = 0, mod2 = 0;  // draw function and its state (OpenBW modifier, modifier_data1/2)
    };
    void setModifier(Image& im, int mod);
    struct Child {
        std::shared_ptr<SpriteSim> sim;
        int x = 0, y = 0;
        bool above = true;
    };
    UnitSprites* sp_ = nullptr;
    std::vector<Image> images_;  // front (index 0) is drawn on top
    int main_ = 0;               // index of the main image in images_
    std::vector<Child> children_;
    int heading_ = 96, tileset_ = 0;
    int target_ = -1, targetDistance_ = 96, turnRate_ = 0, signals_ = 0, warp_ = 0;
    double px_ = 0, py_ = 0;
    uint32_t rng_ = 1;
    std::vector<int> sounds_;
    bool attacked_ = false;
    std::vector<size_t> executed_;
    int rand();
    void setImageDirection(Image& im, int dir32);
    int addImage(int id, int ox, int oy, bool above, int rel, bool special);
    void addChild(int spriteId, int x, int y, bool above, const Image& parent);
    void runAnim(int index, int anim);
    void replaceImages();
    void setHeading(int dir256);
    bool execute(int index);  // false when the image ended
};

class UnitSprites {
public:
    UnitSprites();
    ~UnitSprites();

    bool load(const GameData& data, std::string& error);

    // The tables that load() reads from the classic .dat files. A game with other files (a mod's .datex, src/mods)
    // fills them itself and gives them to loadTables. Each table holds all entries of its file.
    struct Tables {
        std::vector<uint16_t> unitFlingy, unitSubunit;   // units.dat graphics and subunit 1
        std::vector<uint8_t> unitDirection;              // units.dat initial direction
        std::vector<uint16_t> flingySprite, spriteImage;  // flingy.dat sprite, sprites.dat image
        std::vector<uint8_t> spriteSelCircle, spriteSelOffset;  // sprites.dat, by sprite id (the game uses them from 130)
        std::vector<uint32_t> imageGrp;                  // images.dat GRP (1-based images.tbl index)
        std::vector<uint8_t> imageTurns, imageDrawFn, imageRemap;
        std::vector<int> imageScriptId;
        std::vector<uint32_t> imageOverlays[6];          // attack, damage, special, landing dust, lift-off, shield
        // The iscript files, in order (a script ID in two files: the first one's).
        std::vector<std::string> iscriptFiles{"scripts\\iscript.bin"};
        // An images.dat iscript ID's high 16 bits pick its file (its index in iscriptFiles), the low ones the script
        // in it, as Cosmonarchy's images.datex does (0 general, 1 terran, ...). A script its file does not have (506
        // of the mod's 5000 images, most general's 78, which only the terran and askosi files have): the first file's
        // with that number whose Init plays only frames the image's GRP has, or none (the terran 78, the Maverick's,
        // on the Parthenon's 21 frames).
        bool scriptIdFile = false;
    };
    bool loadTables(const GameData& data, Tables tables, std::string& error);

    // Picture of `unitType` owned by `owner` (0..11), facing down-right, including its subunit
    // (e.g. tank turret). Uses `tileset`'s palette. Cached; nullptr if the unit has no graphics.
    const Sprite* unitSprite(uint16_t unitType, uint8_t owner, const Tileset& tileset);
    // The unit's command-card icon (unit\cmdicons\cmdicons.grp, frame = unit id), colored as an
    // enabled button (game	icon.pcx row 0 with that file's palette). Cached; nullptr if none.
    const Sprite* commandIcon(uint16_t unitType);
    // The game's placeholder icon ("BLANK"), which units without an icon of their own share (commandIcon gives it for them).
    const Sprite* blankIcon();

    // Image id (images.dat) used for a unit type, or -1.
    int imageFor(uint16_t unitType) const;
    // Image id of a sprites.dat sprite (THG2 doodad overlays), or -1.
    int imageForSprite(uint16_t sprite) const;
    // The game's selection circle for a unit (sprites.dat): its image id and how far below the unit's
    // origin its center sits. False if the unit has none.
    bool selectionCircle(uint16_t unitType, int& image, int& offsetY) const;
    // Image id of the unit's subunit (turret), or -1.
    int subunitImage(uint16_t unitType) const;
    // Where an image's attachments (a turret) sit for one of its frames: images.dat's special
    // overlay (an .lol file), map pixels from the image's origin; x is for the unflipped frame.
    std::pair<int, int> specialOffset(int image, int frame) const;
    // A point of an image's overlay offsets (.lol): `lo` 0 attack, 1 damage, 2 special, 3 landing dust, 4 lift-off,
    // 5 shield; for one frame, the `index`th point. {0, 0} if none.
    std::pair<int, int> loOffset(int image, int lo, int frame, int index) const;
    // A sprites.dat sprite's image (sprol and the like make sprites), or -1.
    int spriteImage(int sprite) const { return imageForSprite(uint16_t(sprite)); }
    // Frame to show a unit's image with (facing down-right for turning graphics), and whether
    // to mirror it horizontally.
    // `direction` (0 to 31): the facing to show when units.dat gives the unit type a random one (randomDirection);
    // -1: down-right, as before.
    int defaultFrame(uint16_t unitType, int image, bool& flip, int direction = -1) const;
    // The direction (0 to 31) that defaultFrame shows the unit type in.
    int facingDirection(uint16_t unitType, int direction = -1) const;
    // Whether units.dat gives the unit type a random facing (its Unit Direction 32 or more), as most units have.
    bool randomDirection(uint16_t unitType) const { return unitType < unitDirection_.size() && unitDirection_[unitType] >= 32; }
    // The same with the frame an ImageAnim shows: its frame plus the facing for turning images; `turned` (0 to 31): the
    // direction that its script turned the unit to (ImageAnim::direction), in place of defaultFrame's.
    int animatedFrame(uint16_t unitType, int image, int scriptFrame, bool& flip, int direction = -1, int turned = -1) const;
    // Frames in one facing set of a turning image: 17 (up .. down on the right; the left side mirrored), or 32 (every
    // direction, none mirrored: a mod's graphics whose scripts step their frames by 32, as Cosmonarchy's do).
    int facingSet(int image) const;
    // The frame in a facing set for direction `dir32` (0 up, 8 right, 16 down, clockwise), and whether to mirror it.
    int facingIndex(int image, int dir32, bool& flip) const;
    // iscript.bin and where animation `anim` of an image's script starts in it (0 if it has none). A game with
    // more than one iscript file: iscript() is the first one, and scriptCode gives the file of an image's script.
    const Bytes& iscript() const;
    const Bytes& scriptCode(int image) const;
    size_t animOffset(int image, int anim) const;
    // The same for a script by its iscript ID (the game runs script 193 on a warping building).
    const Bytes& scriptCodeById(int scriptId) const;
    size_t scriptAnimOffset(int scriptId, int anim) const;
    // The scripts iscript.bin has, by iscript ID; an image's iscript ID (images.dat, as edited).
    std::vector<uint32_t> scriptIds() const;
    int scriptId(int image) const { return image >= 0 && size_t(image) < imageScriptId_.size() ? imageScriptId_[size_t(image)] : -1; }
    size_t initOffset(int image) const { return animOffset(image, 0); }
    int imageCount() const { return int(imageGrp_.size()); }
    // images.dat draw function (9 = remapped glow, 10 = shadow, 0 = normal).
    int drawFunction(int image) const { return image >= 0 && size_t(image) < imageDrawFn_.size() ? imageDrawFn_[size_t(image)] : 0; }
    // Glows (draw function 9): a pixel's color as it shows over black, from the tileset's remap table
    // (ofire/gfire/bfire/bexpl.pcx, images.dat remapping); the game blends it additively. False for
    // other images.
    bool glowColor(int image, uint8_t index, const Tileset& tileset, Rgb& out) const;
    // A color table of the tileset ("TileSet\<tileset>\<name>.pcx": trans50, dark, ofire, ...), 256 columns (the
    // color under it) per row; empty if the game has none. Cached.
    const std::vector<uint8_t>& tilesetTable(const Tileset& tileset, const std::string& name) const;
    // Draw one frame of an image onto `s` with its origin at s's origin plus (dx, dy): normally,
    // as a shadow (only into empty pixels) or as a glow (added to what is there). Index: the frame's palette index
    // in red and the index after the player color in green (blue 0, alpha 255), for the draw functions that work on
    // palette indices (cloak, EMP shockwave, warp texture).
    enum class Blend { Normal, Shadow, Glow, Index };
    void drawImage(Sprite& s, int image, int frame, bool flip, uint8_t owner, const Tileset& ts, int dx = 0, int dy = 0,
                   Blend blend = Blend::Normal);
    // Whether an image has 17-frame facing sets (images.dat graphics turns).
    bool turns(int image) const { return image >= 0 && size_t(image) < imageTurns_.size() && imageTurns_[size_t(image)]; }

    // Classic GRP for an image (SD graphics), cached; nullptr if missing.
    const Grp* grp(int image);
    // A GRP that no images.dat entry names (the tileset's creep edges, stardraft/creep.h): an image id after all of
    // images.dat's (kExtraImages and up), so that grp() and the renderer load it like an image. Its pixels are the
    // tileset's colors (no player color, no draw function). The same path gives the same id.
    static constexpr int kExtraImages = 0x10000;
    int extraImage(const std::string& path);
    // The game data path of an extra image; nullptr for an images.dat image.
    const std::string* extraPath(int image) const;
    // Palette index for a GRP pixel after player-color remapping: the owner's color row (setColorRows), or row `row`.
    uint8_t remap(uint8_t index, uint8_t owner) const;
    uint8_t remapRow(uint8_t index, int row) const;
    int colorRow(uint8_t owner) const { return colorRow_[owner < 12 ? owner : 11]; }
    // The tunit.pcx row (the color number in a map's COLR) each of the 12 players' units are drawn with (default:
    // its own number). A mod's tunit.pcx has more rows (colorRowCount).
    void setColorRows(const std::array<uint8_t, 12>& rows) { colorRow_ = rows; }
    int colorRowCount() const { return int(playerColors_.size() / 8); }
    // The minimap color of color number `row` in tileset `era` (tminimap.pcx: a row per tileset): a palette index, -1 if
    // the game has no such file or row.
    int minimapColor(int row, int era) const;
    // Init animation of an image (scripts\iscript.bin).
    const ImageScript& script(int image) const;
    // Images drawn with the shadow draw function (images.dat draw function 10).
    bool isShadow(int image) const;

private:
    std::vector<uint32_t> imageOverlays_[6];  // images.dat lo files: attack, damage, special, landing dust, lift-off, shield
    mutable std::map<std::pair<int, int>, std::vector<std::vector<std::pair<int8_t, int8_t>>>> lo_;
    void loadScripts(const GameData& data, const std::vector<int>& scriptIds);
public:
    // A project's edited sprites.dat and images.dat (whole files), as far as a map can change them in the game:
    // the image of each sprite, and each image's Iscript ID, Gfx Turns, Draw Function and Remapping. The GRP and
    // overlay files stay the game's (StarCraft: Remastered does not take changes to them).
    void applyEdits(const Bytes& sprites, const Bytes& images);
    // A project's edited units.dat and flingy.dat (whole files): each unit type's flingy and subunit, and each flingy's
    // sprite. With applyEdits too, a unit type shows as a unit that the game makes after the map's edits run.
    void applyUnitEdits(const Bytes& units, const Bytes& flingy);
    // The unit types that look different in `edited` than in `game`: another image, or another Iscript ID, Gfx Turns,
    // Draw Function or Remapping of the image, for the unit or its subunit. Subunits (turrets) are not in the list:
    // the game makes them again with their base unit.
    static std::vector<uint16_t> changedUnitGraphics(const UnitSprites& game, const UnitSprites& edited);
private:


    const GameData* data_ = nullptr;
    std::vector<uint16_t> unitFlingy_;     // units.dat graphics
    std::vector<uint16_t> unitSubunit_;    // units.dat subunit 1
    std::vector<uint8_t> unitDirection_;   // units.dat initial direction (32 = random)
    std::vector<uint16_t> flingySprite_;   // flingy.dat sprite
    std::vector<uint16_t> spriteImage_;    // sprites.dat image
    std::vector<uint8_t> spriteSelCircle_, spriteSelOffset_;  // sprites.dat, by sprite id (used from 130)
    std::vector<uint32_t> imageGrp_;       // images.dat GRP (1-based images.tbl index)
    std::vector<uint8_t> imageTurns_;      // images.dat graphics turns
    mutable std::vector<uint8_t> facingSet_;  // facingSet() of each image, found on first use (0: not yet)
    std::vector<uint8_t> imageDrawFn_;     // images.dat draw function
    std::vector<uint32_t> imageSpecial_;   // images.dat special overlay (1-based images.tbl index)
    std::vector<uint8_t> imageRemap_;      // images.dat remapping (1 ofire, 2 gfire, 3 bfire, 4 bexpl)
    mutable std::map<std::pair<uint16_t, int>, std::vector<uint8_t>> remapTables_;  // (era, remap) -> pcx
    mutable std::map<std::pair<uint16_t, std::string>, std::vector<uint8_t>> tilesetTables_;  // (era, name) -> pcx
    mutable std::map<int, std::vector<std::pair<int8_t, int8_t>>> specialLo_;  // loaded .lol offsets
    std::vector<ImageScript> scripts_;     // per image
    std::vector<std::string> iscriptFiles_;  // scripts\iscript.bin (a mod: more files)
    std::vector<Bytes> iscripts_;          // their contents
    size_t headerAnimOffset(const Bytes& code, size_t header, int anim) const;
    struct ScriptAt {
        uint8_t file = 0;     // index in iscripts_
        uint32_t header = 0;  // the script header in that file (0 = none)
    };
    std::vector<ScriptAt> scriptHeader_;   // per image
    std::map<uint32_t, ScriptAt> scriptEntries_;  // (file << 16 | iscript ID) -> its header
    std::map<uint16_t, ScriptAt> firstEntries_;   // iscript ID -> its header in the first file that has it
    bool scriptIdFile_ = false;                   // Tables::scriptIdFile
    const ScriptAt* findScript(int scriptId) const;
    const ScriptAt* scriptThatFits(int image, uint16_t id);  // (Tables::scriptIdFile: a script ID its file does not have)
    std::vector<int> imageScriptId_;       // per image: its iscript ID (images.dat)
    std::vector<std::string> imagePaths_;  // images.tbl
    std::vector<uint8_t> playerColors_;    // tunit.pcx: 8 palette indices per color row
    std::vector<uint8_t> minimapColors_;   // tminimap.pcx: a palette index per color row, a row of them per tileset
    int minimapWidth_ = 0;
    std::array<uint8_t, 12> colorRow_{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};  // per player: its tunit.pcx row
    std::map<int, std::unique_ptr<Grp>> grps_;
    std::vector<std::string> extraPaths_;  // extra images (extraImage), from kExtraImages
    std::map<std::pair<uint16_t, uint8_t>, std::unique_ptr<Sprite>> sprites_;  // (unit type, color row)
    std::unique_ptr<Grp> cmdIcons_;
    std::vector<uint8_t> iconRgb_;  // 16 colors (ticon.pcx row 0), RGB
    bool iconsTried_ = false;
    const Sprite* iconFrame(uint16_t frame);  // cmdicons frame, colored
    std::vector<uint16_t> iconBlank_;  // units whose icon is the placeholder
    std::map<uint16_t, std::unique_ptr<Sprite>> icons_;
    const Tileset* spriteTileset_ = nullptr;
};

// Draw units onto a rendered terrain image (same `downscale` as renderTerrain), back to front
// by y so lower units overlap higher ones.
void drawUnits(Image& image, const std::vector<Unit>& units, UnitSprites& sprites, const Tileset& tileset,
               int downscale = 1);

}  // namespace stardraft
