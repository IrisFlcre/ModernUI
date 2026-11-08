// ============================================================================
// FluentInput.cpp — TextBox / PasswordBox / NumberBox 实现
// ============================================================================

#include "FluentInput.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <utility>

namespace modern_ui {
namespace fluent {
namespace {

constexpr int kKeyBackspace = 0x08;
constexpr int kKeyEnter = 0x0D;
constexpr int kKeyDelete = 0x2E;
constexpr int kKeyLeft = 0x25;
constexpr int kKeyUp = 0x26;
constexpr int kKeyRight = 0x27;
constexpr int kKeyDown = 0x28;
constexpr int kKeyHome = 0x24;
constexpr int kKeyEnd = 0x23;
constexpr int kKeyPageUp = 0x21;
constexpr int kKeyPageDown = 0x22;

void appendUtf8(std::string& out, uint32_t codepoint) {
    if (codepoint <= 0x7Fu) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0x10FFFFu) {
        out.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
}

void popUtf8(std::string& text) {
    if (text.empty()) return;
    size_t index = text.size();
    while (index > 0 &&
           (static_cast<unsigned char>(text[index - 1]) & 0xC0u) == 0x80u) {
        --index;
    }
    if (index > 0) --index;
    text.erase(index);
}

size_t utf8Length(const std::string& text) {
    size_t count = 0;
    for (unsigned char ch : text) {
        if ((ch & 0xC0u) != 0x80u) ++count;
    }
    return count;
}

std::string repeatUtf8(const std::string& unit, size_t count) {
    std::string result;
    result.reserve(unit.size() * count);
    for (size_t i = 0; i < count; ++i) result += unit;
    return result;
}

} // namespace

// ===========================================================================
// TextBox
// ===========================================================================
TextBox::TextBox(std::string placeholder, int32_t fontSize)
    : placeholder_(std::move(placeholder)), fontSize_(fontSize) {
    Shape background = makeRRectShape(
        Rect{0, 0, 1, 1}, theme().radiusControl,
        theme().controlFill, theme().stroke, 1);
    backgroundId_ = canvas().addShape(std::move(background));

    Shape text = makeTextShape({}, fontSize_, theme().textTertiary, 0, 0);
    textId_ = canvas().addShape(std::move(text));

    Shape caret = makeRectShape(Rect{0, 0, 1, 1}, theme().accent);
    caret.base.visible = false;
    caretId_ = canvas().addShape(std::move(caret));

    Shape underline = makeRectShape(Rect{0, 0, 1, 2}, theme().accent);
    underline.base.visible = false;
    underlineId_ = canvas().addShape(std::move(underline));
}

void TextBox::setText(std::string text) {
    if (text == text_) return;
    text_ = std::move(text);
    measured_ = Rect::null();
    resetCaretBlink();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void TextBox::setPlaceholder(std::string placeholder) {
    placeholder_ = std::move(placeholder);
    measured_ = Rect::null();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void TextBox::setFontSize(int32_t fontSize) {
    if (fontSize == fontSize_) return;
    fontSize_ = std::max(1, fontSize);
    measured_ = Rect::null();
    invalidateLayout();
}

void TextBox::resetCaretBlink() {
    caretTimer_ = 0.0f;
    caretVisible_ = true;
}

void TextBox::notifyChanged() {
    if (onTextChanged) onTextChanged(text_);
}

Vec2i TextBox::onMeasure(const Vec2i& available) const {
    (void)available;
    const std::string visible = visualText().empty() ? placeholder_ : visualText();
    measured_ = measureString(visible, fontSize_);
    return Vec2i{std::max(120, measured_.w + 24), theme().controlHeight};
}

void TextBox::updateVisuals() {
    if (!laidOut()) return;

    const std::string visual = visualText();
    const bool showPlaceholder = visual.empty();
    const std::string shown = showPlaceholder ? placeholder_ : visual;
    const Rect measured = measureString(shown, fontSize_);
    measured_ = measured;

    const Color fill = enabled()
        ? interactiveFill(theme().controlFill, theme().controlFillHover, theme().controlFillPressed)
        : theme().controlFillDisabled;
    Color stroke = focusStroke(theme().stroke);
    if (!enabled()) stroke = theme().stroke;

    const int32_t frameW = frameWidth();
    const int32_t frameH = frameHeight();

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, frameW, frameH,
                                theme().radiusControl, theme().radiusControl};
        shape.base.fill = fill;
        shape.base.stroke = stroke;
        shape.base.strokeWidth = 1;
    });

