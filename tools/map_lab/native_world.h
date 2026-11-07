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
}
