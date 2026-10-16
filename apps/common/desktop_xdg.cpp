// The free desktops' parts (desktop_xdg.h): the XDG folders, the desktop portal's appearance, notifications and the
// recently used files. D-Bus is libdbus-1.so.3, loaded with dlopen when it is first needed: the program needs no D-Bus
// headers to build and runs on a system without D-Bus (no portal and no notifications then).

#include "desktop_xdg.h"

#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>
#include <type_traits>

namespace fs = std::filesystem;

namespace stardraft::xdg {

namespace {

fs::path home() {
    const char* h = std::getenv("HOME");
    return h && *h ? fs::path(h) : fs::path();
}

// The variable when it is an absolute path (the specification: a relative one is not used), else ~/<fallback>.
fs::path baseDir(const char* var, const char* fallback) {
    if (const char* v = std::getenv(var); v && *v == '/') return fs::path(v);
    const fs::path h = home();
    return h.empty() ? fs::path() : h / fallback;
}

// --- libdbus-1, loaded at run time ---

struct DBusError {  // (its layout in dbus-errors.h)
    const char* name;
    const char* message;
    unsigned int dummy;
    void* padding;
};
struct DBusIter {  // DBusMessageIter: a block that libdbus fills (it is smaller than this)
    alignas(8) unsigned char data[128];
};
using Conn = void;
using Msg = void;

enum : int { kInvalid = 0, kString = 's', kUint32 = 'u', kInt32 = 'i', kVariant = 'v', kDouble = 'd', kArray = 'a', kStruct = 'r', kDictEntry = 'e' };

struct DBus {
    bool loaded = false;
    void (*error_init)(DBusError*) = nullptr;
    void (*error_free)(DBusError*) = nullptr;
    unsigned (*error_is_set)(const DBusError*) = nullptr;
    Conn* (*bus_get_private)(int, DBusError*) = nullptr;
    void (*set_exit_on_disconnect)(Conn*, unsigned) = nullptr;
    unsigned (*name_has_owner)(Conn*, const char*, DBusError*) = nullptr;
    void (*add_match)(Conn*, const char*, DBusError*) = nullptr;
    Msg* (*new_method_call)(const char*, const char*, const char*, const char*) = nullptr;
    unsigned (*append_args)(Msg*, int, ...) = nullptr;
    Msg* (*send_with_reply_and_block)(Conn*, Msg*, int, DBusError*) = nullptr;
    unsigned (*send)(Conn*, Msg*, uint32_t*) = nullptr;
    void (*flush)(Conn*) = nullptr;
    unsigned (*read_write)(Conn*, int) = nullptr;
    Msg* (*pop_message)(Conn*) = nullptr;
    unsigned (*is_signal)(Msg*, const char*, const char*) = nullptr;
    void (*unref)(Msg*) = nullptr;
    unsigned (*iter_init)(Msg*, DBusIter*) = nullptr;
    int (*iter_type)(DBusIter*) = nullptr;
    void (*iter_recurse)(DBusIter*, DBusIter*) = nullptr;
    void (*iter_get_basic)(DBusIter*, void*) = nullptr;
    unsigned (*iter_next)(DBusIter*) = nullptr;
    void (*iter_init_append)(Msg*, DBusIter*) = nullptr;
    unsigned (*iter_append_basic)(DBusIter*, int, const void*) = nullptr;
    unsigned (*iter_open)(DBusIter*, int, const char*, DBusIter*) = nullptr;
    unsigned (*iter_close)(DBusIter*, DBusIter*) = nullptr;
};

DBus& dbus() {
    static DBus d;
    static bool tried = false;
    if (tried) return d;
    tried = true;
    void* lib = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return d;
    bool all = true;
    auto get = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name));
        if (!fn) all = false;
    };
    get(d.error_init, "dbus_error_init"), get(d.error_free, "dbus_error_free"), get(d.error_is_set, "dbus_error_is_set");
    get(d.bus_get_private, "dbus_bus_get_private"), get(d.set_exit_on_disconnect, "dbus_connection_set_exit_on_disconnect");
    get(d.name_has_owner, "dbus_bus_name_has_owner"), get(d.add_match, "dbus_bus_add_match");
    get(d.new_method_call, "dbus_message_new_method_call"), get(d.append_args, "dbus_message_append_args");
    get(d.send_with_reply_and_block, "dbus_connection_send_with_reply_and_block"), get(d.send, "dbus_connection_send");
    get(d.flush, "dbus_connection_flush"), get(d.read_write, "dbus_connection_read_write");
    get(d.pop_message, "dbus_connection_pop_message"), get(d.is_signal, "dbus_message_is_signal"), get(d.unref, "dbus_message_unref");
    get(d.iter_init, "dbus_message_iter_init"), get(d.iter_type, "dbus_message_iter_get_arg_type");
    get(d.iter_recurse, "dbus_message_iter_recurse"), get(d.iter_get_basic, "dbus_message_iter_get_basic");
    get(d.iter_next, "dbus_message_iter_next"), get(d.iter_init_append, "dbus_message_iter_init_append");
    get(d.iter_append_basic, "dbus_message_iter_append_basic"), get(d.iter_open, "dbus_message_iter_open_container");
    get(d.iter_close, "dbus_message_iter_close_container");
    d.loaded = all;
    return d;
}

