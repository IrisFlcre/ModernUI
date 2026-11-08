// ============================================================================
// controls/BarChart.h — 柱状图控件（响应式宽度 / 数据驱动高度）
// ============================================================================
//
// 演示：onLayout 里根据 frame 计算每根柱子的相对坐标与尺寸；
//       setValues 只改本控件的子 Canvas → 只有这个控件会被重新合成。
// ============================================================================

#pragma once

#include <cstdint>
#include <vector>

#include "Widget.h"

namespace demo {

using modern_ui::Rect;
using modern_ui::Vec2i;
using modern_ui::Widget;

class BarChart : public Widget {
public:
    explicit BarChart(std::vector<float> values);
    ~BarChart() override;

    void setValues(std::vector<float> values);   // 取值 [0, 1]
    const std::vector<float>& values() const { return values_; }

    Vec2i onMeasure(const Vec2i& available) const override;
    void onLayout(const Rect& frame) override;

private:
    void ensureShapes();
    void writeGeometry();

    std::vector<float> values_;
    std::vector<uint32_t> barIds_;
    uint32_t baselineId_ = 0;
    Rect lastFrame_{};
};

} // namespace demo
