// ============================================================================
// FluentTheme.cpp — 主题令牌与颜色工具实现
// ============================================================================

#include "FluentTheme.h"

#include <algorithm>
#include <cmath>

namespace modern_ui {
namespace fluent {
namespace {

struct ThemeState {
    FluentTheme theme = FluentTheme::dark();
    uint32_t revision = 1;
};

ThemeState& state() {
    static ThemeState instance;
    return instance;
}

uint8_t clampByte(float value) {
    return static_cast<uint8_t>(std::lround(std::max(0.0f, std::min(255.0f, value))));
}

} // namespace

FluentTheme FluentTheme::dark() {
    return FluentTheme{};
}

FluentTheme FluentTheme::light() {
    FluentTheme theme;
    theme.mode = Mode::Light;

    theme.textPrimary = Color{26, 26, 26, 255};
    theme.textSecondary = Color{92, 92, 92, 255};
    theme.textTertiary = Color{138, 138, 138, 255};
    theme.textDisabled = Color{189, 189, 189, 255};
    theme.textOnAccent = Color{255, 255, 255, 255};

    theme.accent = Color{0, 95, 184, 255};
    theme.accentHover = Color{16, 110, 190, 255};
    theme.accentPressed = Color{13, 95, 166, 255};
    theme.accentDisabled = Color{0, 95, 184, 64};

    theme.background = Color{243, 243, 243, 255};
    theme.card = Color{255, 255, 255, 255};
    theme.cardAlt = Color{251, 251, 251, 255};
    theme.layer = Color{251, 251, 251, 255};
    theme.layerAlt = Color{245, 245, 245, 255};
    theme.overlay = Color{255, 255, 255, 255};
    theme.scrim = Color{0, 0, 0, 102};

    theme.controlFill = Color{255, 255, 255, 255};
    theme.controlFillHover = Color{246, 246, 246, 255};
    theme.controlFillPressed = Color{238, 238, 238, 255};
    theme.controlFillDisabled = Color{249, 249, 249, 255};
    theme.subtleHover = Color{0, 0, 0, 15};
    theme.subtlePressed = Color{0, 0, 0, 8};

    theme.stroke = Color{0, 0, 0, 20};
    theme.strokeStrong = Color{0, 0, 0, 51};
    theme.focusStroke = Color{26, 26, 26, 255};
    theme.divider = Color{0, 0, 0, 16};

    theme.info = Color{0, 95, 184, 255};
    theme.success = Color{15, 123, 15, 255};
    theme.caution = Color{157, 93, 0, 255};
    theme.critical = Color{196, 43, 28, 255};

    return theme;
}

const FluentTheme& currentTheme() {
    return state().theme;
}

void setTheme(const FluentTheme& theme) {
    state().theme = theme;
    ++state().revision;
}

void setThemeMode(FluentTheme::Mode mode) {
    setTheme(mode == FluentTheme::Mode::Dark ? FluentTheme::dark() : FluentTheme::light());
}

FluentTheme::Mode themeMode() {
    return state().theme.mode;
}

uint32_t themeRevision() {
    return state().revision;
}

Color withAlpha(Color color, uint8_t alpha) {
    color.a = alpha;
    return color;
}

Color withOpacity(Color color, float opacity) {
    const float value = static_cast<float>(color.a) * std::max(0.0f, std::min(1.0f, opacity));
    color.a = clampByte(value);
    return color;
}

Color mixColor(const Color& a, const Color& b, float t) {
    const float k = std::max(0.0f, std::min(1.0f, t));
    return Color{
        clampByte(static_cast<float>(a.r) + (static_cast<float>(b.r) - static_cast<float>(a.r)) * k),
        clampByte(static_cast<float>(a.g) + (static_cast<float>(b.g) - static_cast<float>(a.g)) * k),
        clampByte(static_cast<float>(a.b) + (static_cast<float>(b.b) - static_cast<float>(a.b)) * k),
        clampByte(static_cast<float>(a.a) + (static_cast<float>(b.a) - static_cast<float>(a.a)) * k)
    };
}

Color brighten(const Color& color, float amount) {
    const Color white{255, 255, 255, color.a};
    return mixColor(color, white, amount);
}

Color darken(const Color& color, float amount) {
    const Color black{0, 0, 0, color.a};
    return mixColor(color, black, amount);
}

} // namespace fluent
} // namespace modern_ui
