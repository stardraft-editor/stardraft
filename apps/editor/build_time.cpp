// When the editor was built (build_stamp.h): the title, About, crash reports and feedback show it. This file compiles
// again after a new commit and when a file of apps/editor changes: CMakeLists.txt touches build_time.h then.

#include "build_stamp.h"
#include "build_time.h"  // (empty: generated/build_time.h, CMakeLists.txt)

namespace stardraft {

const char* buildTime() { return __DATE__ " " __TIME__; }

}  // namespace stardraft
