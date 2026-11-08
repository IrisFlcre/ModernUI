// ============================================================================
// FluentProgress.cpp — ProgressBar / ProgressRing / InfoBadge / InfoBar
// ============================================================================

#include "FluentProgress.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace modern_ui {
namespace fluent {
namespace {

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

Color readableOn(const Color& background) {
    const float luminance = 0.2126f * static_cast<float>(background.r) +
                            0.7152f * static_cast<float>(background.g) +
                            0.0722f * static_cast<float>(background.b);
    return luminance > 150.0f ? Color{0, 0, 0, 255} : Color{255, 255, 255, 255};
}

} // namespace

// ===========================================================================
// ProgressBar
// ===========================================================================
ProgressBar::ProgressBar(float progress, bool indeterminate)
    : progress_(clamp01(progress)), indeterminate_(indeterminate) {
    Shape track = makeRRectShape(Rect{0, 0, 1, 4}, 2.0f,
                                 withOpacity(currentTheme().textPrimary, 0.16f));
    trackId_ = canvas().addShape(std::move(track));

    Shape primary = makeRRectShape(Rect{0, 0, 1, 4}, 2.0f, currentTheme().accent);
    primaryId_ = canvas().addShape(std::move(primary));

    Shape secondary = makeRRectShape(Rect{0, 0, 1, 4}, 2.0f,
                                     withOpacity(currentTheme().accent, 0.55f));
    secondary.base.visible = false;
    secondaryId_ = canvas().addShape(std::move(secondary));
}

void ProgressBar::setProgress(float progress) {
    const float next = clamp01(progress);
    if (std::fabs(next - progress_) < 1.0e-6f) return;
    progress_ = next;
    if (laidOut()) updateVisuals();
}

void ProgressBar::setIndeterminate(bool indeterminate) {
    if (indeterminate_ == indeterminate) return;
    indeterminate_ = indeterminate;
    phase_ = 0.0f;
    if (laidOut()) updateVisuals();
}

Vec2i ProgressBar::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{220, theme().controlHeight};
}

void ProgressBar::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (width <= 0 || height <= 0) return;

    const int32_t barHeight = 4;
    const int32_t trackY = (height - barHeight) / 2;

    canvas().updateShape(trackId_, [&](Shape& shape) {
        shape.data = RRectShape{0, trackY, width, barHeight, 2.0f, 2.0f};
        shape.base.fill = withOpacity(theme().textPrimary, 0.16f);
        shape.base.visible = true;
    });

    if (indeterminate_) {
        const int32_t primaryWidth = std::max(24, static_cast<int32_t>(width * 0.32f));
        const int32_t secondaryWidth = std::max(16, static_cast<int32_t>(width * 0.18f));

        const float t1 = std::fmod(phase_, 1.0f);
        const float t2 = std::fmod(phase_ + 0.44f, 1.0f);
        const int32_t x1 = static_cast<int32_t>(-primaryWidth + t1 * (width + primaryWidth));
        const int32_t x2 = static_cast<int32_t>(-secondaryWidth + t2 * (width + secondaryWidth));

        canvas().updateShape(primaryId_, [&](Shape& shape) {
            shape.data = RRectShape{x1, trackY, primaryWidth, barHeight, 2.0f, 2.0f};
            shape.base.fill = theme().accent;
            shape.base.visible = true;
        });
        canvas().updateShape(secondaryId_, [&](Shape& shape) {
            shape.data = RRectShape{x2, trackY, secondaryWidth, barHeight, 2.0f, 2.0f};
            shape.base.fill = withOpacity(theme().accent, 0.5f);
            shape.base.visible = true;
        });
    } else {
        const int32_t fillWidth = static_cast<int32_t>(std::lround(width * progress_));
        canvas().updateShape(primaryId_, [&](Shape& shape) {
            shape.data = RRectShape{0, trackY, fillWidth, barHeight, 2.0f, 2.0f};
            shape.base.fill = theme().accent;
            shape.base.visible = fillWidth > 0;
        });
        canvas().updateShape(secondaryId_, [&](Shape& shape) {
            shape.base.visible = false;
        });
    }
}

void ProgressBar::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);
    if (!indeterminate_) return;

    phase_ += std::max(0.0f, dtSeconds) * 0.55f;
    if (phase_ >= 1.0f) phase_ -= std::floor(phase_);
    if (laidOut()) updateVisuals();
}

