cmake_minimum_required(VERSION 3.20)
project(stardraft LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# CMake 4 refuses dependencies that still declare cmake_minimum_required below 3.5 (CascLib).
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# StormLib: MPQ archive reading/writing (.scm/.scx are MPQ archives)
set(STORM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# Wide-char paths on Windows so non-ASCII map file names work.
set(STORM_UNICODE ON CACHE BOOL "" FORCE)
# Its own zlib and bzip2, not the system's: a program made on Linux or macOS then needs no libz/libbz2 of the distro.
set(STORM_USE_BUNDLED_LIBRARIES ON CACHE BOOL "" FORCE)
FetchContent_Declare(StormLib
    GIT_REPOSITORY https://github.com/ladislav-zezula/StormLib.git
    GIT_TAG v9.30
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(StormLib)

# CascLib: reads StarCraft: Remastered game data (tilesets etc.) from CASC storage
set(CASC_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(CASC_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(CASC_UNICODE ON CACHE BOOL "" FORCE)
FetchContent_Declare(CascLib
    GIT_REPOSITORY https://github.com/ladislav-zezula/CascLib.git
    GIT_TAG 38a34665624b8775bb875274b36191b21c38d97b)
FetchContent_MakeAvailable(CascLib)
if(NOT WIN32)  # CascFiles.cpp uses LPDWORD, which CascPort.h defines only on Windows
    target_compile_definitions(casc_static PRIVATE LPDWORD=PDWORD)
endif()

# stb_image_write: PNG output (header only)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20)
FetchContent_MakeAvailable(stb)

# libvpx: the VP8 and VP9 decoders, for the game's portraits (WebM). Built from its C sources only; the configure
# step's output is generated once by tools/gen_libvpx.py into third_party/libvpx/.
FetchContent_Declare(libvpx
    URL https://github.com/webmproject/libvpx/archive/refs/tags/v1.14.1.tar.gz
    URL_HASH SHA256=901747254d80a7937c933d03bd7c5d41e8e6c883e0665fadcb172542167c7977
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(libvpx)  # no CMakeLists of its own: only fetched
include(third_party/libvpx/sources.cmake)
add_library(vpx STATIC ${VPX_SOURCES})
target_include_directories(vpx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvpx ${libvpx_SOURCE_DIR})
if(MSVC)
    target_compile_options(vpx PRIVATE /W0)
else()
    target_compile_options(vpx PRIVATE -w)
endif()

add_library(stardraft
    src/chk.cpp
    src/classic_settings.cpp
    src/classic_triggers.cpp
    src/portraits.cpp
    src/briefing.cpp
    src/msqc.cpp
    src/map_file.cpp
    src/game_data.cpp
    src/tileset.cpp
    src/terrain.cpp
    src/isom.cpp
    src/units.cpp
    src/sprites.cpp
    src/strings.cpp
    src/locations.cpp
    src/doodads.cpp
    src/hd_assets.cpp
    src/hd_preview.cpp
    src/previews.cpp
    src/euddraft.cpp
    src/eptrace.cpp
    src/eud_project.cpp
    src/dat_edit.cpp
    src/dat_extras.cpp
    src/wireframes.cpp
    src/triggers.cpp
    src/editor.cpp
    src/new_map.cpp
    src/terrain_gen.cpp
    src/pathing.cpp
    src/settings.cpp
    src/commands.cpp
    src/process.cpp)
target_include_directories(stardraft PUBLIC include PRIVATE src ${stb_SOURCE_DIR})
find_package(Threads REQUIRED)  # std::async; older glibc needs -pthread for it
target_link_libraries(stardraft PRIVATE storm casc_static vpx PUBLIC Threads::Threads)
if(WIN32)  # wide-char paths, as StormLib and CascLib are built on Windows (TCHAR = wchar_t)
    target_compile_definitions(stardraft PRIVATE UNICODE _UNICODE)
endif()
if(MSVC)
    target_compile_options(stardraft PRIVATE /W4 /utf-8)
else()
    target_compile_options(stardraft PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

add_executable(chk_survey tools/chk_survey.cpp)
target_link_libraries(chk_survey PRIVATE stardraft)

add_executable(casc_ls tools/casc_ls.cpp)
target_link_libraries(casc_ls PRIVATE stardraft)

add_executable(render_map tools/render_map.cpp)
target_link_libraries(render_map PRIVATE stardraft)

add_executable(terrain_check tools/probes/terrain_check.cpp)
target_link_libraries(terrain_check PRIVATE stardraft)

add_executable(isom_test tools/tests/isom_test.cpp)
target_link_libraries(isom_test PRIVATE stardraft)

add_executable(isom_paint tools/probes/isom_paint.cpp)
target_link_libraries(isom_paint PRIVATE stardraft)

add_executable(unit_test tools/tests/unit_test.cpp)
target_link_libraries(unit_test PRIVATE stardraft)

add_executable(isom_demo tools/probes/isom_demo.cpp)
target_link_libraries(isom_demo PRIVATE stardraft)

add_executable(unit_gallery tools/probes/unit_gallery.cpp)
target_link_libraries(unit_gallery PRIVATE stardraft)

add_executable(hd_test tools/tests/hd_test.cpp)
target_link_libraries(hd_test PRIVATE stardraft)

add_executable(editor_test tools/tests/editor_test.cpp)
target_link_libraries(editor_test PRIVATE stardraft)

add_executable(classic_trigger_test tools/tests/classic_trigger_test.cpp)
target_link_libraries(classic_trigger_test PRIVATE stardraft storm)  # storm: reading the protected maps euddraft writes

add_executable(new_map_test tools/tests/new_map_test.cpp)
target_link_libraries(new_map_test PRIVATE stardraft)

add_executable(paint_stress tools/paint_stress.cpp)
target_link_libraries(paint_stress PRIVATE stardraft)
add_executable(save_check tools/tests/save_check.cpp)
target_link_libraries(save_check PRIVATE stardraft)

add_executable(tools_test tools/tests/tools_test.cpp)
target_link_libraries(tools_test PRIVATE stardraft)

add_executable(gen_test tools/tests/gen_test.cpp)
target_link_libraries(gen_test PRIVATE stardraft)
add_executable(tile_swatch tools/probes/tile_swatch.cpp)
target_link_libraries(tile_swatch PRIVATE stardraft)
add_executable(thin_probe tools/probes/thin_probe.cpp)
target_link_libraries(thin_probe PRIVATE stardraft)
add_executable(connect_probe tools/probes/connect_probe.cpp)
target_link_libraries(connect_probe PRIVATE stardraft)
add_executable(preview_gallery tools/probes/preview_gallery.cpp)
target_link_libraries(preview_gallery PRIVATE stardraft)
add_executable(tbl_probe tools/probes/tbl_probe.cpp)
target_link_libraries(tbl_probe PRIVATE stardraft)
add_executable(euddraft_tool tools/euddraft_tool.cpp)
target_link_libraries(euddraft_tool PRIVATE stardraft)
add_executable(trigger_test tools/tests/trigger_test.cpp)
target_link_libraries(trigger_test PRIVATE stardraft)
target_compile_definitions(trigger_test PRIVATE STARDRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_executable(dat_test tools/tests/dat_test.cpp)
target_link_libraries(dat_test PRIVATE stardraft)

add_executable(portrait_test tools/tests/portrait_test.cpp)
target_include_directories(portrait_test PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(portrait_test PRIVATE stardraft)

add_executable(briefing_test tools/tests/briefing_test.cpp)
target_link_libraries(briefing_test PRIVATE stardraft)

add_executable(pathing_test tools/tests/pathing_test.cpp)
target_link_libraries(pathing_test PRIVATE stardraft)

add_executable(settings_test tools/tests/settings_test.cpp)
target_link_libraries(settings_test PRIVATE stardraft)
add_executable(commands_test tools/tests/commands_test.cpp)
target_link_libraries(commands_test PRIVATE stardraft)

# --- GPU renderer (SDL3 GPU API) -------------------------------------------------------------
# Direct3D 12 on Windows (Vulkan when asked: STARDRAFT_GPU_DRIVER=vulkan), Vulkan on Linux, Metal on macOS. Off: the
# library and its tests build without SDL3.
option(STARDRAFT_RENDERER "Build the GPU renderer (SDL3) and its tools" ON)
if(STARDRAFT_RENDERER)
    option(STARDRAFT_SDL_DEBUG_IME "SDL logs the input method's calls on macOS (cmake/patch_sdl.cmake)" OFF)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.18
        GIT_SHALLOW TRUE
        PATCH_COMMAND ${CMAKE_COMMAND} -DDEBUG_IME=${STARDRAFT_SDL_DEBUG_IME} -P ${CMAKE_SOURCE_DIR}/cmake/patch_sdl.cmake  # (macOS: Hanja; see the script)
        UPDATE_DISCONNECTED TRUE)
    FetchContent_MakeAvailable(SDL3)

    # Shaders: SPIR-V and MSL are made once by tools/gen_shaders.py into shaders/gen/ (in git). On Windows the build
    # also makes DXIL: HLSL -> DXIL with dxc from the Windows SDK, embedded as byte-array headers.
    set(STARDRAFT_SHADER_HEADERS)
    if(WIN32)
        file(GLOB STARDRAFT_DXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxc.exe")
        list(SORT STARDRAFT_DXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
        find_program(STARDRAFT_DXC NAMES dxc HINTS ${STARDRAFT_DXC_CANDIDATES})
        if(NOT STARDRAFT_DXC AND STARDRAFT_DXC_CANDIDATES)
            list(GET STARDRAFT_DXC_CANDIDATES 0 STARDRAFT_DXC)
        endif()
        if(NOT STARDRAFT_DXC)
            message(FATAL_ERROR "dxc (DirectX shader compiler) not found; install the Windows SDK")
        endif()
        set(STARDRAFT_SHADER_OUT ${CMAKE_BINARY_DIR}/generated/shaders)
        foreach(name terrain sprite solid downsample)
            foreach(stage vs ps)
                if(stage STREQUAL "vs")
                    set(profile vs_6_0)
                    set(entry VSMain)
                else()
                    set(profile ps_6_0)
                    set(entry PSMain)
                endif()
                set(out ${STARDRAFT_SHADER_OUT}/${name}_${stage}.h)
                add_custom_command(
                    OUTPUT ${out}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${STARDRAFT_SHADER_OUT}
                    COMMAND ${STARDRAFT_DXC} -nologo -T ${profile} -E ${entry} -Vn g_${name}_${stage} -Fh ${out} ${CMAKE_SOURCE_DIR}/shaders/${name}.hlsl
                    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/${name}.hlsl
                    COMMENT "dxc ${name}.hlsl (${stage})")
                list(APPEND STARDRAFT_SHADER_HEADERS ${out})
            endforeach()
        endforeach()
    endif()

    add_library(stardraft_render src/render/renderer.cpp ${STARDRAFT_SHADER_HEADERS})
    target_include_directories(stardraft_render PUBLIC include PRIVATE ${CMAKE_BINARY_DIR}/generated ${CMAKE_SOURCE_DIR}/shaders ${stb_SOURCE_DIR})
    target_link_libraries(stardraft_render PUBLIC stardraft SDL3::SDL3-static)
    if(WIN32)
        target_compile_definitions(stardraft_render PRIVATE STARDRAFT_SHADERS_DXIL)
    endif()
    if(MSVC)
        target_compile_options(stardraft_render PRIVATE /W4 /utf-8)
    endif()

    add_executable(render_bench tools/render_bench.cpp)
    target_link_libraries(render_bench PRIVATE stardraft_render)

    add_executable(terrain_render_test tools/tests/terrain_render_test.cpp)
    target_link_libraries(terrain_render_test PRIVATE stardraft_render)

    add_executable(shadow_test tools/tests/shadow_test.cpp)
    target_link_libraries(shadow_test PRIVATE stardraft_render)

    add_executable(render_smoke_test tools/tests/render_smoke_test.cpp)
    target_link_libraries(render_smoke_test PRIVATE stardraft_render)
endif()

# --- What the shells ask of the system (apps/common): HTTPS, dialogs (Windows), crash reports, updates -------
if(WIN32)
    add_library(stardraft_platform STATIC apps/common/http_win32.cpp apps/common/dialogs_win32.cpp apps/common/crash_win32.cpp apps/common/crash_report.cpp apps/common/update_win32.cpp)
    target_link_libraries(stardraft_platform PUBLIC stardraft winhttp comdlg32 ole32 dbghelp)
else()
    add_library(stardraft_platform STATIC apps/common/http_curl.cpp apps/common/crash_report.cpp apps/common/update_posix.cpp)
    target_link_libraries(stardraft_platform PUBLIC stardraft)
endif()
target_include_directories(stardraft_platform PUBLIC apps/common)
# A request by hand (needs the network, so GitHub does not run it): http_get <url> [<file>]
add_executable(http_get tools/http_get.cpp)
target_link_libraries(http_get PRIVATE stardraft_platform)
add_executable(platform_test tools/tests/platform_test.cpp)
target_link_libraries(platform_test PRIVATE stardraft_platform)

# --- The control kit (apps/kit): the editor's widgets as views on a Canvas (apps/common/canvas.h, view.h), on every
# system. Each widget is its own .cpp; kit_test drives them without a window.
file(GLOB STARDRAFT_KIT_SOURCES CONFIGURE_DEPENDS apps/kit/*.cpp)
add_library(stardraft_kit STATIC ${STARDRAFT_KIT_SOURCES})
target_include_directories(stardraft_kit PUBLIC apps/common apps/kit)
add_executable(kit_test tools/tests/kit_test.cpp)
target_link_libraries(kit_test PRIVATE stardraft_kit)

# --- stardraft-next: the new shell's spike (SDL3 + Dear ImGui; research/ports/PLAN.md, phase 4) ------------------
# A program of its own beside the Win32 one, on every system. Off: no Dear ImGui is fetched.
option(STARDRAFT_NEXT "Build stardraft-next (the new shell's spike)" ${STARDRAFT_RENDERER})
if(STARDRAFT_NEXT AND STARDRAFT_RENDERER)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9b
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(imgui)  # (no CMakeLists of its own: only fetched)
    add_library(imgui STATIC ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
                ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp)
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(imgui PUBLIC SDL3::SDL3-static)
    add_executable(stardraft_next WIN32 MACOSX_BUNDLE apps/next/main.cpp apps/next/kit.cpp)
    target_link_libraries(stardraft_next PRIVATE stardraft_render stardraft_platform imgui)
    set_target_properties(stardraft_next PROPERTIES OUTPUT_NAME stardraft-next  # (Retina: NSHighResolutionCapable)
                          MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/apps/next/Info.plist.in)
    # Its fonts (SIL Open Font License, NOTICE.md): Noto Sans for Latin, Korean and Chinese, at pinned commits,
    # downloaded once into the build and put beside the program (fonts/), so every system shows the same text.
    set(STARDRAFT_FONTS
        "NotoSans-Regular.ttf|https://raw.githubusercontent.com/notofonts/notofonts.github.io/5c3913063bb189df4edc94b766584dfb4c04a187/fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf|478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823"
        "NotoSansKR-Regular.otf|https://raw.githubusercontent.com/notofonts/noto-cjk/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/SubsetOTF/KR/NotoSansKR-Regular.otf|69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68"
        "NotoSansSC-Regular.otf|https://raw.githubusercontent.com/notofonts/noto-cjk/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf|faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9")
    set(STARDRAFT_FONT_FILES)
    foreach(entry ${STARDRAFT_FONTS})
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 name)
        list(GET parts 1 url)
        list(GET parts 2 hash)
        set(font ${CMAKE_BINARY_DIR}/fonts/${name})
        # (a file with the right hash is not downloaded again; a failed download leaves no file)
        file(DOWNLOAD ${url} ${font} EXPECTED_HASH SHA256=${hash} STATUS st)
        list(GET st 0 code)
        if(code EQUAL 0)
            list(APPEND STARDRAFT_FONT_FILES ${font})
        else()
            file(REMOVE ${font})
            message(WARNING "stardraft-next: ${name} was not downloaded (${st}); it uses the system's fonts")
        endif()
    endforeach()
    if(APPLE)
        set(fontDir $<TARGET_BUNDLE_CONTENT_DIR:stardraft_next>/Resources/fonts)
    else()
        set(fontDir $<TARGET_FILE_DIR:stardraft_next>/fonts)
    endif()
    if(STARDRAFT_FONT_FILES)
        add_custom_command(TARGET stardraft_next POST_BUILD COMMAND ${CMAKE_COMMAND} -E make_directory ${fontDir}
                           COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STARDRAFT_FONT_FILES} ${fontDir})
    endif()
    if(MSVC)
        target_compile_options(stardraft_next PRIVATE /W4 /utf-8)
        target_link_options(stardraft_next PRIVATE /SUBSYSTEM:CONSOLE)  # (it prints what it does; a test run reads it)
    endif()
endif()

# --- Windows editor (native Win32 shell) ------------------------------------------------------
if(WIN32 AND STARDRAFT_RENDERER)
    add_executable(stardraft_editor WIN32 apps/editor/main.cpp apps/editor/vorbis.c apps/editor/stb_image.cpp apps/editor/res/lang/ko.cpp apps/editor/res/lang/es.cpp apps/editor/res/lang/ru.cpp apps/editor/res/lang/zh.cpp apps/editor/stardraft.manifest apps/editor/res/stardraft.rc)
    target_include_directories(stardraft_editor PRIVATE ${stb_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
    set_source_files_properties(apps/editor/vorbis.c PROPERTIES COMPILE_OPTIONS "$<$<C_COMPILER_ID:MSVC>:/W0>")
    set_source_files_properties(apps/editor/stb_image.cpp PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/W0>")
    target_link_libraries(stardraft_editor PRIVATE stardraft_render stardraft_platform comctl32 dwmapi shell32 ole32 comdlg32 uxtheme msimg32 gdiplus dbghelp winhttp)
    # Crash reports: the commit, and the token that sends them to GitHub as issues (CI gives it; see crash.inc).
    # The commit: each build writes it into generated/build_info.h (cmake/build_info.cmake), so a build after a new
    # commit names that commit with no configure step.
    set(STARDRAFT_BUILD_INFO ${CMAKE_BINARY_DIR}/generated/build_info.h)
    add_custom_target(stardraft_build_info COMMAND ${CMAKE_COMMAND} -DSTARDRAFT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
                      -DSTARDRAFT_BUILD_INFO=${STARDRAFT_BUILD_INFO} -P ${CMAKE_SOURCE_DIR}/cmake/build_info.cmake
                      BYPRODUCTS ${STARDRAFT_BUILD_INFO} VERBATIM)  # Ninja reads the header again after the step
    add_dependencies(stardraft_editor stardraft_build_info)
    # CI gives it in the environment; a build made here reads .issues-token in the repository's root (not in git).
    # Empty: reports and feedback stay on disk until a build with a token starts.
    set(STARDRAFT_ISSUES_TOKEN "$ENV{STARDRAFT_ISSUES_TOKEN}")
    if(NOT STARDRAFT_ISSUES_TOKEN AND EXISTS ${CMAKE_SOURCE_DIR}/.issues-token)
        file(STRINGS ${CMAKE_SOURCE_DIR}/.issues-token STARDRAFT_ISSUES_TOKEN LIMIT_COUNT 1)
        string(REGEX REPLACE "^ï»¿" "" STARDRAFT_ISSUES_TOKEN "${STARDRAFT_ISSUES_TOKEN}")  # Notepad's UTF-8 mark
        string(STRIP "${STARDRAFT_ISSUES_TOKEN}" STARDRAFT_ISSUES_TOKEN)
        if(STARDRAFT_ISSUES_TOKEN)
            message(STATUS "StarDraft: the issues token from .issues-token is built in")
        endif()
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/.issues-token)
    # The version the program says: a release build gets it from its tag (-DSTARDRAFT_VERSION_NAME=0.5.0.1, see
    # .github/workflows/build.yml); other builds say this one.
    if(NOT STARDRAFT_VERSION_NAME)
        set(STARDRAFT_VERSION_NAME "0.5 Alpha")
    endif()
    target_compile_definitions(stardraft_editor PRIVATE STARDRAFT_VERSION_NAME="${STARDRAFT_VERSION_NAME}" STARDRAFT_ISSUES_TOKEN="${STARDRAFT_ISSUES_TOKEN}")
    if(MSVC)  # symbols for Release, to read crash dumps
        target_compile_options(stardraft_editor PRIVATE $<$<CONFIG:Release>:/Zi>)
        target_link_options(stardraft_editor PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
    endif()
    target_compile_definitions(stardraft_editor PRIVATE UNICODE _UNICODE)
    # The editor's file name: stardraft.exe. A second name (-DSTARDRAFT_EXE_NAME=stardraft-testing) lets a build
    # run while stardraft.exe is open.
    set(STARDRAFT_EXE_NAME stardraft CACHE STRING "File name of the editor (without .exe)")
    set_target_properties(stardraft_editor PROPERTIES OUTPUT_NAME ${STARDRAFT_EXE_NAME})
    if(MSVC)
        target_compile_options(stardraft_editor PRIVATE /W4 /utf-8 /bigobj)  # main.cpp has more sections than the default allows
    endif()
    # StarDraft-latest.exe: starts a copy of the newest build (pin it to Start), so builds can go on while it runs.
    add_executable(stardraft_latest WIN32 apps/latest/main.cpp apps/latest/latest.rc)
    target_link_libraries(stardraft_latest PRIVATE shell32 ole32 advapi32)
    target_compile_definitions(stardraft_latest PRIVATE UNICODE _UNICODE)
    # It goes into the repository's root (not under build/), and finds the editor in the build folder.
    set_target_properties(stardraft_latest PROPERTIES OUTPUT_NAME StarDraft-latest RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}
                                                       RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR} RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
    target_compile_definitions(stardraft_latest PRIVATE STARDRAFT_BUILD_DIR=L"$<TARGET_FILE_DIR:stardraft_editor>")
    add_dependencies(stardraft_editor stardraft_latest)  # built with the editor
endif()
