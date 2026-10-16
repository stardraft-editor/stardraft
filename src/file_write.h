#pragma once

// A checked write of a whole user file.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

namespace stardraft {

// Writes `bytes` to `file` (replaces it). True when the write and the close succeed; else `error` gives the file.
// The bytes go into a new file beside it first, which then takes the file's place (a rename): a full disk, a crash or
// a power loss during the write leaves the old file as it was, never an empty or cut-off one.
inline bool writeFile(const std::filesystem::path& file, std::string_view bytes, std::string& error) {
    // The new file's name is this write's own (two writes at once, in one program or two, do not share one).
    static std::atomic<unsigned> count{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path part = file;
    part += ".part" + std::to_string(static_cast<unsigned long long>(stamp) % 1000000007ULL) + "-" + std::to_string(++count);
    std::error_code ec;
    {
        std::ofstream out(part, std::ios::binary);
        out.write(bytes.data(), std::streamsize(bytes.size()));
        out.close();  // the close writes the rest of the buffer: check after it
        if (!out) {
            std::filesystem::remove(part, ec);
            return error = "cannot write " + file.string(), false;
        }
    }
    // (Windows: a program that has the file open for a moment, as a virus scanner, makes the rename fail: a few tries)
    for (int tries = 0; tries < 10; ++tries) {
        std::filesystem::rename(part, file, ec);
        if (!ec) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (ec) {
        error = "cannot replace " + file.string() + " (" + ec.message() + ")";
        std::filesystem::remove(part, ec);
        return false;
    }
    return true;
}

}  // namespace stardraft