// ===========================================================================
// ProgressRing
// ===========================================================================
ProgressRing::ProgressRing(float progress, bool indeterminate)
    : progress_(clamp01(progress)), indeterminate_(indeterminate) {
    Shape track = makeArcShape(0, 0, 1.0f, 0.0f, 360.0f,
                               withOpacity(currentTheme().textPrimary, 0.16f), 3);
    trackId_ = canvas().addShape(std::move(track));

    Shape arc = makeArcShape(0, 0, 1.0f, -90.0f, progress_ * 360.0f,
                             currentTheme().accent, 3);
    arcId_ = canvas().addShape(std::move(arc));

    Shape secondary = makeArcShape(0, 0, 1.0f, 0.0f, 45.0f,
                                   withOpacity(currentTheme().accent, 0.5f), 3);
    secondary.base.visible = false;
    arcSecondaryId_ = canvas().addShape(std::move(secondary));
}

void ProgressRing::setProgress(float progress) {
    const float next = clamp01(progress);
    if (std::fabs(next - progress_) < 1.0e-6f) return;
    progress_ = next;
    if (laidOut()) updateVisuals();
}

void ProgressRing::setIndeterminate(bool indeterminate) {
    if (indeterminate_ == indeterminate) return;
    indeterminate_ = indeterminate;
    phase_ = 0.0f;
    if (laidOut()) updateVisuals();
}

Vec2i ProgressRing::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{40, 40};
}

void ProgressRing::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (width <= 0 || height <= 0) return;

    const int32_t cx = width / 2;
    const int32_t cy = height / 2;
    const float radius = std::max(1.0f, static_cast<float>(std::min(width, height)) * 0.5f -
                                       static_cast<float>(strokeWidth_) * 0.5f - 1.0f);

    canvas().updateShape(trackId_, [&](Shape& shape) {
        shape.data = ArcShape{cx, cy, radius, radius, 0.0f, 360.0f};
        shape.base.stroke = withOpacity(theme().textPrimary, 0.16f);
        shape.base.strokeWidth = strokeWidth_;
        shape.base.visible = true;
    });

    if (indeterminate_) {
        const float start = -90.0f + phase_ * 360.0f;
        canvas().updateShape(arcId_, [&](Shape& shape) {
            shape.data = ArcShape{cx, cy, radius, radius, start, 100.0f};
            shape.base.stroke = theme().accent;
            shape.base.strokeWidth = strokeWidth_;
            shape.base.visible = true;
        });
        canvas().updateShape(arcSecondaryId_, [&](Shape& shape) {
            shape.data = ArcShape{cx, cy, radius, radius, start + 150.0f, 44.0f};
            shape.base.stroke = withOpacity(theme().accent, 0.5f);
            shape.base.strokeWidth = strokeWidth_;
            shape.base.visible = true;
        });
    } else {
        canvas().updateShape(arcId_, [&](Shape& shape) {
            shape.data = ArcShape{cx, cy, radius, radius, -90.0f, progress_ * 360.0f};
            shape.base.stroke = theme().accent;
            shape.base.strokeWidth = strokeWidth_;
            shape.base.visible = progress_ > 0.0f;
        });
        canvas().updateShape(arcSecondaryId_, [&](Shape& shape) {
            shape.base.visible = false;
        });
    }
}

void ProgressRing::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);
    if (!indeterminate_) return;

    phase_ += std::max(0.0f, dtSeconds) * 1.0f;
    if (phase_ >= 1.0f) phase_ -= std::floor(phase_);
    if (laidOut()) updateVisuals();
}

// ===========================================================================
// RadialGauge
// ===========================================================================
RadialGauge::RadialGauge(float value, std::string label)
    : value_(clamp01(value)), display_(clamp01(value)), label_(std::move(label)) {
    Shape track = makeArcShape(0, 0, 1.0f, -90.0f, 360.0f,
                               withOpacity(currentTheme().textPrimary, 0.14f), 8);
    trackId_ = canvas().addShape(std::move(track));

    Shape arc = makeArcShape(0, 0, 1.0f, -90.0f, 0.0f, currentTheme().accent, 8);
    arcId_ = canvas().addShape(std::move(arc));

    Shape glow = makeCircleShape(0, 0, 1.0f, withOpacity(currentTheme().accent, 0.20f));
    glowId_ = canvas().addShape(std::move(glow));

    Shape cap = makeCircleShape(0, 0, 1.0f, currentTheme().accentHover);
    capId_ = canvas().addShape(std::move(cap));

    Shape valueText = makeTextShape("0%", 22, currentTheme().textPrimary);
    valueId_ = canvas().addShape(std::move(valueText));

    Shape labelText = makeTextShape(label_, 11, currentTheme().textSecondary);
    labelId_ = canvas().addShape(std::move(labelText));
}

