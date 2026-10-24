// The program's settings: a file (stardraft::Settings, the file shellSettingsFile() gives: %APPDATA%\StarDraft\settings.json
// on Windows) that every system can have. The Windows host reads the registry (HKCU\Software\StarDraft, where builds
// before the file kept them) for a value the file does not have yet, and does not write it any more: an older build
// still finds its settings there (shellLegacyValue, shell_win32.inc).
//
// A name is "Value" (the key Software\StarDraft) or "Key/Value" (the subkey Key).

namespace prefs {

stardraft::Settings& store() {
    static stardraft::Settings s(shellSettingsFile());
    return s;
}

// The legacy key (the subkey, without the "Software\StarDraft" part) and value of a name.
std::pair<std::wstring, std::wstring> legacyName(const std::wstring& name) {
    const size_t slash = name.rfind(L'/');
    if (slash == std::wstring::npos) return {L"", name};
    return {name.substr(0, slash), name.substr(slash + 1)};
}

bool legacyValue(const std::wstring& name, LegacyKind kind, std::vector<uint8_t>& out) {
    const auto [sub, value] = legacyName(name);
    return shellLegacyValue(sub, value, kind, out);
}

bool number(const std::wstring& name, int64_t& v) {
    if (const auto n = store().number(narrow(name))) return v = *n, true;
    std::vector<uint8_t> b;
    if (!legacyValue(name, LegacyKind::Dword, b) || b.size() < sizeof(uint32_t)) return false;
    uint32_t d = 0;
    std::memcpy(&d, b.data(), sizeof d);
    v = d;
    return true;
}
// A number in [lo, hi] (clamped), or false when there is none.
bool number(const std::wstring& name, int& v, int lo, int hi) {
    int64_t n = 0;
    if (!number(name, n)) return false;
    v = int(std::clamp<int64_t>(n, lo, hi));
    return true;
}
bool flag(const std::wstring& name, bool& v) {
    int64_t n = 0;
    if (!number(name, n)) return false;
    v = n != 0;
    return true;
}

bool text(const std::wstring& name, std::wstring& v) {
    if (const auto t = store().text(narrow(name))) return v = widen(*t), true;
    std::vector<uint8_t> b;
    if (!legacyValue(name, LegacyKind::Text, b)) return false;
    v.assign(reinterpret_cast<const wchar_t*>(b.data()), b.size() / sizeof(wchar_t));
    v.resize(std::find(v.begin(), v.end(), L'\0') - v.begin());  // (up to the first NUL)
    return true;
}

bool list(const std::wstring& name, std::vector<std::wstring>& v) {
    if (const auto l = store().list(narrow(name))) {
        v.clear();
        for (const auto& s : *l) v.push_back(widen(s));
        return true;
    }
    std::vector<uint8_t> b;
    if (!legacyValue(name, LegacyKind::Texts, b)) return false;
    v.clear();
    std::wstring all(reinterpret_cast<const wchar_t*>(b.data()), b.size() / sizeof(wchar_t));
    all.push_back(L'\0');
    for (const wchar_t* q = all.c_str(); *q; q += wcslen(q) + 1) v.emplace_back(q);
    return true;
}

// Bytes are kept in the file as hexadecimal text.
bool bytes(const std::wstring& name, std::vector<uint8_t>& v) {
    if (const auto t = store().text(narrow(name))) {
        v.clear();
        for (size_t i = 0; i + 1 < t->size(); i += 2) {  // (a pair that is not hexadecimal ends the bytes)
            unsigned b = 0;
            if (std::from_chars(t->data() + i, t->data() + i + 2, b, 16).ptr != t->data() + i + 2) break;
            v.push_back(uint8_t(b));
        }
        return true;
    }
    return legacyValue(name, LegacyKind::Bytes, v);
}

// The names under "Key/" (without it): the file's and the legacy key's values (each once).
std::vector<std::wstring> names(const std::wstring& key) {
    std::vector<std::wstring> out;
    const std::string prefix = narrow(key) + "/";
    for (const auto& n : store().names(prefix)) out.push_back(widen(n.substr(prefix.size())));
    shellLegacyNames(key, out);
    return out;
}

void setNumber(const std::wstring& name, int64_t v) { store().set(narrow(name), v); }
void setText(const std::wstring& name, const std::wstring& v) { store().set(narrow(name), narrow(v)); }
void setList(const std::wstring& name, const std::vector<std::wstring>& v) {
    std::vector<std::string> l;
    for (const auto& s : v) l.push_back(narrow(s));
    store().set(narrow(name), std::move(l));
}
void setBytes(const std::wstring& name, const std::vector<uint8_t>& v) {
    static const char* kHex = "0123456789abcdef";
    std::string t;
    for (uint8_t b : v) t += kHex[b >> 4], t += kHex[b & 15];
    store().set(narrow(name), t);
}
// Gone from the file and from the legacy registry (or the registry's value would come back).
void remove(const std::wstring& name) {
    store().remove(narrow(name));
    const auto [sub, value] = legacyName(name);
    shellLegacyRemove(sub, value);
}

}  // namespace prefs

// Program options > Simulation: Tools > Simulate shows (experimental; off by default).
inline bool simulationEnabled() {
    int on = 0;
    prefs::number(L"SimEnabled", on, 0, 1);
    return on != 0;
}
