// Themes (included into main.cpp right after the color tokens): dark (the default) or light, and the accent color.
// setThemeColors() gives every token its value; applyTheme() (in main.cpp, after the windows exist) also remakes the
// brushes, the Windows themes of the controls, and paints everything again.
//
// The accent: one of kAccents, each with the same perceived brightness (OKLCH lightness) as the default blue, so text
// and lines on it stay readable whatever the user picks. kAccentBright, kAccentLine, kAccentFill, kSel, kSelHot and
// kOnText come from the accent's hue: the same lightness and chroma as the default blue's versions of them.

namespace theme {

struct Oklch {
    double L, C, h;
};

inline double toLinear(double c) {
    c /= 255;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
inline double toSrgb(double c) { return 255 * (c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055); }

inline Oklch oklch(COLORREF c) {
    const double r = toLinear(GetRValue(c)), g = toLinear(GetGValue(c)), b = toLinear(GetBValue(c));
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b), m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b),
                 s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    const double L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                 B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    double h = std::atan2(B, A) * 180 / 3.14159265358979;
    if (h < 0) h += 360;
    return {L, std::hypot(A, B), h};
}

// The color at L, C, h; the chroma comes down until the color is inside sRGB.
inline COLORREF color(double L, double C, double h) {
    for (;; C = std::max(0.0, C - 0.002)) {
        const double A = C * std::cos(h * 3.14159265358979 / 180), B = C * std::sin(h * 3.14159265358979 / 180);
        const double l = std::pow(L + 0.3963377774 * A + 0.2158037573 * B, 3), m = std::pow(L - 0.1055613458 * A - 0.0638541728 * B, 3),
                     s = std::pow(L - 0.0894841775 * A - 1.2914855480 * B, 3);
        const double rgb[3] = {toSrgb(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s), toSrgb(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
                               toSrgb(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)};
        bool inside = true;
        for (double v : rgb) inside &= v >= -0.5 && v <= 255.5;
        if (inside || C <= 0) {
            auto ch = [](double v) { return BYTE(std::clamp(std::lround(v), 0L, 255L)); };
            return RGB(ch(rgb[0]), ch(rgb[1]), ch(rgb[2]));
        }
    }
}

struct Accent {
    const wchar_t* name;
    double hue;  // OKLCH hue; -1: the default blue's own
};
constexpr Accent kAccents[] = {{L"Blue", -1}, {L"Violet", 295}, {L"Pink", 345}, {L"Red", 22}, {L"Orange", 55}, {L"Yellow", 95}, {L"Green", 145}, {L"Teal", 190}};
constexpr int kAccentCount = int(std::size(kAccents));
constexpr COLORREF kDefaultBlue = RGB(98, 176, 240);

// The accent in the dark theme (what the swatches show in either theme).
inline COLORREF swatch(int i) {
    const Oklch b = oklch(kDefaultBlue);
    const double h = kAccents[i].hue < 0 ? b.h : kAccents[i].hue;
    return i == 0 ? kDefaultBlue : color(b.L, b.C, h);
}

// The accent that is nearest to a color (a system's accent color): the index of kAccents whose hue is at most
// `maxApart` degrees from the color's, else -1 (`hue` gets the color's own hue). A gray (low chroma) is the default blue.
inline int nearestAccent(COLORREF c, double maxApart, double& hue) {
    const Oklch o = oklch(c);
    hue = o.h;
    if (o.C < 0.03) return hue = -1, 0;
    int best = -1;
    double bestApart = 1e9;
    for (int i = 0; i < kAccentCount; ++i) {
        const double h = kAccents[i].hue < 0 ? oklch(kDefaultBlue).h : kAccents[i].hue;
        double d = std::fabs(h - o.h);
        d = std::min(d, 360 - d);
        if (d < bestApart) bestApart = d, best = i;
    }
    return bestApart <= maxApart ? best : -1;
}

}  // namespace theme

bool gLightTheme = false;
int gAccentIndex = 0;
double gAccentHue = -1;  // the accent's own hue (OKLCH degrees: a system's accent color), or -1: kAccents[gAccentIndex]'s

