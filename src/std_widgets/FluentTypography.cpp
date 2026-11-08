// ============================================================================
// FluentTypography.cpp — TextBlock / Divider
// ============================================================================

#include "FluentTypography.h"

#include <algorithm>
#include <utility>

namespace modern_ui {
namespace fluent {

// ===========================================================================
// TextBlock
// ===========================================================================
TextBlock::TextBlock(std::string text, int32_t fontSize, Color color)
    : text_(std::move(text)), fontSize_(fontSize), color_(color) {
    Shape shape = makeTextShape(text_, fontSize_, currentTheme().textPrimary, 0, 0);
    shapeId_ = canvas().addShape(std::move(shape));
}

void TextBlock::setText(std::string text) {
    if (text == text_) return;
    text_ = std::move(text);
    measured_ = Rect::null();
    invalidateLayout();
}

void TextBlock::setFontSize(int32_t fontSize) {
    if (fontSize == fontSize_) return;
    fontSize_ = std::max(1, fontSize);
    measured_ = Rect::null();
    invalidateLayout();
}

void TextBlock::setColor(Color color) {
    color_ = color;
    if (laidOut()) updateVisuals();
}

Vec2i TextBlock::onMeasure(const Vec2i& available) const {
    (void)available;
    measured_ = measureString(text_, cssFontSize(fontSize_));
    return Vec2i{std::max(0, measured_.w), std::max(0, measured_.h)};
}

void TextBlock::updateVisuals() {
    if (!laidOut()) return;

    const int32_t fontSize = cssFontSize(fontSize_);
    const Rect measured = measured_.isNull() ? measureString(text_, fontSize) : measured_;
    const Color color = color_.a > 0 ? color_ : theme().textPrimary;

    int32_t x = 0;
    if (align_ == Align::Center) x = (frameWidth() - measured.w) / 2;
    else if (align_ == Align::Right) x = frameWidth() - measured.w;

    const int32_t baseline = frameHeight() > 0
        ? baselineForCenter(measured, frameHeight())
        : -measured.y;

    canvas().updateShape(shapeId_, [&](Shape& shape) {
        setShapeText(shape, text_, fontSize, color, x, baseline);
    });
}

// ===========================================================================
// Divider
// ===========================================================================
Divider::Divider(bool vertical) : vertical_(vertical) {
    Shape line = makeRectShape(Rect{0, 0, 1, 1}, currentTheme().divider);
    lineId_ = canvas().addShape(std::move(line));
}

Vec2i Divider::onMeasure(const Vec2i& available) const {
    return vertical_ ? Vec2i{1, std::max(0, available.y)}
                     : Vec2i{std::max(0, available.x), 1};
}

void Divider::updateVisuals() {
    if (!laidOut()) return;
    canvas().updateShape(lineId_, [&](Shape& shape) {
        shape.data = vertical_
            ? RectShape{frameWidth() / 2, 0, 1, frameHeight()}
            : RectShape{0, frameHeight() / 2, frameWidth(), 1};
        shape.base.fill = theme().divider;
    });
}

} // namespace fluent
} // namespace modern_ui
