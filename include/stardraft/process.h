#pragma once

// Running another program (euddraft, curl, unzip): no console window, no input, its output line by line.

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace stardraft {

// Runs args[0] (found on PATH when it has no folder) with the other arguments as they are (no shell), in `workDir`
// when it is not empty, calling `onLine` for each line of its output (stdout and stderr merged, without the line
// end). Returns the exit code, or -1 if it could not start; on Linux and macOS a program that a signal stopped (a
// crash) gives 128 + the signal's number, as a shell says it.
int runProcess(const std::vector<std::string>& args, const std::filesystem::path& workDir, const std::function<void(const std::string&)>& onLine);

}  // namespace stardraft
