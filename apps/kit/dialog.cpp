#include "dialog.h"

#include <algorithm>

#include "theme.h"

namespace stardraft::kit {

using draw::Canvas;
using draw::Rect;

namespace {

// Sets a view's frame only when it changes (a frame change invalidates the view, and paint places the views too).

}  // namespace

void Dialog::setContent(draw::View& c) {
    if (content_ == &c) return;
    if (content_) remove(*content_);
    content_ = &c;
    add(c, {0, 0, 0, 0});
    placeAll();
}

void Dialog::setButtons(std::vector<DialogButton> bs) {
    for (auto& b : buttons_) remove(*b);
    buttons_.clear();
    widths_.clear();
    specs_ = std::move(bs);
    for (const DialogButton& s : specs_) {
        auto b = std::make_unique<Button>(s.text, s.primary ? Tone::Primary : Tone::Normal, [this, r = s.result] { press(r); });
        add(*b, {0, 0, 0, 0});
        buttons_.push_back(std::move(b));
        widths_.push_back(px(s.width > 0 ? s.width : kButtonMinW));
    }
    placeAll();
}

void Dialog::layout(Canvas& c) {
    for (size_t k = 0; k < buttons_.size(); ++k)
        if (specs_[k].width <= 0) widths_[k] = buttons_[k]->preferredWidth(c);
    placeAll();
}

void Dialog::placeAll() {
    const int W = width(), H = height(), bh = px(kButtonH), gap = px(insets_.buttonGap);
    const int padL = px(insets_.left), padR = px(insets_.right), padT = px(insets_.top), padB = px(insets_.bottom);
    const int by = H - padB - bh;
    int x = W - padR, lx = padL;
    for (size_t k = buttons_.size(); k-- > 0;) {
        if (specs_[k].left) continue;
        buttons_[k]->setFrame({x - widths_[k], by, x, by + bh});
        x -= widths_[k] + gap;
    }
    for (size_t k = 0; k < buttons_.size(); ++k) {
        if (!specs_[k].left) continue;
        buttons_[k]->setFrame({lx, by, lx + widths_[k], by + bh});
        lx += widths_[k] + gap;
    }
    if (content_) {
        const int bottom = buttons_.empty() ? H - padB : by - px(insets_.gap);
        content_->setFrame({padL, padT, std::max(padL, W - padR), std::max(padT, bottom)});
    }
}

const DialogButton* Dialog::find(bool cancel) const {
    for (const DialogButton& s : specs_)
        if (cancel ? s.cancel : s.primary) return &s;
    return nullptr;
}

int Dialog::run(draw::View& from, const std::wstring& title, draw::Point size) {
    done_ = false, result_ = kDialogClosed;
    from_ = &from;
    const Rect at = placed_ ? Rect{place_.x, place_.y, place_.x + size.x, place_.y + size.y} : Rect{0, 0, size.x, size.y};
    if (draw::Host* h = from.host()) h->runModal(from, *this, draw::WindowSpec{draw::WindowKind::Dialog, at, title, workArea_, resizable_, placed_});
    if (done_) return result_;
    const DialogButton* c = find(true);
    return c ? c->result : kDialogClosed;
}

void Dialog::close(int r) {
    if (done_) return;
    done_ = true, result_ = r;
    if (closing) closing();
    draw::Host* h = host();
    if (!h && from_) h = from_->host();
    if (h) h->closeWindow(*this);
}

void Dialog::setAllow(std::function<bool(int)> allow) { allow_ = std::move(allow); }

void Dialog::setEnabledFor(int result, bool on) {
    for (size_t k = 0; k < specs_.size() && k < buttons_.size(); ++k)
        if (specs_[k].result == result) buttons_[k]->setEnabled(on);
}

void Dialog::setTextFor(int result, std::wstring text) {
    for (size_t k = 0; k < specs_.size() && k < buttons_.size(); ++k)
        if (specs_[k].result == result) specs_[k].text = text, buttons_[k]->setText(std::move(text));
    invalidate();  // (paint measures the buttons again: layout)
}

void Dialog::press(int result) {
    for (size_t k = 0; k < specs_.size() && k < buttons_.size(); ++k)
        if (specs_[k].result == result && !buttons_[k]->enabled()) return;  // (Enter on a disabled Next)
    if (allow_ && !allow_(result)) return;
    for (const DialogButton& s : specs_)
        if (s.result == result && s.step) {
            if (stepped) stepped(result);
            return;
        }
    close(result);
}

void Dialog::paint(Canvas& c) {
    layout(c);
    c.fill({0, 0, width(), height()}, theme().bg);
}

bool Dialog::key(const draw::KeyEvent& e) {
    if (!e.down || done_) return false;
    if (e.key == draw::Key::Enter) {
        if (const DialogButton* p = find(false)) return press(p->result), true;
    } else if (e.key == draw::Key::Escape) {
        if (const DialogButton* c = find(true)) return press(c->result), true;
    }
    return false;
}

}  // namespace stardraft::kit
