#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <memory>

namespace maplab {
using Bytes = std::vector<std::uint8_t>;
struct MiningUnit {
  int x, y, type, owner;
  bool cargo = false;
};
struct MiningPatch {
  int x, y, lastCycle = 0;
  double perMinute = 0;
};
struct MiningGuide {
  int x, y, targetX, targetY, kind, stage, type, worker;
};
struct SearchStatus {
  bool running = false, finished = false, exhausted = false, validating = false;
  int found = 0;
  double seconds = 0, progress = 0, score = 0;
};
struct MiningFrame {
  struct Sprite {
    int x, y, image, frame, owner;
    bool flip, shadow;
  };
  std::vector<Sprite> sprites;
  std::vector<MiningUnit> units;
  std::vector<MiningPatch> patches;
  std::vector<MiningGuide> guides;
  std::array<SearchStatus, 3> searches;
  int frame = 0;
  double income = 0;
};
// Development diagnostics; these records do not enter editor snapshots.
struct MiningDebugUnit {
  int id = -1, referenceMovement = -1, referenceFlags = 0,
      referenceNextDirection = 0, referenceCollision = -1;
  std::array<int, 2> raw{}, velocity{}, speed{}, move{}, pathSource{};
  std::array<int, 3> heading{}, target{};
  std::array<int, 4> order{};
  std::array<int, 5>
      animation{}; // image, frame, animation, wait, implementation cursor
  int nativeMotionFlags =
      0; // moving/stopped/settling/planning/arrived/slide/retry/heading/walking
  std::vector<std::array<int, 2>> path;
};
struct RushPoint {
  int x = 0, y = 0;
};
struct RushPath {
  int id = 0, frames = 0, type = 7;
  bool finished = false, reached = false;
  std::vector<RushPoint> points;
  std::vector<MiningFrame::Sprite> sprites;
  double seconds() const { return frames * .042; }
};
struct RushFrame {
  RushPoint start, end;
  std::vector<RushPath> paths;
};
struct RushDebugRunner {
  int id = 0, frame = 0;
  std::uint32_t random = 0;
  MiningDebugUnit unit;
};
} // namespace maplab
