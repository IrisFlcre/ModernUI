#include "ProgressBar.h"

#include <algorithm>
#include <cmath>

namespace demo {
namespace {

constexpr int32_t kBarHeight = 10;

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

} // namespace

ProgressBar::ProgressBar(float value) : value_(clamp01(value)) {
    // 构造函数：初始化子 Canvas 并创建几何体 —— 轨道 + 填充（线性渐变）
    const float radius = static_cast<float>(kBarHeight) * 0.5f;

    modern_ui::Shape track;
    track.base.fill = Color{40, 45, 62, 255};
    track.base.stroke = Color{58, 66, 94, 160};
    track.base.strokeWidth = 1;
    track.data = modern_ui::RRectShape{0, 0, 1, kBarHeight, radius, radius};
    trackId_ = canvas().addShape(std::move(track));

    modern_ui::LinearGradient gradient;
    gradient.stops = {{0.0f, Color{86, 132, 255, 255}}, {1.0f, Color{120, 236, 214, 255}}};

    modern_ui::Shape fill;
    fill.base.fill = std::move(gradient);
    fill.data = modern_ui::RRectShape{0, 0, 1, kBarHeight, radius, radius};
    fillId_ = canvas().addShape(std::move(fill));
}

ProgressBar::~ProgressBar() {
    // 析构函数：删除几何体前的准备 —— 复位展示值
    value_ = 0.0f;
    lastFrame_ = Rect::null();
}

void ProgressBar::setValue(float value) {
    const float next = clamp01(value);
    if (std::fabs(next - value_) < 0.002f) return;
    value_ = next;
    writeGeometry();   // 只改本控件的子 Canvas → View 只会重新镜像这一个控件
}

Vec2i ProgressBar::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{160, kBarHeight};
}

void ProgressBar::onLayout(const Rect& frame) {
    lastFrame_ = frame;
    writeGeometry();
}

void ProgressBar::writeGeometry() {
    const Rect frame = lastFrame_;
    if (frame.w <= 0 || frame.h <= 0) return;

    const float radius = static_cast<float>(frame.h) * 0.5f;
    const int32_t fillWidth = std::max(0, static_cast<int32_t>(std::lround(static_cast<float>(frame.w) * value_)));

    canvas().updateShape(trackId_, [&](modern_ui::Shape& shape) {
        shape.data = modern_ui::RRectShape{0, 0, frame.w, frame.h, radius, radius};
    });
    canvas().updateShape(fillId_, [&](modern_ui::Shape& shape) {
        shape.data = modern_ui::RRectShape{0, 0, fillWidth, frame.h, radius, radius};
        shape.base.visible = fillWidth > 0;

        modern_ui::LinearGradient gradient;
        gradient.to = modern_ui::Vec2f{static_cast<float>(std::max(1, fillWidth)), 0.0f};
        gradient.stops = {{0.0f, Color{86, 132, 255, 255}}, {1.0f, Color{120, 236, 214, 255}}};
        shape.base.fill = std::move(gradient);
    });
}

} // namespace demo
