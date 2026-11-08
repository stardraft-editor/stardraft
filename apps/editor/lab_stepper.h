// SPDX-License-Identifier: MIT
#pragma once
#include "../kit/button.h"
#include <algorithm>

// Compact, keyboard-accessible +/- control shared by the measurement windows.
class LabStepper final : public stardraft::draw::View {
  stardraft::kit::Button less_, more_;
  int value_ = 0, maximum_ = 2;
  bool enabled_ = true, resetDown_ = false;
  std::wstring tip_;
  void step(int direction) {
    if (!enabled_)
      return;
    if (onStep) {
      onStep(direction);
      return;
    }
    const int next = std::clamp(value_ + direction, 0, maximum_);
    if (next == value_)
      return;
    setValue(next);
    if (onChange)
      onChange(value_);
  }

public:
  std::function<void(int)> onChange, onStep;
  std::function<void()> onReset;
  std::function<std::wstring(int)> format;
  LabStepper() {
    less_.setText(L"\u2212");
    more_.setText(L"+");
    less_.onClick = [this] { step(-1); };
    more_.onClick = [this] { step(1); };
    add(less_, {});
    add(more_, {});
  }
  void setMaximum(int n) {
    maximum_ = n;
    setValue(value_);
  }
  int value() const { return value_; }
  void setValue(int n) {
    value_ = std::clamp(n, 0, maximum_);
    setEnabled(enabled_);
    invalidate();
  }
  void setEnabled(bool on) {
    enabled_ = on;
    less_.setEnabled(on && (onStep ? (value_ == 0 || value_ > 1) : value_ > 0));
    more_.setEnabled(on && value_ < maximum_);
    invalidate();
  }
  void setTip(std::wstring text) {
    tip_ = std::move(text);
    less_.setTip(tip_);
    more_.setTip(tip_);
  }
  std::wstring tip(stardraft::draw::Point) override { return tip_; }
  bool focusable() const override { return enabled_; }
  void focusChanged(bool) override { invalidate(); }
  bool key(const stardraft::draw::KeyEvent &e) override {
    using stardraft::draw::Key;
    if (!enabled_ || !e.down)
      return false;
    if (e.key == Key::Left || e.key == Key::Down) {
      step(-1);
      return true;
    }
    if (e.key == Key::Right || e.key == Key::Up) {
      step(1);
      return true;
    }
    if (onReset && (e.key == Key::Home || e.key == Key::Space)) {
      onReset();
      return true;
    }
    return false;
  }
  bool mouse(const stardraft::draw::MouseEvent &e) override {
    if (!enabled_ || !onReset || e.button != 0)
      return false;
    if (e.kind == stardraft::draw::MouseKind::Down) {
      resetDown_ = true;
      return true;
    }
    if (e.kind == stardraft::draw::MouseKind::Up && resetDown_) {
      resetDown_ = false;
      if (e.pt.x >= stardraft::kit::px(28) &&
          e.pt.x < width() - stardraft::kit::px(28))
        onReset();
      return true;
    }
    return false;
  }
  void resized() override {
    const int side = stardraft::kit::px(28);
    less_.setFrame({0, 0, side, height()});
    more_.setFrame({width() - side, 0, width(), height()});
  }
  void paint(stardraft::draw::Canvas &c) override {
    using namespace stardraft;
    const auto &th = kit::theme();
    c.roundBox({0, 0, width(), height()}, th.readOnly,
               hasFocus() ? th.accentLine : th.border, kit::px(4));
    c.text({kit::px(30), 0, width() - kit::px(30), height()},
           format ? format(value_) : std::to_wstring(value_),
           enabled_ ? th.text : th.faint,
           draw::kCenter | draw::kMiddle | draw::kEllipsis);
  }
};
