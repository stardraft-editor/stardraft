#include "text_model.h"

#include <algorithm>

namespace stardraft::kit {

namespace {

constexpr size_t kMaxUndo = 1000;  // steps kept; the oldest go first

void appendCodePoint(std::wstring& o, uint32_t cp) {
    if (cp > 0xFFFF && sizeof(wchar_t) == 2) {
        cp -= 0x10000;
        o += wchar_t(0xD800 + (cp >> 10));
        o += wchar_t(0xDC00 + (cp & 0x3FF));
    } else {
        o += wchar_t(cp);
    }
}

enum CharClass { kSpace, kWord, kPunct, kBreak };

// What a word is for the word moves and the double click.
CharClass classOf(wchar_t c) {
    const uint32_t u = uint32_t(c);
    if (c == '\n') return kBreak;
    if (c == ' ' || c == '\t' || c == '\r' || u == 0xA0 || u == 0x3000) return kSpace;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') return kWord;
    if (u >= 0x80) return (u >= 0x2000 && u <= 0x206F) ? kPunct : kWord;  // letters of any script; general punctuation apart
    return kPunct;
}

}  // namespace

std::wstring fromUtf8(std::string_view s) {
    std::wstring o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0;
        size_t n = 0;
        if (c < 0x80) cp = c, n = 1;
        else if ((c & 0xE0) == 0xC0) cp = c & 0x1F, n = 2;
        else if ((c & 0xF0) == 0xE0) cp = c & 0x0F, n = 3;
        else if ((c & 0xF8) == 0xF0) cp = c & 0x07, n = 4;
        else {
            o += wchar_t(0xFFFD), ++i;
            continue;
        }
        if (i + n > s.size()) {  // cut off at the end
            o += wchar_t(0xFFFD);
            break;
        }
        bool ok = true;
        for (size_t k = 1; k < n && ok; ++k) {
            const unsigned char d = (unsigned char)s[i + k];
            if ((d & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (d & 0x3F);
        }
        if (!ok) {
            o += wchar_t(0xFFFD), ++i;
            continue;
        }
        i += n;
        appendCodePoint(o, cp);
    }
    return o;
}

std::string toUtf8(std::wstring_view w) {
    std::string o;
    o.reserve(w.size());
    auto put = [&](uint32_t cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) o += char(0xC0 | (cp >> 6)), o += char(0x80 | (cp & 63));
        else if (cp < 0x10000) o += char(0xE0 | (cp >> 12)), o += char(0x80 | ((cp >> 6) & 63)), o += char(0x80 | (cp & 63));
        else o += char(0xF0 | (cp >> 18)), o += char(0x80 | ((cp >> 12) & 63)), o += char(0x80 | ((cp >> 6) & 63)), o += char(0x80 | (cp & 63));
    };
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t u = uint32_t(w[i]);
        if (isHighSurrogate(w[i]) && i + 1 < w.size() && isLowSurrogate(w[i + 1])) {
            u = 0x10000 + ((u - 0xD800) << 10) + (uint32_t(w[i + 1]) - 0xDC00);
            ++i;
        } else if (u >= 0xD800 && u <= 0xDFFF) {
            u = 0xFFFD;  // a lone half
        }
        put(u);
    }
    return o;
}

// --- Buffer -------------------------------------------------------------------------------------------------------

void TextModel::setText(std::wstring t) {
    text_ = sanitize(t, maxLength_ ? maxLength_ : size_t(-1));
    caret_ = anchor_ = text_.size();
    undo_.clear();
    redo_.clear();
    groupBreak_ = true;
    ++revision_;
}

// What may go into the buffer: newlines and tabs by the line mode, no control characters, the filter, and at most
// `room` units (cut so that no surrogate pair is split).
std::wstring TextModel::sanitize(std::wstring_view s, size_t room) const {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == '\r') {
            if (i + 1 < s.size() && s[i + 1] == '\n') continue;
            c = '\n';
        }
        if (c == '\n' || c == '\t') {
            if (!multiline_) continue;
        } else if (uint32_t(c) < 0x20) {
            continue;
        }
        if (filter_ && !filter_(c)) continue;
        out += c;
    }
    if (out.size() > room) {
        size_t cut = room;
        if (cut > 0 && isHighSurrogate(out[cut - 1])) --cut;
        out.resize(cut);
    }
    return out;
}

// --- Caret and selection ------------------------------------------------------------------------------------------

void TextModel::setCaret(size_t pos, bool extend) {
    groupBreak_ = true;
    caret_ = std::min(pos, text_.size());
    if (!extend) anchor_ = caret_;
}

void TextModel::setSelection(size_t anchor, size_t caret) {
    groupBreak_ = true;
    anchor_ = std::min(anchor, text_.size());
    caret_ = std::min(caret, text_.size());
}

size_t TextModel::prevPos(size_t pos) const {
    if (pos == 0) return 0;
    size_t p = std::min(pos, text_.size()) - 1;
    if (p > 0 && isLowSurrogate(text_[p]) && isHighSurrogate(text_[p - 1])) --p;
    return p;
}

size_t TextModel::nextPos(size_t pos) const {
    const size_t n = text_.size();
    if (pos >= n) return n;
    size_t p = pos + 1;
    if (isHighSurrogate(text_[pos]) && p < n && isLowSurrogate(text_[p])) ++p;
    return p;
}

// --- Edits --------------------------------------------------------------------------------------------------------

bool TextModel::insert(std::wstring_view s, bool typing) {
    if (readOnly_) return false;
    const size_t from = selectionStart(), to = selectionEnd();
    size_t room = size_t(-1);
    if (maxLength_) room = maxLength_ > text_.size() - (to - from) ? maxLength_ - (text_.size() - (to - from)) : 0;
    const std::wstring clean = sanitize(s, room);
    if (clean.empty()) return false;
    const bool asTyping = typing && from == to;
    return replace(from, to, clean, asTyping ? Kind::Typing : Kind::None, from + clean.size());
}

