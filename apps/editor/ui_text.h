// The program's text in other languages (GitHub feedback #104). The English text is the key: tr() returns the
// translation for the language in use, or the text itself when there is none (English, a text that is not in the
// tables, a file name, a map's name). The tables are res/lang/ko.cpp and res/lang/es.cpp; tools/ui_lang_gen.py makes
// them from res/lang/ko.tsv and es.tsv (English<TAB>translation, with backslash escapes).
//
// The text is translated where it reaches the screen, so the call sites keep the English. The macros below replace
// the painting, measuring, window, menu and message calls with wrappers that translate their text: DrawTextW,
// TextOutW, ExtTextOutW and GetTextExtentPoint32W (the text that is drawn and the text that is measured),
// SetWindowTextW and CreateWindowExW (the caption), AppendMenuW and InsertMenuW (menu items), MessageBoxW, and
// SendMessageW for WM_SETTEXT and the tooltip controls. Lists (LB_ADDSTRING, CB_ADDSTRING) take an item's data as
// well as its text, so their call sites use tr() themselves. A call that draws a part of a text (a substring) must
// translate the whole text first.
//
// The macros are defined after the system headers, in main.cpp, so the wrappers below call the system functions
// through (Name)(...), which the macro does not expand.
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <string_view>
#include <unordered_map>

enum class UiLang { English, Korean, Spanish, Russian, Chinese };  // Chinese: simplified
inline UiLang gUiLang = UiLang::English;  // the language in use (set at the start, see uiLanguageChosen)

struct UiText {
    const wchar_t* en;
    const wchar_t* text;
};
#include <span>
std::span<const UiText> langTableKo();  // res/lang/ko.cpp: the Korean translations (generated)
std::span<const UiText> langTableEs();  // res/lang/es.cpp: the Spanish translations (generated)
std::span<const UiText> langTableRu();  // res/lang/ru.cpp: the Russian translations (generated)
std::span<const UiText> langTableZh();  // res/lang/zh.cpp: the Chinese (simplified) translations (generated)

inline const std::unordered_map<std::wstring_view, const wchar_t*>& uiTable(UiLang lang) {
    auto make = [](std::span<const UiText> rows) {
        std::unordered_map<std::wstring_view, const wchar_t*> m;
        for (const UiText& e : rows) m.emplace(e.en, e.text);
        return m;
    };
    static const auto ko = make(langTableKo()), es = make(langTableEs()), ru = make(langTableRu()), zh = make(langTableZh());
    return lang == UiLang::Korean ? ko : lang == UiLang::Spanish ? es : lang == UiLang::Russian ? ru : zh;
}

// The translation of an English text (a pointer into the tables, or the text itself: it stays valid as long as it was).
inline const wchar_t* tr(const wchar_t* english) {
    if (!english || gUiLang == UiLang::English) return english;
    const auto& m = uiTable(gUiLang);
    const auto it = m.find(std::wstring_view(english));
    return it == m.end() ? english : it->second;
}

inline std::wstring tr(const std::wstring& english) {
    return gUiLang == UiLang::English ? english : std::wstring(tr(english.c_str()));
}

// The language to use: STARDRAFT_LANG (en, ko or es, for test runs) wins, then the setting (auto, en, ko or es),
// and automatic is the language of Windows' user interface.
inline UiLang uiLanguageChosen(const std::wstring& setting) {
    wchar_t env[16] = L"";
    std::wstring code = setting;
    if (GetEnvironmentVariableW(L"STARDRAFT_LANG", env, 16)) code = env;
    if (code == L"ko") return UiLang::Korean;
    if (code == L"es") return UiLang::Spanish;
    if (code == L"ru") return UiLang::Russian;
    if (code == L"zh") return UiLang::Chinese;
    if (code == L"en") return UiLang::English;
    const LANGID ui = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(ui) == LANG_CHINESE)  // simplified (China, Singapore); traditional Chinese Windows: English
        return SUBLANGID(ui) == SUBLANG_CHINESE_SIMPLIFIED || SUBLANGID(ui) == SUBLANG_CHINESE_SINGAPORE ? UiLang::Chinese : UiLang::English;
    switch (PRIMARYLANGID(ui)) {
    case LANG_KOREAN: return UiLang::Korean;
    case LANG_SPANISH: return UiLang::Spanish;
    case LANG_RUSSIAN: return UiLang::Russian;
    default: return UiLang::English;
    }
}

// --- The wrappers (see the comment at the top) -----------------------------------------------------------------

inline int uiDrawText(HDC dc, const wchar_t* text, int count, RECT* rc, UINT fmt) {
    if (gUiLang == UiLang::English || !text) return (DrawTextW)(dc, text, count, rc, fmt);
    if (count < 0) return (DrawTextW)(dc, tr(text), -1, rc, fmt);
    const std::wstring shown = tr(std::wstring(text, size_t(count)));
    return (DrawTextW)(dc, shown.c_str(), int(shown.size()), rc, fmt);
}

inline BOOL uiTextOut(HDC dc, int x, int y, const wchar_t* text, int count) {
    if (gUiLang == UiLang::English || !text) return (TextOutW)(dc, x, y, text, count);
    const std::wstring shown = count < 0 ? tr(std::wstring(text)) : tr(std::wstring(text, size_t(count)));
    return (TextOutW)(dc, x, y, shown.c_str(), int(shown.size()));
}

