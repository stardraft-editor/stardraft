// Text between the library's UTF-8 and the UI's wide strings (apps/common/text_codec.h: the same as Win32 gave).

// UTF-8 when the bytes are valid UTF-8, else Windows-1252 (map strings are often in it).
std::wstring widen(const std::string& s) { return stardraft::text::widen(s); }

std::string narrow(const std::wstring& w) { return stardraft::text::narrow(w); }
