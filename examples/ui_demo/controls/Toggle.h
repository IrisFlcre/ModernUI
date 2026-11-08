// ============================================================================
// controls/Toggle.h — 开关控件
// ============================================================================
//
// 演示：状态（开 / 关）驱动几何体（滑块的圆心位置 + 轨道配色）。
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>

#include "Widget.h"

namespace demo {

using modern_ui::Color;
using modern_ui::Event;
using modern_ui::Rect;
using modern_ui::Vec2i;
using modern_ui::Widget;

class Toggle : public Widget {
public:
    using ToggleHandler = std::function<void(bool)>;

    Toggle();
    ~Toggle() override;

    bool isOn() const { return on_; }
    void setOn(bool on);              // 外部设置（不触发 onChanged）
    ToggleHandler onChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void onLayout(const Rect& frame) override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;
    bool acceptsFocus() const override { return true; }

private:
    void writeGeometry();
    void updateColors();

    uint32_t trackId_ = 0;
    uint32_t knobId_ = 0;
    bool on_ = false;
    Rect lastFrame_{};
};

} // namespace demo
