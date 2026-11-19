#pragma once
// Shared maps (collaborative terrain editing, roadmap: phase 1, terrain only). One program hosts the map; others join it
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

// Random bytes from the system (BCryptGenRandom, getrandom, arc4random). False: none (do not go on).
bool randomBytes(void* out, size_t n);

// --- Invite links -----------------------------------------------------------------------------------------------------
// https://<server>/j/<room>#<e|v>.<secret, base64url>; or stardraft://join?link=<that link, URL-encoded>.
struct InviteLink {
    std::string server;  // "https://host[:port]" (or "http://" for this computer only: localhost, 127.0.0.1)
    std::string room;    // 22 characters (128 bits, base64url)
    Role role = Role::None;
    Key secret{};
};
std::string makeInviteLink(const std::string& server, const std::string& room, Role role, const Key& secret);
bool parseInviteLink(const std::string& text, InviteLink& out, std::string& error);
// A server address as the user typed it: "https://host[:port]" with no path. http only for this computer.
bool normalizeServer(const std::string& text, std::string& out, std::string& error);
std::string relayUrl(const std::string& server);  // ws(s)://host[:port]/relay
std::string relayToken(const Key& secret);        // what the relay checks for a link (64 hex digits); not the secret

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

// --- The session ------------------------------------------------------------------------------------------------------
struct Outbox {
    virtual ~Outbox() = default;
    virtual void text(const std::string& json) = 0;  // a control message to the relay
    virtual void binary(Bytes data) = 0;
    virtual void close() = 0;
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
    bool running() const { return !ended_; }
    Role role() const { return role_; }
    Editor* editor() const { return ed_; }
    void setEditor(Editor* ed);  // guest: the map that welcome made
    const std::vector<Peer>& peers() const { return peers_; }  // host: everyone but the host; guest: the others and the host
    std::string link(Role r) const;  // host: the invite link (empty until the relay made the room, or when it is off)

    // Host.
    void approve(uint32_t id, bool allow);
    void setRole(uint32_t id, Role r);
    void remove(uint32_t id);
    bool newLinks();  // new secrets: the old links stop working (people who joined stay)
    void stop();      // host: ends the session for all; guest: leaves
    void editorClosed();  // the map's tab closed (its editor is gone): the session ends

    // After the editor's undo or redo: the terrain cells that another user wrote last go back to their values (an undo
    // takes back only this user's own edits), and this user's are sent.
    void afterUndo();

    // This user's cursor (map pixels; off: not over the map). Sent at most 20 times a second.
    void cursor(int x, int y, bool on);

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
    Key secrets_[3]{};  // host: by Role (View, Edit); guest: secrets_[0] is the link's
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
    double cursorSent_ = 0;
    uint8_t nextColor_ = 1;
    uint32_t myId_ = 0;  // guest: the relay's number for this program
    std::vector<Waiting> later_;  // patches that wait for a stroke to end (in order)
};

}  // namespace collab
}  // namespace stardraft
