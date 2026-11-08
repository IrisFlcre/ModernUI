#include "BarChart.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace demo {
namespace {

constexpr int32_t kMinBarWidth = 3;
constexpr int32_t kBarGap = 6;

modern_ui::Color barColor(size_t index) {
    switch (index % 4) {
    case 0: return modern_ui::Color{88, 140, 255, 255};
    case 1: return modern_ui::Color{94, 226, 202, 255};
    case 2: return modern_ui::Color{170, 126, 255, 255};
    default: return modern_ui::Color{255, 186, 110, 255};
    }
}

} // namespace

BarChart::BarChart(std::vector<float> values) : values_(std::move(values)) {
    // 构造函数：创建基线 + 对应数量的柱子几何体
    modern_ui::Shape baseline;
    baseline.base.stroke = modern_ui::Color{70, 78, 104, 160};
    baseline.base.strokeWidth = 1;
    baseline.data = modern_ui::LineShape{0, 1, 1, 1};
    baselineId_ = canvas().addShape(std::move(baseline));

    ensureShapes();
}

BarChart::~BarChart() {
    // 析构函数：删除几何体前的准备 —— 清空数据与柱 id 表
    values_.clear();
    values_.shrink_to_fit();
    barIds_.clear();
    lastFrame_ = Rect::null();
}

void BarChart::ensureShapes() {
    while (barIds_.size() < values_.size()) {
        modern_ui::Shape bar;
        bar.base.fill = barColor(barIds_.size());
        bar.base.stroke = modern_ui::Color{255, 255, 255, 30};
        bar.base.strokeWidth = 1;
        bar.data = modern_ui::RRectShape{0, 0, 1, 1, 4.0f, 4.0f};
        barIds_.push_back(canvas().addShape(std::move(bar)));
    }
    while (barIds_.size() > values_.size()) {
        canvas().removeShape(barIds_.back());
        barIds_.pop_back();
    }
}

void BarChart::setValues(std::vector<float> values) {
    for (float& value : values) value = std::max(0.0f, std::min(1.0f, value));
    values_ = std::move(values);
    ensureShapes();
    writeGeometry();
}

Vec2i BarChart::onMeasure(const Vec2i& available) const {
    return Vec2i{std::max(160, std::min(available.x, 420)), 150};
}

void BarChart::onLayout(const Rect& frame) {
    lastFrame_ = frame;
    writeGeometry();
}

void BarChart::writeGeometry() {
    const Rect frame = lastFrame_;
    if (frame.w <= 0 || frame.h <= 0 || values_.empty()) return;

    const int32_t usableHeight = std::max(1, frame.h - 4);
    const int32_t count = static_cast<int32_t>(values_.size());
    const int32_t totalGap = kBarGap * std::max(0, count - 1);
    const int32_t barWidth = std::max(kMinBarWidth, (frame.w - totalGap) / count);

    for (int32_t i = 0; i < count; ++i) {
        const float value = values_[static_cast<size_t>(i)];
        const int32_t height = value <= 0.0f
            ? 0
            : std::max(2, static_cast<int32_t>(std::lround(static_cast<float>(usableHeight) * value)));
        const int32_t x = i * (barWidth + kBarGap);
        const int32_t y = frame.h - height;

        canvas().updateShape(barIds_[static_cast<size_t>(i)], [&](modern_ui::Shape& shape) {
            shape.data = modern_ui::RRectShape{x, y, barWidth, height, 4.0f, 4.0f};
            shape.base.visible = height > 0;
            shape.base.fill = barColor(static_cast<size_t>(i));
        });
    }

    canvas().updateShape(baselineId_, [&](modern_ui::Shape& shape) {
        shape.data = modern_ui::LineShape{0, frame.h, frame.w, frame.h};
        shape.base.stroke = modern_ui::Color{70, 78, 104, 160};
        shape.base.strokeWidth = 1;
    });
}

} // namespace demo
