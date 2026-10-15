#pragma once
// The translation of a text in the language in use. ui_text.h defines it (its tables include the Windows headers, so the
// core only declares it). Included at the top level, before the core's namespace or main.cpp's anonymous namespace.

#include <string>

const wchar_t* tr(const wchar_t* english);
std::wstring tr(const std::wstring& english);
