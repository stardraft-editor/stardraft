# The simulation bar's engine (bar_sim.h): the Map Simulation engine (third_party/map-simulation) for mining and rush
# distance, StarDraft's own engine (stardraft_sim) for combat. Added from the main CMakeLists.txt after stardraft_sim.
# STARDRAFT_MAP_SIM_TESTS=ON also builds the engine's own tests (ctest in the build folder; no game data needed).
option(STARDRAFT_MAP_SIM_TESTS "Build the Map Simulation engine's tests" OFF)
set(BUILD_TESTING ${STARDRAFT_MAP_SIM_TESTS})
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../third_party/map-simulation ${CMAKE_CURRENT_BINARY_DIR}/engine)
unset(BUILD_TESTING)
add_library(stardraft_map_lab STATIC native_world.cpp bar_sim.cpp)
target_include_directories(stardraft_map_lab PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(stardraft_map_lab PUBLIC map-simulation stardraft_sim)
target_compile_features(stardraft_map_lab PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(stardraft_map_lab PRIVATE "$<$<CONFIG:Release>:/Ob3>")
endif()
