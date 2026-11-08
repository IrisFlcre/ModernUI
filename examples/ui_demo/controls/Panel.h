// ============================================================================
// controls/Panel.h — 容器控件（圆角背景 + Flexbox 排版子控件）
// ============================================================================

#pragma once

#include <cstdint>

#include "Widget.h"

namespace demo {

using modern_ui::Color;
using modern_ui::ParentWidget;
using modern_ui::Rect;

// Panel 不摆放子控件：子控件的位置 / 尺寸完全由 Flexbox 依据 style() 决定。
// 它自己只维护一个铺满自身 frame 的圆角矩形（相对坐标 0,0）。
class Panel : public ParentWidget {
public:
    Panel(Color fill = Color::nullColor(),
          Color stroke = Color::nullColor(),
          float radius = 14.0f);
    ~Panel() override;

    void setFill(Color fill);
    void setStroke(Color stroke, int32_t width = 1);
    void setRadius(float radius);

    void onLayout(const Rect& frame) override;

private:
    void writeGeometry();

    uint32_t backgroundId_ = 0;
    Color fill_{};
    Color stroke_{};
    float radius_ = 14.0f;
    int32_t strokeWidth_ = 1;
    Rect lastFrame_{};
};

} // namespace demo
