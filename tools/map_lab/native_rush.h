// SPDX-License-Identifier: MIT
#pragma once
#include "mining.h"
#include "native_world.h"
namespace maplab {
// Editor Rush playback uses independent simulation worlds.
class NativeRush {
public:
  static std::shared_ptr<NativeRush> create(const FileReader &, const Bytes &,
                                            RushPoint, RushPoint, int);
  ~NativeRush();
  void configure(double interval, int retained);
  void advance(int frames);
  RushFrame snapshot() const;
  std::string debugState() const;
  std::vector<RushDebugRunner> debugRunners() const;

private:
  NativeRush();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace maplab
