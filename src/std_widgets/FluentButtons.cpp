// ============================================================================
// FluentButtons.cpp — 按钮族实现
// ============================================================================

#include "FluentButtons.h"

#include <algorithm>
#include <utility>

namespace modern_ui {
namespace fluent {
namespace {

constexpr int kPaddingX = 16;
constexpr int kChevronArea = 22;
constexpr int kKeySpace = 0x20;
constexpr int kKeyEnter = 0x0D;

} // namespace

// ===========================================================================
// Button
// ===========================================================================
Button::Button(std::string label, Style style, int32_t fontSize)
    : label_(std::move(label)), fontSize_(fontSize), style_(style) {
    Shape background = makeRRectShape(
        Rect{0, 0, 1, 1}, theme().radiusControl,
        theme().controlFill, theme().stroke, 1);
    backgroundId_ = canvas().addShape(std::move(background));

    Shape text = makeTextShape(label_, fontSize_, theme().textPrimary, 0, 0);
    labelId_ = canvas().addShape(std::move(text));

    Shape chevron = makeChevronShape(0, 0, 10, 6, true, theme().textPrimary, 2);
    chevron.base.visible = false;
    chevronId_ = canvas().addShape(std::move(chevron));
}

void Button::setLabel(std::string label) {
    if (label == label_) return;
    label_ = std::move(label);
    measured_ = Rect::null();
    invalidateLayout();
}

void Button::setButtonStyle(Style style) {
    if (style == style_) return;
    style_ = style;
    if (laidOut()) updateVisuals();
}

void Button::setContentAlign(ContentAlign align) {
    if (align == contentAlign_) return;
    contentAlign_ = align;
    measured_ = Rect::null();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void Button::setContentPadding(int32_t padding) {
    const int32_t next = std::max(0, padding);
    if (next == contentPadding_) return;
    contentPadding_ = next;
    measured_ = Rect::null();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void Button::setFontSize(int32_t fontSize) {
    if (fontSize == fontSize_) return;
    fontSize_ = std::max(1, fontSize);
    measured_ = Rect::null();
    invalidateLayout();
}

void Button::setShowChevron(bool show) {
    if (showChevron_ == show) return;
    showChevron_ = show;
    measured_ = Rect::null();
    invalidateLayout();
}

Vec2i Button::onMeasure(const Vec2i& available) const {
    (void)available;
    measured_ = measureString(label_, cssFontSize(fontSize_));
    int32_t width = std::max(theme().minControlWidth,
                             measured_.w + contentPadding_ * 2 + (showChevron_ ? kChevronArea : 0));
    return Vec2i{width, theme().controlHeight};
}

void Button::updateVisuals() {
    if (!laidOut()) return;

    const bool toggled = isToggled();
    const bool accent = style_ == Style::Accent || toggled;

    Color fill;
    Color stroke = Color::nullColor();
    Color textColor;

    if (!enabled()) {
        fill = accent ? theme().accentDisabled : theme().controlFillDisabled;
        stroke = theme().stroke;
        textColor = theme().textDisabled;
    } else if (accent) {
        fill = interactiveFill(theme().accent, theme().accentHover, theme().accentPressed);
        stroke = focusStroke(Color::nullColor());
        textColor = theme().textOnAccent;
    } else if (style_ == Style::Subtle) {
        fill = interactiveFill(Color::nullColor(), theme().subtleHover, theme().subtlePressed);
        stroke = focusStroke(Color::nullColor());
        textColor = theme().textPrimary;
    } else if (style_ == Style::Selected) {
        fill = interactiveFill(withOpacity(theme().accent, 0.16f),
                               withOpacity(theme().accent, 0.24f),
                               withOpacity(theme().accent, 0.20f));
        stroke = focusStroke(Color::nullColor());
        textColor = theme().textPrimary;
    } else {
        fill = interactiveFill(theme().controlFill, theme().controlFillHover,
                               theme().controlFillPressed);
        stroke = focusStroke(theme().stroke);
        textColor = theme().textPrimary;
    }

    const int32_t strokeWidth = focusAmount() > 0.15f ? 2 : 1;
    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, frameWidth(), frameHeight(),
                                theme().radiusControl, theme().radiusControl};
        shape.base.fill = fill;
        shape.base.stroke = stroke;
        shape.base.strokeWidth = strokeWidth;
        shape.base.visible = true;
    });

