// apps/common/text_codec.h: the editor's widen/narrow on every system. On Windows each result is checked against what
// Win32 gives (MultiByteToWideChar with UTF-8, then Windows-1252; WideCharToMultiByte to UTF-8), for fixed cases and
// random bytes; elsewhere against the fixed cases.
#include "text_codec.h"

#include <random>

#include "kit_check.h"
#include "wintypes.h"

using stardraft::text::narrow;
using stardraft::text::widen;

#ifdef _WIN32
static std::wstring win32Widen(const std::string& s) {  // (main.cpp's widen before the codec)
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    UINT cp = n > 0 ? CP_UTF8 : 1252;
    n = MultiByteToWideChar(cp, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(cp, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
static std::string win32Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
#endif

KIT_TEST(text_codec) {
    CHECK(widen("") == L"");
    CHECK(widen("Terran Marine") == L"Terran Marine");
    CHECK(widen("\xED\x95\x9C\xEA\xB5\xAD") == L"한국");             // 한국 in UTF-8
    CHECK(widen("caf\xE9") == L"café");                                 // é in Windows-1252 (not UTF-8)
    CHECK(widen("\x80\x81\x9F") == std::wstring(L"€\u0081Ÿ"));     // the 0x80-0x9F range
    CHECK(widen("\xC0\x80") == L"À€");                             // overlong: not UTF-8
    CHECK(widen("\xED\xA0\x80") == L"í €");                   // a surrogate: not UTF-8
    CHECK(narrow(L"한국") == "\xED\x95\x9C\xEA\xB5\xAD");
    CHECK(narrow(L"aé") == "a\xC3\xA9");
    CHECK(widen(narrow(L"\U0001F600")) == L"\U0001F600");  // outside the BMP, both ways
#ifdef _WIN32
    const char* fixed[] = {"", "abc", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xF4\x8F\xBF\xBF", "\xF4\x90\x80\x80", "\xE0\x80\xAF", "\xC3", "a\xC3\xA9z",
                           "\x81\x8D\x8F\x90\x9D", "\xFF\xFE"};
    for (const char* f : fixed) CHECK(widen(f) == win32Widen(f));
    std::mt19937 rng(1234);
    int widenDiff = 0, narrowDiff = 0;
    for (int k = 0; k < 20000; ++k) {
        std::string s(size_t(rng() % 12), '\0');
        for (char& c : s) c = char(rng() % 4 == 0 ? rng() % 0x80 : 0x80 + rng() % 0x80);
        if (widen(s) != win32Widen(s)) ++widenDiff;
        std::wstring w(size_t(rng() % 8), L'\0');
        for (wchar_t& c : w) c = wchar_t(rng() % 3 == 0 ? 0xD800 + rng() % 0x800 : 1 + rng() % 0xFFFF);
        if (narrow(w) != win32Narrow(w)) ++narrowDiff;
    }
    CHECK(widenDiff == 0);
    CHECK(narrowDiff == 0);
#endif
}
