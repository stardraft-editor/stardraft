#include "menu.h"

#include <algorithm>

#include "theme.h"

namespace stardraft::kit {

using draw::Canvas;
using draw::Color;
using draw::Font;
using draw::KeyEvent;
using draw::Key;
using draw::MouseEvent;
using draw::MouseKind;
using draw::Point;
using draw::Rect;
using draw::View;
using draw::WindowKind;
using draw::WindowSpec;

namespace {

// Sizes at 100 %, as the painted context menu of the editor (apps/editor/context_menu.inc).
constexpr int kMenuRowH = 30, kSepH = 9, kPad = 5, kIconCol = 34, kSubW = 22, kMinW = 200, kKeysGap = 36, kRightPad = 18;
constexpr int kPanelRadius = 8, kRowRadius = 6, kNamePad = 10;
constexpr int kHoverMs = 200;  // a submenu opens when the mouse rests on its item this long
constexpr int kIconCheck = 76;  // UiIcon::Check (apps/editor/res/ui_icons.h)

int rowHeight(const MenuItem& it) { return px(it.separator ? kSepH : kMenuRowH); }

bool isLetter(int k) { return (k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9'); }
wchar_t lower(wchar_t c) { return c >= L'A' && c <= L'Z' ? wchar_t(c + 32) : c; }
// The first letter of `label` is `k` (keys are ASCII capitals, see draw::Key).
bool startsWith(const std::wstring& label, int k) { return !label.empty() && lower(label[0]) == lower(wchar_t(k)); }

class Panel;

// The menus open from one menu: the root menu and the submenus, each in its own window. Owns its panels. A panel that
// closed is kept until a timer runs (a panel may be the one handling the event that closes it), and the closed chain is
// deleted by a timer too.
struct Chain {
    Chain(draw::Host& h, Menu::Options o, draw::View* f) : host(h), opts(std::move(o)), from(f) {}
    ~Chain();

    draw::Host& host;
    Menu::Options opts;
    draw::View* from;  // the view the menus were opened from (they get the keys back when they close)
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);  // the timers and the host's callbacks check it
    std::vector<std::unique_ptr<Panel>> owned;      // the panels of the open menus
    std::vector<std::unique_ptr<Panel>> graveyard;  // the panels that closed, deleted by a timer
    std::vector<Panel*> live;  // the open menus, the root first
    int hoverTimer = 0, pruneTimer = 0;
    bool closed = false;

    void cancelHover() {
        if (hoverTimer) host.cancelTimer(hoverTimer), hoverTimer = 0;
    }
    void scheduleHover(int level, int row);
    // Closes the menus from `level` down; the menu above them takes the focus.
    void closeFrom(size_t level);
    // Moves the panels that are no longer open to the graveyard.
    void prune();
    // Closes all the menus. `outside`: the host closed them (a click outside).
    void closeAll(bool outside);
};

Chain* g_current = nullptr;  // the open chain (one at a time)
Menu::TestHook g_testHook;   // Menu::setTestHook
Menu::NativeHook g_nativeHook;  // Menu::setNativeHook
Menu::ShortcutText g_shortcutText;  // Menu::setShortcutText

// One menu: its rows, the highlighted row and the submenu that is open.
class Panel final : public View {
public:
    Panel(Chain& chain, int level, std::vector<MenuItem> items, MenuLayout layout)
        : chain_(chain), level_(level), items_(std::move(items)), layout_(std::move(layout)) {}

    void paint(Canvas& c) override;
    bool mouse(const MouseEvent& e) override;
    bool key(const KeyEvent& e) override;

    // The chain's calls.
    void hoverOpen(int i) {
        if (i == hot_ && open_ != i) openSub(i, false);
    }
    void clearOpen() {
        if (open_ >= 0) open_ = -1, invalidate();
    }
    void highlightFirst() {
        for (int i = 0; i < int(items_.size()); ++i)
            if (enabledRow(i)) return setHot(i);
    }

private:
    bool enabledRow(int i) const {
        return i >= 0 && i < int(items_.size()) && !items_[size_t(i)].separator && items_[size_t(i)].enabled;
    }
    Rect row(int i) const;
    int rowAt(Point p) const;
    void setHot(int i) {
        if (hot_ != i) hot_ = i, invalidate();
    }
    void step(int dir);
    bool jump(int letter);
    void hover(int i);
    void choose(int i, bool byKeys);
    void openSub(int i, bool byKeys);
    void sideways(int dir);

    Chain& chain_;
    int level_;
    std::vector<MenuItem> items_;
    MenuLayout layout_;
    int hot_ = -1, open_ = -1, armed_ = -1;
};

Chain::~Chain() { cancelHover(); }  // (after Panel: deletes the panels)

void Chain::scheduleHover(int level, int row) {
    cancelHover();
    std::weak_ptr<bool> w = alive;
    hoverTimer = host.timer(kHoverMs, [w, this, level, row] {
        if (!w.lock()) return;
        hoverTimer = 0;
        if (level < int(live.size())) live[size_t(level)]->hoverOpen(row);
    });
}

void Chain::closeFrom(size_t level) {
    if (live.size() <= level) return;
    while (live.size() > level) {
        host.closeWindow(*live.back());
        host.forget(*live.back());
        live.pop_back();
    }
    cancelHover();
    prune();
    if (live.empty()) return;
    live.back()->clearOpen();
    host.focus(*live.back());
}

void Chain::prune() {
    for (size_t i = 0; i < owned.size();) {
        Panel* p = owned[i].get();
        if (std::find(live.begin(), live.end(), p) == live.end()) {
            graveyard.push_back(std::move(owned[i]));
            owned.erase(owned.begin() + std::ptrdiff_t(i));
        } else {
            ++i;
        }
    }
    if (graveyard.empty() || pruneTimer) return;
    std::weak_ptr<bool> w = alive;
    pruneTimer = host.timer(0, [w, this] {
        if (!w.lock()) return;
        pruneTimer = 0;
        graveyard.clear();  // the event that closed these panels has been handled
    });
}

void Chain::closeAll(bool outside) {
    if (closed) return;
    closed = true;
    if (g_current == this) g_current = nullptr;
    cancelHover();
    while (!live.empty()) {
        host.closeWindow(*live.back());
        host.forget(*live.back());
        live.pop_back();
    }
    if (!outside && opts.focusFrom && from) host.focus(*from);
    if (opts.closed) {
        auto f = opts.closed;
        f(outside);
    }
    host.timer(0, [c = this] { delete c; });  // after the event that closes the menus has been handled
}

Rect Panel::row(int i) const {
    int y = px(kPad);
    for (int k = 0; k < i; ++k) y += rowHeight(items_[size_t(k)]);
    return {px(kPad), y, width() - px(kPad), y + rowHeight(items_[size_t(i)])};
}

int Panel::rowAt(Point p) const {
    for (int i = 0; i < int(items_.size()); ++i)
        if (!items_[size_t(i)].separator && row(i).contains(p)) return i;
    return -1;
}

void Panel::paint(Canvas& c) {
    const Theme& th = theme();
    const Font of = c.setFont(c.font(draw::FontDesc{}));
#if defined(_WIN32)
    // Windows: a square panel; the system rounds the window's corners as it rounds its own menus (Windows 11), and
    // Windows 10 menus are square.
    c.fill({0, 0, width(), height()}, th.bg);
    c.frame({0, 0, width(), height()}, th.border);
#else
    c.roundBox({0, 0, width(), height()}, th.bg, th.border, px(kPanelRadius));
#endif
    for (int i = 0; i < int(items_.size()); ++i) {
        const MenuItem& it = items_[size_t(i)];
        const Rect r = row(i);
        if (it.separator) {
            const int y = (r.top + r.bottom) / 2;
            c.fill({r.left + px(8), y, r.right - px(8), y + 1}, th.border);
            continue;
        }
        const bool on = it.enabled;
        if (on && (i == hot_ || i == open_)) c.roundBox(r, th.sel, th.sel, px(kRowRadius));
        const Color fg = on ? th.text : th.faint;
        const Rect mark{r.left + px(8), r.top + (r.height() - px(18)) / 2, r.left + px(26), r.top + (r.height() + px(18)) / 2};
        if (it.checked && it.radio) {  // (a bullet, centred in the mark's box)
            const int d = px(8), mx = (mark.left + mark.right) / 2, my = (mark.top + mark.bottom) / 2;
            c.roundBox({mx - d / 2, my - d / 2, mx - d / 2 + d, my - d / 2 + d}, on ? th.accent : th.faint, on ? th.accent : th.faint, d / 2);
        } else if (it.checked) c.icon(mark, kIconCheck, on ? th.accent : th.faint, px(16));
        else if (it.icon >= 0) c.icon(mark, it.icon, on && it.iconColor != draw::kNone ? it.iconColor : fg, px(16));
        const int textRight = r.right - px(it.sub.empty() ? 10 : kSubW);
        c.text({r.left + px(kIconCol), r.top, textRight, r.bottom}, it.label, fg, draw::kMiddle | draw::kEllipsis);
        if (!it.shortcut.empty())
            c.text({r.left + px(kIconCol), r.top, textRight, r.bottom}, Menu::shortcutText(it.shortcut), on ? th.dim : th.faint, draw::kMiddle | draw::kRight);
        if (!it.sub.empty()) {  // a chevron: the submenu opens to the side
            const int cx = r.right - px(13), cy = (r.top + r.bottom) / 2, a = px(4);
            const Point chevron[] = {{cx - a / 2, cy - a}, {cx + a / 2, cy}, {cx - a / 2, cy + a + 1}};
            c.polyline(chevron, 3, fg, std::max(1, px(1)) + 1);
        }
    }
    c.setFont(of);
}

void Panel::step(int dir) {
    const int n = int(items_.size());
    if (!n) return;
    int i = hot_;
    for (int k = 0; k < n; ++k) {
        i = i < 0 ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n;
        if (enabledRow(i)) return setHot(i);
    }
}

// A letter moves to the next enabled item whose label starts with it (it does not run the item).
bool Panel::jump(int letter) {
    if (!isLetter(letter)) return false;
    int pick = -1;
    for (int i = 0; i < int(items_.size()); ++i) {
        if (!enabledRow(i) || !startsWith(items_[size_t(i)].label, letter)) continue;
        if (pick < 0) pick = i;  // the first, when none is after the highlighted item
        if (i > hot_) {
            pick = i;
            break;
        }
    }
    if (pick < 0) return false;
    setHot(pick);
    return true;
}

// The mouse over row `i` (-1: none). A submenu opens after a short wait; another command closes an open submenu.
void Panel::hover(int i) {
    if (!enabledRow(i)) {
        chain_.cancelHover();
        if (open_ < 0) setHot(-1);
        return;
    }
    setHot(i);
    if (!items_[size_t(i)].sub.empty()) {
        if (open_ == i) chain_.cancelHover();
        else chain_.scheduleHover(level_, i);
    } else {
        chain_.cancelHover();
        if (open_ >= 0) chain_.closeFrom(size_t(level_) + 1);
    }
}

void Panel::choose(int i, bool byKeys) {
    if (!enabledRow(i)) return;
    if (!items_[size_t(i)].sub.empty()) return openSub(i, byKeys);
    const std::function<void()> run = items_[size_t(i)].run;  // copied: the chain closes before the item runs
    chain_.closeAll(false);
    if (run) run();
}

void Panel::openSub(int i, bool byKeys) {
    if (open_ == i) return;
    chain_.closeFrom(size_t(level_) + 1);
    const MenuItem& it = items_[size_t(i)];
    const MenuLayout sl = i < int(layout_.sub.size()) ? layout_.sub[size_t(i)] : Menu::measure(it.sub);
    open_ = i;
    invalidate();
    auto sub = std::make_unique<Panel>(chain_, level_ + 1, it.sub, sl);
    Panel* raw = sub.get();
    chain_.owned.push_back(std::move(sub));
    chain_.live.push_back(raw);
    const int top = row(i).top - px(kPad), left = width() - px(3);  // (on the parent's edge, as the system's)
    chain_.host.openWindow(*this, *raw, WindowSpec{WindowKind::Menu, Rect{left, top, left + sl.size.x, top + sl.size.y}, L""}, nullptr);
    if (byKeys) raw->highlightFirst();
    chain_.host.focus(*raw);
}

// Left or Right at the top level: the menu closes, and the owner opens the neighbour.
void Panel::sideways(int dir) {
    const auto fn = chain_.opts.sideways;
    chain_.closeAll(false);
    if (fn) fn(dir);
}

bool Panel::mouse(const MouseEvent& e) {
    switch (e.kind) {
    case MouseKind::Move: hover(rowAt(e.pt)); return true;
    case MouseKind::Leave:
        chain_.cancelHover();
        if (open_ < 0) setHot(-1);
        return true;
    case MouseKind::Down:
    case MouseKind::DoubleClick: {
        if (e.button != 0) return true;
        const int i = rowAt(e.pt);
        armed_ = enabledRow(i) ? i : -1;
        if (armed_ >= 0) {
            setHot(armed_);
            if (!items_[size_t(armed_)].sub.empty()) openSub(armed_, false);
        }
        return true;
    }
    case MouseKind::Up: {
        if (e.button != 0) return true;
        const int down = armed_;
        armed_ = -1;
        if (down >= 0 && rowAt(e.pt) == down && items_[size_t(down)].sub.empty()) choose(down, false);
        return true;
    }
    default: return false;
    }
}

bool Panel::key(const KeyEvent& e) {
    if (!e.down || (e.mods & (draw::kCtrl | draw::kSuper))) return false;
    if (e.mods & draw::kAlt) {  // Alt and a letter: another menu of the owner (the menu bar)
        const auto fn = chain_.opts.letter;
        return fn && isLetter(int(e.key)) && fn(int(e.key));
    }
    switch (e.key) {
    case Key::Down: step(1); return true;
    case Key::Up: step(-1); return true;
    case Key::Home: setHot(-1), step(1); return true;
    case Key::End: setHot(-1), step(-1); return true;
    case Key::Right:
        if (enabledRow(hot_) && !items_[size_t(hot_)].sub.empty()) openSub(hot_, true);
        else if (level_ == 0 && chain_.opts.sideways) sideways(1);
        return true;
    case Key::Left:
        if (level_ > 0) chain_.closeFrom(size_t(level_));
        else if (chain_.opts.sideways) sideways(-1);
        return true;
    case Key::Escape:
        if (level_ > 0) chain_.closeFrom(size_t(level_));
        else chain_.closeAll(false);
        return true;
    case Key::Enter:
    case Key::Space:
        if (hot_ >= 0) choose(hot_, true);
        return true;
    case Key::Tab: chain_.closeAll(false); return true;
    default: return jump(int(e.key));
    }
}

}  // namespace

MenuLayout Menu::measure(const std::vector<MenuItem>& items, Canvas* c) {
    MenuLayout out;
    Font of{};
    if (c) of = c->setFont(c->font(draw::FontDesc{}));
    int textW = 0, keysW = 0, h = 2 * px(kPad);
    bool anySub = false;
    for (const MenuItem& it : items) {
        h += rowHeight(it);
        if (c && !it.separator) textW = std::max(textW, c->measure(it.label)), keysW = std::max(keysW, c->measure(Menu::shortcutText(it.shortcut)));
        anySub = anySub || !it.sub.empty();
        out.sub.push_back(it.sub.empty() ? MenuLayout{} : measure(it.sub, c));
    }
    if (c) c->setFont(of);
    const int w = std::max(px(kMinW), px(kIconCol) + textW + (keysW ? px(kKeysGap) + keysW : 0) + (anySub ? px(kSubW) : 0) + px(kRightPad) + 2 * px(kPad));
    out.size = {w, h};
    return out;
}

void Menu::setTestHook(TestHook hook) { g_testHook = std::move(hook); }
void Menu::setNativeHook(NativeHook hook) { g_nativeHook = std::move(hook); }
void Menu::setShortcutText(ShortcutText f) { g_shortcutText = std::move(f); }
std::wstring Menu::shortcutText(const std::wstring& s) { return g_shortcutText && !s.empty() ? g_shortcutText(s) : s; }

bool Menu::popup(View& from, Point at, const std::vector<MenuItem>& items, const MenuLayout& layout, Options opts) {
    if (g_testHook && g_testHook(items)) {
        if (opts.closed) opts.closed(false);
        return true;
    }
    draw::Host* host = from.host();
    if (!host) return false;
    close();
    if (g_nativeHook) {
        const MenuItem* picked = nullptr;
        if (g_nativeHook(from, at, items, picked)) {
            const std::function<void()> run = picked && picked->enabled ? picked->run : std::function<void()>();
            if (opts.closed) opts.closed(false);
            if (run) run();
            return true;
        }
    }
    auto chain = std::make_unique<Chain>(*host, std::move(opts), &from);
    Chain* c = chain.release();
    g_current = c;
    auto root = std::make_unique<Panel>(*c, 0, items, layout);
    Panel* raw = root.get();
    c->owned.push_back(std::move(root));
    c->live.push_back(raw);
    std::weak_ptr<bool> alive = c->alive;
    host->openWindow(from, *raw, WindowSpec{WindowKind::Menu, Rect{at.x, at.y, at.x + layout.size.x, at.y + layout.size.y}, L""},
                     [alive, c] {
                         if (alive.lock()) c->closeAll(true);
                     });
    if (c->opts.byKeys) raw->highlightFirst();
    host->focus(*raw);
    return true;
}

bool Menu::popup(View& from, Point at, const std::vector<MenuItem>& items, Canvas& c, Options opts) {
    return popup(from, at, items, measure(items, &c), std::move(opts));
}

void Menu::close() {
    if (g_current) g_current->closeAll(false);
}

bool Menu::open() { return g_current != nullptr; }

// --- The menu bar ---------------------------------------------------------------------------------------------------

void MenuBar::setMenus(std::vector<MenuGroup> groups) {
    if (open_ >= 0) Menu::close();
    menus_ = std::move(groups);
    names_.clear();
    layouts_.clear();
    measured_ = false;
    open_ = hot_ = hover_ = -1;
    closedAt_ = -1;
    invalidate();
}

void MenuBar::layout(Canvas& c) {
    names_.clear();
    layouts_.clear();
    const Font of = c.setFont(c.font(draw::FontDesc{}));
    int x = 0;
    for (const MenuGroup& g : menus_) {
        const int w = c.measure(g.label) + 2 * px(kNamePad);
        names_.push_back({x, 0, x + w, height()});
        x += w;
    }
    c.setFont(of);
    for (const MenuGroup& g : menus_) layouts_.push_back(Menu::measure(g.items, &c));
}

int MenuBar::nameAt(Point pt) const {
    for (int i = 0; i < int(names_.size()); ++i)
        if (names_[size_t(i)].contains(pt)) return i;
    return -1;
}

void MenuBar::paint(Canvas& c) {
    const Theme& th = theme();
    c.fill({0, 0, width(), height()}, th.bg);
    // The text is measured once (the menus are long: every label and shortcut), again only at another scale.
    const float scale = host() ? host()->scale() : 1.0f;
    if (!measured_ || scale != measuredScale_) layout(c), measured_ = true, measuredScale_ = scale;
    const Font of = c.setFont(c.font(draw::FontDesc{}));
    for (int i = 0; i < int(names_.size()); ++i) {
        const Rect r = names_[size_t(i)];
        const bool isOpen = i == open_;
        if (isOpen) c.roundBox(r, th.sel, th.accentLine, px(kRadius));
        else if (i == hover_ || (i == hot_ && hasFocus())) c.roundBox(r, th.ctlHot, draw::kNone, px(kRadius));
        c.text(r, menus_[size_t(i)].label, isOpen ? th.accent : th.text, draw::kMiddle | draw::kCenter);
        if (i == hot_ && keys_ && hasFocus() && !isOpen) c.roundBox(r, draw::kNone, th.accent, px(kRadius), std::max(1, int(px(2))));  // the keyboard's focus
    }
    c.setFont(of);
}

// Tab came to the bar: the focus is on its first menu's name (Left and Right go to the others, Enter opens it).
void MenuBar::tabbedTo(bool) {
    if (menus_.empty()) return;
    hot_ = 0, keys_ = true;
    invalidate();
}

std::wstring MenuBar::focusedItem() const {
    return hot_ >= 0 && hot_ < int(menus_.size()) && hasFocus() ? menus_[size_t(hot_)].label : std::wstring();
}

void MenuBar::openMenu(int i, bool byKeys) {
    const int n = int(menus_.size());
    if (i < 0 || i >= n) return;
    std::weak_ptr<bool> alive = alive_;
    Menu::Options o;
    o.closed = [alive, this, i](bool outside) {
        if (!alive.lock()) return;
        open_ = -1, closedAt_ = outside ? i : -1;
        invalidate();
        if (!outside) focus();  // the keys are the bar's again
    };
    o.sideways = [alive, this, i](int dir) {
        if (!alive.lock()) return;
        const int count = int(menus_.size());
        openMenu((i + dir + count) % count, true);
    };
    o.letter = [alive, this](int letter) { return alive.lock() && openByLetter(letter); };
    o.focusFrom = false;  // the bar gives the keys back itself (in closed); the bar may be gone before the menus close
    const Point at{i < int(names_.size()) ? names_[size_t(i)].left : 0, height()};
    const MenuLayout layout = i < int(layouts_.size()) ? layouts_[size_t(i)] : Menu::measure(menus_[size_t(i)].items);
    o.byKeys = byKeys;
    if (!Menu::popup(*this, at, menus_[size_t(i)].items, layout, std::move(o))) return;
    open_ = hot_ = i;
    closedAt_ = -1;
    invalidate();
}

bool MenuBar::openByLetter(int letter) {
    for (int i = 0; i < int(menus_.size()); ++i)
        if (startsWith(menus_[size_t(i)].label, letter)) {
            openMenu(i, true);
            return true;
        }
    return false;
}

bool MenuBar::mouse(const draw::MouseEvent& e) {
    switch (e.kind) {
    case MouseKind::Move: {
        closedAt_ = -1;
        const int i = nameAt(e.pt);
        if (i != hover_) hover_ = i, invalidate();
        if (open_ >= 0 && i >= 0 && i != open_) openMenu(i, false);  // (a menu is open: the one under the mouse opens)
        return true;
    }
    case MouseKind::Leave:
        if (hover_ >= 0) hover_ = -1, invalidate();
        return true;
    case MouseKind::Down: {
        if (e.button != 0) return false;
        keys_ = false;
        const int i = nameAt(e.pt);
        if (i < 0) return true;
        const bool closedHere = i == closedAt_;  // the click that closed this menu does not open it again
        closedAt_ = -1;
        if (closedHere) return true;
        if (open_ == i) {
            Menu::close();
            return true;
        }
        openMenu(i, false);
        return true;
    }
    case MouseKind::Up:
    case MouseKind::DoubleClick: return nameAt(e.pt) >= 0;
    default: return false;
    }
}

bool MenuBar::key(const KeyEvent& e) {
    if (!e.down || menus_.empty()) return false;
    const int n = int(menus_.size());
    if (e.mods & draw::kAlt) return isLetter(int(e.key)) && openByLetter(int(e.key));  // the menu whose name starts with it
    if (e.mods & (draw::kCtrl | draw::kSuper)) return false;
    if (e.key == Key::Left || e.key == Key::Right) keys_ = true;
    switch (e.key) {
    case Key::Right:
        hot_ = hot_ < 0 ? 0 : (hot_ + 1) % n;
        invalidate();
        return true;
    case Key::Left:
        hot_ = hot_ < 0 ? n - 1 : (hot_ + n - 1) % n;
        invalidate();
        return true;
    case Key::Down:
    case Key::Enter:
    case Key::Space:
        openMenu(hot_ < 0 ? 0 : hot_, true);
        return true;
    default: return false;
    }
}

void MenuBar::focusChanged(bool has) {
    if (!has && hot_ >= 0) hot_ = -1;
    invalidate();
}

MenuBar::~MenuBar() {
    alive_.reset();  // the open menu's callbacks do nothing from now on
    if (open_ >= 0) Menu::close();
    if (draw::Host* h = host()) h->forget(*this);  // the keys were the menus' while they were open: none is left here
}

}  // namespace stardraft::kit