    const int32_t fontSize = cssFontSize(fontSize_);
    if (measured_.isNull()) measured_ = measureString(label_, fontSize);
    const int32_t contentWidth = frameWidth() - (showChevron_ ? kChevronArea : 0);
    const int32_t textX = contentAlign_ == ContentAlign::Left
        ? std::min(contentPadding_, std::max(0, contentWidth - measured_.w))
        : std::max(0, (contentWidth - measured_.w) / 2);
    const int32_t baseline = baselineForCenter(measured_, frameHeight());

    canvas().updateShape(labelId_, [&](Shape& shape) {
        setShapeText(shape, label_, fontSize, textColor, textX, baseline);
    });

    canvas().updateShape(chevronId_, [&](Shape& shape) {
        const int32_t chevronWidth = 9;
        const int32_t chevronHeight = 5;
        const int32_t x = frameWidth() - kChevronArea + (kChevronArea - chevronWidth) / 2;
        const int32_t y = (frameHeight() - chevronHeight) / 2 + 1;
        shape = makeChevronShape(x, y, chevronWidth, chevronHeight, true, textColor, 2);
        shape.base.visible = showChevron_;
    });
}

void Button::activate() {
    if (!enabled()) return;
    if (onClick) onClick();
}

// ===========================================================================
// ToggleButton
// ===========================================================================
ToggleButton::ToggleButton(std::string label, Style style, int32_t fontSize)
    : Button(std::move(label), style, fontSize) {}

void ToggleButton::setChecked(bool checked) {
    if (checked_ == checked) return;
    checked_ = checked;
    if (laidOut()) updateVisuals();
}

void ToggleButton::activate() {
    if (!enabled()) return;
    checked_ = !checked_;
    if (laidOut()) updateVisuals();
    if (onToggled) onToggled(checked_);
    if (onClick) onClick();
}

// ===========================================================================
// RepeatButton
// ===========================================================================
RepeatButton::RepeatButton(std::string label)
    : Button(std::move(label)) {}

void RepeatButton::fire() {
    if (!enabled()) return;
    if (onClick) onClick();
}

bool RepeatButton::onEvent(const Event& ev) {
    if (!enabled()) return false;

    if (ev.type == EventType::MouseDown) {
        repeating_ = true;
        fired_ = false;
        elapsed_ = 0.0f;
        return FluentControl::onEvent(ev);
    }

    if (ev.type == EventType::MouseUp) {
        repeating_ = false;
        const bool handled = FluentControl::onEvent(ev);
        if (clicked() && !fired_) fire();
        fired_ = false;
        return handled;
    }

    if (ev.type == EventType::KeyDown && (ev.key == kKeySpace || ev.key == kKeyEnter)) {
        fire();
        return true;
    }

    return FluentControl::onEvent(ev);
}

void RepeatButton::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);
    if (!repeating_ || !pressed()) {
        if (!pressed()) repeating_ = false;
        return;
    }

    elapsed_ += std::max(0.0f, dtSeconds);
    if (!fired_) {
        if (elapsed_ >= 0.42f) {
            fired_ = true;
            elapsed_ = 0.0f;
            fire();
        }
        return;
    }

    while (elapsed_ >= 0.08f) {
        elapsed_ -= 0.08f;
        fire();
    }
}

// ===========================================================================
// HyperlinkButton
// ===========================================================================
HyperlinkButton::HyperlinkButton(std::string label, int32_t fontSize)
    : label_(std::move(label)), fontSize_(fontSize) {
    Shape text = makeTextShape(label_, fontSize_, theme().accent, 0, 0);
    textId_ = canvas().addShape(std::move(text));

    Shape underline = makeRectShape(Rect{0, 0, 1, 1}, theme().accent);
    underlineId_ = canvas().addShape(std::move(underline));
}

void HyperlinkButton::setLabel(std::string label) {
    if (label == label_) return;
    label_ = std::move(label);
    measured_ = Rect::null();
    invalidateLayout();
}

Vec2i HyperlinkButton::onMeasure(const Vec2i& available) const {
    (void)available;
    measured_ = measureString(label_, fontSize_);
    return Vec2i{std::max(1, measured_.w), theme().controlHeight};
}

void HyperlinkButton::updateVisuals() {
    if (!laidOut()) return;
    if (measured_.isNull()) measured_ = measureString(label_, fontSize_);

    const Color accent = interactiveFill(theme().accent, theme().accentHover,
                                         theme().accentPressed);
    const Color color = enabled() ? accent : theme().textDisabled;
    const int32_t baseline = baselineForCenter(measured_, frameHeight());

    canvas().updateShape(textId_, [&](Shape& shape) {
        setShapeText(shape, label_, fontSize_, color, 0, baseline);
    });

    const int32_t underlineY = std::min(frameHeight() - 2,
                                        baseline + measured_.h - measured_.y + 2);
    const int32_t thickness = (hoverAmount() > 0.1f || focused()) ? 2 : 1;
    const Color underline = withOpacity(color, 0.35f + 0.65f * std::max(hoverAmount(), focusAmount()));

    canvas().updateShape(underlineId_, [&](Shape& shape) {
        shape.data = RectShape{0, underlineY, measured_.w, thickness};
        shape.base.fill = underline;
        shape.base.visible = enabled();
    });
}