// The session bus: a connection of this program's own (SDL has its own). None when there is no session bus: no
// address and no $XDG_RUNTIME_DIR/bus (libdbus would start a bus of its own then: that is not wanted).
Conn* session() {
    static Conn* conn = nullptr;
    static bool tried = false;
    if (tried) return conn;
    tried = true;
    DBus& d = dbus();
    if (!d.loaded) return nullptr;
    const char* addr = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    if (!addr || !*addr) {
        const char* run = std::getenv("XDG_RUNTIME_DIR");
        std::error_code ec;
        if (!run || !*run || !fs::exists(fs::path(run) / "bus", ec)) return nullptr;
    }
    DBusError err;
    d.error_init(&err);
    conn = d.bus_get_private(0, &err);  // (DBUS_BUS_SESSION)
    if (d.error_is_set(&err)) d.error_free(&err);
    if (conn) d.set_exit_on_disconnect(conn, 0);
    return conn;
}

bool hasOwner(const char* name) {
    Conn* c = session();
    if (!c) return false;
    DBus& d = dbus();
    DBusError err;
    d.error_init(&err);
    const bool yes = d.name_has_owner(c, name, &err) != 0;
    if (d.error_is_set(&err)) d.error_free(&err);
    return yes;
}

// A method call that waits for its reply (at most `timeoutMs`); null on an error, with the error's name in `*error`.
Msg* call(Msg* m, int timeoutMs, std::string* error = nullptr) {
    Conn* c = session();
    DBus& d = dbus();
    if (!c || !m) {
        if (m) d.unref(m);
        return nullptr;
    }
    DBusError err;
    d.error_init(&err);
    Msg* reply = d.send_with_reply_and_block(c, m, timeoutMs, &err);
    d.unref(m);
    if (d.error_is_set(&err)) {
        if (error && err.name) *error = err.name;
        d.error_free(&err);
    }
    return reply;
}

// The portal reads run on the UI thread: each one waits at most kPortalWaitMs. After a read that got no reply (a
// portal that hangs while it starts), the reads stop for kPortalPauseS, so the window does not wait again and again.
constexpr int kPortalWaitMs = 250;
constexpr std::time_t kPortalPauseS = 10;
std::time_t gPortalPausedUntil = 0;

bool portalPaused() {
    return gPortalPausedUntil != 0 && std::time(nullptr) < gPortalPausedUntil;
}

// The portal's setting (ReadOne, else the older Read when the portal has no ReadOne): the reply's value with its
// variants opened, in `out` (which points into `reply`: unref the reply after use). Null: no portal or no such setting.
Msg* readSetting(const char* ns, const char* key, DBusIter& out) {
    if (!session() || portalPaused()) return nullptr;
    DBus& d = dbus();
    for (const char* method : {"ReadOne", "Read"}) {
        Msg* m = d.new_method_call("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Settings", method);
        if (!m) return nullptr;
        d.append_args(m, kString, &ns, kString, &key, kInvalid);
        std::string error;
        Msg* reply = call(m, kPortalWaitMs, &error);
        if (!reply) {
            if (error == "org.freedesktop.DBus.Error.NoReply" || error == "org.freedesktop.DBus.Error.Timeout" ||
                error == "org.freedesktop.DBus.Error.TimedOut") {
                gPortalPausedUntil = std::time(nullptr) + kPortalPauseS;
                return nullptr;
            }
            if (error == "org.freedesktop.DBus.Error.UnknownMethod") continue;  // (an old portal: try Read)
            return nullptr;
        }
        DBusIter it;
        if (!d.iter_init(reply, &it)) {
            d.unref(reply);
            return nullptr;
        }
        while (d.iter_type(&it) == kVariant) {
            DBusIter inner;
            d.iter_recurse(&it, &inner);
            it = inner;
        }
        out = it;
        return reply;
    }
    return nullptr;
}

