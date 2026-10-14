// The program's settings: a file (stardraft::Settings, %APPDATA%\StarDraft\settings.json) that every system can have.
// The registry (HKCU\Software\StarDraft, where builds before the file kept them) is read for a value the file does not
// have yet, and is not written any more: an older build still finds its settings there.
//
// The environment variable STARDRAFT_SETTINGS gives another file.
//
// A name is "Value" (HKCU\Software\StarDraft) or "Key/Value" (HKCU\Software\StarDraft\Key).

namespace prefs {

stardraft::Settings& store() {
    static stardraft::Settings s = [] {
        if (const wchar_t* file = _wgetenv(L"STARDRAFT_SETTINGS"); file && *file) return stardraft::Settings(file);  // (tests)
        fs::path dir;
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) dir = p;
        CoTaskMemFree(p);
        if (dir.empty()) dir = fs::temp_directory_path();
        return stardraft::Settings(dir / L"StarDraft" / L"settings.json");
    }();
    return s;
}

// The registry key and value of a name.
std::pair<std::wstring, std::wstring> regName(const std::wstring& name) {
    const size_t slash = name.rfind(L'/');
    if (slash == std::wstring::npos) return {L"Software\\StarDraft", name};
    std::wstring key = L"Software\\StarDraft\\" + name.substr(0, slash);
    std::replace(key.begin(), key.end(), L'/', L'\\');
    return {key, name.substr(slash + 1)};
}

bool regValue(const std::wstring& name, DWORD flags, std::vector<BYTE>& out, DWORD* type = nullptr) {
    const auto [key, value] = regName(name);
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value.c_str(), flags, type, nullptr, &size) != ERROR_SUCCESS) return false;
    out.assign(size, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value.c_str(), flags, type, out.data(), &size) != ERROR_SUCCESS) return false;
    out.resize(size);
    return true;
}

bool number(const std::wstring& name, int64_t& v) {
    if (const auto n = store().number(narrow(name))) return v = *n, true;
    std::vector<BYTE> b;
    if (!regValue(name, RRF_RT_REG_DWORD, b) || b.size() < sizeof(DWORD)) return false;
    DWORD d = 0;
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
    std::vector<BYTE> b;
    if (!regValue(name, RRF_RT_REG_SZ, b)) return false;
    v.assign(reinterpret_cast<const wchar_t*>(b.data()), b.size() / sizeof(wchar_t));
    v.resize(wcsnlen(v.c_str(), v.size()));
    return true;
}

bool list(const std::wstring& name, std::vector<std::wstring>& v) {
    if (const auto l = store().list(narrow(name))) {
        v.clear();
        for (const auto& s : *l) v.push_back(widen(s));
        return true;
    }
    std::vector<BYTE> b;
    if (!regValue(name, RRF_RT_REG_MULTI_SZ, b)) return false;
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
    std::vector<BYTE> b;
    if (!regValue(name, RRF_RT_REG_BINARY, b)) return false;
    v.assign(b.begin(), b.end());
    return true;
}

// The names under "Key/" (without it): the file's and the registry key's values (each once).
std::vector<std::wstring> names(const std::wstring& key) {
    std::vector<std::wstring> out;
    const std::string prefix = narrow(key) + "/";
    for (const auto& n : store().names(prefix)) out.push_back(widen(n.substr(prefix.size())));
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, (L"Software\\StarDraft\\" + key).c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return out;
    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        DWORD len = 256;
        if (RegEnumValueW(k, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        if (std::find(out.begin(), out.end(), name) == out.end()) out.emplace_back(name);
    }
    RegCloseKey(k);
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
// Gone from the file and from the registry (or the registry's value would come back).
void remove(const std::wstring& name) {
    store().remove(narrow(name));
    const auto [key, value] = regName(name);
    RegDeleteKeyValueW(HKEY_CURRENT_USER, key.c_str(), value.c_str());
}

}  // namespace prefs
