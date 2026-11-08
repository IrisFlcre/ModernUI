#include "Slider.h"

#include <algorithm>
#include <cmath>

namespace demo {
namespace {

constexpr int32_t kTrackHeight = 6;
constexpr int32_t kKnobRadius  = 9;

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

} // namespace

Slider::Slider(float value) : value_(clamp01(value)) {
    // 构造函数：初始化子 Canvas 并创建几何体 —— 轨道 / 已完成段 / 滑块
    const float radius = static_cast<float>(kTrackHeight) * 0.5f;

    modern_ui::Shape track;
    track.base.fill = Color{44, 49, 66, 255};
    track.data = modern_ui::RRectShape{0, 0, 1, kTrackHeight, radius, radius};
    trackId_ = canvas().addShape(std::move(track));

    modern_ui::Shape fill;
    fill.base.fill = Color{78, 138, 255, 255};
    fill.data = modern_ui::RRectShape{0, 0, 1, kTrackHeight, radius, radius};
    fillId_ = canvas().addShape(std::move(fill));

    modern_ui::Shape knob;
    knob.base.fill = Color{236, 242, 255, 255};
    knob.base.stroke = Color{64, 118, 255, 255};
    knob.base.strokeWidth = 2;
    knob.data = modern_ui::CircleShape{kKnobRadius, 0, static_cast<float>(kKnobRadius)};
    knobId_ = canvas().addShape(std::move(knob));
}

Slider::~Slider() {
    // 析构函数：删除几何体前的准备 —— 断开回调、结束拖动状态
    onValueChanged = nullptr;
    dragging_ = false;
    lastFrame_ = Rect::null();
}

void Slider::setValue(float value) {
    const float next = clamp01(value);
    if (std::fabs(next - value_) < 0.0005f) return;
    value_ = next;
    writeGeometry();
}

Vec2i Slider::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{180, kKnobRadius * 2 + 6};
}

void Slider::onLayout(const Rect& frame) {
    lastFrame_ = frame;
    writeGeometry();
}

void Slider::writeGeometry() {
    const Rect frame = lastFrame_;
    if (frame.w <= 0 || frame.h <= 0) return;

    const int32_t trackY = (frame.h - kTrackHeight) / 2;
    const float radius = static_cast<float>(kTrackHeight) * 0.5f;
    const int32_t span = std::max(0, frame.w - kKnobRadius * 2);
    const int32_t knobX = kKnobRadius + static_cast<int32_t>(std::lround(static_cast<float>(span) * value_));
    const int32_t fillWidth = std::max(0, knobX);

    canvas().updateShape(trackId_, [&](modern_ui::Shape& shape) {
        shape.data = modern_ui::RRectShape{0, trackY, frame.w, kTrackHeight, radius, radius};
    });
    canvas().updateShape(fillId_, [&](modern_ui::Shape& shape) {
        shape.data = modern_ui::RRectShape{0, trackY, fillWidth, kTrackHeight, radius, radius};
    });
    canvas().updateShape(knobId_, [&](modern_ui::Shape& shape) {
        shape.data = modern_ui::CircleShape{knobX, frame.h / 2, static_cast<float>(kKnobRadius)};
    });
    updateColors();
}

void Slider::updateColors() {
    const Color fillColor = pressed() ? Color{56, 112, 236, 255}
                                      : (hovered() ? Color{96, 156, 255, 255} : Color{78, 138, 255, 255});
    const Color knobStroke = focused() ? Color{178, 206, 255, 255} : Color{64, 118, 255, 255};

    canvas().updateShape(fillId_, [&](modern_ui::Shape& shape) {
        shape.base.fill = fillColor;
    });
    canvas().updateShape(knobId_, [&](modern_ui::Shape& shape) {
        shape.base.stroke = knobStroke;
        shape.base.strokeWidth = 2;
    });
}

void Slider::onStateChanged() {
    if (lastFrame_.isNull()) return;
    updateColors();
}

void Slider::applyValueFromLocalX(int32_t localX, bool notify) {
    const int32_t span = std::max(1, lastFrame_.w - kKnobRadius * 2);
    const float next = clamp01(static_cast<float>(localX - kKnobRadius) / static_cast<float>(span));
    if (std::fabs(next - value_) < 0.0005f) return;

    value_ = next;
    writeGeometry();
    if (notify && onValueChanged) onValueChanged(value_);
}

bool Slider::onEvent(const Event& ev) {
    switch (ev.type) {
    case modern_ui::EventType::MouseDown:
        dragging_ = true;
        applyValueFromLocalX(ev.x, true);
        return true;

    case modern_ui::EventType::MouseMove:
        if (!dragging_) return false;
        // 坐标可能已经是控件外的负值 / 超宽值（View 保持按压捕获），内部做钳制
        applyValueFromLocalX(ev.x, true);
        return true;

    case modern_ui::EventType::MouseUp:
        dragging_ = false;
        return true;

    default:
        return false;
    }
}

} // namespace demo
