// SPDX-License-Identifier: MIT
#pragma once
#include "../kit/button.h"
#include <cmath>

class LabBasePreview final : public stardraft::draw::View {
  int selected_ = 0;
  stardraft::draw::Rect mapRect() const {
    const int p = stardraft::kit::px(3);
    const float f =
        std::min(float(width() - 2 * p) / std::max(1, terrain.width()),
                 float(height() - 2 * p) / std::max(1, terrain.height()));
    const int w = int(terrain.width() * f), h = int(terrain.height() * f);
    return {(width() - w) / 2, (height() - h) / 2, (width() + w) / 2,
            (height() + h) / 2};
  }

public:
  struct Marker {
    float x, y;
    stardraft::draw::Color color;
  }; // normalized map coordinates
  stardraft::draw::Bitmap terrain;
  std::vector<Marker> markers;
  std::function<void(int)> onSelect;
  void setValue(int n) {
    selected_ = n;
    invalidate();
  }
  bool focusable() const override { return !markers.empty(); }
  void focusChanged(bool) override { invalidate(); }
  std::wstring tip(stardraft::draw::Point) override {
    return L"Click a colored base to select it. Arrow keys choose the next "
           L"base.";
  }
  bool key(const stardraft::draw::KeyEvent &e) override {
    using stardraft::draw::Key;
    if (!e.down || markers.empty())
      return false;
    const int d = e.key == Key::Down || e.key == Key::Right ? 1
                  : e.key == Key::Up || e.key == Key::Left  ? -1
                                                            : 0;
    if (!d)
      return false;
    if (onSelect)
      onSelect(std::clamp(selected_ + d, 0, int(markers.size()) - 1));
    return true;
  }
  bool mouse(const stardraft::draw::MouseEvent &e) override {
    using namespace stardraft;
    if (e.kind != draw::MouseKind::Up || e.button != 0 || markers.empty())
      return false;
    const auto r = mapRect();
    int best = 0;
    double distance = 1e20;
    for (size_t i = 0; i < markers.size(); ++i) {
      const auto m = markers[i];
      const double d = std::hypot(r.left + m.x * r.width() - e.pt.x,
                                  r.top + m.y * r.height() - e.pt.y);
      if (d < distance) {
        distance = d;
        best = int(i);
      }
    }
    if (onSelect)
      onSelect(best);
    return true;
  }
  void paint(stardraft::draw::Canvas &c) override {
    using namespace stardraft;
    const auto &th = kit::theme();
    c.roundBox({0, 0, width(), height()}, th.readOnly,
               hasFocus() ? th.accentLine : th.border, kit::px(4));
    if (terrain.width() == 0)
      return;
    const auto r = mapRect();
    c.bitmap(terrain, {0, 0, terrain.width(), terrain.height()}, r, true);
    for (size_t i = 0; i < markers.size(); ++i) {
      const auto m = markers[i];
      const int x = r.left + int(m.x * r.width()),
                y = r.top + int(m.y * r.height()),
                k = kit::px(int(i) == selected_ ? 4 : 3);
      c.ellipse({x - k, y - k, x + k + 1, y + k + 1}, m.color,
                draw::rgb(238, 239, 243));
    }
  }
};
