#include "Toggle.h"

#include <algorithm>

namespace demo {
namespace {

constexpr int32_t kTrackWidth  = 54;
constexpr int32_t kTrackHeight = 30;
constexpr int32_t kKnobInset   = 4;

} // namespace

Toggle::Toggle() {
    // 构造函数：初始化子 Canvas 并创建几何体 —— 轨道（圆角矩形）+ 滑块（圆）
    modern_ui::Shape track;
    track.base.fill = Color{54, 60, 82, 255};
    track.base.stroke = Color{92, 102, 132, 160};
    track.base.strokeWidth = 1;
    track.data = modern_ui::RRectShape{0, 0, kTrackWidth, kTrackHeight,
                                       static_cast<float>(kTrackHeight) * 0.5f,
                                       static_cast<float>(kTrackHeight) * 0.5f};
    trackId_ = canvas().addShape(std::move(track));

    modern_ui::Shape knob;
    knob.base.fill = Color{206, 214, 236, 255};
    knob.data = modern_ui::CircleShape{kKnobInset + 11, kTrackHeight / 2, 11.0f};
    knobId_ = canvas().addShape(std::move(knob));
}

Toggle::~Toggle() {
    // 析构函数：删除几何体前的准备 —— 断开回调并复位状态
    onChanged = nullptr;
    on_ = false;
    lastFrame_ = Rect::null();
}

void Toggle::setOn(bool on) {
    if (on_ == on) return;
    on_ = on;
    writeGeometry();
}

Vec2i Toggle::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{kTrackWidth, kTrackHeight};
}

void Toggle::onLayout(const Rect& frame) {
    lastFrame_ = frame;
    writeGeometry();
}

void Toggle::writeGeometry() {
    const Rect frame = lastFrame_;
    if (frame.w <= 0 || frame.h <= 0) return;

    const float radius = static_cast<float>(frame.h) * 0.5f;
    const int32_t knobRadius = std::max(3, frame.h / 2 - kKnobInset);
    const int32_t knobX = on_ ? frame.w - knobRadius - kKnobInset : knobRadius + kKnobInset;

    canvas().updateShape(trackId_, [&](modern_ui::Shape& shape) {
        shape.data = modern_ui::RRectShape{0, 0, frame.w, frame.h, radius, radius};
    });
    canvas().updateShape(knobId_, [&](modern_ui::Shape& shape) {
        shape.data = modern_ui::CircleShape{knobX, frame.h / 2, static_cast<float>(knobRadius)};
    });
    updateColors();
}

void Toggle::updateColors() {
    Color trackFill = on_ ? Color{64, 128, 255, 255} : Color{54, 60, 82, 255};
    if (hovered()) {
        trackFill = on_ ? Color{84, 148, 255, 255} : Color{68, 76, 102, 255};
    }
    if (pressed()) {
        trackFill = on_ ? Color{48, 108, 226, 255} : Color{46, 52, 72, 255};
    }
    const Color trackStroke = focused() ? Color{178, 206, 255, 220} : Color{92, 102, 132, 160};
    const Color knobFill = hovered() || pressed() ? Color{240, 246, 255, 255} : Color{206, 214, 236, 255};

    canvas().updateShape(trackId_, [&](modern_ui::Shape& shape) {
        shape.base.fill = trackFill;
        shape.base.stroke = trackStroke;
        shape.base.strokeWidth = 1;
    });
    canvas().updateShape(knobId_, [&](modern_ui::Shape& shape) {
        shape.base.fill = knobFill;
    });
}

void Toggle::onStateChanged() {
    if (lastFrame_.isNull()) return;
    updateColors();
}

bool Toggle::onEvent(const Event& ev) {
    switch (ev.type) {
    case modern_ui::EventType::MouseUp:
        if (clicked()) {
            on_ = !on_;
            writeGeometry();          // 事件直接修改几何体：滑块位置 + 配色
            if (onChanged) onChanged(on_);
        }
        return true;

    case modern_ui::EventType::MouseDown:
    case modern_ui::EventType::MouseLeave:
        return true;

    default:
        return false;
    }
}

} // namespace demo
