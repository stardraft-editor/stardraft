#include "form.h"

#include <algorithm>

namespace stardraft::kit {

using draw::Canvas;
using draw::Rect;

namespace {

constexpr int kLabelGap = 8;   // between a label and its field, at 100 %
constexpr int kNotePad = 8;    // above and below a note, at 100 %
constexpr int kNoteInset = 4;  // from a note's top to its text, at 100 %
// The estimate of a note's text before the first paint: the UI font's letters (7 px) and lines (16 px) at 100 %.
constexpr int kEstCharW = 7, kEstLineH = 16;

int estimateTextH(const std::wstring& t, int width) {
    int lines = 0;
    size_t start = 0;
    while (true) {
        const size_t nl = t.find(L'\n', start);
        const size_t len = (nl == std::wstring::npos ? t.size() : nl) - start;
        const int w = int(len) * px(kEstCharW);
        lines += std::max(1, (w + std::max(1, width) - 1) / std::max(1, width));
        if (nl == std::wstring::npos) break;
        start = nl + 1;
    }
    return lines * px(kEstLineH);
}

bool isChild(const draw::View& parent, const draw::View* v) {
    const auto& ch = parent.children();
    return std::find(ch.begin(), ch.end(), v) != ch.end();
}

}  // namespace

void Form::setLabelWidth(int w100) {
    labelW_ = w100;
    place(nullptr);
}

void Form::addSection(const std::wstring& name) {
    Item it;
    it.kind = Kind::Section, it.text = name;
    items_.push_back(it);
    place(nullptr);
}

void Form::addRow(const std::wstring& label, draw::View& field, RowMark mark) {
    add(field, {0, 0, 0, 0});
    Item it;
    it.kind = Kind::Row, it.text = label, it.field = &field, it.mark = mark;
    items_.push_back(it);
    place(nullptr);
}

void Form::addNote(const std::wstring& text) {
    Item it;
    it.kind = Kind::Note, it.text = text;
    items_.push_back(it);
    place(nullptr);
}

void Form::addSpace(int h100) {
    Item it;
    it.kind = Kind::Space, it.space100 = h100;
    items_.push_back(it);
    place(nullptr);
}

void Form::clear() {
    for (const Item& it : items_)
        if (it.kind == Kind::Row && isChild(*this, it.field)) remove(*it.field);
    items_.clear();
    place(nullptr);
}

void Form::setMark(const draw::View& field, RowMark mark) {
    for (Item& it : items_)
        if (it.kind == Kind::Row && it.field == &field && it.mark != mark) it.mark = mark, invalidate();
}

void Form::layout(Canvas& c) { place(&c); }

void Form::place(Canvas* c) {
    // A row whose field was destroyed has left the children (View's destructor removes it): drop the row.
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [&](const Item& it) { return it.kind == Kind::Row && !isChild(*this, it.field); }),
                 items_.end());
    const int W = width();
    const int labelW = px(labelW_), rowH = px(kFormRowH), fieldH = px(kFieldH);
    int y = 0;
    for (Item& it : items_) {
        it.top = y;
        switch (it.kind) {
        case Kind::Section: it.h = rowH + px(kFormSectionGap); break;
        case Kind::Row: {
            const int fy = y + (rowH - fieldH) / 2;
            const Rect r{labelW, fy, std::max(labelW, W), fy + fieldH};
            const Rect cur = it.field->frame();
            if (cur.left != r.left || cur.top != r.top || cur.right != r.right || cur.bottom != r.bottom) it.field->setFrame(r);
            it.h = rowH;
            break;
        }
        case Kind::Note:
            if (c) it.textH = c->measureBox(it.text, W, draw::kWrap).y;
            else if (it.textH < 0) it.textH = estimateTextH(it.text, W);
            it.h = it.textH + px(kNotePad);
            break;
        case Kind::Space: it.h = px(it.space100); break;
        }
        y += it.h;
    }
    height_ = y;
    if (height() != y) setSize(W, y);
}

void Form::paint(Canvas& c) {
    place(&c);
    const Theme& th = theme();
    const int W = width(), labelW = px(labelW_), rowH = px(kFormRowH);
    for (const Item& it : items_) {
        switch (it.kind) {
        case Kind::Section:
            c.text({0, it.top, W, it.top + rowH}, it.text, th.text, draw::kMiddle);
            c.fill({0, it.top + rowH - 1, W, it.top + rowH}, th.border);
            break;
        case Kind::Row: {
            const draw::Color col = it.mark == RowMark::Disabled ? th.faint : it.mark == RowMark::Changed ? th.good : th.dim;
            c.text({0, it.top, labelW - px(kLabelGap), it.top + rowH}, it.text, col, draw::kMiddle | draw::kEllipsis);
            break;
        }
        case Kind::Note: {
            const int y = it.top + px(kNoteInset);
            c.text({0, y, W, y + it.textH}, it.text, th.dim, draw::kWrap);
            break;
        }
        case Kind::Space: break;
        }
    }
}

}  // namespace stardraft::kit
