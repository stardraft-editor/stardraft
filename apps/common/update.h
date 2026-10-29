#pragma once

// The self-update's system part: the running program's file, unpacking a release's archive, putting a new program in
// place of the running one, and starting it. Windows: update_win32.cpp; Linux and macOS: update_posix.cpp. Finding a
// release (GitHub) and what the user is asked stay in the program.

#include <filesystem>
#include <string>
#include <vector>

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
std::filesystem::path oldProgramFile(const std::filesystem::path& self);  // StarDraft 2.exe.old
// After replaceProgram, when the new program did not start: the new file goes and the old program is at `self` again.
// False when the old program could not be put back (it stays at oldProgramFile(self)).
bool restoreProgram(const std::filesystem::path& self);
// The new program of an update, running, that cannot work on this computer (no graphics device): the old program goes
// back in its place (Windows: the running file is renamed to failedProgramFile(self), as a running program's file cannot
// be replaced). False when there is no old program or it could not be put back (then nothing changed).
bool rollBackUpdate(const std::filesystem::path& self);
std::filesystem::path failedProgramFile(const std::filesystem::path& self);  // StarDraft 2.exe.failed

// Starts a program (its folder the working folder) with these arguments, not waiting for it. False when it did not
// start (Windows: a file that is not a program for this system, such as a damaged download).
bool startProgram(const std::filesystem::path& exe, const std::vector<std::string>& args = {});

}  // namespace stardraft::platform
