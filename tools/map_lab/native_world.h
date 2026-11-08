// SPDX-License-Identifier: MIT
#pragma once
#include <mapeditor/simulation/world.h>
#include <mapeditor/simulation/scenario.h>
#include <functional>
namespace maplab {
using Bytes = std::vector<std::uint8_t>;
using FileReader = std::function<Bytes(const std::string&)>;
std::unique_ptr<mapeditor::simulation::World>
loadNativeWorld(const FileReader&, const Bytes&);
// The same with game data that is already loaded (worlds can share it).
std::unique_ptr<mapeditor::simulation::World>
loadNativeWorld(std::shared_ptr<const mapeditor::simulation::Definitions>,
                const FileReader&, const Bytes&);
}
