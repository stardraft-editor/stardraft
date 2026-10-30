#pragma once

// Cosmonarchy BW (CMBW, https://fraudsclub.com/cosmonarchy-bw): a StarCraft 1.16.1 mod with its own units. Its
// files are loose in <install>\mpq (arr\*.datex, rez\unitnames.tbl, scripts\iscript_*.bin, unit\...), over the
// 1.16.1 MPQs in <install>\..\Starcraft. CMBW maps use no EUD.
//
// The .datex files hold the fields of the classic .dat files, field after field, but each field has all entries
// (3000 units, 1000 flingy, 2000 sprites, 5000 images, ...) and some fields that point into a larger table are 2 bytes
// wide (units.dat graphics and weapons).

#include <array>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "stardraft/classic_triggers.h"
#include "stardraft/strings.h"
#include "stardraft/terrain_stamp.h"

namespace stardraft {

class GameData;
struct Tileset;
struct TerrainLayer;
class UnitTypes;
class Chk;

namespace cmbw {

// The CMBW install folder (the one that holds mpq\arr\units.datex): the one useInstall gave, else Prerelease first,
// then Release under Program Files\Cosmonarchy. Empty if none.
std::filesystem::path findInstall();
// The install folder in `folder`: the folder itself when it holds mpq\arr\units.datex, else its Prerelease or Release
// folder (a Cosmonarchy folder). Empty if none.
std::filesystem::path installIn(const std::filesystem::path& folder);
// The folder that findInstall gives from now on (the user's choice: the editor's Cosmonarchy folder); empty: the search.
void useInstall(const std::filesystem::path& install);
// A terrain rules file (.isom, mods/terrain_rules.h) put over the built-in rules and the mod's own file when a
// tileset loads (Program options > Plugins: ISOM terrain rules override); empty: none.
void useTerrainOverride(const std::filesystem::path& file);
// On: the tilesets this thread loads leave the override out (the default rules: the shipped file and the built-in ones).
void skipTerrainOverride(bool on);
// On: the tilesets this thread loads leave the built-in repaired rows out (the repair probe starts from the rows themselves).
void skipRepairedRules(bool on);

// Opens `data` on the CMBW files of `install` (findInstall): the loose mpq folder first, then the 1.16.1 MPQs
// (patch_rt, BrooDat, StarDat). Unit types and sprites then come from the .datex files.
bool open(GameData& data, const std::filesystem::path& install, std::string& error);

// Whether a map file is a CMBW map by what it shows: it is in the Cosmonarchy folder (findInstall's parent), or it has
// units that only the mod has (unit ids 228 and up).
bool looksLikeModMap(const std::filesystem::path& map);

// A unit's ranges by the mod's tables (units.datex, weapons.datex; its turret's weapons when it has none): weapon ranges in
// pixels (0: none), target acquisition in tiles; `cliff`: the mod's height advantage applies to it (a ground unit, not one
// of the units and traps the mod leaves out). False: not a unit of the mod's tables.
struct UnitRanges {
    int ground = 0, air = 0, seek = 0, sight = 0;  // (sight in tiles)
    uint32_t flags = 0;                            // (units.dat's special ability flags)
    bool flyer = false, cliff = false;
};
bool unitRanges(const GameData& data, uint16_t unit, UnitRanges& out);
// The mod's height of a map tile (TILE or MTXM value), as it gives ground units range (Cosmonarchy-GPTP
// TileHeight_extender): the minitiles' height (VF4 flags 0x6, plus the mod's own height in the flags' high byte) of the
// walkable ones, summed and divided by 16, rounded; at most 127.
int tileHeight(const Tileset& tileset, uint16_t tileValue);

// Which of the mod terrains' edge and wall pieces stand beside each other on the maps in `mapsFolder` (each era's):
// {era, a piece's left tile, its neighbor's left tile, the place (Tileset::pieceNeighbors)}, sorted. src/mods/
// cmbw_neighbors.inc is this list (tools/plugins/cmbw/probes/cmbw_neighbors.cpp --inc writes it).
std::vector<std::array<uint16_t, 4>> learnPieceNeighbors(const GameData& data, const std::filesystem::path& mapsFolder);
// The range a unit's weapon has on ground at `height` (tileHeight; above targets at height 0): a weapon of more than 32
// px gains 32 px per level (the mod's cliff advantage).
inline int cliffRange(int range, int height, bool cliff) { return cliff && range > 32 && height > 0 ? range + 32 * height : range; }

// The mod's terrain brushes of tileset `era`, from its SCMDraft brush file (Cosmonarchy-<tileset>.scmdbrh in the
// Starcraft folder's Maps/CMBW-Root/CMBW-editor/brush), as terrain stamps named "Cosmonarchy_<folder>_<name>" (no file:
// they are the mod's, not the user's). Read once per tileset.
const std::vector<TerrainStamp>& brushStamps(const std::filesystem::path& install, uint16_t era);

// How well a brush placed with its top left tile at (x0, y0) of `map` joins the tiles around it: the mean difference of
// the pixels (R, G and B, 0-255) where its tiles meet the map's tiles beside them (each tile's outer row or column against
// the facing one); -1 where it meets none (all around it is its own, or off the map). Low: it continues them.
int brushEdgeScore(const Tileset& tileset, const TerrainStamp& brush, const TerrainLayer& map, int x0, int y0);

// The establishing shot (rez\est.txt in a map): pages that Cosmonarchy shows before the map starts, each a picture
// (BACKGROUND, an 8-bit PCX of 1920 x 1080: the game's screen) with text at a place, for DISPLAYTIME ms, fading in and out
// over FADESPEED ms. The tags before a page's place are that page's, as the mod's example file has them
// (fraudsclub.com/cosmonarchy-bw/sites/scenario-setup, pastebin k14M3iDt).
struct EstablishingShot {
    struct Page {
        std::string place = "SCREENLOWERLEFT";  // the place tag (SCREENCENTER, SCREENUPPERLEFT, ...)
        std::vector<std::string> lines;          // its text (and tags the editor does not know, as they are)
        std::string background, font;            // BACKGROUND and FONTCOLOR (empty: none in the file)
        int displayMs = -1;                      // DISPLAYTIME (-1: none in the file)
        std::vector<std::string> comments;       // the COMMENT lines before it, kept
        std::vector<std::string> tags;           // other tags before it ("</TAG value>"), kept as they are
    };
    int fadeMs = 5000;  // FADESPEED; -1: none in the file
    std::vector<Page> pages;
};
EstablishingShot parseEstablishingShot(const std::string& text);
std::string establishingShotText(const EstablishingShot& shot);  // the file's text (CR LF lines)
// The places a page's text can have, in the order a list shows them, and each one's name.
const std::vector<std::pair<std::string, std::string>>& establishingShotPlaces();

// CRIGs: the mod's trigger comment commands. Each line of a trigger's Comment is a command and its arguments
// ("kill Anywhere 2 * 5 true"; a condition: "condition deaths ..."), or plain text (kept as it is).
struct CrigArg {
    std::string kind;  // area, players, player, units, unit, quota, timequota, equality, time, seconds, number, decimal,
                       // bool, text, name, position, word
    std::string name;  // "source_players"
    bool optional = false;
};
struct CrigCommand {
    std::string kind;                // condition, action, local_action
    std::vector<std::string> names;  // the first is the name to write; the others are aliases
    std::vector<CrigArg> args;
    bool readsMore = false;          // the command reads more of its line itself (the rest is kept as text)
};
const std::vector<CrigCommand>& crigCommands();
// A line's words: quoted text ("Omega Squadron") and parentheses ((4000 5024 ~128 ~128)) are one word each.
std::vector<std::string> crigWords(const std::string& line);
struct CrigLine {
    const CrigCommand* command = nullptr;  // nullptr: plain text
    bool condition = false;                // written after "condition "
    std::vector<std::string> values;       // one per argument given (a quota's "op value" is one)
    std::string rest;                      // what the command does not take (or the whole line of plain text)
};
CrigLine parseCrigLine(const std::string& line);
// A Comment's text with a location's name changed in the command lines that use it as a location (an area argument,
// any case; a name with a space quoted); the other lines stay as they are.
std::string crigRenameLocation(const std::string& text, const std::string& from, const std::string& to);
std::string crigLineText(const CrigLine& l);
// The words an argument of kind "word" takes, as the mod's documentation lists them (spawn types, factions, races, ...);
// empty: any word (typed).
const std::vector<std::string>& crigWordChoices(const std::string& command, const std::string& arg);
// A value of `kind` for a new command's argument.
std::string crigDefaultValue(const std::string& kind);
// A command's name as the trigger lists show conditions and actions: "set_race" -> "Set race".
std::string crigTitle(const CrigCommand& c);
// What a command does, from the mod's Scenario Setup page (src/mods/cmbw_crig_docs.inc); empty when it does not say.
std::string crigHelp(const CrigCommand& c);
// A new line of a command: its name and a value for each argument it needs.
std::string crigNewLine(const CrigCommand& c);

// CRIGs as conditions and actions (the Classic triggers page of a Cosmonarchy map): a trigger's Comment that has
// all commands becomes a row for each line (type kClassicCrigType: conditions in the conditions, the rest in the
// actions, where the Comment was). A Comment with a line that is not a command, or a disabled one, stays a Comment; and
// a trigger whose rows would not give back the same trigger (crigExpand) stays as it is.
ClassicTriggers crigCollapse(const ClassicTriggers& t, const StringTable& strings);
// The rows back into Comments: each run of action rows of one depth from one Comment (or new rows next to them) is one
// Comment, its lines in the rows' order and its conditions' lines in their places; a new condition goes into the
// trigger's first Comment of commands at the top level (a new one at the top when there is none); a row turned off is a
// turned-off Comment of its own. A Comment keeps its string while its text is the same; a string in `own` (those the
// page made) changes in place, and a new one goes into `own`. `reread`: the triggers as crigCollapse reads the result
// (the page's rows after a save, as after opening the map again).
ClassicTriggers crigExpand(const ClassicTriggers& t, StringTable& strings, std::set<uint32_t>* own = nullptr, ClassicTriggers* reread = nullptr);
// The trigger comment lines that the game would not read as the mapper meant (for a check before a save): each problem as
// "Trigger <n>: "<line>": <what>". A line whose first word has a "_" and is one letter off a command's name (a typo: the
// game skips the line). Values are not checked: the game reads them more loosely than its documentation says (the mod's
// 416 maps). Turned-off Comments are left out.
std::vector<std::string> crigProblems(const Chk& chk);
// What the editor knows of a map for the checks of a line's values.
struct CrigMapInfo {
    size_t unitTypes = 0;             // unit ids below it are units
    size_t orders = 0;                // order ids below it are orders (0: not checked)
    std::set<std::string> locations;  // the map's location names, lower case (and "anywhere")
};
CrigMapInfo crigMapInfo(const Chk& chk, size_t unitTypes);
// The mod's order names (arr\orders.txt), by id.
std::vector<std::string> orderNames(const GameData& data);
// What an argument's value is, for its picker and its check: its kind, or what its name says of a number ("order" for
// order_id, "player" for target_player and the like, "unit" for secondary_unit_id, "flag" for 0 or 1: do_loop...).
std::string crigArgKind(const CrigArg& a);
// What is wrong with value k of a command's line, for a mark on it (empty: nothing found): a location name the map does
// not have, a unit id that is no unit, a bool that is not true or false. Other values the game reads loosely.
std::string crigValueProblem(const CrigLine& l, size_t k, const CrigMapInfo& map);
// Every value problem (crigValueProblem) of the map's turned-on Comments: "Trigger 3, action 2: "line": what".
std::vector<std::string> crigValueProblems(const Chk& chk, const CrigMapInfo& map);
// A line that reads as a command but is a note ("Kill map revealers": its first word capitalized): its values are not
// marked (the mod's maps have 60 such notes).
bool crigLooksLikeNote(const std::string& line);
// False when a trigger's rows need more than 64 actions written back, or a trigger without command rows has more than
// 64 rows (it had some): the page does not set the triggers then.
bool crigFits(const ClassicTriggers& t, const StringTable& strings);
// The actions a trigger has once its command rows are Comments again (its action count when it has none).
int crigActionCount(const ClassicTrigger& t);

// An 8-bit PCX picture (the game's pictures: its palette at the end) as RGBA. False when it is not one.
bool decodePcxRgba(const std::vector<uint8_t>& file, int& width, int& height, std::vector<uint8_t>& rgba);
// A picture (RGBA, rows from the top) as an 8-bit PCX file of `outW` x `outH` (scaled to it, smoothly), its 256 colors
// picked by median cut: for an establishing shot from a PNG (the game's size, 1920 x 1080).
std::vector<uint8_t> encodePcx8(const std::vector<uint8_t>& rgba, int width, int height, int outW = 1920, int outH = 1080);

// For the mining simulation (it knows only the game's units): a map's UNIT section with each unit as the game's unit of
// its role in the mod (by units.dat flags and race): workers as an SCV, Drone or Probe; resource depots as a Command
// Center, Hatchery or Nexus; neutral resources as mineral fields (the small ones) or a Vespene Geyser; a player's gas
// buildings as the race's Refinery, Extractor or Assimilator. Other units past the game's 228 are left out; the others stay.
std::vector<uint8_t> simUnits(const std::vector<uint8_t>& unitSection, const UnitTypes& types);

}  // namespace cmbw
}  // namespace stardraft
