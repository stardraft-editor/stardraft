#pragma once
// Text between the library's UTF-8 and the UI's wide strings, the same on every system: as Win32's
// MultiByteToWideChar / WideCharToMultiByte did it for the editor (kit_test checks this against Win32 on Windows).
// A wide string is UTF-16 where wchar_t has 16 bits (Windows) and UTF-32 where it has 32 (macOS, Linux).

#include <cstdint>
#include <string>
#include <string_view>

namespace stardraft::text {

// The code point of each byte in Windows-1252 (map strings are often in it); 0x81, 0x8D, 0x8F, 0x90 and 0x9D, which it
// leaves out, stay as they are (as Windows maps them).
inline char32_t cp1252(unsigned char b) {
    static const char16_t high[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                      0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                      0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
    return b >= 0x80 && b < 0xA0 ? char32_t(high[b - 0x80]) : char32_t(b);
}

inline void appendWide(std::wstring& out, char32_t c) {
    if constexpr (sizeof(wchar_t) == 2) {
        if (c >= 0x10000) {
            c -= 0x10000;
            out.push_back(wchar_t(0xD800 + (c >> 10)));
            out.push_back(wchar_t(0xDC00 + (c & 0x3FF)));
            return;
        }
    }
    out.push_back(wchar_t(c));
}

// Strict UTF-8 (no overlong forms, no surrogates, nothing above U+10FFFF): the code points, or false.
inline bool decodeUtf8(std::string_view s, std::wstring& out) {
    out.clear();
    for (size_t i = 0; i < s.size();) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            out.push_back(wchar_t(b)), ++i;
            continue;
        }
        int n = b >= 0xF0 && b <= 0xF4 ? 3 : b >= 0xE0 ? (b <= 0xEF ? 2 : -1) : b >= 0xC2 && b <= 0xDF ? 1 : -1;
        if (n < 0 || i + size_t(n) >= s.size()) return false;  // not a lead byte, or the sequence is cut off
        char32_t c = b & (n == 1 ? 0x1F : n == 2 ? 0x0F : 0x07);
        for (int k = 1; k <= n; ++k) {
            const unsigned char t = static_cast<unsigned char>(s[i + size_t(k)]);
            if ((t & 0xC0) != 0x80) return false;
            c = c << 6 | (t & 0x3F);
        }
        if ((n == 2 && c < 0x800) || (n == 3 && (c < 0x10000 || c > 0x10FFFF)) || (c >= 0xD800 && c <= 0xDFFF)) return false;
        appendWide(out, c);
        i += size_t(n) + 1;
    }
    return true;
}

// UTF-8 when the bytes are valid UTF-8, else Windows-1252.
inline std::wstring widen(std::string_view s) {
    std::wstring w;
    if (decodeUtf8(s, w)) return w;
    w.clear();
    for (char ch : s) appendWide(w, cp1252(static_cast<unsigned char>(ch)));
    return w;
}

// UTF-8; a lone surrogate (UTF-16) becomes U+FFFD.
inline std::string narrow(std::wstring_view w) {
    std::string out;
    auto put = [&](char32_t c) {
        if (c < 0x80) out.push_back(char(c));
        else if (c < 0x800) out.push_back(char(0xC0 | (c >> 6))), out.push_back(char(0x80 | (c & 0x3F)));
        else if (c < 0x10000) out.push_back(char(0xE0 | (c >> 12))), out.push_back(char(0x80 | ((c >> 6) & 0x3F))), out.push_back(char(0x80 | (c & 0x3F)));
        else
            out.push_back(char(0xF0 | (c >> 18))), out.push_back(char(0x80 | ((c >> 12) & 0x3F))), out.push_back(char(0x80 | ((c >> 6) & 0x3F))),
                out.push_back(char(0x80 | (c & 0x3F)));
    };
    for (size_t i = 0; i < w.size(); ++i) {
        char32_t c = char32_t(uint32_t(w[i]));
        if constexpr (sizeof(wchar_t) == 2) {
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size() && uint32_t(w[i + 1]) >= 0xDC00 && uint32_t(w[i + 1]) <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (char32_t(uint32_t(w[i + 1])) - 0xDC00);
                ++i;
            } else if (c >= 0xD800 && c <= 0xDFFF) {
                c = 0xFFFD;
            }
        } else if ((c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) {
            c = 0xFFFD;
        }
        put(c);
    }
    return out;
}

}  // namespace stardraft::text