std::string isoNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[40];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// file:///a%20b/c.scx: the absolute path, its bytes other than the unreserved ones and "/" as %XX.
std::string fileUri(const fs::path& file) {
    std::error_code ec;
    const std::string p = fs::absolute(file, ec).lexically_normal().string();
    std::string out = "file://";
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : p) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '.' || c == '_' || c == '~') out += char(c);
        else out += '%', out += hex[c >> 4], out += hex[c & 15];
    }
    return out;
}

std::string xmlAttr(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '"') o += "&quot;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '\'') o += "&apos;";
        else o += c;
    }
    return o;
}

// In the tag that starts at `tag` (to its '>'): the attribute's value set (added when the tag does not have it).
void setAttr(std::string& xml, size_t tag, const std::string& name, const std::string& value) {
    const size_t end = xml.find('>', tag);
    if (end == std::string::npos) return;
    const std::string key = " " + name + "=\"";
    const size_t at = xml.find(key, tag);
    if (at != std::string::npos && at < end) {
        const size_t v = at + key.size(), q = xml.find('"', v);
        if (q != std::string::npos && q < end) return void(xml.replace(v, q - v, value));
    }
    const size_t close = xml[end - 1] == '/' ? end - 1 : end;
    xml.insert(close, key + value + "\"");
}

const char* const kAppName = "StarDraft 2";
const char* const kExec = "&apos;stardraft %u&apos;";

std::string applicationTag(const std::string& now, int count) {
    return std::string("<bookmark:application name=\"") + kAppName + "\" exec=\"" + kExec + "\" modified=\"" + now + "\" count=\"" +
           std::to_string(count) + "\"/>";
}

}  // namespace

fs::path configHome() { return baseDir("XDG_CONFIG_HOME", ".config"); }
fs::path dataHome() { return baseDir("XDG_DATA_HOME", ".local/share"); }
fs::path cacheHome() { return baseDir("XDG_CACHE_HOME", ".cache"); }
fs::path stateHome() { return baseDir("XDG_STATE_HOME", ".local/state"); }

fs::path appImageFile() {
    const char* a = std::getenv("APPIMAGE");
    return a && *a ? fs::path(a) : fs::path();
}
bool inFlatpak() {
    std::error_code ec;
    return fs::exists("/.flatpak-info", ec);
}

int portalColorScheme() {
    DBusIter it;
    Msg* reply = readSetting("org.freedesktop.appearance", "color-scheme", it);
    if (!reply) return 0;
    uint32_t v = 0;
    DBus& d = dbus();
    if (d.iter_type(&it) == kUint32) d.iter_get_basic(&it, &v);
    d.unref(reply);
    return v == 1 || v == 2 ? int(v) : 0;
}

bool portalAccent(uint8_t& r, uint8_t& g, uint8_t& b) {
    DBusIter it;
    Msg* reply = readSetting("org.freedesktop.appearance", "accent-color", it);
    if (!reply) return false;
    DBus& d = dbus();
    double c[3] = {-1, -1, -1};
    if (d.iter_type(&it) == kStruct) {
        DBusIter s;
        d.iter_recurse(&it, &s);
        for (int i = 0; i < 3; ++i) {
            if (d.iter_type(&s) != kDouble) break;
            d.iter_get_basic(&s, &c[i]);
            if (!d.iter_next(&s)) break;
        }
    }
    d.unref(reply);
    for (double v : c)
        if (v < 0 || v > 1) return false;  // (out of range: the user set no accent color)
    r = uint8_t(c[0] * 255 + 0.5), g = uint8_t(c[1] * 255 + 0.5), b = uint8_t(c[2] * 255 + 0.5);
    return true;
}

namespace {
bool gWatching = false;
}

void watchLook() {
    if (gWatching) return;
    Conn* c = session();
    if (!c) return;
    DBus& d = dbus();
    DBusError err;
    d.error_init(&err);
    d.add_match(c, "type='signal',interface='org.freedesktop.portal.Settings',member='SettingChanged'", &err);
    if (d.error_is_set(&err)) return d.error_free(&err);
    gWatching = true;
}

bool lookChanged() {
    if (!gWatching) return false;
    Conn* c = session();
    DBus& d = dbus();
    bool changed = false;
    d.read_write(c, 0);  // (reads what came; does not wait)
    while (Msg* m = d.pop_message(c)) {
        if (d.is_signal(m, "org.freedesktop.portal.Settings", "SettingChanged")) {
            DBusIter it;
            const char* ns = nullptr;
            if (d.iter_init(m, &it) && d.iter_type(&it) == kString) d.iter_get_basic(&it, &ns);
            if (ns && std::strcmp(ns, "org.freedesktop.appearance") == 0) changed = true;
        }
        d.unref(m);
    }
    return changed;
}