void RadialGauge::setValue(float value) {
    const float next = clamp01(value);
    if (std::fabs(next - value_) < 1.0e-6f) return;
    value_ = next;
    if (laidOut()) updateVisuals();
}

void RadialGauge::setLabel(std::string label) {
    if (label == label_) return;
    label_ = std::move(label);
    labelMeasured_ = Rect::null();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

Vec2i RadialGauge::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{140, 140};
}

void RadialGauge::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (width <= 0 || height <= 0) return;

    const int32_t size = std::min(width, height);
    const int32_t cx = width / 2;
    const int32_t cy = height / 2;
    const int32_t stroke = std::max(3, size / 16);
    const float radius = std::max(1.0f, static_cast<float>(size) * 0.5f - static_cast<float>(stroke) * 1.6f);

    canvas().updateShape(trackId_, [&](Shape& shape) {
        shape.data = ArcShape{cx, cy, radius, radius, -90.0f, 360.0f};
        shape.base.stroke = withOpacity(theme().textPrimary, 0.14f);
        shape.base.strokeWidth = stroke;
        shape.base.visible = true;
    });

    const float sweep = 360.0f * std::max(0.0f, std::min(1.0f, display_));
    LinearGradient gradient;
    gradient.from = Vec2f{static_cast<float>(cx) - radius, static_cast<float>(cy) - radius};
    gradient.to = Vec2f{static_cast<float>(cx) + radius, static_cast<float>(cy) + radius};
    gradient.stops = {
        GradientStop{0.0f, theme().accent},
        GradientStop{0.55f, mixColor(theme().accent, theme().info, 0.55f)},
        GradientStop{1.0f, theme().info}
    };

    canvas().updateShape(arcId_, [&](Shape& shape) {
        shape.data = ArcShape{cx, cy, radius, radius, -90.0f, sweep};
        shape.base.stroke = Paint::linear(gradient);
        shape.base.strokeWidth = stroke;
        shape.base.visible = sweep > 0.1f;
    });

    const float angle = (-90.0f + sweep) * 3.14159265f / 180.0f;
    const int32_t capX = cx + static_cast<int32_t>(std::lround(radius * std::cos(angle)));
    const int32_t capY = cy + static_cast<int32_t>(std::lround(radius * std::sin(angle)));
    canvas().updateShape(glowId_, [&](Shape& shape) {
        shape.data = CircleShape{capX, capY, static_cast<float>(stroke) * 1.7f};
        shape.base.fill = withOpacity(theme().accent, 0.22f);
        shape.base.visible = sweep > 0.1f;
    });
    canvas().updateShape(capId_, [&](Shape& shape) {
        shape.data = CircleShape{capX, capY, static_cast<float>(stroke) * 0.55f};
        shape.base.fill = enabled() ? theme().accentHover : theme().accentDisabled;
        shape.base.visible = sweep > 0.1f;
    });

    const int32_t valueFont = std::max(14, size / 6);
    const std::string valueText = std::to_string(
        static_cast<int32_t>(std::lround(display_ * 100.0f))) + "%";
    valueMeasured_ = measureString(valueText, valueFont);
    const int32_t valueBaseline = cy - valueMeasured_.h / 2 - valueMeasured_.y -
                                  (label_.empty() ? 0 : 4);
    canvas().updateShape(valueId_, [&](Shape& shape) {
        setShapeText(shape, valueText, valueFont, theme().textPrimary,
                     cx - valueMeasured_.w / 2, valueBaseline);
    });

    if (label_.empty()) {
        canvas().updateShape(labelId_, [&](Shape& shape) { shape.base.visible = false; });
    } else {
        labelMeasured_ = measureString(label_, 11);
        const int32_t labelBaseline = cy + valueMeasured_.h / 2 - labelMeasured_.y + 2;
        canvas().updateShape(labelId_, [&](Shape& shape) {
            setShapeText(shape, label_, 11, theme().textSecondary,
                         cx - labelMeasured_.w / 2, labelBaseline);
        });
    }
}

void RadialGauge::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);
    if (animateTowards(display_, value_, 9.0f, dtSeconds)) {
        if (laidOut()) updateVisuals();
    }
}