// ===========================================================================
// DropDownButton
// ===========================================================================
DropDownButton::DropDownButton(std::string label)
    : Button(std::move(label), Style::Standard, 14) {
    setShowChevron(true);
}

void DropDownButton::activate() {
    if (!enabled()) return;
    if (onDropDown) onDropDown();
    else if (onClick) onClick();
}

// ===========================================================================
// SplitButton
// ===========================================================================
SplitButton::SplitButton(std::string label)
    : label_(std::move(label)) {
    Shape background = makeRRectShape(
        Rect{0, 0, 1, 1}, theme().radiusControl,
        theme().controlFill, theme().stroke, 1);
    backgroundId_ = canvas().addShape(std::move(background));

    Shape text = makeTextShape(label_, 14, theme().textPrimary, 0, 0);
    labelId_ = canvas().addShape(std::move(text));

    Shape divider = makeRectShape(Rect{0, 0, 1, 1}, theme().stroke);
    dividerId_ = canvas().addShape(std::move(divider));

    Shape chevron = makeChevronShape(0, 0, 9, 5, true, theme().textPrimary, 2);
    chevronId_ = canvas().addShape(std::move(chevron));
}

void SplitButton::setLabel(std::string label) {
    if (label == label_) return;
    label_ = std::move(label);
    measured_ = Rect::null();
    invalidateLayout();
}

Vec2i SplitButton::onMeasure(const Vec2i& available) const {
    (void)available;
    measured_ = measureString(label_, fontSize_);
    return Vec2i{std::max(theme().minControlWidth * 2,
                          measured_.w + kPaddingX * 2 + 32),
                 theme().controlHeight};
}

void SplitButton::updateVisuals() {
    if (!laidOut()) return;
    if (measured_.isNull()) measured_ = measureString(label_, fontSize_);

    const Color fill = enabled()
        ? interactiveFill(theme().controlFill, theme().controlFillHover, theme().controlFillPressed)
        : theme().controlFillDisabled;
    const Color stroke = focusStroke(theme().stroke);
    const Color textColor = enabled() ? theme().textPrimary : theme().textDisabled;
    const int32_t separatorX = std::max(0, frameWidth() - 32);

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, frameWidth(), frameHeight(),
                                theme().radiusControl, theme().radiusControl};
        shape.base.fill = fill;
        shape.base.stroke = stroke;
        shape.base.strokeWidth = focusAmount() > 0.15f ? 2 : 1;
    });

    const int32_t textX = std::max(0, (separatorX - measured_.w) / 2);
    const int32_t baseline = baselineForCenter(measured_, frameHeight());
    canvas().updateShape(labelId_, [&](Shape& shape) {
        setShapeText(shape, label_, fontSize_, textColor, textX, baseline);
    });

    canvas().updateShape(dividerId_, [&](Shape& shape) {
        shape.data = RectShape{separatorX, 6, 1, std::max(0, frameHeight() - 12)};
        shape.base.fill = theme().divider;
    });

    canvas().updateShape(chevronId_, [&](Shape& shape) {
        const int32_t cx = separatorX + 16;
        const int32_t cy = frameHeight() / 2;
        shape = makeChevronShape(cx - 4, cy - 2, 9, 5, true, textColor, 2);
    });
}

void SplitButton::primary() {
    if (onPrimaryClick) onPrimaryClick();
    else if (onClick) onClick();
}

void SplitButton::secondary() {
    if (onSecondaryClick) onSecondaryClick();
}

bool SplitButton::onEvent(const Event& ev) {
    if (!enabled()) return false;

    if (ev.type == EventType::MouseDown) return true;
    if (ev.type == EventType::MouseUp) {
        if (clicked()) {
            if (ev.x >= frameWidth() - 32) secondary();
            else primary();
        }
        return true;
    }
    if (ev.type == EventType::KeyDown && (ev.key == kKeySpace || ev.key == kKeyEnter)) {
        primary();
        return true;
    }
    return false;
}

} // namespace fluent
} // namespace modern_ui
