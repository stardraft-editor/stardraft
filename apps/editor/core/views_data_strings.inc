// The Data workspace's Strings page: the model (portable, core.inc). A string is stat_txt.tbl's text with the game's control
// bytes; a button's text starts with two bytes (its hotkey, then its type). The page's paint and its text editor stay the
// program's; the rules of the text (what a list shows, what the editor holds, what a commit writes back, the button
// parts and the way a button's prefix is rewritten) are here, so both programs use the same ones.

// A string's text starts with a button prefix: a hotkey (a printable byte) and a type byte (0-5).
bool buttonPrefix(const std::string& s) { return s.size() >= 2 && static_cast<unsigned char>(s[1]) <= 5 && static_cast<unsigned char>(s[0]) >= 0x20; }

// A string's text for a list: without its button prefix, and with its control bytes left out (a line).
std::wstring stringLine(const std::string& raw) {
    std::string o;
    for (size_t i = buttonPrefix(raw) ? 2 : 0; i < raw.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c == '\n' || c == '\r') o += ' ';
        else if (c >= 0x20) o += char(c);
    }
    return widen(o);
}

// The editor holds the text without a button's hotkey and type characters (the fields show them).
std::string stringEditBody(const std::string& raw) { return buttonPrefix(raw) ? raw.substr(2) : raw; }
std::string stringEditPrefix(const std::string& raw) { return buttonPrefix(raw) ? raw.substr(0, 2) : ""; }

// What a commit writes back: the prefix the string had, then the editor's text.
std::string stringEditCommit(const std::string& raw, const std::string& editorText) { return stringEditPrefix(raw) + editorText; }

// A button's tooltip in the game text pop-out (the Buttons tab): the text without the hotkey and type, with "\r\n" as
// "\n"; and what the pop-out's text writes back (the prefix, and the string's own line ends).
std::string tooltipEditText(const std::string& raw) {
    std::string body = stringEditBody(raw);
    for (size_t at; (at = body.find("\r\n")) != std::string::npos;) body.erase(at, 1);
    return body;
}
std::string tooltipEditCommit(const std::string& raw, const std::string& edited) {
    if (stringEditBody(raw).find("\r\n") == std::string::npos) return stringEditPrefix(raw) + edited;
    std::string o;
    for (char ch : edited) ch == '\n' ? o += "\r\n" : o += ch;
    return stringEditPrefix(raw) + o;
}

// A button's hotkey and type as the page's fields show them. `has`: the string starts with a button prefix.
struct StringButtonParts {
    bool has = false;
    char hot = 0;   // the hotkey byte (0 when none)
    int type = 0;   // the button type byte (0-5; 0 when none)
};
StringButtonParts stringButtonParts(const std::string& raw) {
    StringButtonParts p;
    p.has = buttonPrefix(raw);
    if (p.has) p.hot = raw[0], p.type = raw[1];
    return p;
}

// The hotkey's field text: None, Esc, Space, or the key in capitals.
std::wstring stringHotkeyText(const StringButtonParts& p) {
    if (!p.has) return L"None";
    if (p.hot == 27) return L"Esc";
    if (p.hot == ' ') return L"Space";
    return std::wstring(1, wchar_t(std::toupper(static_cast<unsigned char>(p.hot))));
}

// The button types, in the order of the type byte.
const wchar_t* stringButtonTypeName(int type) {
    static const wchar_t* types[] = {L"Command", L"Make a unit", L"Upgrade", L"Use a technology", L"Research a technology", L"Morph a unit"};
    return type >= 0 && type < 6 ? types[type] : L"None";
}

// The string with a new hotkey and type: the text's body stays, the prefix is replaced. A string with no prefix gets
// one (its hotkey and type given); the caller stores the result.
std::string stringWithButtonPrefix(const std::string& raw, char hot, int type) {
    const std::string body = buttonPrefix(raw) ? raw.substr(2) : raw;
    return std::string(1, hot) + char(type) + body;
}
