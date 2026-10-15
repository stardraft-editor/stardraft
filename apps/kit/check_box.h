#pragma once
// Check boxes and radio buttons (docs/dev/CONTROLS.md, "Check boxes"): a 14 px box or circle with its label. The whole
// row is the click area. A click or Space changes it. A changed setting's label is green. A RadioGroup holds the radio
// buttons of one choice: choosing one turns the others off.

#include <functional>
#include <string>
#include <vector>

#include "theme.h"
#include "view.h"

namespace stardraft::kit {

// The box or circle and its label: what CheckBox and RadioButton share.
class ChoiceBox : public draw::View {
public:
    const std::wstring& label() const { return label_; }
    void setLabel(std::wstring t) {
        label_ = std::move(t);
        invalidate();
    }
    bool on() const { return on_; }
    // Sets the state without a change event.
    virtual void setOn(bool on) {
        if (on_ == on) return;
        on_ = on;
        invalidate();
    }
    // The label in green: a setting changed from its default.
    void setChanged(bool c) {
        changed_ = c;
        invalidate();
    }
    bool changed() const { return changed_; }
    void setEnabled(bool e) {
        enabled_ = e;
        invalidate();
    }
    bool enabled() const { return enabled_; }
    void setTip(std::wstring t) { tip_ = std::move(t); }
    // The width of the box, its gap and the label on one line.
    int preferredWidth(draw::Canvas& c) const;

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    draw::Cursor cursor(draw::Point) override { return enabled_ ? draw::Cursor::Hand : draw::Cursor::Arrow; }
    std::wstring tip(draw::Point) override { return tip_; }
    bool focusable() const override { return enabled_; }
    void focusChanged(bool has) override;
    void captureLost() override;

protected:
    ChoiceBox(std::wstring label, bool on) : on_(on), label_(std::move(label)) {}
    // What a click or Space does.
    virtual void activate() = 0;
    // The box or circle `b`: its state, the pointer over it, and whether it has the focus.
    virtual void drawMark(draw::Canvas& c, draw::Rect b, bool over, bool focused) const = 0;

    bool on_ = false;

private:
    std::wstring label_, tip_;
    bool changed_ = false, enabled_ = true, hot_ = false, down_ = false;
    // The last focus came from the mouse (a click): the box shows no focus ring, only the keyboard's focus does.
    bool pointer_ = false;
};

// A check box: a click or Space turns it on or off.
class CheckBox : public ChoiceBox {
public:
    CheckBox() : ChoiceBox({}, false) {}
    CheckBox(std::wstring label, bool on = false, std::function<void(bool)> onChange = nullptr)
        : ChoiceBox(std::move(label), on), onChange(std::move(onChange)) {}

    // Called with the new state after a click or Space (not for setOn).
    std::function<void(bool)> onChange;

protected:
    void activate() override;
    void drawMark(draw::Canvas& c, draw::Rect b, bool over, bool focused) const override;
};

class RadioButton;

// The radio buttons of one choice. A button joins its group when it is made and leaves it when it is destroyed.
class RadioGroup {
public:
    RadioGroup() = default;
    ~RadioGroup();
    RadioGroup(const RadioGroup&) = delete;
    RadioGroup& operator=(const RadioGroup&) = delete;

    // Called with the button the user chose (not for select).
    std::function<void(RadioButton&)> onChange;

    // The button that is on, or null.
    RadioButton* selected() const;
    // The buttons in the order they were made; the arrow keys go through them in this order.
    const std::vector<RadioButton*>& buttons() const { return buttons_; }
    // The one button Tab stops on: the chosen one, else the first; a hidden or disabled button is not one. Null when
    // none is.
    RadioButton* tabStop() const;
    // Turns `b` on and the others off, without a change event.
    void select(RadioButton& b);

private:
    friend class RadioButton;
    std::vector<RadioButton*> buttons_;
};

// A radio button: a circle that is on when its group chose it. Click, Space, or an arrow key (which moves to the next
// or the one before, and chooses it) changes the group. Tab goes through a group once, at its tabStop().
class RadioButton : public ChoiceBox {
public:
    // In `group`; `on`: this one is the group's choice (the others go off).
    RadioButton(std::wstring label, RadioGroup& group, bool on = false);
    ~RadioButton() override;
    RadioButton(const RadioButton&) = delete;
    RadioButton& operator=(const RadioButton&) = delete;

    RadioGroup* group() const { return group_; }
    // On: the group's other buttons go off. Off: this one only.
    void setOn(bool on) override;
    bool key(const draw::KeyEvent& e) override;
    // The group has one Tab stop: its tabStop(). The arrow keys reach the others.
    bool focusable() const override;

protected:
    void activate() override;
    void drawMark(draw::Canvas& c, draw::Rect b, bool over, bool focused) const override;

private:
    friend class RadioGroup;
    void setState(bool on);
    RadioGroup* group_ = nullptr;
};

}  // namespace stardraft::kit
