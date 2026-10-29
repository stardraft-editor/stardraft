#pragma once

// The build's commit and time. Only two small files read them (build_info.h, written on each build, and __DATE__ and
// __TIME__): apps/common/build_stamp.cpp and apps/editor/build_time.cpp. The big files (the editor's main_sdl.cpp, the
// CLI's main.cpp) then stay the same after a commit, so a compiler cache (sccache and ccache in CI) keeps them.

namespace stardraft {

// The commit of the build, 7 characters ("unknown": a build with no git). apps/common/build_stamp.cpp.
const char* buildCommit();

// The commit's time in seconds since 1970 (0: no git). apps/common/build_stamp.cpp.
long long buildCommitTime();

// When the editor was built ("Oct 29 2026 14:05:33"). The editor only: apps/editor/build_time.cpp, compiled again after
// a new commit and when a file of apps/editor changes (CMakeLists.txt).
const char* buildTime();

}  // namespace stardraft