bool notify(const std::string& summary, const std::string& body) {
    if (!hasOwner("org.freedesktop.Notifications")) return false;
    DBus& d = dbus();
    Msg* m = d.new_method_call("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify");
    if (!m) return false;
    DBusIter it, actions, hints, entry, value;
    const char* app = kAppName;
    const char* icon = kAppId;
    const char* s = summary.c_str();
    const char* text = body.c_str();
    const uint32_t replaces = 0;
    const int32_t timeout = -1;  // (the server's own time)
    const char* key = "desktop-entry";
    const char* id = kAppId;
    d.iter_init_append(m, &it);
    d.iter_append_basic(&it, kString, &app);
    d.iter_append_basic(&it, kUint32, &replaces);
    d.iter_append_basic(&it, kString, &icon);
    d.iter_append_basic(&it, kString, &s);
    d.iter_append_basic(&it, kString, &text);
    d.iter_open(&it, kArray, "s", &actions);
    d.iter_close(&it, &actions);
    d.iter_open(&it, kArray, "{sv}", &hints);
    d.iter_open(&hints, kDictEntry, nullptr, &entry);
    d.iter_append_basic(&entry, kString, &key);
    d.iter_open(&entry, kVariant, "s", &value);
    d.iter_append_basic(&value, kString, &id);
    d.iter_close(&entry, &value);
    d.iter_close(&hints, &entry);
    d.iter_close(&it, &hints);
    d.iter_append_basic(&it, kInt32, &timeout);
    const bool sent = d.send(session(), m, nullptr) != 0;
    d.unref(m);
    if (sent) d.flush(session());
    return sent;
}

fs::path recentFile() {
    const fs::path d = dataHome();
    return d.empty() ? fs::path() : d / "recently-used.xbel";
}

std::string mimeTypeOf(const fs::path& file) {
    std::string ext = file.extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".scm") return "application/x-starcraft-scm";
    if (ext == ".scx") return "application/x-starcraft-scx";
    if (ext == ".eds") return "application/x-euddraft-project";
    if (ext == ".stardraft") return "application/x-stardraft-project";
    std::error_code ec;
    if (fs::is_directory(file, ec)) return "inode/directory";
    return "application/octet-stream";
}