    const int32_t innerWidth = std::max(1, frameW - 24);
    const int32_t overflow = std::max(0, measured.w - innerWidth);
    const int32_t textX = 12 - overflow;
    const int32_t baseline = baselineForCenter(measured, frameH);
    const Color textColor = !enabled()
        ? theme().textDisabled
        : (showPlaceholder ? theme().textTertiary : theme().textPrimary);

    canvas().updateShape(textId_, [&](Shape& shape) {
        setShapeText(shape, shown, fontSize_, textColor, textX, baseline);
    });

    const int32_t caretX = std::min(frameW - 12,
                                    std::max(11, 12 + measured.w - overflow));
    const bool caretVisible = enabled() && focused() && caretVisible_;
    canvas().updateShape(caretId_, [&](Shape& shape) {
        shape.data = RectShape{caretX, 8, 1, std::max(0, frameH - 16)};
        shape.base.fill = theme().accent;
        shape.base.visible = caretVisible;
    });

    canvas().updateShape(underlineId_, [&](Shape& shape) {
        shape.data = RectShape{1, std::max(0, frameH - 2), std::max(0, frameW - 2), 2};
        shape.base.fill = withOpacity(theme().accent, focusAmount());
        shape.base.visible = focusAmount() > 0.01f;
    });
}

void TextBox::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);

    if (!focused()) {
        if (caretVisible_) {
            caretVisible_ = false;
            if (laidOut()) updateVisuals();
        }
        return;
    }

    const float dt = std::max(0.0f, dtSeconds);
    if (dt <= 0.0f) return;

    caretTimer_ += dt;
    if (caretTimer_ >= 0.53f) {
        caretTimer_ = 0.0f;
        caretVisible_ = !caretVisible_;
        if (laidOut()) updateVisuals();
    }
}

bool TextBox::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseDown:
        resetCaretBlink();
        if (laidOut()) updateVisuals();
        return true;

    case EventType::KeyDown:
        if (ev.key == kKeyBackspace) {
            if (!text_.empty()) {
                popUtf8(text_);
                resetCaretBlink();
                notifyChanged();
                measured_ = Rect::null();
                invalidateLayout();
                if (laidOut()) updateVisuals();
            }
            return true;
        }
        if (ev.key == kKeyDelete) {
            if (!text_.empty()) {
                text_.clear();
                resetCaretBlink();
                notifyChanged();
                measured_ = Rect::null();
                invalidateLayout();
                if (laidOut()) updateVisuals();
            }
            return true;
        }
        if (ev.key == kKeyEnter) {
            if (onSubmit) onSubmit(text_);
            resetCaretBlink();
            return true;
        }
        return false;

    case EventType::Char:
        if (ev.codepoint >= 32 && ev.codepoint != 127) {
            appendUtf8(text_, ev.codepoint);
            resetCaretBlink();
            notifyChanged();
            measured_ = Rect::null();
            invalidateLayout();
            if (laidOut()) updateVisuals();
        }
        return true;

    case EventType::MouseUp:
        return FluentControl::onEvent(ev);

    default:
        return false;
    }
}

void TextBox::onStateChanged() {
    if (focused() && !wasFocused_) resetCaretBlink();
    if (!focused()) caretVisible_ = false;
    wasFocused_ = focused();
    if (laidOut()) updateVisuals();
}