// `hue` (0 to 360): the accent has that hue (a system's accent color that is not near one of kAccents; `accent` is the
// nearest one, for its name); -1: kAccents[accent]'s hue.
void setThemeColors(bool light, int accent, double hue = -1) {
    using namespace theme;
    gLightTheme = light;
    gAccentIndex = accent = std::clamp(accent, 0, kAccentCount - 1);
    gAccentHue = hue >= 0 && hue <= 360 ? hue : -1;
    const double h = gAccentHue >= 0 ? gAccentHue : kAccents[accent].hue < 0 ? oklch(kDefaultBlue).h : kAccents[accent].hue;
    if (!light) {
        kBg = RGB(32, 32, 32), kPanel = RGB(28, 28, 30), kCtl = RGB(43, 43, 43), kCtlHot = RGB(52, 52, 56), kCtlDown = RGB(36, 36, 38);
        kReadOnly = RGB(30, 30, 32), kReadOnlyLine = RGB(48, 48, 52);
        kText = RGB(224, 224, 224), kDim = RGB(148, 148, 148), kFaint = RGB(90, 90, 96);
        kBorder = RGB(60, 60, 64), kBorderHot = RGB(88, 88, 94), kBorderStrong = RGB(110, 110, 118);
        kRowHot = RGB(46, 46, 50), kHot = RGB(62, 62, 66);
        kGood = RGB(127, 201, 138), kGoodFill = RGB(28, 49, 34), kGoodLine = RGB(61, 107, 71), kWarn = RGB(224, 164, 88), kBad = RGB(232, 104, 96);
        kFeedback = RGB(242, 204, 84);
        // The accent and its versions: the default blue's lightness and chroma, the accent's hue.
        kAccent = gAccentHue >= 0 ? color(oklch(kDefaultBlue).L, oklch(kDefaultBlue).C, h) : swatch(accent);
        kAccentBright = color(0.807, 0.105, h), kAccentLine = color(0.515, 0.094, h), kAccentFill = kAccentLine;
        kSel = color(0.296, 0.041, h), kSelHot = color(0.349, 0.052, h), kOnText = color(0.951, 0.017, h);
        kChip = color(0.282, 0.022, h), kChipLine = color(0.373, 0.030, h), kChipDim = RGB(38, 38, 41);
        kBadFill = RGB(64, 32, 32), kBadFillHot = RGB(84, 40, 38);
        kThumb = RGB(84, 84, 90), kThumbHot = RGB(120, 120, 128), kRowLine = RGB(44, 46, 52), kGripDot = RGB(110, 110, 116);
        if (accent == 0 && gAccentHue < 0)  // the default blue keeps its own values exactly
            kChip = RGB(34, 42, 52), kChipLine = RGB(52, 66, 80),
            kAccentBright = RGB(130, 200, 255), kAccentLine = kAccentFill = RGB(47, 109, 153), kSel = RGB(23, 48, 63), kSelHot = RGB(30, 62, 82),
            kOnText = RGB(230, 240, 250);
    } else {
        kBg = RGB(243, 243, 245), kPanel = RGB(236, 236, 239), kCtl = RGB(255, 255, 255), kCtlHot = RGB(244, 245, 248), kCtlDown = RGB(230, 231, 235);
        kReadOnly = RGB(238, 238, 241), kReadOnlyLine = RGB(214, 214, 220);
        kText = RGB(28, 28, 32), kDim = RGB(98, 98, 106), kFaint = RGB(158, 158, 166);
        kBorder = RGB(208, 208, 214), kBorderHot = RGB(162, 162, 172), kBorderStrong = RGB(128, 128, 138);
        kRowHot = RGB(228, 229, 234), kHot = RGB(218, 219, 225);
        kGood = RGB(30, 128, 56), kGoodFill = RGB(224, 243, 228), kGoodLine = RGB(132, 192, 146), kWarn = RGB(158, 90, 8), kBad = RGB(196, 46, 40);
        kFeedback = RGB(190, 138, 0);
        // On a light ground the accent is darker (text in it must read on white); every swatch has the same lightness.
        kAccent = color(0.50, 0.15, h), kAccentBright = color(0.43, 0.15, h), kAccentLine = color(0.62, 0.12, h), kAccentFill = kAccent;
        kSel = color(0.945, 0.03, h), kSelHot = color(0.91, 0.045, h), kOnText = RGB(255, 255, 255);
        kChip = color(0.955, 0.022, h), kChipLine = color(0.84, 0.045, h), kChipDim = RGB(238, 238, 241);
        kBadFill = RGB(253, 234, 232), kBadFillHot = RGB(250, 218, 214);
        kThumb = RGB(196, 196, 204), kThumbHot = RGB(150, 150, 160), kRowLine = RGB(226, 227, 232), kGripDot = RGB(150, 150, 158);
    }
    kRail = kPanel;  // (a host can give the rail the system's sidebar color after this: macOS)
}

// WCAG contrast of two colors (1 to 21).
double contrast(COLORREF a, COLORREF b) {
    auto lum = [](COLORREF c) {
        return 0.2126 * theme::toLinear(GetRValue(c)) + 0.7152 * theme::toLinear(GetGValue(c)) + 0.0722 * theme::toLinear(GetBValue(c));
    };
    double x = lum(a) + 0.05, y = lum(b) + 0.05;
    return x > y ? x / y : y / x;
}