// ===========================================================================
// Skeleton
// ===========================================================================
Skeleton::Skeleton(float radius, bool circle)
    : radius_(std::max(0.0f, radius)), circle_(circle) {
    Shape block = makeRRectShape(Rect{0, 0, 1, 1}, radius_,
                                 withOpacity(currentTheme().textPrimary, 0.08f));
    blockId_ = canvas().addShape(std::move(block));
}

Vec2i Skeleton::onMeasure(const Vec2i& available) const {
    (void)available;
    return circle_ ? Vec2i{40, 40} : Vec2i{220, 14};
}

void Skeleton::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (width <= 0 || height <= 0) return;

    const Color base = withOpacity(theme().textPrimary, 0.075f);
    Paint paint = base;

    if (shimmer_) {
        const float span = static_cast<float>(width);
        const float center = (std::fmod(phase_, 1.0f) * 1.7f - 0.35f) * span;
        LinearGradient gradient;
        gradient.from = Vec2f{center - span * 0.32f, 0.0f};
        gradient.to = Vec2f{center + span * 0.32f, 0.0f};
        gradient.stops = {
            GradientStop{0.0f, base},
            GradientStop{0.5f, withOpacity(theme().textPrimary, 0.16f)},
            GradientStop{1.0f, base}
        };
        paint = Paint::linear(gradient);
    }

    const float radius = circle_
        ? static_cast<float>(std::min(width, height)) * 0.5f
        : std::min(radius_, static_cast<float>(std::min(width, height)) * 0.5f);

    canvas().updateShape(blockId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, width, height, radius, radius};
        shape.base.fill = paint;
        shape.base.stroke = Paint{};
        shape.base.visible = true;
    });
}

void Skeleton::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);
    if (!shimmer_ || !laidOut()) return;

    phase_ += std::max(0.0f, dtSeconds) * 0.85f;
    if (phase_ >= 1.0f) phase_ -= std::floor(phase_);
    updateVisuals();
}

// ===========================================================================
// InfoBadge
// ===========================================================================
InfoBadge::InfoBadge(int32_t value, Kind kind)
    : value_(value), kind_(kind) {
    Shape background = makeRRectShape(Rect{0, 0, 1, 1}, 10.0f, currentTheme().accent);
    backgroundId_ = canvas().addShape(std::move(background));

    Shape text = makeTextShape("1", 12, currentTheme().textOnAccent);
    textId_ = canvas().addShape(std::move(text));
}

Vec2i InfoBadge::onMeasure(const Vec2i& available) const {
    (void)available;
    if (value_ < 0) return Vec2i{10, 10};
    measured_ = measureString(std::to_string(value_), 12);
    return Vec2i{std::max(20, measured_.w + 12), 20};
}

void InfoBadge::updateVisuals() {
    if (!laidOut()) return;

    Color color = theme().accent;
    switch (kind_) {
    case Kind::Attention:     color = theme().accent; break;
    case Kind::Informational: color = theme().info; break;
    case Kind::Success:       color = theme().success; break;
    case Kind::Critical:      color = theme().critical; break;
    }

    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    const bool dot = value_ < 0;

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, width, height,
                                static_cast<float>(std::min(width, height)) * 0.5f,
                                static_cast<float>(std::min(width, height)) * 0.5f};
        shape.base.fill = color;
        shape.base.visible = true;
    });

    if (dot) {
        canvas().updateShape(textId_, [&](Shape& shape) { shape.base.visible = false; });
        return;
    }

    const std::string text = std::to_string(value_);
    const Rect measured = measureString(text, 12);
    measured_ = measured;
    const int32_t baseline = baselineForCenter(measured, height);
    const int32_t textX = std::max(0, (width - measured.w) / 2);
    canvas().updateShape(textId_, [&](Shape& shape) {
        setShapeText(shape, text, 12, readableOn(color), textX, baseline);
    });
}

