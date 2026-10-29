// The build's commit (build_stamp.h).

#include "build_stamp.h"

#if __has_include("build_info.h")
#include "build_info.h"  // STARDRAFT_COMMIT, STARDRAFT_COMMIT_TIME: written on each build (cmake/build_info.cmake)
#endif
#ifndef STARDRAFT_COMMIT
#define STARDRAFT_COMMIT "unknown"
#endif
#ifndef STARDRAFT_COMMIT_TIME
#define STARDRAFT_COMMIT_TIME 0  // (no git: unknown)
#endif

namespace stardraft {

const char* buildCommit() { return STARDRAFT_COMMIT; }
long long buildCommitTime() { return (long long)STARDRAFT_COMMIT_TIME; }

}  // namespace stardraft