// ===========================================================================
// PasswordBox
// ===========================================================================
PasswordBox::PasswordBox(std::string placeholder)
    : TextBox(std::move(placeholder)) {}

std::string PasswordBox::visualText() const {
    return repeatUtf8("\xE2\x97\x8F", utf8Length(rawText()));   // U+25CF ●
}

// ===========================================================================
// NumberBox
// ===========================================================================
NumberBox::NumberBox(double value, double minimum, double maximum, double step)
    : value_(value), minimum_(minimum), maximum_(maximum), step_(step > 0.0 ? step : 1.0) {
    if (maximum_ < minimum_) std::swap(minimum_, maximum_);
    value_ = std::max(minimum_, std::min(maximum_, value_));

    Shape background = makeRRectShape(
        Rect{0, 0, 1, 1}, theme().radiusControl,
        theme().controlFill, theme().stroke, 1);
    backgroundId_ = canvas().addShape(std::move(background));

    Shape minusArea = makeRRectShape(Rect{0, 0, 1, 1}, 4.0f, Color::nullColor());
    minusAreaId_ = canvas().addShape(std::move(minusArea));

    Shape plusArea = makeRRectShape(Rect{0, 0, 1, 1}, 4.0f, Color::nullColor());
    plusAreaId_ = canvas().addShape(std::move(plusArea));

    Shape minusIcon = makeMinusShape(0, 0, 10, theme().textSecondary, 1);
    minusIconId_ = canvas().addShape(std::move(minusIcon));

    Shape plusIcon = makePlusShape(0, 0, 10, theme().textSecondary, 1);
    plusIconId_ = canvas().addShape(std::move(plusIcon));

    Shape valueShape = makeTextShape(formattedValue(), 14, theme().textPrimary);
    valueId_ = canvas().addShape(std::move(valueShape));
}

std::string NumberBox::formattedValue() const {
    char buffer[64];
    if (decimals_ <= 0) {
        std::snprintf(buffer, sizeof(buffer), "%.0f", value_);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.*f", decimals_, value_);
    }
    return std::string(buffer);
}

void NumberBox::setValue(double value) {
    const double next = std::max(minimum_, std::min(maximum_, value));
    if (std::fabs(next - value_) < 1.0e-9) return;
    value_ = next;
    if (laidOut()) updateVisuals();
}

void NumberBox::setRange(double minimum, double maximum) {
    if (maximum < minimum) std::swap(minimum, maximum);
    minimum_ = minimum;
    maximum_ = maximum;
    value_ = std::max(minimum_, std::min(maximum_, value_));
    if (laidOut()) updateVisuals();
}

void NumberBox::setDecimals(int32_t decimals) {
    decimals_ = std::max(0, std::min(6, decimals));
    if (laidOut()) updateVisuals();
}

Vec2i NumberBox::onMeasure(const Vec2i& available) const {
    (void)available;
    measured_ = measureString(formattedValue(), 14);
    return Vec2i{std::max(140, measured_.w + 100), theme().controlHeight};
}