// ===========================================================================
// InfoBar
// ===========================================================================
InfoBar::InfoBar(std::string title, std::string message, Severity severity)
    : title_(std::move(title)), message_(std::move(message)), severity_(severity) {
    Shape background = makeRRectShape(Rect{0, 0, 1, 1}, 8.0f,
                                      currentTheme().layer, currentTheme().stroke, 1);
    backgroundId_ = canvas().addShape(std::move(background));

    Shape iconBackground = makeCircleShape(0, 0, 10.0f, currentTheme().accent);
    iconBackgroundId_ = canvas().addShape(std::move(iconBackground));

    Shape glyph = makeCheckShape(0, 0, 10, currentTheme().textOnAccent, 2);
    iconGlyphId_ = canvas().addShape(std::move(glyph));

    Shape titleShape = makeTextShape(title_, 14, currentTheme().textPrimary);
    titleId_ = canvas().addShape(std::move(titleShape));

    Shape messageShape = makeTextShape(message_, 12, currentTheme().textSecondary);
    messageId_ = canvas().addShape(std::move(messageShape));

    Shape closeBackground = makeRRectShape(Rect{0, 0, 1, 1}, 4.0f, Color::nullColor());
    closeBackgroundId_ = canvas().addShape(std::move(closeBackground));

    Shape closeGlyph = makeXShape(0, 0, 9, currentTheme().textSecondary, 1);
    closeGlyphId_ = canvas().addShape(std::move(closeGlyph));
}

