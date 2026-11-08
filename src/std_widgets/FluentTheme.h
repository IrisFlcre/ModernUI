// ============================================================================
// FluentTheme.h — Windows 11 Fluent Design 主题令牌
// ============================================================================
//
// 设计：
//   - 所有控件共享一份全局主题（暗色 / 亮色），切换时 themeRevision 自增，
//     控件在 onTick() 中检测版本变化并重绘，无需遍历控件树。
//   - 颜色按 WinUI 3 的语义命名（ControlFill / CardBackground / Stroke / Accent…）。
//   - 尺寸常量覆盖控件高度 / 行高 / 圆角 / 字号；动画时长供控件使用。
//
// ============================================================================

#pragma once

#include <cstdint>

#include "Canvas.h"

namespace modern_ui {
namespace fluent {

struct FluentTheme {
    enum class Mode : uint8_t { Light = 0, Dark };

    Mode mode = Mode::Dark;

    // ---- 文本 ----
    Color textPrimary{255, 255, 255, 255};
    Color textSecondary{197, 197, 197, 255};
    Color textTertiary{157, 157, 157, 255};
    Color textDisabled{93, 93, 93, 255};
    Color textOnAccent{0, 0, 0, 255};

    // ---- 强调色 ----
    Color accent{96, 205, 255, 255};
    Color accentHover{82, 184, 233, 255};
    Color accentPressed{71, 169, 217, 255};
    Color accentDisabled{96, 205, 255, 64};

    // ---- 背景 / 表面 ----
    Color background{32, 32, 32, 255};
    Color card{43, 43, 43, 255};
    Color cardAlt{39, 39, 39, 255};
    Color layer{39, 39, 39, 255};
    Color layerAlt{50, 50, 50, 255};
    Color overlay{44, 44, 44, 255};
    Color scrim{0, 0, 0, 166};

    // ---- 控件填充 ----
    Color controlFill{255, 255, 255, 15};
    Color controlFillHover{255, 255, 255, 23};
    Color controlFillPressed{255, 255, 255, 10};
    Color controlFillDisabled{255, 255, 255, 11};
    Color subtleHover{255, 255, 255, 15};
    Color subtlePressed{255, 255, 255, 8};

    // ---- 描边 / 焦点 ----
    Color stroke{255, 255, 255, 18};
    Color strokeStrong{255, 255, 255, 38};
    Color focusStroke{255, 255, 255, 255};
    Color divider{255, 255, 255, 16};

    // ---- 状态色 ----
    Color info{96, 205, 255, 255};
    Color success{108, 203, 95, 255};
    Color caution{252, 225, 0, 255};
    Color critical{255, 153, 164, 255};

    // ---- 尺寸 ----
    int32_t controlHeight = 32;
    int32_t listItemHeight = 36;
    int32_t minControlWidth = 40;
    float   radiusControl = 4.0f;
    float   radiusCard = 8.0f;
    float   radiusOverlay = 8.0f;
    float   borderWidth = 1.0f;

    // ---- 字号 ----
    int32_t captionFontSize = 12;
    int32_t bodyFontSize = 14;
    int32_t subtitleFontSize = 18;
    int32_t titleFontSize = 24;
    int32_t displayFontSize = 32;

    // ---- Motion ----
    float fastDuration = 0.10f;
    float normalDuration = 0.20f;
    float slowDuration = 0.32f;

    static FluentTheme dark();
    static FluentTheme light();
};

// 当前主题（函数内静态，避免全局构造顺序问题）
const FluentTheme& currentTheme();
void setTheme(const FluentTheme& theme);
void setThemeMode(FluentTheme::Mode mode);
FluentTheme::Mode themeMode();
uint32_t themeRevision();

// ---- 颜色工具 ----
Color withAlpha(Color color, uint8_t alpha);
Color withOpacity(Color color, float opacity);   // 与自身 alpha 相乘
Color mixColor(const Color& a, const Color& b, float t);
Color brighten(const Color& color, float amount);
Color darken(const Color& color, float amount);

} // namespace fluent
} // namespace modern_ui
