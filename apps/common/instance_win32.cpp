// One program for all maps on Windows (instance.h): a named pipe. A system mutex makes programs that start at the same
// moment take turns: the first one makes the pipe before the next one looks for it. The listener makes the pipe's
// next instance before it closes the one that a program sent to, so a program that sends at that moment finds one.

#include "instance.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <thread>

namespace fs = std::filesystem;

namespace stardraft::platform {

namespace {

bool sendTo(const std::wstring& name, const std::string& text) {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int tries = 0; tries < 5 && pipe == INVALID_HANDLE_VALUE; ++tries) {
        pipe = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE && (GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipeW(name.c_str(), 2000))) break;
    }
    if (pipe == INVALID_HANDLE_VALUE) return false;
    AllowSetForegroundWindow(ASFW_ANY);  // (the open program's window may come to the front)
    DWORD wrote = 0;
    const bool ok = WriteFile(pipe, text.data(), DWORD(text.size()), &wrote, nullptr) && wrote == text.size();
    FlushFileBuffers(pipe);
    CloseHandle(pipe);
    return ok;
}

HANDLE makePipe(const std::wstring& name, bool first) {
    return CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 0, 64 * 1024, 0,
                            nullptr);
}

void listen(const std::wstring& name, HANDLE first, std::function<void(std::string)> got) {
    std::thread([name, first, got = std::move(got)] {
        HANDLE pipe = first;
        for (;;) {
            const bool connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
            HANDLE next = makePipe(name, false);  // (before this one closes: a program that sends now finds one)
            std::string text;
            if (connected) {
                char buf[4096];
                DWORD n = 0;
                while (ReadFile(pipe, buf, sizeof buf, &n, nullptr) && n) text.append(buf, n);
            }
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            if (!text.empty()) got(std::move(text));
            if (next == INVALID_HANDLE_VALUE) return;
            pipe = next;
        }
    }).detach();
}

}  // namespace

InstanceStart sendOrListen(const fs::path& where, const std::string& text, std::function<void(std::string)> got) {
    const std::wstring name = where.wstring();
    // The mutex's name: the pipe's, in the session's own names.
    std::wstring tail = name.substr(name.find_last_of(L'\\') == std::wstring::npos ? 0 : name.find_last_of(L'\\') + 1);
    HANDLE turn = CreateMutexW(nullptr, FALSE, (L"Local\\" + tail + L"-start").c_str());
    if (turn) WaitForSingleObject(turn, 10000);  // (WAIT_ABANDONED: a program that ended while it had the turn)
    InstanceStart r = InstanceStart::Neither;
    if (!text.empty() && sendTo(name, text)) {
        r = InstanceStart::Sent;
    } else if (HANDLE first = makePipe(name, true); first != INVALID_HANDLE_VALUE) {
        listen(name, first, std::move(got));
        r = InstanceStart::Listening;
    }
    if (turn) ReleaseMutex(turn), CloseHandle(turn);
    return r;
}

}  // namespace stardraft::platform
