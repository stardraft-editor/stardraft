// The simulation bar's engine (apps/editor/sdl_simulation.inc): mining and rush distance run on the Map Simulation
// engine (third_party/map-simulation), combat on StarDraft's own engine (tools/sim). One sim::Simulator for the bar.
#pragma once

#include <memory>

#include "../sim/sim.h"

namespace maplab {

// The bar's simulator: startMining and startRush on the Map Simulation engine, all else on sim::createSimulator().
std::unique_ptr<sim::Simulator> createBarSimulator();

}  // namespace maplab
