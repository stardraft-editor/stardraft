// SPDX-License-Identifier: MIT
#pragma once
#include "animation.h"
#include "dat.h"
#include "geometry.h"
#include <cstdlib>
#include <deque>
#include <list>
#include <optional>

namespace mapeditor::simulation {
struct SpriteImage {
  int type = 0, baseFrame = 0, direction = 0, modifier = 0;
  int warpStart = 0, warpDelay = 1, warpElapsed = 0;
  Point offset;
  std::optional<int> frozenWorldY;
  bool flip = false, hidden = false, specialOffset = false;
  iscript::Cursor cursor;
  int frame() const { return baseFrame + direction; }
};
struct SpriteContext {
  int heading = 0, signal = 0, speed = 0, topSpeed = 0, tileset = 0;
  bool speedWritten = false;
  bool target = false, flying = false, uninterruptible = false;
  bool unitContext = true, flingyContext = true;
  bool resourceContainer = false;
  int resources = 0;
  int targetDistance = 0, targetHeading = 0;
};
struct SpriteResources {
  const std::array<dat::Image, 999> &images;
  const iscript::Program &program;
  std::function<int(int)> frameCount;
  std::function<Point(int, int, int, int, bool)> offset;
};
// Images use stable numeric handles within each sprite. Child image
// initialization may execute recursively without invalidating its parent's
// cursor or a snapshot.
class Sprite {
public:
  std::uint64_t sceneKey = ~std::uint64_t(0);
  std::deque<SpriteImage> images;
  std::list<std::size_t> order; // top to bottom
  std::size_t main = 0;
  SpriteContext context;
  using External = std::function<iscript::HostAction(
      const iscript::Instruction &, const SpriteImage &)>;
  std::size_t attach(int image, const SpriteResources &data, Random &random,
                     const External &external = {}) {
    auto id = add(image, {}, true, main, data, random, external);
    images[id].specialOffset = true;
    refreshOffsets(data);
    return id;
  }
  std::size_t underlay(int image, const SpriteResources &data, Random &random,
                       const External &external = {}) {
    return add(image, {}, false, main, data, random, external);
  }
  std::size_t overlay(int image, Point offset, int direction,
                      const SpriteResources &data, Random &random,
                      const External &external = {}) {
    auto id = add(image, offset, true, main, data, random, external);
    orient(id, direction);
    return id;
  }
  std::size_t topOverlay(int image, Point offset, const SpriteResources &data,
                        Random &random, const External &external = {}) {
    if (order.empty())
      throw std::logic_error("Missing base image for top overlay");
    return add(image, offset, true, order.front(), data, random, external);
  }
  void detach(std::size_t id) {
    if (id == main || id >= images.size())
      throw std::invalid_argument("Invalid attached image handle");
    order.remove(id);
    images[id].cursor.ended = true;
  }
  void refreshOffsets(const SpriteResources &data) {
    for (auto id : order)
      if (images[id].specialOffset) {
        if (!data.offset)
          throw std::runtime_error("Missing attached image offsets");
        const auto &base = images[main];
        images[id].offset =
            data.offset(base.type, base.frame(), 2, 0, base.flip);
      }
  }
  void initialize(int image, const SpriteResources &data, Random &random,
                  const External &external = {}) {
    images.clear();
    order.clear();
    add(image, {}, true, 0, data, random, external);
  }
  void setHeading(int heading, const SpriteResources *data = nullptr) {
    if (data)
      refreshOffsets(*data);
    context.heading = signedHeading(heading);
    int index = ((heading & 255) + 4) / 8;
    for (auto id : order)
      orient(id, index);
  }
  // Run a one-off entry from another script while preserving this image's
  // normal animation lookup for subsequent selections.
  void runMainEntry(int script, int animation, int modifier,
                    const SpriteResources &data, Random &random,
                    const External &external = {}) {
    if (script < 0 || script > 65535)
      throw std::out_of_range("Invalid temporary image script");
    auto &image = images[main];
    auto normal = image.cursor.script;
    image.cursor.script = std::uint16_t(script);
    bool selected = iscript::Animation(data.program, random)
                        .select(image.cursor, animation);
    image.cursor.script = normal;
    if (!selected)
      throw std::runtime_error("Missing temporary image animation");
    execute(main, data, random, external);
    image.modifier = modifier;
    execute(main, data, random, external);
  }
  void animate(int animation, const SpriteResources &data, Random &random,
               const External &external = {}) {
    auto pending = std::vector<std::size_t>(order.begin(), order.end());
    for (auto id : pending) {
      auto &image = images[id];
      if (image.cursor.ended)
        continue;
      int old = image.cursor.animation;
      if ((animation == 1 && old == 1) ||
          ((animation == 11 || animation == 19) && animation == old))
        continue;
      if (!data.images.at(image.type).animated && animation > 1)
        continue;
      int selected = animation;
      if (animation == 5 && old != 5 && old != 2)
        selected = 2;
      if (animation == 6 && old != 6 && old != 3)
        selected = 3;
      if (!iscript::Animation(data.program, random)
               .select(image.cursor, selected))
        throw std::runtime_error("Missing image animation " +
                                 std::to_string(selected));
      execute(id, data, random, external);
    }
  }
  void tick(const SpriteResources &data, Random &random,
            const External &external = {}) {
    // Save the next handle before execution, matching an intrusive traversal:
    // a newly added image above the current image starts on the next tick.
    auto at = order.begin();
    while (at != order.end()) {
      auto id = *at++;
      auto &image = images[id];
      if (image.modifier == 17) {
        ++image.warpElapsed;
        int elapsed = image.warpElapsed - image.warpDelay;
        if (elapsed >= 0 && elapsed % 3 == 0 &&
            image.warpStart + 1 + elapsed / 3 >= 64) {
          if (!iscript::Animation(data.program, random).select(image.cursor, 1))
            throw std::runtime_error("Missing warp overlay death animation");
          execute(id, data, random, external);
          if (context.flingyContext)
            context.signal |= 1;
        }
      }
      if (!image.cursor.ended && image.cursor.wait)
        --image.cursor.wait;
      else if (!tickPassive(id, data, random))
        execute(id, data, random, external, true);
    }
  }

private:
  bool tickPassive(std::size_t id, const SpriteResources &data,
                   Random &random) {
    auto &image = images[id];
    auto &cursor = image.cursor;
    if (!cursor.ended) {
      if (cursor.wait) {
        --cursor.wait;
        return true;
      }
      const auto *step = data.program.spriteStep(cursor.pc);
      if (!step)
        return false;
      if ((step->flags & iscript::SpriteStep::MainFrame) && id != main)
        image.baseFrame = images[main].baseFrame;
      else if (step->flags & iscript::SpriteStep::Frame)
        image.baseFrame = step->frame;
      if ((step->flags & iscript::SpriteStep::MainDirection) && id != main) {
        image.direction = images[main].direction;
        image.flip = images[main].flip;
      }
      if (step->flags & iscript::SpriteStep::X)
        image.offset.x = step->x;
      if (step->flags & iscript::SpriteStep::Y)
        image.offset.y = step->y;
      cursor.pc = step->next;
      if (step->terminal == 5)
        cursor.wait = step->wait;
      else if (step->terminal == 6) {
        if (step->span <= 0)
          throw std::runtime_error("Inverted animation wait range");
        cursor.wait = step->wait + int((random.next() & 255) % step->span);
      } else
        cursor.ended = true;
    }
    if (cursor.ended) {
      order.remove(id);
      if (id == main && !order.empty())
        main = order.front();
    }
    return true;
  }
  std::vector<bool> directional_;
  void orient(std::size_t id, int index) {
    if (id >= directional_.size() || !directional_[id])
      return;
    if (index < 0 || index > 32)
      throw std::out_of_range("Invalid sprite direction");
    images[id].flip = index > 16;
    images[id].direction = index > 16 ? 32 - index : index;
  }
  std::size_t add(int type, Point offset, bool above, std::size_t relative,
                  const SpriteResources &data, Random &random,
                  const External &external) {
    const auto &definition = data.images.at(type);
    if (images.size() >= 5000)
      throw std::runtime_error("Sprite image capacity exceeded");
    auto id = images.size();
    SpriteImage image;
    image.type = type;
    image.offset = offset;
    image.modifier = definition.modifier;
    if (image.modifier == 17) {
      image.warpStart = 48;
      image.warpDelay = 3;
    }
    if (definition.script > 65535)
      throw std::runtime_error("Invalid image script ID");
    image.cursor.script = std::uint16_t(definition.script);
    images.push_back(image);
    if (id == 0) {
      main = 0;
      directional_.clear();
      order.push_back(id);
    } else {
      auto where = std::find(order.begin(), order.end(), relative);
      if (where == order.end())
        throw std::runtime_error("Missing relative image");
      if (!above)
        ++where;
      order.insert(where, id);
    }
    directional_.push_back(definition.directional);
    if (!iscript::Animation(data.program, random).select(images[id].cursor, 0))
      throw std::runtime_error("Missing image Init animation");
    execute(id, data, random, external);
    return id;
  }
  void execute(std::size_t id, const SpriteResources &data, Random &random,
               const External &external, bool active = false) {
    if (!active && tickPassive(id, data, random))
      return;
    auto &image = images[id];
    // Waiting cannot dispatch an event or change image membership. Preserve
    // the cursor's exact frame count without constructing callback wrappers.
    if (!image.cursor.ended && image.cursor.wait) {
      --image.cursor.wait;
      return;
    }
    auto condition = [&](const iscript::Instruction &i) {
      auto &a = i.arguments;
      switch (i.opcode) {
      case 48:
        return !context.unitContext || context.target;
      case 57:
        return id != main;
      case 58:
        return context.unitContext && context.target &&
               context.targetDistance <= a[0];
      case 59:
        return context.unitContext && context.target &&
               std::abs(signedHeading(a[0] - context.targetHeading)) < a[1];
      case 60:
        return context.unitContext &&
               std::abs(signedHeading(context.heading - a[0])) < a[1];
      case 63:
        return context.unitContext && context.flying;
      default:
        throw std::runtime_error("Invalid image branch");
      }
    };
    auto apply = [&](const iscript::Instruction &i) {
      const auto &a = i.arguments;
      switch (i.opcode) {
      case 0:
        image.baseFrame = a[0];
        break;
      case 1:
        if (!data.frameCount)
          throw std::runtime_error("Missing image frame counts");
        if (a[0] + context.tileset < data.frameCount(image.type))
          image.baseFrame = a[0] + context.tileset;
        break;
      case 2:
        image.offset.x = a[0];
        break;
      case 3:
        image.offset.y = a[0];
        break;
      case 4:
        image.offset = {a[0], a[1]};
        break;
      case 8:
      case 9:
      case 10:
      case 11:
      case 13:
      case 61: {
        int type = a[0];
        Point offset{};
        if (i.opcode == 8 || i.opcode == 9)
          offset = {image.offset.x + a[1], image.offset.y + a[2]};
        if (i.opcode == 13) {
          if (!data.offset)
            throw std::runtime_error("Missing image offset tables");
          offset =
              data.offset(image.type, image.frame(), a[1], a[2], image.flip);
        }
        if (i.opcode == 61) {
          type = image.type + 1;
          offset = {image.offset.x + a[0], image.offset.y + a[1]};
        }
        int direction = image.flip ? 32 - image.direction : image.direction;
        auto child =
            add(type, offset, i.opcode == 8 || i.opcode == 10 || i.opcode == 13,
                id, data, random, external);
        orient(child, direction);
        if (i.opcode == 10 || i.opcode == 11)
          images[child].specialOffset = true;
        break;
      }
      case 23:
        image.flip = a[0] != 0;
        break;
      case 24:
        break; // Audio output has no simulation side effect; RNG was consumed
               // by the VM.
      case 29:
        image.baseFrame = images[main].baseFrame;
        image.direction = images[main].direction;
        image.flip = images[main].flip;
        break;
      case 31:
        if (context.unitContext)
          setHeading(context.heading - a[0] * 8, &data);
        break;
      case 32:
        if (context.unitContext)
          setHeading(context.heading + a[0] * 8, &data);
        break;
      case 33:
        if (context.unitContext && !context.target)
          setHeading(context.heading + 8, &data);
        break;
      case 36:
        if (context.flingyContext)
          context.signal |= a[0];
        break;
      case 41:
        if (context.unitContext) {
          context.speed = a[0] * 256;
          context.speedWritten = true;
        }
        break;
      case 43:
        image.baseFrame = a[0];
        image.direction = images[main].direction;
        image.flip = images[main].flip;
        break;
      case 44:
        if (!data.frameCount)
          throw std::runtime_error("Missing image frame counts");
        image.baseFrame = images[main].baseFrame +
                          (data.frameCount(images[main].type) & 32767) * a[0];
        image.direction = images[main].direction;
        image.flip = images[main].flip;
        break;
      case 46:
        if (context.unitContext)
          context.uninterruptible = true;
        break;
      case 47:
        if (context.unitContext)
          context.uninterruptible = false;
        break;
      case 50:
        image.hidden = true;
        break;
      case 51:
        image.hidden = false;
        break;
      case 52:
        if (context.unitContext)
          setHeading(a[0] * 8, &data);
        break;
      case 55:
        if (context.unitContext)
          context.topSpeed = a[0];
        break;
      case 56:
        if (context.unitContext && context.resourceContainer) {
          if (!data.offset)
            throw std::runtime_error("Missing gas overlay offsets");
          auto offset =
              data.offset(image.type, image.frame(), 2, a[0], image.flip);
          offset.x += image.offset.x;
          offset.y += image.offset.y;
          add((context.resources ? 430 : 435) + a[0], offset, true, id, data,
              random, external);
        }
        break;
      case 64:
        image.warpStart = a[0] & 255;
        image.warpDelay = ((a[0] >> 8) & 255) + 1;
        image.warpElapsed = 0;
        break;
      case 65:
        if (context.flingyContext)
          context.signal &= ~a[0];
        break;
      default:
        if (!external)
          throw std::runtime_error("Unhandled world animation effect " +
                                   std::to_string(i.opcode));
        return external(i, image);
      }
      return iscript::HostAction::Continue;
    };
    const iscript::DirectAnimationHost host{std::move(apply),
                                            std::move(condition)};
    iscript::Animation(data.program, random).tick(image.cursor, host);
    if (image.cursor.ended) {
      order.remove(id);
      if (id == main && !order.empty())
        main = order.front();
    }
  }
};
} // namespace mapeditor::simulation