bool TextModel::erase(size_t from, size_t to) {
    if (readOnly_) return false;
    from = std::min(from, text_.size()), to = std::min(to, text_.size());
    if (from >= to) return false;
    return replace(from, to, {}, Kind::None, from);
}

bool TextModel::backspace() {
    if (readOnly_) return false;
    if (hasSelection()) return deleteSelection();
    if (caret_ == 0) return false;
    const size_t p = prevPos(caret_);
    return replace(p, caret_, {}, Kind::Back, p);
}

bool TextModel::deleteForward() {
    if (readOnly_) return false;
    if (hasSelection()) return deleteSelection();
    if (caret_ >= text_.size()) return false;
    return replace(caret_, nextPos(caret_), {}, Kind::Forward, caret_);
}

bool TextModel::backspaceWord() {
    if (readOnly_) return false;
    if (hasSelection()) return deleteSelection();
    if (caret_ == 0) return false;
    const size_t p = wordStartBefore(caret_);
    return replace(p, caret_, {}, Kind::None, p);
}

bool TextModel::deleteWordForward() {
    if (readOnly_) return false;
    if (hasSelection()) return deleteSelection();
    const size_t e = wordEndAfter(caret_);
    return replace(caret_, e, {}, Kind::None, caret_);
}

bool TextModel::replace(size_t from, size_t to, std::wstring_view ins, Kind kind, size_t caret) {
    if (readOnly_ || (from == to && ins.empty())) return false;
    Edit e;
    e.pos = from;
    e.removed = text_.substr(from, to - from);
    e.inserted = std::wstring(ins);
    e.caretBefore = caret_, e.anchorBefore = anchor_;
    e.caretAfter = e.anchorAfter = caret;
    e.kind = kind;
    text_.replace(from, to - from, ins);
    caret_ = anchor_ = caret;
    ++revision_;

    // A run of typing, or of Backspace or Delete, is one step: the new edit joins the last when they meet.
    bool merged = false;
    if (!groupBreak_ && kind != Kind::None && !undo_.empty() && undo_.back().kind == kind) {
        Edit& last = undo_.back();
        if (kind == Kind::Typing && e.removed.empty() && last.removed.empty() && e.pos == last.pos + last.inserted.size()) {
            last.inserted += e.inserted, merged = true;
        } else if (kind == Kind::Back && e.inserted.empty() && last.inserted.empty() && e.pos + e.removed.size() == last.pos) {
            last.removed = e.removed + last.removed, last.pos = e.pos, merged = true;
        } else if (kind == Kind::Forward && e.inserted.empty() && last.inserted.empty() && e.pos == last.pos) {
            last.removed += e.removed, merged = true;
        }
        if (merged) last.caretAfter = e.caretAfter, last.anchorAfter = e.anchorAfter;
    }
    if (!merged) {
        undo_.push_back(std::move(e));
        if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    }
    redo_.clear();
    groupBreak_ = kind == Kind::None;
    return true;
}

bool TextModel::undo() {
    if (readOnly_ || undo_.empty()) return false;
    Edit e = std::move(undo_.back());
    undo_.pop_back();
    text_.replace(e.pos, e.inserted.size(), e.removed);
    caret_ = e.caretBefore, anchor_ = e.anchorBefore;
    ++revision_;
    groupBreak_ = true;
    redo_.push_back(std::move(e));
    return true;
}

bool TextModel::redo() {
    if (readOnly_ || redo_.empty()) return false;
    Edit e = std::move(redo_.back());
    redo_.pop_back();
    text_.replace(e.pos, e.removed.size(), e.inserted);
    caret_ = e.caretAfter, anchor_ = e.anchorAfter;
    ++revision_;
    groupBreak_ = true;
    undo_.push_back(std::move(e));
    return true;
}

// --- Words and lines ----------------------------------------------------------------------------------------------

size_t TextModel::wordStartBefore(size_t pos) const {
    size_t i = std::min(pos, text_.size());
    while (i > 0 && classOf(text_[i - 1]) == kSpace) --i;
    if (i == 0) return 0;
    const CharClass cls = classOf(text_[i - 1]);
    while (i > 0 && classOf(text_[i - 1]) == cls) --i;
    return i;
}

size_t TextModel::wordEndAfter(size_t pos) const {
    const size_t n = text_.size();
    size_t i = std::min(pos, n);
    if (i < n && classOf(text_[i]) != kSpace) {
        const CharClass cls = classOf(text_[i]);
        while (i < n && classOf(text_[i]) == cls) ++i;
    }
    while (i < n && classOf(text_[i]) == kSpace) ++i;
    return i;
}

void TextModel::wordAt(size_t pos, size_t& from, size_t& to) const {
    const size_t n = text_.size();
    if (n == 0) {
        from = to = 0;
        return;
    }
    const size_t i = std::min(pos, n - 1);  // at the end: the character before
    const CharClass cls = classOf(text_[i]);
    from = i;
    while (from > 0 && classOf(text_[from - 1]) == cls) --from;
    to = i + 1;
    while (to < n && classOf(text_[to]) == cls) ++to;
}

size_t TextModel::lineStart(size_t pos) const {
    size_t i = std::min(pos, text_.size());
    while (i > 0 && text_[i - 1] != '\n') --i;
    return i;
}

size_t TextModel::lineEnd(size_t pos) const {
    const size_t n = text_.size();
    size_t i = std::min(pos, n);
    while (i < n && text_[i] != '\n') ++i;
    return i;
}

}  // namespace stardraft::kit
