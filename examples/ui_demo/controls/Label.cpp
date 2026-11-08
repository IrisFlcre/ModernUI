#include "Label.h"

#include <algorithm>
#include <utility>

namespace demo {

Label::Label(std::string text, int32_t fontSize, Color color)
    : text_(std::move(text)), fontSize_(fontSize), color_(color) {
    // 构造函数：创建几何体（子 Canvas 局部坐标，基准线在 y = 0）
    modern_ui::Shape shape;
    shape.base.fill = color_;
    modern_ui::TextShape textShape;
    textShape.text = text_;
    textShape.fontSize = fontSize_;
    textShape.x = 0;
    textShape.y = 0;
    shape.data = std::move(textShape);
    shapeId_ = canvas().addShape(std::move(shape));
}

Label::~Label() {
    // 析构函数：删除几何体前的准备 —— 释放文本缓冲（几何体由基类统一摘除 / 清空）
    text_.clear();
    text_.shrink_to_fit();
}

Rect Label::measureText() const {
    modern_ui::TextShape probe;
    probe.text = text_;
    probe.fontSize = fontSize_;
    canvas().measureTextShape(probe);
    if (probe.measured && !probe.measuredBounds.isNull()) return probe.measuredBounds;

    // 没有文本度量能力时的退化估计，避免布局塌陷
    const int32_t advance = std::max(1, static_cast<int32_t>(static_cast<float>(fontSize_) * 0.6f));
    return Rect{0, -fontSize_, std::max(1, static_cast<int32_t>(text_.size()) * advance + 2), fontSize_ + 4};
}

Vec2i Label::onMeasure(const Vec2i& available) const {
    (void)available;
    measured_ = measureText();
    return Vec2i{measured_.w, measured_.h};
}

void Label::onLayout(const Rect& frame) {
    lastFrame_ = frame;
    if (measured_.isNull()) measured_ = measureText();
    writeGeometry();
}

void Label::writeGeometry() {
    const Rect frame = lastFrame_;
    const Rect measured = measured_;

    // 文本垂直居中；左内边距由 style().padding 控制
    const int32_t baselineY = (frame.h - measured.h) / 2 - measured.y;
    const int32_t originX = style().padding.left - measured.x;

    canvas().updateShape(shapeId_, [&](modern_ui::Shape& shape) {
        auto& textShape = std::get<modern_ui::TextShape>(shape.data);
        textShape.text = text_;
        textShape.fontSize = fontSize_;
        textShape.x = originX;
        textShape.y = baselineY;
        shape.base.fill = color_;
    });
}

void Label::setText(std::string text) {
    if (text == text_) return;
    text_ = std::move(text);
    measured_ = Rect::null();
    invalidateLayout();   // 固有尺寸可能变化 → 交给 Flexbox 重新排版
}

void Label::setColor(Color color) {
    color_ = color;
    canvas().updateShape(shapeId_, [this](modern_ui::Shape& shape) {
        shape.base.fill = color_;
    });
}

} // namespace demo
