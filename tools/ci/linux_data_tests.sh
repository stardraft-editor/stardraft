#!/usr/bin/env bash
# The tests that need the game's data, on Linux: each in a new, empty scratch folder. A test that fails is counted;
# the script goes on with the next one and exits with 1 at the end if any failed.
# usage: tools/ci/linux_data_tests.sh <build dir> <install dir> <scratch dir>
# <install dir> holds .build.info, with the file names in their exact case (CascLib looks for Data/data, ...), and a
# Maps folder with the Blizzard maps.
set -u
build=$1 install=$2 scratch=$3
maps="$install/Maps"
rm -rf "$scratch" && mkdir -p "$scratch"
failed=()

run() {  # run <name> <program> <args...>
    local name=$1; shift
    echo "::group::$name"
    if "$@"; then echo "$name: passed"; else echo "$name: FAILED ($?)"; failed+=("$name"); fi
    echo "::endgroup::"
}

s() { mkdir -p "$scratch/$1" && echo "$scratch/$1"; }

run editor_test "$build/editor_test" "$install" "$maps/(2)Bottleneck.scm" "$(s editor_test)"
run save_check "$build/save_check" "$install" "$(s save_check)" "$maps"
run new_map_test "$build/new_map_test" "$install" "$(s new_map_test)/new.scx"
run render_map "$build/render_map" "$install" "$maps/(2)Bottleneck.scm" "$(s render_map)/bottleneck.png"
run hd_test "$build/hd_test" "$install"
run portrait_test "$build/portrait_test" "$install" "$(s portrait_test)"
run pathing_test "$build/pathing_test" "$install" "$maps"
run briefing_test "$build/briefing_test" "$install"
run isom_test "$build/isom_test" "$maps"
run trigger_test "$build/trigger_test" "$(s trigger_test)" "$install" "$maps/BroodWar/(2)Binary Burghs.scx"
run dat_test "$build/dat_test" "$install" "$maps/BroodWar/(2)Binary Burghs.scx" "$(s dat_test)"
# The renderer (Vulkan; with SDL_VIDEODRIVER=offscreen no window is needed), when the build has it.
if [[ -x "$build/terrain_render_test" ]]; then
    run terrain_render_test "$build/terrain_render_test" "$install" "$maps"
fi

if ((${#failed[@]})); then
    echo "failed: ${failed[*]}"
    exit 1
fi
echo "all passed"
