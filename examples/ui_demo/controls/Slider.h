// ============================================================================
// controls/Slider.h — 滑杆控件（拖动 / 点击取值）
// ============================================================================
//
// 演示：按压捕获（拖出控件仍持续收到 MouseMove/MouseUp）+ 事件修改几何体。
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

class Slider : public Widget {
public:
    using ValueHandler = std::function<void(float)>;

    explicit Slider(float value = 0.5f);
    ~Slider() override;

    float value() const { return value_; }
    void setValue(float value);            // 外部设置（不回调）
    ValueHandler onValueChanged;           // 拖动 / 点击时回调，取值 [0, 1]

    Vec2i onMeasure(const Vec2i& available) const override;
    void onLayout(const Rect& frame) override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;
    bool acceptsFocus() const override { return true; }

private:
    void applyValueFromLocalX(int32_t localX, bool notify);
    void writeGeometry();
    void updateColors();

    uint32_t trackId_ = 0;
    uint32_t fillId_ = 0;
    uint32_t knobId_ = 0;
    float value_ = 0.5f;
    bool dragging_ = false;
    Rect lastFrame_{};
};

} // namespace demo