bool addRecent(const fs::path& xbel, const fs::path& file, const std::string& mimeType, std::string& error) {
    std::string xml;
    {
        std::ifstream in(xbel, std::ios::binary);
        if (in) {
            std::ostringstream s;
            s << in.rdbuf();
            xml = s.str();
        }
    }
    if (xml.find("</xbel>") == std::string::npos)  // (no file, or not one of these: a new one)
        xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<xbel version=\"1.0\"\n"
              "      xmlns:bookmark=\"http://www.freedesktop.org/standards/desktop-bookmarks\"\n"
              "      xmlns:mime=\"http://www.freedesktop.org/standards/shared-mime-info\"\n>\n</xbel>\n";
    const std::string now = isoNow(), href = xmlAttr(fileUri(file));
    const size_t at = xml.find("<bookmark href=\"" + href + "\"");
    size_t end = at == std::string::npos ? std::string::npos : xml.find("</bookmark>", at);
    if (at != std::string::npos && end != std::string::npos) {
        // The file's entry is there: its times, and this program's count (other programs' entries stay).
        const std::string name = std::string("<bookmark:application name=\"") + kAppName + "\"";
        const size_t app = xml.find(name, at);
        if (app != std::string::npos && app < end) {
            int count = 0;
            const size_t c = xml.find(" count=\"", app);
            if (c != std::string::npos && c < xml.find('>', app)) count = std::atoi(xml.c_str() + c + 8);
            setAttr(xml, app, "modified", now);
            setAttr(xml, app, "count", std::to_string(count + 1));
        } else if (const size_t apps = xml.find("</bookmark:applications>", at); apps != std::string::npos && apps < end) {
            xml.insert(apps, applicationTag(now, 1));
        }
        setAttr(xml, at, "modified", now);
        setAttr(xml, at, "visited", now);
    } else {
        const std::string entry = "  <bookmark href=\"" + href + "\" added=\"" + now + "\" modified=\"" + now + "\" visited=\"" + now +
                                  "\">\n    <info>\n      <metadata owner=\"http://freedesktop.org\">\n        <mime:mime-type type=\"" +
                                  xmlAttr(mimeType) + "\"/>\n        <bookmark:applications>\n          " + applicationTag(now, 1) +
                                  "\n        </bookmark:applications>\n      </metadata>\n    </info>\n  </bookmark>\n";
        xml.insert(xml.rfind("</xbel>"), entry);
    }
    std::error_code ec;
    fs::create_directories(xbel.parent_path(), ec);
    fs::path tmp = xbel;
    tmp += ".stardraft-" + std::to_string(getpid());
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << xml;
        if (!out.flush()) {
            error = "could not write " + tmp.string();
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, xbel, ec);
    if (ec) {
        error = "could not replace " + xbel.string() + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// --- One program for all maps ---

fs::path instanceSocket(const std::string& name) {
    std::error_code ec;
    fs::path dir;
    if (const char* run = std::getenv("XDG_RUNTIME_DIR"); run && *run == '/' && fs::is_directory(run, ec)) {
        dir = run;
        // (a Flatpak: $XDG_RUNTIME_DIR is the sandbox's own; app/<id> is shared by all of the app's sandboxes)
        const char* id = std::getenv("FLATPAK_ID");
        if (inFlatpak() && id && *id && fs::is_directory(dir / "app" / id, ec)) dir = dir / "app" / id;
    } else {
        dir = "/tmp/stardraft-" + std::to_string(getuid());
        mkdir(dir.c_str(), 0700);
        struct stat st{};
        // (a folder that another user made, or that others can open, is not used)
        if (lstat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 077) != 0) return {};
    }
    const fs::path p = dir / name;
    return p.string().size() < sizeof(sockaddr_un{}.sun_path) ? p : fs::path();
}

namespace {

bool socketAddress(const fs::path& socket, sockaddr_un& a) {
    a = {};
    a.sun_family = AF_UNIX;
    const std::string s = socket.string();
    if (s.empty() || s.size() >= sizeof a.sun_path) return false;
    std::memcpy(a.sun_path, s.c_str(), s.size() + 1);
    return true;
}

// A connection to the program that listens on `socket` (-1: none listens).
int connectTo(const fs::path& socket) {
    sockaddr_un a;
    if (!socketAddress(socket, a)) return -1;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

}  // namespace

bool sendToInstance(const fs::path& socket, const std::string& text) {
    const int fd = connectTo(socket);
    if (fd < 0) return false;
    size_t sent = 0;
    while (sent < text.size()) {
        const ssize_t n = ::send(fd, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += size_t(n);
    }
    shutdown(fd, SHUT_WR);
    char done;
    while (recv(fd, &done, 1, 0) > 0) {}  // (until the open program has read it all and closes the connection)
    close(fd);
    return sent == text.size();
}

bool listenAsInstance(const fs::path& socket, std::function<void(std::string)> got) {
    sockaddr_un a;
    if (!socketAddress(socket, a)) return false;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    if (bind(fd, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) {
        // The file is there: another program listens (it stays the first), or a program that ended left the file.
        if (errno != EADDRINUSE) return close(fd), false;
        if (const int other = connectTo(socket); other >= 0) return close(other), close(fd), false;
        unlink(a.sun_path);
        if (bind(fd, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) return close(fd), false;
    }
    if (listen(fd, 8) != 0) return close(fd), false;
    std::thread([fd, got = std::move(got)] {
        for (;;) {
            const int c = accept4(fd, nullptr, nullptr, SOCK_CLOEXEC);
            if (c < 0) {
                if (errno == EINTR || errno == ECONNABORTED) continue;
                return;
            }
            std::string text;
            char buf[4096];
            for (ssize_t n; (n = recv(c, buf, sizeof buf, 0)) > 0 && text.size() < (1u << 20);) text.append(buf, size_t(n));
            close(c);
            if (!text.empty()) got(std::move(text));  // (an empty one: a program that looked if this one listens)
        }
    }).detach();
    return true;
}

std::string report() {
    std::string out;
    const bool bus = session() != nullptr;
    out += std::string("desktop: libdbus ") + (dbus().loaded ? "yes" : "no") + ", session bus " + (bus ? "yes" : "no") + "\n";
    const bool portal = bus && hasOwner("org.freedesktop.portal.Desktop");
    out += std::string("desktop: portal ") + (portal ? "yes" : "no") + "\n";
    if (portal) {
        const int scheme = portalColorScheme();
        out += std::string("desktop: color scheme ") + (scheme == 1 ? "dark" : scheme == 2 ? "light" : "no preference") + "\n";
        uint8_t r = 0, g = 0, b = 0;
        if (portalAccent(r, g, b)) out += "desktop: accent color " + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "\n";
        else out += "desktop: accent color none\n";
    }
    out += std::string("desktop: notifications ") + (bus && hasOwner("org.freedesktop.Notifications") ? "yes" : "no") + "\n";
    return out;
}

}  // namespace stardraft::xdg
