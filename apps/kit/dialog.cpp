#include "dialog.h"

#include <algorithm>

#include "theme.h"

namespace stardraft::kit {

using draw::Canvas;
using draw::Rect;

namespace {

// Sets a view's frame only when it changes (a frame change invalidates the view, and paint places the views too).
void place(draw::View& v, Rect r) {
    const Rect cur = v.frame();
    if (cur.left != r.left || cur.top != r.top || cur.right != r.right || cur.bottom != r.bottom) v.setFrame(r);
}

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
        auto b = std::make_unique<Button>(s.text, s.primary ? Tone::Primary : Tone::Normal, [this, r = s.result] { close(r); });
        add(*b, {0, 0, 0, 0});
        buttons_.push_back(std::move(b));
        widths_.push_back(px(kButtonMinW));
    }
    placeAll();
}

void Dialog::layout(Canvas& c) {
    for (size_t k = 0; k < buttons_.size(); ++k) widths_[k] = buttons_[k]->preferredWidth(c);
    placeAll();
}

void Dialog::placeAll() {
    const int W = width(), H = height(), pad = px(kDialogPad), bh = px(kButtonH), gap = px(kButtonGap), sep = px(kDialogGap);
    const int by = H - pad - bh;
    int x = W - pad;
    for (size_t k = buttons_.size(); k-- > 0;) {
        place(*buttons_[k], {x - widths_[k], by, x, by + bh});
        x -= widths_[k] + gap;
    }
    if (content_) {
        const int bottom = buttons_.empty() ? H - pad : by - sep;
        place(*content_, {pad, pad, std::max(pad, W - pad), std::max(pad, bottom)});
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
    if (draw::Host* h = from.host()) h->runModal(from, *this, draw::WindowSpec{draw::WindowKind::Dialog, Rect{0, 0, size.x, size.y}, title});
    if (done_) return result_;
    const DialogButton* c = find(true);
    return c ? c->result : kDialogClosed;
}

void Dialog::close(int r) {
    if (done_) return;
    done_ = true, result_ = r;
    draw::Host* h = host();
    if (!h && from_) h = from_->host();
    if (h) h->closeWindow(*this);
}

void Dialog::paint(Canvas& c) {
    layout(c);
    c.fill({0, 0, width(), height()}, theme().bg);
}

bool Dialog::key(const draw::KeyEvent& e) {
    if (!e.down || done_) return false;
    if (e.key == draw::Key::Enter) {
        if (const DialogButton* p = find(false)) return close(p->result), true;
    } else if (e.key == draw::Key::Escape) {
        if (const DialogButton* c = find(true)) return close(c->result), true;
    }
    return false;
}

}  // namespace stardraft::kit
