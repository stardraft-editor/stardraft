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

// widenLegacy: a map's old text (UTF-8, else Korean CP949, else Windows-1252). On Windows each 2-byte CP949 code is
// checked against code page 949.
KIT_TEST(text_codec_legacy) {
    using stardraft::text::widenLegacy;
    CHECK(widenLegacy("") == L"");
    CHECK(widenLegacy("Terran Marine") == L"Terran Marine");
    CHECK(widenLegacy("\xED\x95\x9C\xEA\xB5\xAD") == L"한국");               // UTF-8 stays UTF-8
    CHECK(widenLegacy("\xC7\xD1\xB1\xB9\xBE\xEE") == L"한국어");              // CP949 (KS X 1001)
    CHECK(widenLegacy("\xC7\xD1\xB1\xB9 \x8C\x63 Map") == L"한국 똠 Map");    // with a UHC syllable (8C63) and ASCII
    CHECK(widenLegacy("\x01\xBF\xEC\xB8\xAE\x04 \xB1\xBA\xB4\xEB") == L"\x01우리\x04 군대");  // with the game's color codes
    CHECK(widenLegacy("caf\xE9") == L"café");                                 // Windows-1252: é at the end is no CP949
    CHECK(widenLegacy("\xC4pfel") == L"Äpfel");                                // a valid UHC code, but no common syllable
    CHECK(widenLegacy("\xE4\xE4") == L"ää");                                   // a valid Hanja code, but no Hangul
    CHECK(widenLegacy("\x80\x81\x9F") == std::wstring(L"€\u0081Ÿ"));
    CHECK(stardraft::text::cp949(0xC6, 0x52) == U'힣');  // the last UHC syllable
    CHECK(stardraft::text::cp949(0xC6, 0x53) == 0);
    CHECK(stardraft::text::cp949(0xB0, 0xA1) == U'가');
    CHECK(stardraft::text::cp949(0x81, 0x41) == U'갂');
    CHECK(stardraft::text::cp949(0xA1, 0xD8) == U'※');
    CHECK(stardraft::text::cp949(0xF9, 0xD3) == U'漢');
    CHECK(stardraft::text::cp949(0xC9, 0xA1) == 0xE000);  // the user-defined rows
    CHECK(stardraft::text::cp949(0x80, 0x41) == 0);
    int mapped = 0;
    for (int l = 0x81; l <= 0xFE; ++l)
        for (int t = 0x41; t <= 0xFE; ++t) mapped += stardraft::text::cp949((unsigned char)l, (unsigned char)t) ? 1 : 0;
    CHECK(mapped == 17048 + 188);  // the codec's codes and the two user-defined rows
#ifdef _WIN32
    int diff = 0;
    for (int l = 0x81; l <= 0xFE; ++l)
        for (int t = 0x41; t <= 0xFE; ++t) {
            const char b[2] = {char(l), char(t)};
            wchar_t w[4]{};
            const int n = MultiByteToWideChar(949, MB_ERR_INVALID_CHARS, b, 2, w, 4);
            const char32_t ours = stardraft::text::cp949((unsigned char)l, (unsigned char)t);
            if (ours ? (n != 1 || char32_t(w[0]) != ours) : n == 1) ++diff;
        }
    CHECK(diff == 0);
#endif
}