void InfoBar::setTitle(std::string title) {
    if (title == title_) return;
    title_ = std::move(title);
    titleMeasured_ = Rect::null();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void InfoBar::setMessage(std::string message) {
    if (message == message_) return;
    message_ = std::move(message);
    messageMeasured_ = Rect::null();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void InfoBar::setSeverity(Severity severity) {
    if (severity_ == severity) return;
    severity_ = severity;
    if (laidOut()) updateVisuals();
}

void InfoBar::close() {
    if (!open_) return;
    open_ = false;
    invalidateLayout();
    if (laidOut()) updateVisuals();
    if (onClose) onClose();
}

Color InfoBar::severityColor() const {
    switch (severity_) {
    case Severity::Informational: return theme().info;
    case Severity::Success:       return theme().success;
    case Severity::Warning:       return theme().caution;
    case Severity::Error:         return theme().critical;
    }
    return theme().info;
}

Color InfoBar::glyphColor(const Color& background) const {
    return readableOn(background);
}

Vec2i InfoBar::onMeasure(const Vec2i& available) const {
    (void)available;
    if (!open_) return Vec2i{0, 0};

    titleMeasured_ = measureString(title_, 14);
    messageMeasured_ = message_.empty() ? Rect::null() : measureString(message_, 12);
    const int32_t contentWidth = std::max(titleMeasured_.w, messageMeasured_.w);
    const int32_t height = message_.empty() ? 52 : 68;
    return Vec2i{std::max(280, contentWidth + 100), height};
}

void InfoBar::updateVisuals() {
    if (!laidOut()) return;

    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (!open_ || width <= 0 || height <= 0) {
        for (uint32_t id : {backgroundId_, iconBackgroundId_, iconGlyphId_,
                            titleId_, messageId_, closeBackgroundId_, closeGlyphId_}) {
            canvas().updateShape(id, [](Shape& shape) { shape.base.visible = false; });
        }
        return;
    }

    const Color severity = severityColor();
    const Color surface = mixColor(theme().layer, severity, 0.10f);

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, width, height, 8.0f, 8.0f};
        shape.base.fill = surface;
        shape.base.stroke = theme().stroke;
        shape.base.strokeWidth = 1;
        shape.base.visible = true;
    });

    const int32_t iconCenterX = 28;
    const int32_t iconCenterY = height / 2;
    canvas().updateShape(iconBackgroundId_, [&](Shape& shape) {
        shape.data = CircleShape{iconCenterX, iconCenterY, 10.0f};
        shape.base.fill = severity;
        shape.base.stroke = Paint{};
        shape.base.visible = true;
    });

    const Color glyph = glyphColor(severity);
    canvas().updateShape(iconGlyphId_, [&](Shape& shape) {
        switch (severity_) {
        case Severity::Success:
            shape = makeCheckShape(iconCenterX - 5, iconCenterY - 5, 10, glyph, 2);
            break;
        case Severity::Error:
            shape = makeXShape(iconCenterX - 4, iconCenterY - 4, 8, glyph, 2);
            break;
        case Severity::Warning:
            shape = makePathShape(
                {
                    {Vec2f{static_cast<float>(iconCenterX), static_cast<float>(iconCenterY - 5)},
                     Vec2f{static_cast<float>(iconCenterX), static_cast<float>(iconCenterY + 1)}},
                    {Vec2f{static_cast<float>(iconCenterX), static_cast<float>(iconCenterY + 4)},
                     Vec2f{static_cast<float>(iconCenterX), static_cast<float>(iconCenterY + 4.5f)}}
                },
                glyph, 2);
            break;
        default:
            shape = makePathShape(
                {
                    {Vec2f{static_cast<float>(iconCenterX), static_cast<float>(iconCenterY - 1)},
                     Vec2f{static_cast<float>(iconCenterX), static_cast<float>(iconCenterY + 4)}},
                    {Vec2f{static_cast<float>(iconCenterX), static_cast<float>(iconCenterY - 5)},
                     Vec2f{static_cast<float>(iconCenterX), static_cast<float>(iconCenterY - 4.5f)}}
                },
                glyph, 2);
            break;
        }
        shape.base.visible = true;
    });

    const int32_t textX = 46;
    const int32_t textMax = std::max(0, width - textX - (closable_ ? 48 : 16));
    const std::string shownTitle = ellipsizeText(canvas(), title_, 14, textMax);
    if (message_.empty()) {
        const Rect measured = measureString(shownTitle, 14);
        titleMeasured_ = measured;
        const int32_t baseline = baselineForCenter(measured, height);
        canvas().updateShape(titleId_, [&](Shape& shape) {
            setShapeText(shape, shownTitle, 14, theme().textPrimary, textX, baseline);
            shape.base.visible = !title_.empty();
        });
        canvas().updateShape(messageId_, [&](Shape& shape) { shape.base.visible = false; });
    } else {
        const std::string shownMessage = ellipsizeText(canvas(), message_, 12, textMax);
        const Rect titleSize = measureString(shownTitle, 14);
        const Rect messageSize = measureString(shownMessage, 12);
        titleMeasured_ = titleSize;
        messageMeasured_ = messageSize;

        const int32_t total = titleSize.h + 2 + messageSize.h;
        const int32_t top = std::max(6, (height - total) / 2);
        const int32_t titleBaseline = top - titleSize.y;
        const int32_t messageBaseline = top + titleSize.h + 4 - messageSize.y;

        canvas().updateShape(titleId_, [&](Shape& shape) {
            setShapeText(shape, shownTitle, 14, theme().textPrimary, textX, titleBaseline);
            shape.base.visible = !title_.empty();
        });
        canvas().updateShape(messageId_, [&](Shape& shape) {
            setShapeText(shape, shownMessage, 12, theme().textSecondary, textX, messageBaseline);
            shape.base.visible = true;
        });
    }

    if (!closable_) {
        canvas().updateShape(closeBackgroundId_, [&](Shape& shape) { shape.base.visible = false; });
        canvas().updateShape(closeGlyphId_, [&](Shape& shape) { shape.base.visible = false; });
    } else {
        const int32_t buttonSize = 28;
        const int32_t buttonX = width - buttonSize - 8;
        const int32_t buttonY = (height - buttonSize) / 2;
        const Color background = closeHover_ ? theme().subtleHover : Color::nullColor();

        canvas().updateShape(closeBackgroundId_, [&](Shape& shape) {
            shape.data = RRectShape{buttonX, buttonY, buttonSize, buttonSize, 4.0f, 4.0f};
            shape.base.fill = background;
            shape.base.visible = background.a > 0;
        });
        canvas().updateShape(closeGlyphId_, [&](Shape& shape) {
            const int32_t size = 9;
            shape = makeXShape(buttonX + (buttonSize - size) / 2,
                               buttonY + (buttonSize - size) / 2,
                               size, theme().textSecondary, 1);
            shape.base.visible = true;
        });
    }
}

bool InfoBar::onEvent(const Event& ev) {
    if (!open_ || !enabled()) return false;

    const int32_t closeLeft = frameWidth() - 36;
    const auto inClose = [&](int32_t x, int32_t y) {
        return closable_ && x >= closeLeft && x < frameWidth() - 8 &&
               y >= (frameHeight() - 28) / 2 && y < (frameHeight() + 28) / 2;
    };

    switch (ev.type) {
    case EventType::MouseMove: {
        const bool hover = inClose(ev.x, ev.y);
        if (hover != closeHover_) {
            closeHover_ = hover;
            if (laidOut()) updateVisuals();
        }
        return false;
    }
    case EventType::MouseDown:
        return true;
    case EventType::MouseUp:
        if (clicked() && inClose(ev.x, ev.y)) close();
        return true;
    default:
        return false;
    }
}

void InfoBar::onStateChanged() {
    if (!hovered() && closeHover_) {
        closeHover_ = false;
        if (laidOut()) updateVisuals();
    }
}

} // namespace fluent
} // namespace modern_ui
