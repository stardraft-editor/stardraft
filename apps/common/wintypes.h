#pragma once
// The plain value types and helpers of Win32 that the editor's portable code uses (rectangles, points, colors), so that
// code moved off the window system compiles unchanged on macOS and Linux. On Windows this is <windows.h> itself.
// Only values and arithmetic: nothing here talks to a window system.

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else

#include <algorithm>
#include <cstdint>

using BYTE = uint8_t;
using WORD = uint16_t;
using DWORD = uint32_t;
using UINT = unsigned int;
using LONG = int32_t;  // (32 bits as on Windows; not the system's long)
using BOOL = int;
using COLORREF = DWORD;
using UINT_PTR = uintptr_t;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

struct RECT {
    LONG left, top, right, bottom;
};
struct POINT {
    LONG x, y;
};
struct SIZE {
    LONG cx, cy;
};

constexpr COLORREF RGB(int r, int g, int b) { return COLORREF(BYTE(r)) | COLORREF(BYTE(g)) << 8 | COLORREF(BYTE(b)) << 16; }
constexpr BYTE GetRValue(COLORREF c) { return BYTE(c); }
constexpr BYTE GetGValue(COLORREF c) { return BYTE(c >> 8); }
constexpr BYTE GetBValue(COLORREF c) { return BYTE(c >> 16); }
constexpr COLORREF CLR_NONE = 0xFFFFFFFF;

inline BOOL PtInRect(const RECT* r, POINT p) { return p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom; }
inline BOOL IsRectEmpty(const RECT* r) { return r->right <= r->left || r->bottom <= r->top; }
inline BOOL EqualRect(const RECT* a, const RECT* b) {
    return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}
inline BOOL SetRect(RECT* r, int l, int t, int rt, int b) {
    *r = {l, t, rt, b};
    return TRUE;
}
inline BOOL SetRectEmpty(RECT* r) {
    *r = {0, 0, 0, 0};
    return TRUE;
}
inline BOOL OffsetRect(RECT* r, int dx, int dy) {
    r->left += dx, r->right += dx, r->top += dy, r->bottom += dy;
    return TRUE;
}
inline BOOL InflateRect(RECT* r, int dx, int dy) {
    r->left -= dx, r->right += dx, r->top -= dy, r->bottom += dy;
    return TRUE;
}
inline BOOL IntersectRect(RECT* out, const RECT* a, const RECT* b) {
    *out = {std::max(a->left, b->left), std::max(a->top, b->top), std::min(a->right, b->right), std::min(a->bottom, b->bottom)};
    if (IsRectEmpty(out)) {
        *out = {0, 0, 0, 0};
        return FALSE;
    }
    return TRUE;
}
inline BOOL UnionRect(RECT* out, const RECT* a, const RECT* b) {
    if (IsRectEmpty(a)) return *out = *b, !IsRectEmpty(b);
    if (IsRectEmpty(b)) return *out = *a, TRUE;
    *out = {std::min(a->left, b->left), std::min(a->top, b->top), std::max(a->right, b->right), std::max(a->bottom, b->bottom)};
    return TRUE;
}
// a * b / c, rounded half away from zero (as Windows'); -1 when c is 0.
inline int MulDiv(int a, int b, int c) {
    if (c == 0) return -1;
    const long long n = (long long)a * b;
    const bool neg = (n < 0) != (c < 0);
    const long long an = n < 0 ? -n : n, ac = c < 0 ? -(long long)c : c;
    const long long q = (an + ac / 2) / ac;
    return int(neg ? -q : q);
}

#endif
