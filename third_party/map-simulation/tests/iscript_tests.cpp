// SPDX-License-Identifier: MIT
#include <iostream>
#include <mapeditor/simulation/animation.h>
#include <mapeditor/simulation/iscript.h>
#include <mapeditor/simulation/sprite.h>
#include <vector>
using mapeditor::simulation::iscript::Program;
void require(bool ok) {
  if (!ok)
    throw std::runtime_error("IScript test failed");
}
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  require(caught);
}
void word(std::vector<std::uint8_t> &b, int p, int n) {
  b.at(p) = n & 255;
  b.at(p + 1) = (n >> 8) & 255;
}
std::vector<std::uint8_t> fixture() {
  std::vector<std::uint8_t> bytes(64);
  word(bytes, 0, 56); // header index
  bytes[4] = 'S';
  bytes[5] = 'C';
  bytes[6] = 'P';
  bytes[7] = 'E';
  word(bytes, 8, 0);
  word(bytes, 12, 16);
  word(bytes, 14, 32);
  // Init: playfram 17; setpos -3,5; wait 1; goto Init
  bytes[16] = 0;
  word(bytes, 17, 17);
  bytes[19] = 4;
  bytes[20] = 253;
  bytes[21] = 5;
  bytes[22] = 5;
  bytes[23] = 1;
  bytes[24] = 7;
  word(bytes, 25, 16);
  // Death: playsndrand 2,100,101; call subroutine; end
  bytes[32] = 25;
  bytes[33] = 2;
  word(bytes, 34, 100);
  word(bytes, 36, 101);
  bytes[38] = 53;
  word(bytes, 39, 48);
  bytes[41] = 22;
  bytes[48] = 36;
  bytes[49] = 1;
  bytes[50] = 54;
  word(bytes, 56, 7);
  word(bytes, 58, 4);
  word(bytes, 60, 65535);
  return bytes;
}
int main() {
  try {
    auto bytes = fixture();
    Program p(bytes);
    require(p.scripts().size() == 1 && p.script(7).animations.size() == 2 &&
            p.script(7).animations[0] == 16 && p.instructions().size() == 9);
    require(p.instruction(19).arguments == std::vector<int>({-3, 5}));
    require(p.instruction(32).arguments == std::vector<int>({2, 100, 101}));
    require(p.instruction(24).arguments[0] == 16 &&
            p.instruction(50).opcode == 54);
    {
      auto detached = [&] {
        Program local(p);
        return Program(local);
      }();
      require(&detached.instruction(32) == &detached.instructions().at(32) &&
              &detached.instruction(32) != &p.instruction(32));
      Program assigned(p);
      assigned = detached;
      assigned = assigned;
      Program moved(std::move(assigned));
      require(&moved.instruction(32) == &moved.instructions().at(32) &&
              moved.instruction(32).arguments == p.instruction(32).arguments);
      rejects([&] { moved.instruction(31); });
      rejects([&] { moved.instruction(65536); });
    }
    using namespace mapeditor::simulation;
    Random random;
    iscript::Animation vm(p, random);
    iscript::Cursor cursor;
    cursor.script = 7;
    std::vector<iscript::Instruction> effects;
    iscript::AnimationHost host;
    host.apply = [&](const iscript::Instruction &i) {
      effects.push_back(i);
      return iscript::HostAction::Continue;
    };
    require(vm.select(cursor, 0));
    vm.tick(cursor, host);
    require(cursor.pc == 24 && cursor.wait == 0 && !cursor.ended &&
            effects.size() == 2);
    require(effects[0].opcode == 0 && effects[0].arguments[0] == 17 &&
            effects[1].arguments == std::vector<int>({-3, 5}));
    auto copy = cursor;
    vm.tick(cursor, host);
    require(cursor.pc == copy.pc && effects.size() == 4);
    require(!vm.select(cursor, 2) && cursor.pc == 24);
    require(vm.select(cursor, 1));
    vm.tick(cursor, host);
    require(cursor.ended && effects.size() == 6 && effects[4].opcode == 24 &&
            effects[4].arguments[0] == 100);
    require(effects[5].opcode == 36 && effects[5].arguments[0] == 1 &&
            random.state() == 953210035U);
    require(p.instruction(32).opcode == 25 &&
            p.instruction(32).arguments == std::vector<int>({2, 100, 101}));
    vm.tick(cursor, host);
    require(effects.size() == 6);
    // Wait state counts complete intervening frames, independently of dispatch.
    copy.wait = 2;
    vm.tick(copy, host);
    require(copy.wait == 1 && effects.size() == 6);
    vm.tick(copy, host);
    require(copy.wait == 0 && effects.size() == 6);
    vm.tick(copy, host);
    require(effects.size() == 8);
    vm.select(copy, 0);
    rejects([&] { vm.tick(copy, {}, 10); });
    vm.select(copy, 0);
    rejects([&] { vm.tick(copy, host, 1); });
    std::vector<std::uint8_t> walking(128);
    word(walking, 0, 116);
    walking[4] = 'S';
    walking[5] = 'C';
    walking[6] = 'P';
    walking[7] = 'E';
    word(walking, 8, 12);
    word(walking, 12, 48);
    word(walking, 12 + 11 * 2, 49);
    walking[48] = 22;
    walking[49] = 41;
    walking[50] = 9;
    walking[51] = 41;
    walking[52] = 3;
    walking[53] = 25;
    walking[54] = 1;
    word(walking, 55, 77);
    walking[57] = 34;
    walking[58] = 5;
    walking[59] = 5;
    walking[60] = 2;
    walking[61] = 7;
    word(walking, 62, 49);
    word(walking, 116, 7);
    word(walking, 118, 4);
    word(walking, 120, 65535);
    Program walkingProgram(walking);
    Random walkingRandom;
    auto beforeWalking = walkingRandom.state();
    iscript::Animation walkingVm(walkingProgram, walkingRandom);
    require(walkingVm.averageWalkingSpeed(7) == 384);
    require(walkingRandom.state() == beforeWalking);
    auto eventBytes = fixture();
    eventBytes.resize(96);
    word(eventBytes, 0, 80);
    word(eventBytes, 80, 7);
    word(eventBytes, 82, 4);
    word(eventBytes, 84, 65535);
    eventBytes[32] = 28;
    eventBytes[38] = 26;
    word(eventBytes, 39, 200);
    word(eventBytes, 41, 203);
    eventBytes[43] = 34;
    eventBytes[44] = 5;
    eventBytes[45] = 22;
    Program eventProgram(eventBytes);
    Random eventRandom, expectedEventRandom;
    auto chosenSound = 100 + expectedEventRandom.next() % 2;
    auto rangedSound = 200 + expectedEventRandom.next() % 4;
    auto randomTurn = expectedEventRandom.next() % 4 == 1 ? 31 : 32;
    iscript::Animation eventVm(eventProgram, eventRandom);
    iscript::Cursor eventCursor;
    eventCursor.script = 7;
    require(eventVm.select(eventCursor, 1));
    std::vector<iscript::Instruction> events;
    iscript::AnimationHost eventHost;
    eventHost.apply = [&](const auto &event) {
      events.push_back(event);
      return iscript::HostAction::Continue;
    };
    eventVm.tick(eventCursor, eventHost);
    require(eventCursor.ended && events.size() == 4 &&
            eventRandom.state() == expectedEventRandom.state());
    require(events[0].opcode == 28 && events[0].arguments.empty() &&
            events[0].offset == 32 && events[0].next == 38);
    require(events[1].opcode == 24 && events[1].arguments[0] == chosenSound &&
            events[2].opcode == 24 && events[2].arguments[0] == rangedSound &&
            events[3].opcode == randomTurn &&
            events[3].arguments == std::vector<int>({5}));
    require(eventProgram.instruction(32).opcode == 28 &&
            eventProgram.instruction(32).arguments ==
                std::vector<int>({2, 100, 101}) &&
            eventProgram.instruction(38).opcode == 26 &&
            eventProgram.instruction(38).arguments ==
                std::vector<int>({200, 203}) &&
            eventProgram.instruction(43).opcode == 34 &&
            eventProgram.instruction(43).arguments == std::vector<int>({5}));
    Random suspendedRandom;
    auto beforeSuspend = suspendedRandom.state();
    iscript::Animation suspendedVm(eventProgram, suspendedRandom);
    auto suspendedCursor = eventCursor;
    require(suspendedVm.select(suspendedCursor, 1));
    eventHost.apply = [](const auto &) { return iscript::HostAction::Suspend; };
    suspendedVm.tick(suspendedCursor, eventHost);
    require(suspendedCursor.pc == 38 && !suspendedCursor.ended &&
            suspendedRandom.state() == beforeSuspend &&
            eventProgram.instruction(32).opcode == 28);
    require(!vm.averageWalkingSpeed(7));
    rejects([&] { walkingVm.averageWalkingSpeed(7, 0); });
    std::array<dat::Image, 999> imageTypes{};
    imageTypes[0].script = 7;
    imageTypes[0].directional = true;
    imageTypes[0].animated = true;
    // Compare compiled image-only blocks against the full VM, including RNG,
    // wait timing, self/main frame dependencies and a changing main image.
    for (int variant = 0; variant < 2; ++variant) {
      std::vector<std::uint8_t> basic(128);
      word(basic, 0, 116);
      basic[4] = 'S';
      basic[5] = 'C';
      basic[6] = 'P';
      basic[7] = 'E';
      word(basic, 12, 16);
      word(basic, 14, 80);
      basic[16] = 0;
      word(basic, 17, 7);
      basic[19] = 2;
      basic[20] = 253;
      basic[21] = 3;
      basic[22] = 5;
      basic[23] = 4;
      basic[24] = 252;
      basic[25] = 8;
      if (variant == 0) {
        basic[26] = 29;
        basic[27] = 43;
        basic[28] = 9;
      } else {
        basic[26] = 43;
        basic[27] = 9;
        basic[28] = 29;
      }
      basic[29] = 6;
      basic[30] = 1;
      basic[31] = 3;
      basic[32] = 7;
      word(basic, 33, 16);
      basic[80] = 22;
      word(basic, 116, 7);
      word(basic, 118, 4);
      word(basic, 120, 65535);
      Program basicProgram(basic);
      SpriteResources data{imageTypes, basicProgram, {}, {}};
      Random fastRandom, fullRandom;
      Sprite fast;
      fast.initialize(0, data, fastRandom);
      const auto child = fast.underlay(0, data, fastRandom);
      std::array<SpriteImage, 2> full{};
      iscript::Animation reference(basicProgram, fullRandom);
      auto advance = [&](std::size_t id) {
        iscript::AnimationHost host;
        host.apply = [&](const iscript::Instruction &i) {
          auto &image = full[id];
          const auto &a = i.arguments;
          switch (i.opcode) {
          case 0:
            image.baseFrame = a[0];
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
          case 29:
            image.baseFrame = full[0].baseFrame;
            image.direction = full[0].direction;
            image.flip = full[0].flip;
            break;
          case 43:
            image.baseFrame = a[0];
            image.direction = full[0].direction;
            image.flip = full[0].flip;
            break;
          default:
            throw std::runtime_error("Unexpected image-only fixture effect");
          }
          return iscript::HostAction::Continue;
        };
        reference.tick(full[id].cursor, host);
      };
      for (std::size_t id = 0; id < 2; ++id) {
        full[id].cursor.script = 7;
        require(reference.select(full[id].cursor, 0));
        advance(id);
      }
      require(child == 1 && fastRandom.state() == fullRandom.state());
      for (int frame = 0; frame < 128; ++frame) {
        full[0].baseFrame = fast.images[0].baseFrame = 17 + frame % 3;
        full[0].direction = fast.images[0].direction = frame % 5;
        full[0].flip = fast.images[0].flip = frame % 7 == 0;
        fast.tick(data, fastRandom);
        advance(0);
        advance(1);
        require(fastRandom.state() == fullRandom.state());
        for (std::size_t id = 0; id < 2; ++id) {
          const auto &a = fast.images[id], &b = full[id];
          require(a.baseFrame == b.baseFrame && a.direction == b.direction &&
                  a.flip == b.flip && a.offset.x == b.offset.x &&
                  a.offset.y == b.offset.y && a.cursor.pc == b.cursor.pc &&
                  a.cursor.wait == b.cursor.wait &&
                  a.cursor.returnTo == b.cursor.returnTo &&
                  a.cursor.ended == b.cursor.ended);
        }
      }
      fast.animate(1, data, fastRandom);
      require(fast.order.empty());
    }
    {
      auto frames = fixture();
      word(frames, 14, 40); frames[40] = 22;
      frames[16] = 0; word(frames, 17, 5);
      frames[19] = 5; frames[20] = 1;
      frames[21] = 0; word(frames, 22, 9);
      frames[24] = 5; frames[25] = 1;
      frames[26] = 32; frames[27] = 0; // Reapply heading without turning.
      frames[28] = 5; frames[29] = 125;
      frames[30] = 7; word(frames, 31, 26);
      Program frameProgram(frames);
      SpriteResources frameResources{imageTypes, frameProgram, {},
          [](int, int frame, int, int, bool) { return Point{frame, frame / 2}; }};
      Sprite attached;
      attached.initialize(0, frameResources, random);
      auto cargo = attached.attach(0, frameResources, random);
      require(attached.images[cargo].offset.x == 5);
      attached.tick(frameResources, random);
      require(attached.images[attached.main].baseFrame == 9 &&
              attached.images[cargo].offset.x == 5);
      attached.tick(frameResources, random);
      require(attached.images[cargo].offset.x == 9);
    }
    SpriteResources resources{imageTypes, p, {}, {}};
    Sprite sprite;
    sprite.initialize(0, resources, random);
    sprite.setHeading(-1);
    require(sprite.images[0].direction == 0 && sprite.images[0].flip &&
            sprite.images[0].baseFrame == 17);
    auto spriteCopy = sprite;
    spriteCopy.images[0].offset.x = 200;
    require(sprite.images[0].offset.x == -3);
    auto waitingSprite = spriteCopy;
    waitingSprite.images[0].cursor.wait = 2;
    auto beforeSpriteWait = random.state();
    waitingSprite.tick(resources, random);
    require(waitingSprite.images[0].cursor.wait == 1 &&
            waitingSprite.images[0].offset.x == 200 &&
            random.state() == beforeSpriteWait);
    waitingSprite.tick(resources, random);
    require(waitingSprite.images[0].cursor.wait == 0 &&
            waitingSprite.images[0].offset.x == 200 &&
            random.state() == beforeSpriteWait);
    auto endedWaiting = waitingSprite;
    endedWaiting.images[0].cursor.ended = true;
    endedWaiting.images[0].cursor.wait = 2;
    endedWaiting.tick(resources, random);
    require(endedWaiting.order.empty() &&
            endedWaiting.images[0].cursor.wait == 2);
    waitingSprite.tick(resources, random);
    require(waitingSprite.images[0].offset.x == -3 &&
            random.state() == beforeSpriteWait);
    sprite.animate(1, resources, random);
    require(sprite.order.empty() && sprite.context.signal == 1 &&
            !spriteCopy.order.empty());
    std::vector<std::uint8_t> layered(128);
    word(layered, 0, 112);
    for (int at : {4, 48}) {
      layered[at] = 'S';
      layered[at + 1] = 'C';
      layered[at + 2] = 'P';
      layered[at + 3] = 'E';
    }
    word(layered, 12, 16);
    word(layered, 14, 32);
    layered[16] = 8;
    word(layered, 17, 1);
    layered[19] = 1;
    layered[20] = 254;
    layered[21] = 5;
    layered[22] = 2;
    layered[23] = 7;
    word(layered, 24, 26);
    layered[26] = 5;
    layered[27] = 2;
    layered[28] = 7;
    word(layered, 29, 26);
    layered[32] = 22;
    word(layered, 56, 60);
    word(layered, 58, 70);
    layered[60] = 0;
    word(layered, 61, 9);
    layered[63] = 5;
    layered[64] = 1;
    layered[65] = 7;
    word(layered, 66, 60);
    layered[70] = 22;
    word(layered, 112, 7);
    word(layered, 114, 4);
    word(layered, 116, 8);
    word(layered, 118, 48);
    word(layered, 120, 65535);
    Program childProgram(layered);
    imageTypes[1].script = 8;
    imageTypes[1].directional = true;
    SpriteResources childResources{imageTypes, childProgram, {}, {}};
    sprite.initialize(0, childResources, random);
    require(sprite.images.size() == 2 && sprite.order.front() == 1 &&
            sprite.order.back() == 0);
    require(sprite.images[1].offset.x == 1 && sprite.images[1].offset.y == -2 &&
            sprite.images[1].baseFrame == 9);
    sprite.setHeading(120);
    require(sprite.images[1].frame() == 24);
    sprite.tick(childResources, random);
    require(sprite.images[0].cursor.wait == 0 &&
            sprite.images[1].cursor.wait == 0);
    rejects([] { Program p({}); });
    for (int size : {1, 3, 7, 15, 59, 63})
      rejects([&] { Program p(std::span(bytes).first(size)); });
    auto bad = bytes;
    word(bad, 25, 17);
    rejects([&] { Program p(bad); });
    bad = bytes;
    word(bad, 25, 4);
    rejects([&] { Program p(bad); });
    bad = bytes;
    bad[16] = 255;
    rejects([&] { Program p(bad); });
    bad = bytes;
    word(bad, 8, 999);
    rejects([&] { Program p(bad); });
    bad = bytes;
    word(bad, 12, 0);
    rejects([&] { Program p(bad); });
    bad = bytes;
    bad[33] = 255;
    rejects([&] { Program p(bad); });
    std::cout << "PASS: bounded IScript graph decoding\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
