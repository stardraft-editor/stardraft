#pragma once

// One program for all maps (Windows: a named pipe, instance_win32.cpp; Linux and the BSDs: a socket file,
// desktop_xdg.cpp). The first program listens; a program that starts later with a file sends it there and ends.
// Programs that start at the same moment (Explorer starts one for each of several selected maps) take turns (a
// system lock), so exactly one of them listens and the others send to it.

#include <filesystem>
#include <functional>
#include <string>

namespace stardraft::platform {

enum class InstanceStart {
    Sent,       // a program listens: `text` went to it (this program ends)
    Listening,  // this program is the first: it listens now, and the text of each later program goes to `got`
    Neither,    // no program listens and this one cannot (the pipe or the socket cannot be made)
};

// `where`: the pipe (\\.\pipe\<name>) or the socket file. `text` empty: only listen, when no program listens yet.
// `got` runs on the listener's own thread.
InstanceStart sendOrListen(const std::filesystem::path& where, const std::string& text, std::function<void(std::string)> got);

}  // namespace stardraft::platform
