#include "Button.h"

#include <algorithm>
#include <utility>

namespace demo {
namespace {

constexpr int32_t kPaddingX = 18;
constexpr int32_t kPaddingY = 10;
constexpr float   kRadius   = 10.0f;

} // namespace

Button::Button(std::string label, int32_t fontSize)
    : label_(std::move(label)), fontSize_(fontSize) {
    // 构造函数：初始化子 Canvas 并创建几何体（全部是相对坐标）
    modern_ui::Shape background;
    background.base.fill = fillBase_;
    background.base.stroke = Color{120, 156, 255, 80};
    background.base.strokeWidth = 1;
    background.data = modern_ui::RRectShape{0, 0, 1, 1, kRadius, kRadius};
    backgroundId_ = canvas().addShape(std::move(background));

    modern_ui::Shape text;
    text.base.fill = Color{238, 244, 255, 255};
    modern_ui::TextShape textShape;
    textShape.text = label_;
    textShape.fontSize = fontSize_;
    text.data = std::move(textShape);
    labelId_ = canvas().addShape(std::move(text));
}

Button::~Button() {
    // 析构函数：删除几何体前的准备 —— 先断开外部回调（回调可能引用其它控件）
    onClick = nullptr;
    label_.clear();
    label_.shrink_to_fit();
}

void Button::setLabel(std::string label) {
    if (label == label_) return;
    label_ = std::move(label);
    measured_ = Rect::null();
    invalidateLayout();
}

void Button::setColors(Color base, Color hover, Color pressed) {
    fillBase_ = base;
    fillHover_ = hover;
    fillPressed_ = pressed;
    updateColors();
}

Rect Button::measureText() const {
    modern_ui::TextShape probe;
    probe.text = label_;
    probe.fontSize = fontSize_;
    canvas().measureTextShape(probe);
    if (probe.measured && !probe.measuredBounds.isNull()) return probe.measuredBounds;

    const int32_t advance = std::max(1, static_cast<int32_t>(static_cast<float>(fontSize_) * 0.6f));
    return Rect{0, -fontSize_, std::max(1, static_cast<int32_t>(label_.size()) * advance + 2), fontSize_ + 4};
}

Vec2i Button::onMeasure(const Vec2i& available) const {
    (void)available;
    measured_ = measureText();
    return Vec2i{measured_.w + kPaddingX * 2, measured_.h + kPaddingY * 2};
}

void Button::onLayout(const Rect& frame) {
    lastFrame_ = frame;
    if (measured_.isNull()) measured_ = measureText();
    writeGeometry();
}

void Button::writeGeometry() {
    const Rect frame = lastFrame_;
    const Rect measured = measured_;

    const int32_t baselineY = (frame.h - measured.h) / 2 - measured.y;
    const int32_t originX = (frame.w - measured.w) / 2 - measured.x;

    canvas().updateShape(backgroundId_, [&](modern_ui::Shape& shape) {
        shape.data = modern_ui::RRectShape{0, 0, frame.w, frame.h, kRadius, kRadius};
    });
    canvas().updateShape(labelId_, [&](modern_ui::Shape& shape) {
        auto& textShape = std::get<modern_ui::TextShape>(shape.data);
        textShape.text = label_;
        textShape.fontSize = fontSize_;
        textShape.x = originX;
        textShape.y = baselineY;
    });
    updateColors();
}

void Button::updateColors() {
    const Color fill = pressed() ? fillPressed_ : (hovered() ? fillHover_ : fillBase_);
    const Color stroke = focused() ? Color{178, 206, 255, 220}
                                   : (hovered() ? Color{150, 188, 255, 150} : Color{120, 156, 255, 80});

    canvas().updateShape(backgroundId_, [&](modern_ui::Shape& shape) {
        shape.base.fill = fill;
        shape.base.stroke = stroke;
        shape.base.strokeWidth = 1;
    });
}

void Button::onStateChanged() {
    if (lastFrame_.isNull()) return;
    updateColors();
}

bool Button::onEvent(const Event& ev) {
    switch (ev.type) {
    case modern_ui::EventType::MouseUp:
        // 按下与抬起都在本控件内 → 点击
        if (clicked() && onClick) onClick();
        return true;

    case modern_ui::EventType::MouseDown:
    case modern_ui::EventType::MouseLeave:
        // 事件本身不改变几何体；状态由 View 更新后经 onStateChanged 同步
        return true;

    default:
        return false;
    }
}

} // namespace demo
