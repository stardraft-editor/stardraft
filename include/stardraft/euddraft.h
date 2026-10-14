#pragma once

// euddraft (github.com/armoha/euddraft): finding a copy, keeping a local one up to date, and running a
// build. StarDraft keeps its own copies, one folder per version, under
//   %LOCALAPPDATA%\StarDraft\euddraft\<version>\  (Windows; ~/Library/Application Support on macOS;
//   $XDG_DATA_HOME or ~/.local/share on Linux)
// A copy the user chose wins and is never replaced. Other copies on the machine (EUD Editor's, the
// user's own) are listed but not built with: StarDraft keeps its own.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace stardraft {

struct EuddraftInstall {
    std::filesystem::path exe;
    std::string version;   // from the VERSION file beside the exe (folder names are not reliable)
    std::string source;    // "chosen", "stardraft" or "eud editor"
};

namespace euddraft {

// Where StarDraft keeps its copies.
std::filesystem::path managedRoot();
// The version in the VERSION file next to `exe` ("" if none).
std::string versionOf(const std::filesystem::path& exe);
// Every copy found: the chosen one (if it exists), StarDraft's, then EUD Editor's; newest first within each.
std::vector<EuddraftInstall> installed(const std::filesystem::path& chosen = {});
// The copy to build with: the chosen one if it exists, else StarDraft's own newest copy (nullopt
// if there is none yet: install the latest release then).
std::optional<EuddraftInstall> find(const std::filesystem::path& chosen = {});

struct Release {
    std::string version;  // "0.11.0.1"
    std::string zipUrl;   // the asset for this platform
};
// The newest release on GitHub (needs curl on the PATH; it ships with Windows 10+, macOS, Linux).
bool latestRelease(Release& out, std::string& error);
// Download and unpack a release into managedRoot()/<version>/. Reports progress lines.
bool install(const Release& release, EuddraftInstall& out, std::string& error,
             const std::function<void(const std::string&)>& progress = {});
// Compare dotted versions: <0, 0, >0.
int compareVersions(const std::string& a, const std::string& b);

struct BuildResult {
    bool ok = false;
    int exitCode = -1;
    std::string log;    // everything euddraft printed
    std::vector<std::string> errors;  // the lines that report errors
};
// Run euddraft on an .eds/.edd file (working directory: the file's folder). `onLine` sees each line
// of output as it arrives. Succeeds when euddraft exits cleanly without reporting an error.
BuildResult build(const std::filesystem::path& exe, const std::filesystem::path& edsFile,
                  const std::function<void(const std::string&)>& onLine = {});

}  // namespace euddraft
}  // namespace stardraft