void NumberBox::updateVisuals() {
    if (!laidOut()) return;

    const int32_t frameW = frameWidth();
    const int32_t frameH = frameHeight();
    const int32_t sideWidth = std::min(32, frameW / 2);

    const Color fill = enabled()
        ? interactiveFill(theme().controlFill, theme().controlFillHover, theme().controlFillPressed)
        : theme().controlFillDisabled;
    Color stroke = focusStroke(theme().stroke);
    if (!enabled()) stroke = theme().stroke;

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, frameW, frameH,
                                theme().radiusControl, theme().radiusControl};
        shape.base.fill = fill;
        shape.base.stroke = stroke;
        shape.base.strokeWidth = 1;
    });

    const Color hoverFill = enabled()
        ? (hoverSide_ < 0 ? theme().subtleHover : Color::nullColor())
        : Color::nullColor();
    const Color hoverFillRight = enabled()
        ? (hoverSide_ > 0 ? theme().subtleHover : Color::nullColor())
        : Color::nullColor();

    canvas().updateShape(minusAreaId_, [&](Shape& shape) {
        shape.data = RRectShape{4, 4, sideWidth, frameH - 8, 4.0f, 4.0f};
        shape.base.fill = hoverFill;
        shape.base.visible = hoverFill.a > 0;
    });
    canvas().updateShape(plusAreaId_, [&](Shape& shape) {
        shape.data = RRectShape{frameW - sideWidth - 4, 4, sideWidth, frameH - 8, 4.0f, 4.0f};
        shape.base.fill = hoverFillRight;
        shape.base.visible = hoverFillRight.a > 0;
    });

    const Color iconColor = enabled() ? theme().textSecondary : theme().textDisabled;
    canvas().updateShape(minusIconId_, [&](Shape& shape) {
        shape = makeMinusShape(sideWidth / 2 + 4, frameH / 2, 10, iconColor, 1);
    });
    canvas().updateShape(plusIconId_, [&](Shape& shape) {
        shape = makePlusShape(frameW - sideWidth / 2 - 4, frameH / 2, 10, iconColor, 1);
    });

    const std::string text = formattedValue();
    const Rect measured = measureString(text, 14);
    measured_ = measured;
    const int32_t middleStart = sideWidth + 8;
    const int32_t middleWidth = std::max(1, frameW - sideWidth * 2 - 16);
    const int32_t textX = middleStart + std::max(0, (middleWidth - measured.w) / 2);
    const int32_t baseline = baselineForCenter(measured, frameH);
    const Color textColor = enabled() ? theme().textPrimary : theme().textDisabled;

    canvas().updateShape(valueId_, [&](Shape& shape) {
        setShapeText(shape, text, 14, textColor, textX, baseline);
    });
}

void NumberBox::commitValue(double value) {
    value = std::max(minimum_, std::min(maximum_, value));
    if (step_ > 0.0) {
        value = minimum_ + std::round((value - minimum_) / step_) * step_;
        value = std::max(minimum_, std::min(maximum_, value));
    }
    if (std::fabs(value - value_) < 1.0e-9) return;
    value_ = value;
    if (laidOut()) updateVisuals();
    if (onValueChanged) onValueChanged(value_);
}

int32_t NumberBox::sideAt(int32_t localX) const {
    const int32_t sideWidth = std::min(32, frameWidth() / 2);
    if (localX < sideWidth) return -1;
    if (localX >= frameWidth() - sideWidth) return 1;
    return 0;
}

bool NumberBox::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseMove: {
        const int32_t side = sideAt(ev.x);
        if (side != hoverSide_) {
            hoverSide_ = side;
            if (laidOut()) updateVisuals();
        }
        return false;
    }
    case EventType::MouseDown: {
        const int32_t side = sideAt(ev.x);
        if (side < 0) commitValue(value_ - step_);
        else if (side > 0) commitValue(value_ + step_);
        return true;
    }
    case EventType::KeyDown: {
        if (ev.key == kKeyUp || ev.key == kKeyRight) {
            commitValue(value_ + step_);
            return true;
        }
        if (ev.key == kKeyDown || ev.key == kKeyLeft) {
            commitValue(value_ - step_);
            return true;
        }
        if (ev.key == kKeyPageUp) {
            commitValue(value_ + step_ * 10.0);
            return true;
        }
        if (ev.key == kKeyPageDown) {
            commitValue(value_ - step_ * 10.0);
            return true;
        }
        if (ev.key == kKeyHome) {
            commitValue(minimum_);
            return true;
        }
        if (ev.key == kKeyEnd) {
            commitValue(maximum_);
            return true;
        }
        return false;
    }
    case EventType::MouseUp:
        return FluentControl::onEvent(ev);
    default:
        return false;
    }
}

void NumberBox::onStateChanged() {
    if (!hovered() && hoverSide_ != 0) {
        hoverSide_ = 0;
        if (laidOut()) updateVisuals();
    }
}

} // namespace fluent
} // namespace modern_ui
