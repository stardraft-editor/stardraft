#pragma once

// A checked write of a whole user file.

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace stardraft {

// Writes `bytes` to `file` (replaces it). True when the write and the close succeed; else `error` gives the file.
inline bool writeFile(const std::filesystem::path& file, std::string_view bytes, std::string& error) {
    std::ofstream out(file, std::ios::binary);
    out.write(bytes.data(), std::streamsize(bytes.size()));
    out.close();  // the close writes the rest of the buffer: check after it
    if (!out) return error = "cannot write " + file.string(), false;
    return true;
}

}  // namespace stardraft
