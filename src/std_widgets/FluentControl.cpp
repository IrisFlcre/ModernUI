// ============================================================================
// FluentControl.cpp — 控件基类实现
// ============================================================================

#include "FluentControl.h"

namespace modern_ui {
namespace fluent {
namespace {

constexpr int kKeySpace = 0x20;
constexpr int kKeyEnter = 0x0D;

using Entry = StyleSheet::Entry;
using Origin = StyleSheet::Origin;

bool cssOrigin(const StyleSheet::Value& value) {
    return !value.empty() && value.origin != Origin::Default;
}

// 取颜色：CSS 显式设置的 token 优先；其次 CSS 通用属性（如 background / color）；
// 再次默认 token；最后回退到基础主题。
Color pickColor(const StyleSheet& sheet, Entry token, Entry alt, Color fallback) {
    const StyleSheet::Value& tokenValue = sheet.raw(token);
    const StyleSheet::Value& altValue = sheet.raw(alt);

    if (cssOrigin(tokenValue)) return sheet.color(token, fallback);
    if (cssOrigin(altValue)) return sheet.color(alt, fallback);

    if (!tokenValue.empty()) return sheet.color(token, fallback);
    if (!altValue.empty()) return sheet.color(alt, fallback);
    return fallback;
}

Color pickColor(const StyleSheet& sheet, Entry token, Color fallback) {
    return pickColor(sheet, token, Entry::Count, fallback);
}

float pickNumber(const StyleSheet& sheet, Entry token, Entry alt, float fallback) {
    const StyleSheet::Value& tokenValue = sheet.raw(token);
    const StyleSheet::Value& altValue = sheet.raw(alt);

    if (cssOrigin(tokenValue)) return sheet.number(token, fallback);
    if (cssOrigin(altValue)) return sheet.number(alt, fallback);

    if (!tokenValue.empty()) return sheet.number(token, fallback);
    if (!altValue.empty()) return sheet.number(alt, fallback);
    return fallback;
}

float pickNumber(const StyleSheet& sheet, Entry token, float fallback) {
    return pickNumber(sheet, token, Entry::Count, fallback);
}

// StyleSheet → 该控件自己的 FluentTheme 视图
FluentTheme deriveTheme(const StyleSheet& sheet) {
    FluentTheme theme = currentTheme();

    theme.background = pickColor(sheet, Entry::Background, theme.background);
    theme.card = pickColor(sheet, Entry::Card, Entry::Background, theme.card);
    theme.cardAlt = pickColor(sheet, Entry::CardAlt, Entry::Background, theme.cardAlt);
    theme.layer = pickColor(sheet, Entry::Layer, Entry::Background, theme.layer);
    theme.layerAlt = pickColor(sheet, Entry::LayerAlt, Entry::Background, theme.layerAlt);
    theme.overlay = pickColor(sheet, Entry::Overlay, Entry::Background, theme.overlay);
    theme.scrim = pickColor(sheet, Entry::Scrim, theme.scrim);

    theme.controlFill = pickColor(sheet, Entry::ControlFill, Entry::Background, theme.controlFill);
    theme.controlFillHover = pickColor(sheet, Entry::ControlFillHover, Entry::BackgroundHover,
                                      theme.controlFillHover);
    theme.controlFillPressed = pickColor(sheet, Entry::ControlFillPressed, Entry::BackgroundPressed,
                                        theme.controlFillPressed);
    theme.controlFillDisabled = pickColor(sheet, Entry::ControlFillDisabled,
                                         Entry::BackgroundDisabled, theme.controlFillDisabled);
    theme.subtleHover = pickColor(sheet, Entry::SubtleHover, theme.subtleHover);
    theme.subtlePressed = pickColor(sheet, Entry::SubtlePressed, theme.subtlePressed);

    theme.textPrimary = pickColor(sheet, Entry::TextPrimary, Entry::Foreground, theme.textPrimary);
    theme.textSecondary = pickColor(sheet, Entry::TextSecondary, Entry::Foreground,
                                    theme.textSecondary);
    theme.textTertiary = pickColor(sheet, Entry::TextTertiary, Entry::Foreground,
                                   theme.textTertiary);
    theme.textDisabled = pickColor(sheet, Entry::TextDisabled, Entry::ForegroundDisabled,
                                   theme.textDisabled);
    theme.textOnAccent = pickColor(sheet, Entry::TextOnAccent, Entry::ForegroundOnAccent,
                                   theme.textOnAccent);

    theme.accent = pickColor(sheet, Entry::Accent, theme.accent);
    theme.accentHover = pickColor(sheet, Entry::AccentHover, theme.accentHover);
    theme.accentPressed = pickColor(sheet, Entry::AccentPressed, theme.accentPressed);
    theme.accentDisabled = pickColor(sheet, Entry::AccentDisabled, theme.accentDisabled);

    theme.stroke = pickColor(sheet, Entry::Stroke, Entry::BorderColor, theme.stroke);
    theme.strokeStrong = pickColor(sheet, Entry::StrokeStrong, Entry::BorderColor,
                                   theme.strokeStrong);
    theme.focusStroke = pickColor(sheet, Entry::FocusStroke, Entry::BorderColor,
                                  theme.focusStroke);
    theme.divider = pickColor(sheet, Entry::Divider, Entry::BorderColor, theme.divider);

    theme.info = pickColor(sheet, Entry::Info, theme.info);
    theme.success = pickColor(sheet, Entry::Success, theme.success);
    theme.caution = pickColor(sheet, Entry::Caution, theme.caution);
    theme.critical = pickColor(sheet, Entry::Critical, theme.critical);

    theme.controlHeight = static_cast<int32_t>(
        pickNumber(sheet, Entry::ControlHeight, static_cast<float>(theme.controlHeight)));
    theme.listItemHeight = static_cast<int32_t>(
        pickNumber(sheet, Entry::ListItemHeight, static_cast<float>(theme.listItemHeight)));
    theme.minControlWidth = static_cast<int32_t>(
        pickNumber(sheet, Entry::MinControlWidth, static_cast<float>(theme.minControlWidth)));

    theme.radiusControl = pickNumber(sheet, Entry::Radius, theme.radiusControl);
    theme.radiusCard = pickNumber(sheet, Entry::RadiusCard, Entry::Radius, theme.radiusCard);
    theme.radiusOverlay = pickNumber(sheet, Entry::RadiusOverlay, Entry::Radius,
                                     theme.radiusOverlay);
    theme.borderWidth = pickNumber(sheet, Entry::BorderWidth, theme.borderWidth);

    // font-size 作为所有字号令牌的回退：CSS 写 font-size 会整体缩放文本
    theme.captionFontSize = static_cast<int32_t>(
        pickNumber(sheet, Entry::CaptionFontSize, Entry::FontSize,
                   static_cast<float>(theme.captionFontSize)));
    theme.bodyFontSize = static_cast<int32_t>(
        pickNumber(sheet, Entry::FontSize, static_cast<float>(theme.bodyFontSize)));
    theme.subtitleFontSize = static_cast<int32_t>(
        pickNumber(sheet, Entry::SubtitleFontSize, Entry::FontSize,
                   static_cast<float>(theme.subtitleFontSize)));
    theme.titleFontSize = static_cast<int32_t>(
        pickNumber(sheet, Entry::TitleFontSize, Entry::FontSize,
                   static_cast<float>(theme.titleFontSize)));
    theme.displayFontSize = static_cast<int32_t>(
        pickNumber(sheet, Entry::DisplayFontSize, Entry::FontSize,
                   static_cast<float>(theme.displayFontSize)));

    theme.fastDuration = pickNumber(sheet, Entry::FastDuration, Entry::NormalDuration,
                                    theme.fastDuration);
    theme.normalDuration = pickNumber(sheet, Entry::NormalDuration, theme.normalDuration);
    theme.slowDuration = pickNumber(sheet, Entry::SlowDuration, Entry::NormalDuration,
                                    theme.slowDuration);

    return theme;
}

} // namespace

FluentControl::FluentControl() {
    visualThemeRevision_ = themeRevision();
    widgetTheme_ = currentTheme();
}

// ============================================================================
// 样式：默认样式表（主题令牌）+ 控件级主题派生
// ============================================================================

StyleSheet FluentControl::defaultStyleSheet() const {
    const FluentTheme& base = currentTheme();
    StyleSheet sheet;

    auto setColor = [&sheet](Entry entry, Color color) { sheet.setColor(entry, color); };
    auto setNumber = [&sheet](Entry entry, float number) { sheet.setNumber(entry, number); };

    // 表面 / 填充
    setColor(Entry::Card, base.card);
    setColor(Entry::CardAlt, base.cardAlt);
    setColor(Entry::Layer, base.layer);
    setColor(Entry::LayerAlt, base.layerAlt);
    setColor(Entry::Overlay, base.overlay);
    setColor(Entry::Scrim, base.scrim);
    setColor(Entry::ControlFill, base.controlFill);
    setColor(Entry::ControlFillHover, base.controlFillHover);
    setColor(Entry::ControlFillPressed, base.controlFillPressed);
    setColor(Entry::ControlFillDisabled, base.controlFillDisabled);
    setColor(Entry::SubtleHover, base.subtleHover);
    setColor(Entry::SubtlePressed, base.subtlePressed);

    // 描边 / 焦点
    setColor(Entry::BorderColor, base.stroke);
    setColor(Entry::Stroke, base.stroke);
    setColor(Entry::StrokeStrong, base.strokeStrong);
    setColor(Entry::FocusStroke, base.focusStroke);
    setColor(Entry::Divider, base.divider);

    // 前景
    setColor(Entry::Foreground, base.textPrimary);
    setColor(Entry::ForegroundSecondary, base.textSecondary);
    setColor(Entry::ForegroundTertiary, base.textTertiary);
    setColor(Entry::ForegroundDisabled, base.textDisabled);
    setColor(Entry::ForegroundOnAccent, base.textOnAccent);
    setColor(Entry::TextPrimary, base.textPrimary);
    setColor(Entry::TextSecondary, base.textSecondary);
    setColor(Entry::TextTertiary, base.textTertiary);
    setColor(Entry::TextDisabled, base.textDisabled);
    setColor(Entry::TextOnAccent, base.textOnAccent);

    // 强调 / 状态色
    setColor(Entry::Accent, base.accent);
    setColor(Entry::AccentHover, base.accentHover);
    setColor(Entry::AccentPressed, base.accentPressed);
    setColor(Entry::AccentDisabled, base.accentDisabled);
    setColor(Entry::Info, base.info);
    setColor(Entry::Success, base.success);
    setColor(Entry::Caution, base.caution);
    setColor(Entry::Critical, base.critical);

    // 尺寸 / 动效
    setNumber(Entry::ControlHeight, static_cast<float>(base.controlHeight));
    setNumber(Entry::ListItemHeight, static_cast<float>(base.listItemHeight));
    setNumber(Entry::MinControlWidth, static_cast<float>(base.minControlWidth));
    setNumber(Entry::Radius, base.radiusControl);
    setNumber(Entry::RadiusCard, base.radiusCard);
    setNumber(Entry::RadiusOverlay, base.radiusOverlay);
    setNumber(Entry::BorderWidth, base.borderWidth);
    setNumber(Entry::FontSize, static_cast<float>(base.bodyFontSize));
    setNumber(Entry::CaptionFontSize, static_cast<float>(base.captionFontSize));
    setNumber(Entry::SubtitleFontSize, static_cast<float>(base.subtitleFontSize));
    setNumber(Entry::TitleFontSize, static_cast<float>(base.titleFontSize));
    setNumber(Entry::DisplayFontSize, static_cast<float>(base.displayFontSize));
    setNumber(Entry::FastDuration, base.fastDuration);
    setNumber(Entry::NormalDuration, base.normalDuration);
    setNumber(Entry::SlowDuration, base.slowDuration);

    // 变量：允许 .css 直接引用 var(--accent) 等主题令牌
    sheet.setVar("--background", style::colorToCss(base.background));
    sheet.setVar("--card", style::colorToCss(base.card));
    sheet.setVar("--layer", style::colorToCss(base.layer));
    sheet.setVar("--overlay", style::colorToCss(base.overlay));
    sheet.setVar("--control-fill", style::colorToCss(base.controlFill));
    sheet.setVar("--text-primary", style::colorToCss(base.textPrimary));
    sheet.setVar("--text-secondary", style::colorToCss(base.textSecondary));
    sheet.setVar("--text-disabled", style::colorToCss(base.textDisabled));
    sheet.setVar("--accent", style::colorToCss(base.accent));
    sheet.setVar("--accent-hover", style::colorToCss(base.accentHover));
    sheet.setVar("--accent-pressed", style::colorToCss(base.accentPressed));
    sheet.setVar("--stroke", style::colorToCss(base.stroke));
    sheet.setVar("--stroke-strong", style::colorToCss(base.strokeStrong));
    sheet.setVar("--info", style::colorToCss(base.info));
    sheet.setVar("--success", style::colorToCss(base.success));
    sheet.setVar("--caution", style::colorToCss(base.caution));
    sheet.setVar("--critical", style::colorToCss(base.critical));
    sheet.setVar("--radius", style::numberToCss(base.radiusControl));
    sheet.setVar("--control-height", style::numberToCss(static_cast<float>(base.controlHeight)));
    sheet.setVar("--font-size", style::numberToCss(static_cast<float>(base.bodyFontSize)));

    return sheet;
}

void FluentControl::rebuildWidgetTheme() {
    widgetTheme_ = deriveTheme(styleSheet());
}

void FluentControl::onStyleChanged() {
    rebuildWidgetTheme();
    if (laidOut_) updateVisuals();
}

void FluentControl::setEnabled(bool enabledIn) {
    if (enabled_ == enabledIn) return;
    enabled_ = enabledIn;
    if (laidOut_) updateVisuals();
}

bool FluentControl::hitTest(const Vec2i& localPoint) const {
    if (!enabled_) return false;
    return Widget::hitTest(localPoint);
}

bool FluentControl::onEvent(const Event& ev) {
    if (!enabled_ &&
        (ev.type == EventType::MouseDown || ev.type == EventType::MouseUp ||
         ev.type == EventType::KeyDown)) {
        return false;
    }

    switch (ev.type) {
    case EventType::MouseDown:
        return true;

    case EventType::MouseUp:
        if (clicked()) activate();
        return true;

    case EventType::KeyDown:
        if (ev.key == kKeySpace || ev.key == kKeyEnter) {
            activate();
            return true;
        }
        return false;

    default:
        return false;
    }
}

void FluentControl::onStateChanged() {
    // 目标值由 hovered() / pressed() / focused() 提供，onTick 内平滑逼近
}

void FluentControl::onTick(float dtSeconds) {
    const float dt = std::max(0.0f, dtSeconds);
    bool changed = animateTowards(hoverProgress_, hovered() ? 1.0f : 0.0f, 18.0f, dt);
    changed = animateTowards(pressProgress_, pressed() ? 1.0f : 0.0f, 34.0f, dt) || changed;
    changed = animateTowards(focusProgress_, focused() ? 1.0f : 0.0f, 24.0f, dt) || changed;

    const uint32_t revision = themeRevision();
    if (revision != visualThemeRevision_) {
        visualThemeRevision_ = revision;
        // 主题切换：重建默认样式表 → 重新套用 .css → 派生控件主题
        refreshStyle();
        invalidateLayout();
        changed = true;
    }

    if (changed && laidOut_) updateVisuals();
}

void FluentControl::onLayout(const Rect& frame) {
    const Rect local{0, 0, std::max(0, frame.w), std::max(0, frame.h)};
    const bool firstLayout = !laidOut_;
    if (local == lastFrame_ && !firstLayout) return;

    lastFrame_ = local;
    laidOut_ = true;
    updateVisuals();
}

} // namespace fluent
} // namespace modern_ui
