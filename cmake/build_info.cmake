# Writes build_info.h with the commit the build is made from (run on each build, see CMakeLists.txt):
#   cmake -DSTARDRAFT_SOURCE_DIR=<repository> -DSTARDRAFT_BUILD_INFO=<header> -P build_info.cmake
# The header changes only when its text changes, so main.cpp compiles again only after a new commit.
# 7 characters, as the release title's "(build <commit>)" (Check for updates compares the two; git's own short form
# grows with the repository).
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY ${STARDRAFT_SOURCE_DIR} OUTPUT_VARIABLE commit
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(commit)
    string(SUBSTRING ${commit} 0 7 commit)
endif()
if(NOT commit)
    set(commit "unknown")  # no git (a zip download): the value of the crash.inc fallback
endif()
get_filename_component(dir ${STARDRAFT_BUILD_INFO} DIRECTORY)
file(MAKE_DIRECTORY ${dir})
file(WRITE ${STARDRAFT_BUILD_INFO}.tmp "#pragma once\n#define STARDRAFT_COMMIT \"${commit}\"\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STARDRAFT_BUILD_INFO}.tmp ${STARDRAFT_BUILD_INFO})
