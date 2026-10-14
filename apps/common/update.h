#pragma once

// The self-update's system part: the running program's file, unpacking a release's archive, putting a new program in
// place of the running one, and starting it. Windows: update_win32.cpp; Linux and macOS: update_posix.cpp. Finding a
// release (GitHub) and what the user is asked stay in the program.

#include <filesystem>
#include <string>

namespace stardraft::platform {

// The running program's file.
std::filesystem::path programFile();

// The name ending of this system's release download ("-win64.zip"); other systems' downloads end in other ways.
const char* releaseAssetSuffix();

// Unpacks a release's archive (.zip, or .tar.gz on Linux and macOS) into a folder (Windows: tar.exe; Linux: unzip or
// tar; macOS: tar). False when it did not work.
bool unpackArchive(const std::filesystem::path& archive, const std::filesystem::path& dir);

// Puts `fresh` in place of the running program `self`: self becomes oldProgramFile(self), fresh takes its name.
// NewNotPlaced: the old program is back in place. Broken: it could not be put back; it is at oldProgramFile(self).
enum class Replace { Done, OldNotMoved, NewNotPlaced, Broken };
Replace replaceProgram(const std::filesystem::path& self, const std::filesystem::path& fresh);
std::filesystem::path oldProgramFile(const std::filesystem::path& self);  // stardraft.exe.old

// Starts a program (its folder the working folder), not waiting for it. False when it did not start.
bool startProgram(const std::filesystem::path& exe);

}  // namespace stardraft::platform
