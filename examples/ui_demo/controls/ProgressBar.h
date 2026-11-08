// ============================================================================
// controls/ProgressBar.h — 进度条控件（只读展示）
// ============================================================================
//
// 演示：setValue 直接修改自己子 Canvas 的几何体（填充段宽度），
//       父级只需让 Flexbox 给出宽度（本例用 flexGrow 拉伸）。
// ============================================================================

#pragma once

#include <cstdint>

#include "Widget.h"

namespace demo {

using modern_ui::Color;
using modern_ui::Rect;
using modern_ui::Vec2i;
using modern_ui::Widget;

class ProgressBar : public Widget {
public:
    explicit ProgressBar(float value = 0.0f);
    ~ProgressBar() override;

    float value() const { return value_; }
    void setValue(float value);       // [0, 1]

    Vec2i onMeasure(const Vec2i& available) const override;
    void onLayout(const Rect& frame) override;

private:
    void writeGeometry();

    uint32_t trackId_ = 0;
    uint32_t fillId_ = 0;
    float value_ = 0.0f;
    Rect lastFrame_{};
};

} // namespace demo
