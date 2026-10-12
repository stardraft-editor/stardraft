#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

// Path of the scenario inside a .scm/.scx MPQ archive.
inline constexpr const char* kChkArchivePath = "staredit\\scenario.chk";

struct ArchiveFile {
    std::string name;
    Bytes data;
};

// Read one file (by default staredit\scenario.chk) from a .scm/.scx archive.
bool readMapFile(const std::filesystem::path& mapPath, Bytes& out, std::string& error,
                 const char* nameInArchive = kChkArchivePath);

inline bool readMapChk(const std::filesystem::path& mapPath, Bytes& chkOut, std::string& error) {
    return readMapFile(mapPath, chkOut, error);
}

// Read every file in the archive except the internal (listfile)/(attributes)/(signature).
// Files are found through the archive's listfile plus `extraNames` (e.g. WAV paths taken
// from the string table, for maps whose listfile was removed). Names that are not
// present are skipped. Files other than scenario.chk that fail to read (e.g. Blizzard's
// blizzard_2.cnd, junk entries in protected maps) are left out and listed in `unreadable`,
// as are files whose names are unknown (StormLib pseudo-names like "File00000001.wav").
bool readMapFiles(const std::filesystem::path& mapPath, std::vector<ArchiveFile>& out, std::string& error,
                  const std::vector<std::string>& extraNames = {},
                  std::vector<std::string>* unreadable = nullptr);

// Write a map archive at `dstPath` holding `chk` plus `otherFiles`. The archive is
// always rebuilt from scratch (MPQ v1, PKWARE implode) into a temporary file that
// then replaces `dstPath`, so `dstPath` may be the map the files were read from.
bool writeMap(const std::filesystem::path& dstPath, const Bytes& chk,
              const std::vector<ArchiveFile>& otherFiles, std::string& error);

}  // namespace stardraft
