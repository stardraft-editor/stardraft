#pragma once
// Collaborative editing (a preview): the terrain, units, doodads, sprites and locations of a map, edited live. One program hosts the map; others join it
// with an invite link (to edit or to view). All messages go through a relay (tools/collab-relay) that cannot read them:
// each member and the host agree on keys (X25519) bound to the invite link's secret, which only the link holds, and every
// message after that is encrypted and authenticated (XChaCha20-Poly1305) with counters, so none can be read, changed,
// replayed or reordered by the relay.
//
// The sync model: the host's order is the truth. Each edit is a patch of absolute values of the terrain sections' cells
// (ISOM, MTXM, TILE), the last one in the host's order wins per cell. A program reads its own edits as the difference of
// the terrain from what it sent last (any tool, as it is), so no tool needs to know about sharing. A member's edits show
// at once; values from others for cells that the member changed and the host has not yet confirmed wait (no flicker,
// as Figma does). Undo and redo are each user's own: the editor's undo works as always, then afterUndo() keeps only
// the cells that this user wrote last; the others' cells stay as they are (each cell knows who wrote it last).
//
// This file has no network and no UI: a Session reads and writes messages through an Outbox, and the program calls
// tick() often (it reads the editor's changes and applies the others').

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {
class Editor;

namespace collab {

enum class Role : uint8_t { None = 0, View = 1, Edit = 2 };
const char* roleName(Role r);  // "view", "edit"

using Key = std::array<uint8_t, 32>;
using Secret = std::array<uint8_t, 16>;  // an invite link's secret (128 bits: 22 characters in the link)

// Random bytes from the system (BCryptGenRandom, getrandom, arc4random). False: none (do not go on).
bool randomBytes(void* out, size_t n);

// --- Invite links -----------------------------------------------------------------------------------------------------
// https://<server>/j/<room>#<e|v>.<secret, base64url>; or stardraft://join?link=<that link, URL-encoded>.
struct InviteLink {
    std::string server;  // "https://host[:port]" (or "http://" for this computer only: localhost, 127.0.0.1)
    std::string room;    // 22 characters (128 bits, base64url)
    Role role = Role::None;
    Secret secret{};
};
std::string makeInviteLink(const std::string& server, const std::string& room, Role role, const Secret& secret);
bool parseInviteLink(const std::string& text, InviteLink& out, std::string& error);
// A server address as the user typed it: "https://host[:port]" with no path. http only for this computer.
bool normalizeServer(const std::string& text, std::string& out, std::string& error);
// ws(s)://host[:port]/relay (a host makes a room there), or /relay/<room> (a member joins it).
std::string relayUrl(const std::string& server, const std::string& room = {});
std::string relayToken(const Secret& secret);        // what the relay checks for a link (64 hex digits); not the secret

// --- The terrain ------------------------------------------------------------------------------------------------------
enum Part : uint8_t { kIsom = 0, kMtxm = 1, kTile = 2, kSections = 3 };
struct Terrain {
    uint16_t width = 0, height = 0, era = 0;
    bool hasTile = false;
    std::vector<uint16_t> cells[kSections];
    static size_t cellCount(Part s, uint16_t w, uint16_t h) {
        return s == kIsom ? size_t(w / 2 + 1) * (h + 1) * 4 : size_t(w) * h;
    }
};
// The map's terrain sections (DIM, ERA, the first cells of ISOM, MTXM and TILE). False: a section is missing or short.
bool readTerrain(const Chk& chk, Terrain& out, std::string& error);

struct Run {
    uint8_t section = 0;
    uint32_t at = 0;  // the first cell
    std::vector<uint16_t> values;
};
using Patch = std::vector<Run>;
Patch diffTerrain(const Terrain& from, const Terrain& to);
void applyPatch(Terrain& t, const Patch& p);
void writePatch(Chk& chk, const Patch& p);  // the cells into the sections (they must be as large as readTerrain needs)
// Every run in the terrain's sections, in order, at most all cells; tile values (MTXM, TILE) of a group < tileGroups.
bool validPatch(const Terrain& shape, const Patch& p, size_t tileGroups);
size_t patchCells(const Patch& p);

// --- Units, doodads, sprites, locations (the map's objects) ---------------------------------------------------------------
// Each is a section of the map: UNIT, DD2 and THG2 are lists of records (an edit: the records removed and the records
// added, merged by the host); MRGN is a list of slots (an edit: the slots that changed); STR and STRx (the locations'
// names) are whole (the last edit wins).
enum ObjSection : uint8_t { oUnits = 0, oDoodads, oSprites, oLocations, oStrings, oStringsX, kObjSections };
struct ObjEdit {
    uint8_t section = 0;
    std::vector<Bytes> removed, added;              // records
    std::vector<std::pair<uint32_t, Bytes>> slots;  // slots
    bool whole = false;                             // the whole section (strings)
    Bytes data;
    bool empty() const { return removed.empty() && added.empty() && slots.empty() && !whole; }
};
Bytes readObjects(const Chk& chk, uint8_t section);                // all sections of that name, one after the other
void writeObjects(Chk& chk, uint8_t section, const Bytes& data);   // one section (strings: none when empty)
ObjEdit diffObjects(uint8_t section, const Bytes& from, const Bytes& to);
Bytes applyObjects(uint8_t section, const Bytes& data, const ObjEdit& e);  // (units: a record not found is the unit of its serial)
// A section's change from one state to the next, in order (the host sends each member only this): runs of records the
// member has (from, count) and new records. deltaObjects false: the data is not that section's.
Bytes deltaObjects(uint8_t section, const Bytes& from, const Bytes& to);
bool undeltaObjects(uint8_t section, const Bytes& from, const Bytes& delta, Bytes& out);
uint32_t objectsHash(const Bytes& data);
// The section's limits: its record or slot size, at most so many bytes, and (units) the unit types there are.
bool validObjects(uint8_t section, const Bytes& data, size_t unitTypes);

// --- The session ------------------------------------------------------------------------------------------------------
struct Outbox {
    virtual ~Outbox() = default;
    virtual void text(const std::string& json) = 0;  // a control message to the relay
    virtual void binary(Bytes data) = 0;
    virtual void close() = 0;
};

// Where a person works (all see it): the part of the map in their view, their tool and brush, what they selected.
enum class Tool : uint8_t { None = 0, Terrain, Units, Doodads, Sprites, Locations };
constexpr size_t kMaxSelected = 64;  // selected units or locations that the others see (the first ones)
constexpr uint32_t kNobody = 0xFFFFFFFF;
constexpr size_t kMaxSay = 255;  // a chat message: bytes of UTF-8 (a message's text has a length of one byte)  // (Presence::follows: nobody; 0 is the host)
struct Presence {
    int viewX = 0, viewY = 0, viewW = 0, viewH = 0;  // map pixels (no size: no view)
    Tool tool = Tool::None;
    uint8_t brushW = 0, brushH = 0;    // terrain brush: tiles (0: none)
    std::vector<uint32_t> units;       // the selected units (their serials)
    std::vector<uint16_t> locations;   // the selected locations (their indexes)
    uint16_t zoom = 0;                 // the view's zoom x 256 (0: none)
    int selLeft = 0, selTop = 0, selRight = 0, selBottom = 0;  // the terrain selection's box (map pixels; no size: none)
    uint32_t follows = kNobody;        // the person whose view this one follows (kNobody: none)
    bool operator==(const Presence&) const = default;
};

// An edit of another person, as this program applied it: who, what (0: terrain; 1 + ObjSection: units, doodads, ...),
// how many cells or records, and where (map pixels: left, top, right, bottom; at most kMaxChangeRects).
constexpr size_t kMaxChangeRects = 64;
struct Change {
    uint32_t who = 0;
    uint8_t what = 0;
    uint32_t count = 0;
    std::vector<std::array<int, 4>> rects;
};

struct Peer {
    uint32_t id = 0;  // 0: the host
    std::string name;
    Role role = Role::None;
    uint8_t color = 0;  // 0-7 (the program picks the colors)
    bool approved = false;
    std::string code;  // the safety code both sides see (6 digits)
    bool cursorOn = false;
    int x = 0, y = 0;  // map pixels
    double cursorTime = 0;  // when it last moved (seconds, the session's clock)
    std::string chat;       // cursor chat: the text at the cursor ("": none), at most kMaxChat bytes
    Presence presence;
};

class Session {
public:
    struct Events {
        // Host: someone with a link wants to join (approve() answers; until then, nothing goes to them).
        std::function<void(const Peer&)> joinRequest;
        // Guest: the host let us in: make the map from `terrain` and give its editor (setEditor). False: could not.
        std::function<bool(const Terrain&, const std::string& hostName, Role role)> welcome;
        std::function<void(const std::string&)> status;  // a short message for the status bar
        std::function<void(const std::string&)> ended;    // the session ended (why); the Session can be deleted
        std::function<void()> peersChanged;              // members, roles or cursors changed
        std::function<void()> linksChanged;              // host: the room is ready, or new links
        std::function<void(const Change&)> changed;      // another person's edit was applied (the change flash, the activity)
        std::function<void(uint32_t who, int x, int y)> ping;  // someone points at a place (map pixels)
        std::function<void(uint32_t who, const std::string& text)> said;  // someone's chat message (not this user's own)
        // The connection to the server was lost (true): make a new one to reconnectUrl() (and call opened() etc. as for
        // the first), or reconnectFailed() to give up. False: the session is live again.
        std::function<void(bool lost)> connection;
        std::function<void()> becameHost;                // guest: the host handed the session over: this program hosts now
    };
    using Clock = std::function<double()>;  // seconds

    // Host the map open in `editor`. False (and `error`): the map cannot be shared (no ISOM, a short section, no random).
    static std::unique_ptr<Session> host(Editor& editor, const std::string& server, const std::string& name, Outbox& out, Events ev,
                                         std::string& error);
    static std::unique_ptr<Session> join(const InviteLink& link, const std::string& name, const std::string& version, Outbox& out, Events ev);
    ~Session();

    // The transport's events (the UI thread).
    void opened();
    void received(const std::string& text);
    void received(const Bytes& data);
    void closed(const std::string& why);

    // Read this editor's changes and send them; apply the others' (not during a stroke). The program calls it often
    // (every 50-100 ms) and after each received message.
    void tick();
    void setClock(Clock c) { clock_ = std::move(c); }

    bool isHost() const { return host_; }
    uint32_t selfId() const { return me(); }  // this program's number in the session (0: the host)
    bool running() const { return !ended_; }
    Role role() const { return role_; }
    Editor* editor() const { return ed_; }
    void setEditor(Editor* ed);  // guest: the map that welcome made
    const std::vector<Peer>& peers() const { return peers_; }  // host: everyone but the host; guest: the others and the host
    std::string link(Role r) const;  // host: the invite link (empty until the relay made the room, or when it is off)
    // Guest: the safety code of the connection to the host ("" until the keys are made); whether the server took the join.
    std::string safetyCode() const;
    const std::string& hostName() const { return hostName_; }

    // Host.
    void approve(uint32_t id, bool allow);
    void setRole(uint32_t id, Role r);
    void remove(uint32_t id);
    bool newLinks();  // new secrets: the old links stop working (people who joined stay)
    void stop();      // host: ends the session for all; guest: leaves
    void editorClosed();  // the map's tab closed (its editor is gone): the session ends
    // While on, others' edits wait (a window that holds the units by their places in the list: Unit properties).
    void setHold(bool on) { hold_ = on; }

    // After the editor's undo or redo: the terrain cells that another user wrote last go back to their values (an undo
    // takes back only this user's own edits), and this user's are sent.
    void afterUndo();

    // This user's cursor (map pixels; off: not over the map). Sent at most 20 times a second.
    void cursor(int x, int y, bool on);
    // Cursor chat: the text at this user's cursor, as it is typed ("": none). Everyone in the session sees it.
    void chat(const std::string& text);
    // A new display name (Program options): the others see it at once.
    void rename(const std::string& name);
    // This user's view, tool and selection (sent at most 5 times a second, when it changed).
    void presence(const Presence& p);
    const Presence& myPresence() const { return presence_; }
    // Points at a place of the map for all (map pixels).
    void ping(int x, int y);
    // A chat message to all in the session (at most kMaxSay bytes; the others get it as Events::said).
    void say(const std::string& text);

    // Reconnecting: the connection was lost and a new one is on its way (the session waits up to kReconnectFor).
    bool reconnecting() const { return reconnecting_; }
    std::string reconnectUrl() const { return relayUrl(server_, room_); }
    void reconnectFailed();
    static constexpr double kReconnectFor = 120;  // seconds (the relay keeps a room that long without its host)
    // Host: gives the session to an editor (who hosts it from then on, with the same links); this program leaves it.
    bool handOver(uint32_t id);

    struct Link;  // (a member's keys and counters: collab.cpp)

private:
    Session() = default;
    struct Waiting {
        uint32_t from = 0, seq = 0;
        Patch patch;
    };
    void end(const std::string& why, bool notify = true);
    void sendTo(uint32_t id, const Bytes& plain);
    void sendSealed(Link& l, uint32_t id, const Bytes& plain);
    void hostFrame(uint32_t from, const uint8_t* p, size_t n);
    void guestFrame(const uint8_t* p, size_t n);
    void hostMessage(uint32_t from, const Bytes& m);
    void guestMessage(const Bytes& m);
    bool flushLocal(bool force = false);
    void flushObjects(bool undone);
    void objectsFromHost(uint8_t section, uint32_t ack, uint32_t writer, Bytes data);
    void applyObjectsToEditor(uint8_t section, const Bytes& data);
    void hostObjects(uint32_t from, uint32_t seq, ObjEdit e);
    void noteMine(uint8_t section, const ObjEdit& e, const std::vector<std::pair<Bytes, Bytes>>& pairs);
    ObjEdit filterUndo(uint8_t section, const ObjEdit& e, std::vector<std::pair<Bytes, Bytes>>& pairs);
    void applyRemote(const Waiting& w, Terrain* editorNow);
    void sendCursors();
    uint32_t me() const { return host_ ? 0 : myId_; }
    bool applyToEditor(const Patch& p);
    void sendPatch(const Patch& p);
    void broadcastPeers();
    Peer* peer(uint32_t id);
    void forget(uint32_t id);
    double now() const { return clock_ ? clock_() : 0; }

    bool host_ = false, ended_ = false, inTick_ = false;
    Role role_ = Role::None;
    Editor* ed_ = nullptr;
    Outbox* out_ = nullptr;
    Events ev_;
    Clock clock_;
    std::string server_, room_, name_, version_, hostName_;
    Secret secrets_[3]{};  // host: by Role (View, Edit); guest: secrets_[0] is the link's
    std::vector<Peer> peers_;
    std::map<uint32_t, std::unique_ptr<Link>> links_;  // host: by member; guest: [0], the host
    Terrain base_;  // the terrain as this program last sent or applied it
    uint64_t seenRevision_ = 0;
    // Guest: own patches the host has not confirmed (by number), and how many of them change each cell.
    uint32_t nextSeq_ = 1;
    std::map<uint32_t, Patch> unconfirmed_;
    std::map<uint64_t, int> pending_;
    std::vector<uint32_t> writer_[kSections];  // who wrote each cell last (0: the host)
    uint64_t maxState_ = 0;                    // the editor's newest state seen (an older one: an undo or a redo)
    bool undoFilter_ = false;
    double lastFlush_ = -1, cursorsSent_ = 0;
    bool cursorsDirty_ = false;
    size_t tileGroups_ = 0;                    // guest: the host's tileset's group count (WELCOME)
    std::map<uint32_t, double> linkTime_;      // host: when each connection came (one with no keys in 10 s goes)
    int cursorX_ = 0, cursorY_ = 0;
    bool cursorOn_ = false, cursorDirty_ = false;
    std::string chat_;
    double cursorSent_ = 0;
    uint8_t nextColor_ = 1;
    uint32_t myId_ = 0;  // guest: the relay's number for this program
    std::vector<Waiting> later_;  // patches that wait for a stroke to end (in order)
    // The objects. objBase_: the host's (host: as sent; guest: as the host sent it). Guest: objShown_, what the editor
    // has (the host's with this program's edits that the host did not confirm yet, objPending_).
    struct ObjPending {
        uint32_t seq = 0;
        ObjEdit edit;
    };
    Bytes objBase_[kObjSections], objShown_[kObjSections];
    std::vector<ObjPending> objPending_[kObjSections];
    uint32_t objWriter_[kObjSections] = {};
    bool objDirty_[kObjSections] = {};
    double objSent_ = 0;
    std::map<uint32_t, uint32_t> objAck_;  // host: each member's last edit that the host applied
    std::map<uint32_t, std::array<Bytes, kObjSections>> objSentTo_;  // host: each section as each member has it
    std::map<uint32_t, std::array<bool, kObjSections>> objKnown_;    // (false: the member gets the whole section next)
    std::map<Bytes, int> mineAdded_[kObjSections];    // this user's records, as this user left them (the undo's filter)
    std::map<Bytes, Bytes> mineFrom_[kObjSections];     // a record of this user's change (a move, a property): the one it came from
    std::map<Bytes, int> mineDeleted_[kObjSections];  // records this user deleted (an undo brings back only these)
    std::vector<uint32_t> slotWriter_[kObjSections];
    struct ObjWaiting {
        uint8_t section = 0;
        uint32_t ack = 0, writer = 0;
        Bytes data;
        bool delta = false;
        uint32_t hash = 0;
    };
    std::vector<ObjWaiting> objLater_;  // guest: the host's object states that wait for a stroke to end
    struct ObjEditWaiting {
        uint32_t from = 0, seq = 0;
        ObjEdit edit;
    };
    std::vector<ObjEditWaiting> hostObjLater_;  // host: members' object edits that wait for a stroke to end
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> serialMap_;  // host: (member, its unit's serial) -> the serial the host gave
    size_t slotLimit_ = 0;  // locations: at most the map's slots at the start (64 or 255)
    bool hold_ = false;
    // Presence: this user's (sent when dirty), the host's list of all (sent when dirty).
    Presence presence_;
    bool presenceDirty_ = false, presencesDirty_ = false;
    double presenceSent_ = 0, presencesSent_ = 0;
    // Reconnecting. The host gives each member a ticket at the welcome: a member that comes back with it is let in
    // again at once, as the same person (name, color, role). hostSecret_: the room's host token's secret (the relay
    // gives the room back to the host that has it).
    struct Ticket {
        std::string name;
        uint8_t color = 0;
        Role role = Role::None;
        uint32_t lastSeq = 0;  // the member's last edit that the host applied (it does not go again after a reconnect)
    };
    bool reconnecting_ = false;
    Secret hostSecret_{};
    // The members' secret (all who are in have it, from the welcome): a member comes back with it, also after new links.
    Secret memberSecret_{};
    bool hasMemberSecret_ = false;
    std::map<uint32_t, uint32_t> resumeAck_;  // host: a member that came back with its ticket: its last applied edit
    // Guest: a welcome or a handover that waits for the stroke or the hold to end (as the others' edits do).
    bool welcomeWaiting_ = false, handoverWaiting_ = false;
    Terrain welcomeTerrain_;
    Bytes welcomeObjs_[kObjSections];
    uint32_t welcomeAck_ = 0;
    Bytes handover_;
    void noteApplied(uint32_t from, uint32_t seq);
    std::string ticket_;                       // guest: this program's ticket (16 bytes)
    std::map<std::string, Ticket> tickets_;    // host: the members' tickets
    std::map<uint32_t, std::string> ticketOf_; // host: each member's ticket
    void lost();
    void welcomeAgain(const Terrain& t, Bytes* objs, uint32_t ack);
    bool becomeHost(const uint8_t* p, size_t n);
    void sendPresences();
    void report(uint32_t who, uint8_t what, const Patch* p, const Bytes* before, const Bytes* after);
};

}  // namespace collab
}  // namespace stardraft