// A call with a dx array keeps its text: the widths belong to the English characters.
inline BOOL uiExtTextOut(HDC dc, int x, int y, UINT opt, const RECT* rc, const wchar_t* text, UINT count, const INT* dx) {
    if (gUiLang == UiLang::English || !text || dx) return (ExtTextOutW)(dc, x, y, opt, rc, text, count, dx);
    const std::wstring shown = tr(std::wstring(text, count));
    return (ExtTextOutW)(dc, x, y, opt, rc, shown.c_str(), UINT(shown.size()), dx);
}

inline BOOL uiTextExtent(HDC dc, const wchar_t* text, int count, LPSIZE sz) {
    if (gUiLang == UiLang::English || !text) return (GetTextExtentPoint32W)(dc, text, count, sz);
    const std::wstring shown = tr(std::wstring(text, size_t(count)));
    return (GetTextExtentPoint32W)(dc, shown.c_str(), int(shown.size()), sz);
}

// A text box keeps its text as it is: it holds the user's data (a force name, a map string), which must not be
// translated and then saved translated. Class names: Edit, RichEdit, and a combo box that can be typed in.
inline bool uiTextBoxClass(const wchar_t* cls, DWORD style) {
    if (!cls || IS_INTRESOURCE(cls)) return false;
    return _wcsicmp(cls, L"Edit") == 0 || _wcsnicmp(cls, L"RichEdit", 8) == 0 ||
           (_wcsicmp(cls, L"ComboBox") == 0 && (style & 3) == CBS_DROPDOWN);
}
inline bool uiTextBox(HWND hwnd) {
    wchar_t cls[32] = L"";
    return hwnd && GetClassNameW(hwnd, cls, 32) && uiTextBoxClass(cls, DWORD(GetWindowLongPtrW(hwnd, GWL_STYLE)));
}

inline BOOL uiSetWindowText(HWND hwnd, const wchar_t* text) {
    return (SetWindowTextW)(hwnd, uiTextBox(hwnd) ? text : tr(text));
}

inline HWND uiCreateWindow(DWORD ex, const wchar_t* cls, const wchar_t* name, DWORD style, int x, int y, int w, int h, HWND parent,
                           HMENU menu, HINSTANCE inst, void* param) {
    return (CreateWindowExW)(ex, cls, uiTextBoxClass(cls, style) ? name : tr(name), style, x, y, w, h, parent, menu, inst, param);
}

inline int uiMessageBox(HWND owner, const wchar_t* text, const wchar_t* caption, UINT flags) {
    return (MessageBoxW)(owner, tr(text), tr(caption), flags);
}

// A menu item with a text (not a bitmap, not an owner-drawn or a separator item).
inline const wchar_t* uiMenuText(UINT flags, const wchar_t* text) {
    return (flags & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR)) || !text ? text : tr(text);
}

inline BOOL uiAppendMenu(HMENU menu, UINT flags, UINT_PTR id, const wchar_t* text) {
    return (AppendMenuW)(menu, flags, id, uiMenuText(flags, text));
}

inline BOOL uiInsertMenu(HMENU menu, UINT pos, UINT flags, UINT_PTR id, const wchar_t* text) {
    return (InsertMenuW)(menu, pos, flags, id, uiMenuText(flags, text));
}

inline LRESULT uiSendMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (gUiLang != UiLang::English && lp) {
        if (msg == WM_SETTEXT && uiTextBox(hwnd)) {
            // a text box: its text stays (see uiTextBox)
        } else if (msg == WM_SETTEXT || msg == CB_ADDSTRING || msg == CB_INSERTSTRING) {  // (combo boxes: the item's text, not data)
            lp = LPARAM(tr(reinterpret_cast<const wchar_t*>(lp)));
        } else if (msg == TTM_ADDTOOLW || msg == TTM_UPDATETIPTEXTW) {  // the tooltip control copies the text
            auto* ti = reinterpret_cast<TTTOOLINFOW*>(lp);
            if (ti->lpszText && ti->lpszText != LPSTR_TEXTCALLBACKW && !IS_INTRESOURCE(ti->lpszText))
                ti->lpszText = const_cast<wchar_t*>(tr(ti->lpszText));
        }
    }
    return (SendMessageW)(hwnd, msg, wp, lp);
}

// The macros for main.cpp (after its system headers, before the code that uses them).
#define DrawTextW(dc, text, count, rc, fmt) uiDrawText(dc, text, count, rc, fmt)
#define TextOutW(dc, x, y, text, count) uiTextOut(dc, x, y, text, count)
#define ExtTextOutW(dc, x, y, opt, rc, text, count, dx) uiExtTextOut(dc, x, y, opt, rc, text, count, dx)
#define GetTextExtentPoint32W(dc, text, count, sz) uiTextExtent(dc, text, count, sz)
#define SetWindowTextW(hwnd, text) uiSetWindowText(hwnd, text)
#define CreateWindowExW(ex, cls, name, style, x, y, w, h, parent, menu, inst, param) \
    uiCreateWindow(ex, cls, name, style, x, y, w, h, parent, menu, inst, param)
#define MessageBoxW(owner, text, caption, flags) uiMessageBox(owner, text, caption, flags)
#define AppendMenuW(menu, flags, id, text) uiAppendMenu(menu, flags, id, text)
#define InsertMenuW(menu, pos, flags, id, text) uiInsertMenu(menu, pos, flags, id, text)
#define SendMessageW(hwnd, msg, wp, lp) uiSendMessage(hwnd, msg, wp, lp)
