// ============================================================================
// FluentControl.h — std_widgets 控件基类
// ============================================================================
//
// 统一处理：
//   - hover / pressed / focused 的平滑过渡（Fluent Motion）
//   - 禁用状态（命中测试 / 焦点 / 配色）
//   - 主题切换检测（themeRevision 变化时自动重绘）
//   - 鼠标点击 / 键盘 Space、Enter 激活
//   - 尺寸变化时把 frame 写进控件局部 Canvas
//
// 派生控件只需：
//   - 构造函数中创建几何体
//   - 实现 onMeasure()
//   - 实现 updateVisuals()（按 hoverAmount / pressAmount / focusAmount 配色）
//   - 可选 override activate()
//
// ============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

#include "FluentDraw.h"
#include "FluentTheme.h"
#include "Widget.h"

namespace modern_ui {
namespace fluent {

// 指数逼近；返回本帧是否产生可见变化
inline bool animateTowards(float& value, float target, float rate, float dt,
                           float epsilon = 0.004f) {
    if (std::fabs(value - target) <= 1.0e-6f) {
        value = target;
        return false;
    }
    const float alpha = 1.0f - std::exp(-std::max(0.0f, rate) * std::max(0.0f, dt));
    float next = value + (target - value) * alpha;
    if (std::fabs(next - target) < epsilon) next = target;
    const bool changed = std::fabs(next - value) > 1.0e-6f;
    value = next;
    return changed;
}

class FluentControl : public ParentWidget {
public:
    using Callback = std::function<void()>;

    FluentControl();
    ~FluentControl() override = default;

    Callback onClick;

    // ---- 启用 / 禁用 ----
    bool enabled() const { return enabled_; }
    void setEnabled(bool enabled);

    // ---- Widget 接口 ----
    bool hitTest(const Vec2i& localPoint) const override;
    bool acceptsFocus() const override { return enabled_; }
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;
    void onTick(float dtSeconds) override;
    void onLayout(const Rect& frame) override;

    // ---- 样式：每个 Fluent 控件默认样式由主题令牌生成，可被 .css 覆盖 ----
    StyleSheet defaultStyleSheet() const override;
    void onStyleChanged() override;

protected:
    // 派生控件实现：用当前 frame / 状态推进配色与几何体
    virtual void updateVisuals() = 0;

    // 点击 / 键盘激活；默认触发 onClick
    virtual void activate() { if (onClick) onClick(); }

    // 当前控件自己的主题视图（默认主题 + 该控件 StyleSheet 的覆盖）
    const FluentTheme& theme() const { return widgetTheme_; }

    // 直接读取当前样式表（自定义 Entry / 额外视觉参数）
    Color cssColor(StyleSheet::Entry entry, Color fallback) const {
        return styleSheet().color(entry, fallback);
    }
    float cssNumber(StyleSheet::Entry entry, float fallback) const {
        return styleSheet().number(entry, fallback);
    }
    // CSS 显式设置了 font-size 时使用样式值，否则保留控件自己的字号
    int32_t cssFontSize(int32_t fallback) const {
        const StyleSheet::Value& value = styleSheet().raw(StyleSheet::Entry::FontSize);
        if (!value.empty() && value.origin != StyleSheet::Origin::Default) {
            return styleSheet().integer(StyleSheet::Entry::FontSize, fallback);
        }
        return fallback;
    }

    Rect measureString(const std::string& text, int32_t fontSize) const {
        return measureTextLine(canvas(), text, fontSize);
    }

    int32_t frameWidth() const { return lastFrame_.w; }
    int32_t frameHeight() const { return lastFrame_.h; }
    const Rect& localFrame() const { return lastFrame_; }
    bool laidOut() const { return laidOut_; }

    float hoverAmount() const { return hoverProgress_; }
    float pressAmount() const { return pressProgress_; }
    float focusAmount() const { return focusProgress_; }

    // 状态色：rest → hover → pressed（按当前过渡进度混合）
    Color interactiveFill(const Color& rest, const Color& hover, const Color& pressed) const {
        const Color a = mixColor(rest, hover, hoverProgress_);
        return mixColor(a, pressed, pressProgress_);
    }

    Color focusStroke(const Color& base) const {
        return mixColor(base, theme().focusStroke, focusProgress_);
    }

    // 主题版本变化时派生控件可据此重绘
    uint32_t visualThemeRevision() const { return visualThemeRevision_; }

    // 所有视觉属性修改完通知 Canvas 标脏
    void commitVisuals() { canvas().markDirtyAll(); }

private:
    void rebuildWidgetTheme();

    bool enabled_ = true;
    bool laidOut_ = false;
    Rect lastFrame_{};
    FluentTheme widgetTheme_{};

    float hoverProgress_ = 0.0f;
    float pressProgress_ = 0.0f;
    float focusProgress_ = 0.0f;

    uint32_t visualThemeRevision_ = 0;
};

} // namespace fluent
} // namespace modern_ui
