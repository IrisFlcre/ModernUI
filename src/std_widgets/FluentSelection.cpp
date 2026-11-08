// ============================================================================
// FluentSelection.cpp — 选择类控件实现
// ============================================================================

#include "FluentSelection.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <utility>

#include "View.h"

namespace modern_ui {
namespace fluent {
namespace {

constexpr int kKeySpace = 0x20;
constexpr int kKeyEnter = 0x0D;
constexpr int kKeyEscape = 0x1B;
constexpr int kKeyLeft = 0x25;
constexpr int kKeyUp = 0x26;
constexpr int kKeyRight = 0x27;
constexpr int kKeyDown = 0x28;
constexpr int kKeyHome = 0x24;
constexpr int kKeyEnd = 0x23;

// ComboBox 浮出层默认 Blur Behind：模糊强度（高斯 σ）与背景蒙层透明度
constexpr float kPopupBlurSigma = 10.0f;
constexpr float kPopupTintOpacity = 0.06f;

float clampValue(float value, float minimum, float maximum) {
    if (maximum < minimum) std::swap(minimum, maximum);
    return std::max(minimum, std::min(maximum, value));
}

// RadioButton 分组表（仅 UI 线程使用）
std::unordered_map<std::string, std::vector<RadioButton*>>& radioGroups() {
    static std::unordered_map<std::string, std::vector<RadioButton*>> groups;
    return groups;
}

void removeFromRadioGroup(RadioButton* button, const std::string& group) {
    if (group.empty()) return;
    auto it = radioGroups().find(group);
    if (it == radioGroups().end()) return;
    auto& list = it->second;
    list.erase(std::remove(list.begin(), list.end(), button), list.end());
    if (list.empty()) radioGroups().erase(it);
}

} // namespace

// ===========================================================================
// CheckBox
// ===========================================================================
CheckBox::CheckBox(std::string label, bool checked)
    : label_(std::move(label)), checked_(checked) {
    checkProgress_ = checked_ ? 1.0f : 0.0f;

    Shape box = makeRRectShape(Rect{0, 0, 20, 20}, 4.0f,
                               theme().controlFill, theme().stroke, 1);
    boxId_ = canvas().addShape(std::move(box));

    Shape check = makeCheckShape(3, 3, 14, theme().textOnAccent, 2);
    check.base.visible = checked_;
    checkId_ = canvas().addShape(std::move(check));

    Shape text = makeTextShape(label_, 14, theme().textPrimary, 0, 0);
    labelId_ = canvas().addShape(std::move(text));
}

void CheckBox::setLabel(std::string label) {
    if (label == label_) return;
    label_ = std::move(label);
    measured_ = Rect::null();
    invalidateLayout();
}

void CheckBox::setChecked(bool checked) {
    if (checked_ == checked) return;
    checked_ = checked;
    if (laidOut()) updateVisuals();
}

Vec2i CheckBox::onMeasure(const Vec2i& available) const {
    (void)available;
    measured_ = measureString(label_, fontSize_);
    return Vec2i{32 + std::max(0, measured_.w), theme().controlHeight};
}

void CheckBox::updateVisuals() {
    if (!laidOut()) return;
    if (measured_.isNull()) measured_ = measureString(label_, fontSize_);

    const int32_t boxSize = 20;
    const int32_t boxY = (frameHeight() - boxSize) / 2;

    Color baseFill = enabled()
        ? interactiveFill(theme().controlFill, theme().controlFillHover, theme().controlFillPressed)
        : theme().controlFillDisabled;
    const Color checkedFill = enabled()
        ? interactiveFill(theme().accent, theme().accentHover, theme().accentPressed)
        : theme().accentDisabled;
    const Color fill = mixColor(baseFill, checkedFill, checkProgress_);

    Color stroke = mixColor(theme().stroke, theme().accent, checkProgress_);
    stroke = focusStroke(stroke);
    if (!enabled()) stroke = theme().stroke;

    canvas().updateShape(boxId_, [&](Shape& shape) {
        shape.data = RRectShape{0, boxY, boxSize, boxSize, 4.0f, 4.0f};
        shape.base.fill = fill;
        shape.base.stroke = stroke;
        shape.base.strokeWidth = focusAmount() > 0.15f ? 2 : 1;
    });

    canvas().updateShape(checkId_, [&](Shape& shape) {
        shape = makeCheckShape(3, boxY + 3, 14,
                               withOpacity(theme().textOnAccent, checkProgress_), 2);
        shape.base.visible = checkProgress_ > 0.02f;
    });

    const int32_t baseline = baselineForCenter(measured_, frameHeight());
    const Color textColor = enabled() ? theme().textPrimary : theme().textDisabled;
    canvas().updateShape(labelId_, [&](Shape& shape) {
        setShapeText(shape, label_, fontSize_, textColor, 32, baseline);
    });
}

void CheckBox::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);
    if (animateTowards(checkProgress_, checked_ ? 1.0f : 0.0f, 18.0f, dtSeconds)) {
        if (laidOut()) updateVisuals();
    }
}

void CheckBox::activate() {
    if (!enabled()) return;
    checked_ = !checked_;
    if (onCheckedChanged) onCheckedChanged(checked_);
    if (onClick) onClick();
}

// ===========================================================================
// RadioButton
// ===========================================================================
RadioButton::RadioButton(std::string label, bool checked)
    : label_(std::move(label)), checked_(checked) {
    checkProgress_ = checked_ ? 1.0f : 0.0f;

    Shape ring = makeCircleShape(10, 10, 10.0f, Color::nullColor(),
                                 theme().strokeStrong, 1);
    ringId_ = canvas().addShape(std::move(ring));

    Shape dot = makeCircleShape(10, 10, 4.0f * checkProgress_,
                                withOpacity(theme().accent, checkProgress_));
    dot.base.visible = checked_;
    dotId_ = canvas().addShape(std::move(dot));

    Shape text = makeTextShape(label_, 14, theme().textPrimary, 0, 0);
    labelId_ = canvas().addShape(std::move(text));
}

RadioButton::~RadioButton() {
    removeFromRadioGroup(this, group_);
}

void RadioButton::setLabel(std::string label) {
    if (label == label_) return;
    label_ = std::move(label);
    measured_ = Rect::null();
    invalidateLayout();
}

void RadioButton::setGroup(std::string group) {
    if (group == group_) return;
    removeFromRadioGroup(this, group_);
    group_ = std::move(group);
    if (!group_.empty()) {
        radioGroups()[group_].push_back(this);
        if (checked_) uncheckGroupSiblings();
    }
}

void RadioButton::setChecked(bool checked) {
    if (checked_ == checked) return;
    checked_ = checked;
    if (laidOut()) updateVisuals();
}

Vec2i RadioButton::onMeasure(const Vec2i& available) const {
    (void)available;
    measured_ = measureString(label_, fontSize_);
    return Vec2i{32 + std::max(0, measured_.w), theme().controlHeight};
}

void RadioButton::updateVisuals() {
    if (!laidOut()) return;
    if (measured_.isNull()) measured_ = measureString(label_, fontSize_);

    const int32_t centerX = 10;
    const int32_t centerY = frameHeight() / 2;

    Color ringColor = mixColor(theme().strokeStrong, theme().accent, checkProgress_);
    ringColor = focusStroke(ringColor);
    if (!enabled()) ringColor = theme().stroke;

    canvas().updateShape(ringId_, [&](Shape& shape) {
        shape.data = CircleShape{centerX, centerY, 9.0f};
        shape.base.fill = Paint{};
        shape.base.stroke = ringColor;
        shape.base.strokeWidth = (focusAmount() > 0.15f || checked_) ? 2 : 1;
        shape.base.visible = true;
    });

    const Color dotColor = enabled() ? theme().accent : theme().accentDisabled;
    canvas().updateShape(dotId_, [&](Shape& shape) {
        shape.data = CircleShape{centerX, centerY, 4.0f * checkProgress_};
        shape.base.fill = withOpacity(dotColor, checkProgress_);
        shape.base.visible = checkProgress_ > 0.05f;
    });

    const int32_t baseline = baselineForCenter(measured_, frameHeight());
    const Color textColor = enabled() ? theme().textPrimary : theme().textDisabled;
    canvas().updateShape(labelId_, [&](Shape& shape) {
        setShapeText(shape, label_, fontSize_, textColor, 30, baseline);
    });
}

void RadioButton::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);
    if (animateTowards(checkProgress_, checked_ ? 1.0f : 0.0f, 18.0f, dtSeconds)) {
        if (laidOut()) updateVisuals();
    }
}

void RadioButton::activate() {
    if (!enabled()) return;
    if (!checked_) {
        checked_ = true;
        uncheckGroupSiblings();
        if (onCheckedChanged) onCheckedChanged(true);
    }
    if (onClick) onClick();
}

void RadioButton::uncheckGroupSiblings() {
    if (group_.empty()) return;
    auto it = radioGroups().find(group_);
    if (it == radioGroups().end()) return;

    for (RadioButton* sibling : it->second) {
        if (!sibling || sibling == this) continue;
        if (!sibling->checked_) continue;
        sibling->checked_ = false;
        if (sibling->onCheckedChanged) sibling->onCheckedChanged(false);
    }
}

// ===========================================================================
// ToggleSwitch
// ===========================================================================
ToggleSwitch::ToggleSwitch(std::string label, bool checked)
    : label_(std::move(label)), checked_(checked) {
    knobProgress_ = checked_ ? 1.0f : 0.0f;

    Shape track = makeRRectShape(Rect{0, 0, 40, 20}, 10.0f,
                                 theme().controlFill, theme().strokeStrong, 1);
    trackId_ = canvas().addShape(std::move(track));

    Shape knob = makeCircleShape(10, 10, 7.0f, theme().textSecondary);
    knobId_ = canvas().addShape(std::move(knob));

    Shape text = makeTextShape(label_, 14, theme().textPrimary, 0, 0);
    labelId_ = canvas().addShape(std::move(text));
}

void ToggleSwitch::setLabel(std::string label) {
    if (label == label_) return;
    label_ = std::move(label);
    measured_ = Rect::null();
    invalidateLayout();
}

void ToggleSwitch::setChecked(bool checked) {
    if (checked_ == checked) return;
    checked_ = checked;
}

Vec2i ToggleSwitch::onMeasure(const Vec2i& available) const {
    (void)available;
    measured_ = measureString(label_, fontSize_);
    const int32_t labelWidth = label_.empty() ? 0 : (12 + measured_.w);
    return Vec2i{40 + labelWidth, theme().controlHeight};
}

void ToggleSwitch::updateVisuals() {
    if (!laidOut()) return;
    if (measured_.isNull()) measured_ = measureString(label_, fontSize_);

    const int32_t trackX = 0;
    const int32_t trackY = (frameHeight() - 20) / 2;

    Color offFill = enabled()
        ? interactiveFill(theme().controlFill, theme().controlFillHover, theme().controlFillPressed)
        : theme().controlFillDisabled;
    const Color onFill = enabled()
        ? interactiveFill(theme().accent, theme().accentHover, theme().accentPressed)
        : theme().accentDisabled;
    const Color trackFill = mixColor(offFill, onFill, knobProgress_);

    Color trackStroke = mixColor(theme().strokeStrong, theme().accent, knobProgress_);
    trackStroke = focusStroke(trackStroke);
    if (!enabled()) trackStroke = theme().stroke;

    canvas().updateShape(trackId_, [&](Shape& shape) {
        shape.data = RRectShape{trackX, trackY, 40, 20, 10.0f, 10.0f};
        shape.base.fill = trackFill;
        shape.base.stroke = trackStroke;
        shape.base.strokeWidth = focusAmount() > 0.15f ? 2 : 1;
    });

    const int32_t knobCenterX = trackX + 10 + static_cast<int32_t>(std::lround(20.0f * knobProgress_));
    const int32_t knobCenterY = trackY + 10;
    const Color knobColor = enabled()
        ? mixColor(theme().textSecondary, theme().textOnAccent, knobProgress_)
        : theme().textDisabled;

    canvas().updateShape(knobId_, [&](Shape& shape) {
        shape.data = CircleShape{knobCenterX, knobCenterY, 7.0f};
        shape.base.fill = knobColor;
        shape.base.stroke = Paint{};
        shape.base.strokeWidth = 0;
        shape.base.visible = true;
    });

    const int32_t baseline = baselineForCenter(measured_, frameHeight());
    const Color textColor = enabled() ? theme().textPrimary : theme().textDisabled;
    canvas().updateShape(labelId_, [&](Shape& shape) {
        setShapeText(shape, label_, fontSize_, textColor, 52, baseline);
    });
}

void ToggleSwitch::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);
    if (animateTowards(knobProgress_, checked_ ? 1.0f : 0.0f, 16.0f, dtSeconds)) {
        if (laidOut()) updateVisuals();
    }
}

void ToggleSwitch::activate() {
    if (!enabled()) return;
    checked_ = !checked_;
    if (onToggled) onToggled(checked_);
    if (onClick) onClick();
}

// ===========================================================================
// SegmentedControl
// ===========================================================================
SegmentedControl::SegmentedControl(std::vector<std::string> items)
    : items_(std::move(items)) {
    Shape track = makeRRectShape(Rect{0, 0, 1, 1}, 16.0f,
                                 withOpacity(theme().textPrimary, 0.06f),
                                 theme().stroke, 1);
    trackId_ = canvas().addShape(std::move(track));

    Shape indicator = makeRRectShape(Rect{0, 0, 1, 1}, 14.0f, theme().accent);
    indicatorId_ = canvas().addShape(std::move(indicator));

    if (items_.empty()) items_.push_back("Option 1");
    rebuildLabels();
}

void SegmentedControl::setItems(std::vector<std::string> items) {
    if (items == items_) return;
    items_ = std::move(items);
    if (items_.empty()) items_.push_back("Option 1");
    measured_.clear();
    rebuildLabels();
    if (selectedIndex_ >= static_cast<int32_t>(items_.size())) {
        selectedIndex_ = static_cast<int32_t>(items_.size()) - 1;
    }
    indicatorReady_ = false;
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void SegmentedControl::setSelectedIndex(int32_t index) {
    if (items_.empty()) return;
    const int32_t clamped = std::max(0, std::min(index, static_cast<int32_t>(items_.size()) - 1));
    if (clamped == selectedIndex_) return;
    selectedIndex_ = clamped;
    if (laidOut()) updateVisuals();
}

void SegmentedControl::rebuildLabels() {
    for (uint32_t id : labelIds_) canvas().removeShape(id);
    labelIds_.clear();
    labelIds_.reserve(items_.size());
    for (const std::string& item : items_) {
        Shape text = makeTextShape(item, fontSize_, theme().textSecondary);
        labelIds_.push_back(canvas().addShape(std::move(text)));
    }
}

Vec2i SegmentedControl::onMeasure(const Vec2i& available) const {
    (void)available;
    measured_.clear();
    measured_.reserve(items_.size());
    int32_t total = 0;
    for (const std::string& item : items_) {
        const Rect bounds = measureString(item, fontSize_);
        measured_.push_back(bounds);
        total += std::max(56, bounds.w + 32);
    }
    return Vec2i{std::max(120, total), theme().controlHeight};
}

void SegmentedControl::computeSegment(int32_t totalWidth, int32_t count, int32_t segment,
                                      int32_t& x, int32_t& width) const {
    if (count <= 0) {
        x = 0;
        width = 0;
        return;
    }
    const int32_t base = totalWidth / count;
    x = base * segment;
    width = base + (segment == count - 1 ? totalWidth - base * count : 0);
}

void SegmentedControl::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (width <= 0 || height <= 0 || items_.empty()) return;

    // 等分宽度（余数给最后一段），保证 indicator 与文本对齐
    const int32_t count = static_cast<int32_t>(items_.size());

    int32_t targetX = 0, targetW = 0;
    computeSegment(width, count, selectedIndex_, targetX, targetW);

    if (!indicatorReady_) {
        indicatorX_ = static_cast<float>(targetX);
        indicatorWidth_ = static_cast<float>(targetW);
        indicatorReady_ = true;
    }

    const float radius = static_cast<float>(height) * 0.5f;
    canvas().updateShape(trackId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, width, height, radius, radius};
        shape.base.fill = enabled()
            ? interactiveFill(withOpacity(theme().textPrimary, 0.06f),
                              withOpacity(theme().textPrimary, 0.09f),
                              withOpacity(theme().textPrimary, 0.05f))
            : withOpacity(theme().textDisabled, 0.06f);
        shape.base.stroke = focusStroke(theme().stroke);
        shape.base.strokeWidth = focusAmount() > 0.15f ? 2 : 1;
        shape.base.visible = true;
    });

    // 选中胶囊：强调色竖向渐变，位置与宽度都做指数插值
    LinearGradient pill;
    pill.from = Vec2f{0.0f, 0.0f};
    pill.to = Vec2f{0.0f, static_cast<float>(height)};
    pill.stops = {
        GradientStop{0.0f, theme().accentHover},
        GradientStop{1.0f, theme().accent}
    };
    const float inset = 3.0f;
    const float pillRadius = std::max(2.0f, radius - inset);
    canvas().updateShape(indicatorId_, [&](Shape& shape) {
        shape.data = RRectShape{
            static_cast<int32_t>(std::lround(indicatorX_ + inset)),
            static_cast<int32_t>(inset),
            std::max(2, static_cast<int32_t>(std::lround(indicatorWidth_ - inset * 2.0f))),
            std::max(2, height - static_cast<int32_t>(inset * 2.0f)),
            pillRadius, pillRadius};
        shape.base.fill = enabled() ? Paint::linear(pill) : Paint{theme().accentDisabled};
        shape.base.stroke = Paint{};
        shape.base.visible = width > 8 && height > 8;
    });

    for (int32_t i = 0; i < count; ++i) {
        int32_t segX = 0, segW = 0;
        computeSegment(width, count, i, segX, segW);

        const bool selected = (i == selectedIndex_);
        const bool hot = (i == hoverIndex_);
        Color color = selected ? theme().textOnAccent
                    : hot      ? theme().textPrimary
                               : theme().textSecondary;
        if (!enabled()) color = theme().textDisabled;

        Rect measured = i < static_cast<int32_t>(measured_.size())
            ? measured_[static_cast<size_t>(i)] : Rect::null();
        if (measured.isNull()) measured = measureString(items_[static_cast<size_t>(i)], fontSize_);

        const int32_t textX = segX + (segW - measured.w) / 2;
        const int32_t baseline = baselineForCenter(measured, height);
        canvas().updateShape(labelIds_[static_cast<size_t>(i)], [&](Shape& shape) {
            setShapeText(shape, items_[static_cast<size_t>(i)], fontSize_, color, textX, baseline);
            shape.base.visible = true;
        });
    }
}

void SegmentedControl::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);
    if (!laidOut() || items_.empty()) return;

    const int32_t width = frameWidth();
    const int32_t count = static_cast<int32_t>(items_.size());
    if (width <= 0 || count <= 0) return;

    int32_t targetX = 0, targetW = 0;
    computeSegment(width, count, selectedIndex_, targetX, targetW);

    bool changed = animateTowards(indicatorX_, static_cast<float>(targetX), 22.0f, dtSeconds);
    changed = animateTowards(indicatorWidth_, static_cast<float>(targetW), 22.0f, dtSeconds) || changed;
    if (changed) updateVisuals();
}

int32_t SegmentedControl::indexAt(int32_t localX) const {
    const int32_t width = frameWidth();
    const int32_t count = static_cast<int32_t>(items_.size());
    if (width <= 0 || count <= 0 || localX < 0 || localX >= width) return -1;

    const int32_t base = width / count;
    if (localX < base * (count - 1)) return localX / base;
    return count - 1;
}

void SegmentedControl::commitSelection(int32_t index, bool notify) {
    if (index < 0 || index >= static_cast<int32_t>(items_.size())) return;
    if (index == selectedIndex_) {
        if (laidOut()) updateVisuals();
        return;
    }
    selectedIndex_ = index;
    if (laidOut()) updateVisuals();
    if (notify && onSelectionChanged) onSelectionChanged(selectedIndex_);
    if (notify && onClick) onClick();
}

bool SegmentedControl::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseMove: {
        const int32_t index = indexAt(ev.x);
        if (index != hoverIndex_) {
            hoverIndex_ = index;
            if (laidOut()) updateVisuals();
        }
        return false;
    }

    case EventType::MouseDown:
        return true;

    case EventType::MouseUp:
        if (clicked()) {
            commitSelection(indexAt(ev.x), true);
            return true;
        }
        return false;

    case EventType::KeyDown: {
        const int32_t count = static_cast<int32_t>(items_.size());
        if (count <= 0) return false;
        if (ev.key == kKeyLeft || ev.key == kKeyUp) {
            commitSelection(std::max(0, selectedIndex_ - 1), true);
            return true;
        }
        if (ev.key == kKeyRight || ev.key == kKeyDown) {
            commitSelection(std::min(count - 1, selectedIndex_ + 1), true);
            return true;
        }
        if (ev.key == kKeyHome) {
            commitSelection(0, true);
            return true;
        }
        if (ev.key == kKeyEnd) {
            commitSelection(count - 1, true);
            return true;
        }
        return FluentControl::onEvent(ev);
    }

    default:
        return FluentControl::onEvent(ev);
    }
}

void SegmentedControl::onStateChanged() {
    if (!hovered() && hoverIndex_ >= 0) {
        hoverIndex_ = -1;
        if (laidOut()) updateVisuals();
    }
}

// ===========================================================================
// Slider
// ===========================================================================
Slider::Slider(float value, float minimum, float maximum, float step)
    : value_(value), minimum_(minimum), maximum_(maximum), step_(std::max(0.0f, step)) {
    if (maximum_ < minimum_) std::swap(minimum_, maximum_);
    value_ = clampValue(value_, minimum_, maximum_);

    Shape track = makeRRectShape(Rect{0, 0, 1, 4}, 2.0f,
                                 withOpacity(theme().textPrimary, 0.22f));
    trackId_ = canvas().addShape(std::move(track));

    Shape fill = makeRRectShape(Rect{0, 0, 0, 4}, 2.0f, theme().accent);
    fillId_ = canvas().addShape(std::move(fill));

    Shape knob = makeCircleShape(0, 0, 9.0f, theme().accent);
    knobId_ = canvas().addShape(std::move(knob));
}

float Slider::normalized() const {
    const float span = maximum_ - minimum_;
    if (span <= 1.0e-6f) return 0.0f;
    return clampValue((value_ - minimum_) / span, 0.0f, 1.0f);
}

void Slider::setValue(float value) {
    const float next = clampValue(value, minimum_, maximum_);
    if (std::fabs(next - value_) < 1.0e-6f) return;
    value_ = next;
    if (laidOut()) updateVisuals();
}

void Slider::setRange(float minimum, float maximum) {
    if (maximum < minimum) std::swap(minimum, maximum);
    minimum_ = minimum;
    maximum_ = maximum;
    value_ = clampValue(value_, minimum_, maximum_);
    if (laidOut()) updateVisuals();
}

Vec2i Slider::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{180, theme().controlHeight};
}

void Slider::updateVisuals() {
    if (!laidOut()) return;
    const int32_t frameW = frameWidth();
    const int32_t frameH = frameHeight();
    if (frameW <= 24 || frameH <= 0) return;

    const int32_t trackX = 9;
    const int32_t span = std::max(1, frameW - 18);
    const int32_t trackY = (frameH - 4) / 2;
    const int32_t knobX = trackX + static_cast<int32_t>(std::lround(span * normalized()));
    const int32_t fillWidth = std::max(0, knobX - trackX);

    const Color trackColor = enabled()
        ? withOpacity(theme().textPrimary, 0.22f)
        : withOpacity(theme().textDisabled, 0.25f);
    canvas().updateShape(trackId_, [&](Shape& shape) {
        shape.data = RRectShape{trackX, trackY, span, 4, 2.0f, 2.0f};
        shape.base.fill = trackColor;
        shape.base.visible = true;
    });

    const Color fillColor = enabled()
        ? interactiveFill(theme().accent, theme().accentHover, theme().accentPressed)
        : theme().accentDisabled;
    canvas().updateShape(fillId_, [&](Shape& shape) {
        shape.data = RRectShape{trackX, trackY, std::max(2, fillWidth), 4, 2.0f, 2.0f};
        shape.base.fill = fillColor;
        shape.base.visible = fillWidth > 0;
    });

    const float knobRadius = 9.0f + hoverAmount() * 1.0f + pressAmount() * 1.5f;
    canvas().updateShape(knobId_, [&](Shape& shape) {
        shape.data = CircleShape{knobX, frameH / 2, knobRadius};
        shape.base.fill = fillColor;
        shape.base.stroke = focusAmount() > 0.01f
            ? withOpacity(theme().focusStroke, focusAmount())
            : Color::nullColor();
        shape.base.strokeWidth = 2;
        shape.base.visible = enabled();
    });
}

void Slider::commitValue(float next, bool notify) {
    next = clampValue(next, minimum_, maximum_);
    if (step_ > 0.0f) {
        const float steps = std::round((next - minimum_) / step_);
        next = clampValue(minimum_ + steps * step_, minimum_, maximum_);
    }
    if (std::fabs(next - value_) < 1.0e-6f) return;

    value_ = next;
    if (laidOut()) updateVisuals();
    if (notify && onValueChanged) onValueChanged(value_);
}

void Slider::applyFromLocalX(int32_t localX, bool notify) {
    const int32_t span = std::max(1, frameWidth() - 18);
    const float t = clampValue(static_cast<float>(localX - 9) / static_cast<float>(span),
                               0.0f, 1.0f);
    commitValue(minimum_ + t * (maximum_ - minimum_), notify);
}

bool Slider::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseDown:
        dragging_ = true;
        applyFromLocalX(ev.x, true);
        return true;

    case EventType::MouseMove:
        if (!dragging_) return false;
        applyFromLocalX(ev.x, true);
        return true;

    case EventType::MouseUp:
        if (!dragging_) return false;
        applyFromLocalX(ev.x, true);
        dragging_ = false;
        return true;

    case EventType::KeyDown: {
        const float unit = step_ > 0.0f ? step_ : (maximum_ - minimum_) / 100.0f;
        if (ev.key == kKeyLeft || ev.key == kKeyDown) {
            commitValue(value_ - unit, true);
            return true;
        }
        if (ev.key == kKeyRight || ev.key == kKeyUp) {
            commitValue(value_ + unit, true);
            return true;
        }
        if (ev.key == kKeyHome) {
            commitValue(minimum_, true);
            return true;
        }
        if (ev.key == kKeyEnd) {
            commitValue(maximum_, true);
            return true;
        }
        return FluentControl::onEvent(ev);
    }

    default:
        return FluentControl::onEvent(ev);
    }
}

// ===========================================================================
// RangeSlider
// ===========================================================================
RangeSlider::RangeSlider(float low, float high, float minimum, float maximum, float step)
    : low_(low), high_(high), minimum_(minimum), maximum_(maximum),
      step_(std::max(0.0f, step)) {
    if (maximum_ < minimum_) std::swap(minimum_, maximum_);
    low_ = clampValue(low_, minimum_, maximum_);
    high_ = clampValue(high_, minimum_, maximum_);
    if (high_ < low_) std::swap(low_, high_);

    Shape track = makeRRectShape(Rect{0, 0, 1, 4}, 2.0f,
                                 withOpacity(theme().textPrimary, 0.18f));
    trackId_ = canvas().addShape(std::move(track));

    Shape fill = makeRRectShape(Rect{0, 0, 1, 4}, 2.0f, theme().accent);
    fillId_ = canvas().addShape(std::move(fill));

    Shape lowGlow = makeCircleShape(0, 0, 1.0f, withOpacity(theme().accent, 0.0f));
    lowGlowId_ = canvas().addShape(std::move(lowGlow));

    Shape highGlow = makeCircleShape(0, 0, 1.0f, withOpacity(theme().accent, 0.0f));
    highGlowId_ = canvas().addShape(std::move(highGlow));

    Shape lowKnob = makeCircleShape(0, 0, 9.0f, theme().textPrimary);
    lowKnob.base.stroke = theme().accent;
    lowKnob.base.strokeWidth = 2;
    lowKnobId_ = canvas().addShape(std::move(lowKnob));

    Shape highKnob = makeCircleShape(0, 0, 9.0f, theme().textPrimary);
    highKnob.base.stroke = theme().accent;
    highKnob.base.strokeWidth = 2;
    highKnobId_ = canvas().addShape(std::move(highKnob));
}

float RangeSlider::normalizedLow() const {
    const float span = maximum_ - minimum_;
    if (span <= 1.0e-6f) return 0.0f;
    return clampValue((low_ - minimum_) / span, 0.0f, 1.0f);
}

float RangeSlider::normalizedHigh() const {
    const float span = maximum_ - minimum_;
    if (span <= 1.0e-6f) return 0.0f;
    return clampValue((high_ - minimum_) / span, 0.0f, 1.0f);
}

void RangeSlider::setValues(float low, float high) {
    commitValues(low, high, false);
}

void RangeSlider::setRange(float minimum, float maximum) {
    if (maximum < minimum) std::swap(minimum, maximum);
    minimum_ = minimum;
    maximum_ = maximum;
    commitValues(clampValue(low_, minimum_, maximum_),
                 clampValue(high_, minimum_, maximum_), false);
}

int32_t RangeSlider::thumbX(float normalized, int32_t trackX, int32_t span) const {
    return trackX + static_cast<int32_t>(std::lround(static_cast<float>(span) * normalized));
}

RangeSlider::Thumb RangeSlider::thumbAt(int32_t localX, int32_t trackX, int32_t span) const {
    const int32_t lowX = thumbX(normalizedLow(), trackX, span);
    const int32_t highX = thumbX(normalizedHigh(), trackX, span);
    const int32_t lowDist = std::abs(localX - lowX);
    const int32_t highDist = std::abs(localX - highX);

    if (lowDist <= 16 && lowDist <= highDist) return Thumb::Low;
    if (highDist <= 16) return Thumb::High;
    return Thumb::None;
}

void RangeSlider::applyFromLocalX(int32_t localX, Thumb thumb, bool notify) {
    if (thumb == Thumb::None) return;

    const int32_t trackX = 9;
    const int32_t span = std::max(1, frameWidth() - 18);
    const float t = clampValue(static_cast<float>(localX - trackX) / static_cast<float>(span),
                               0.0f, 1.0f);
    const float value = minimum_ + t * (maximum_ - minimum_);

    if (thumb == Thumb::Low) commitValues(std::min(value, high_), high_, notify);
    else                     commitValues(low_, std::max(value, low_), notify);
}

void RangeSlider::commitValues(float low, float high, bool notify) {
    low = clampValue(low, minimum_, maximum_);
    high = clampValue(high, minimum_, maximum_);
    if (step_ > 0.0f) {
        const float lowSteps = std::round((low - minimum_) / step_);
        const float highSteps = std::round((high - minimum_) / step_);
        low = clampValue(minimum_ + lowSteps * step_, minimum_, maximum_);
        high = clampValue(minimum_ + highSteps * step_, minimum_, maximum_);
    }
    if (low > high) std::swap(low, high);
    if (std::fabs(low - low_) < 1.0e-6f && std::fabs(high - high_) < 1.0e-6f) return;

    low_ = low;
    high_ = high;
    if (laidOut()) updateVisuals();
    if (notify && onRangeChanged) onRangeChanged(low_, high_);
}

Vec2i RangeSlider::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{220, theme().controlHeight};
}

void RangeSlider::updateVisuals() {
    if (!laidOut()) return;
    const int32_t frameW = frameWidth();
    const int32_t frameH = frameHeight();
    if (frameW <= 24 || frameH <= 0) return;

    const int32_t trackX = 9;
    const int32_t span = std::max(1, frameW - 18);
    const int32_t trackY = (frameH - 4) / 2;
    const int32_t lowX = thumbX(normalizedLow(), trackX, span);
    const int32_t highX = thumbX(normalizedHigh(), trackX, span);
    const int32_t fillWidth = std::max(0, highX - lowX);

    canvas().updateShape(trackId_, [&](Shape& shape) {
        shape.data = RRectShape{trackX, trackY, span, 4, 2.0f, 2.0f};
        shape.base.fill = enabled()
            ? withOpacity(theme().textPrimary, 0.18f)
            : withOpacity(theme().textDisabled, 0.20f);
        shape.base.visible = true;
    });

    const Color accent = enabled()
        ? interactiveFill(theme().accent, theme().accentHover, theme().accentPressed)
        : theme().accentDisabled;
    canvas().updateShape(fillId_, [&](Shape& shape) {
        shape.data = RRectShape{lowX, trackY, fillWidth, 4, 2.0f, 2.0f};
        shape.base.fill = accent;
        shape.base.visible = fillWidth > 0;
    });

    // 悬停 / 拖动光晕：以低成本的半透明圆扩展触摸反馈
    const float lowGlow = enabled() ? 14.0f + lowHover_ * 5.0f : 0.0f;
    const float highGlow = enabled() ? 14.0f + highHover_ * 5.0f : 0.0f;
    canvas().updateShape(lowGlowId_, [&](Shape& shape) {
        shape.data = CircleShape{lowX, frameH / 2, lowGlow};
        shape.base.fill = withOpacity(accent, 0.16f * std::min(1.0f, lowHover_ + 0.01f));
        shape.base.visible = lowHover_ > 0.01f;
    });
    canvas().updateShape(highGlowId_, [&](Shape& shape) {
        shape.data = CircleShape{highX, frameH / 2, highGlow};
        shape.base.fill = withOpacity(accent, 0.16f * std::min(1.0f, highHover_ + 0.01f));
        shape.base.visible = highHover_ > 0.01f;
    });

    const float lowRadius = 9.0f + lowHover_ * 1.2f;
    const float highRadius = 9.0f + highHover_ * 1.2f;
    canvas().updateShape(lowKnobId_, [&](Shape& shape) {
        shape.data = CircleShape{lowX, frameH / 2, lowRadius};
        shape.base.fill = theme().card;
        shape.base.stroke = enabled() ? accent : theme().accentDisabled;
        shape.base.strokeWidth = 2;
        shape.base.visible = enabled();
    });
    canvas().updateShape(highKnobId_, [&](Shape& shape) {
        shape.data = CircleShape{highX, frameH / 2, highRadius};
        shape.base.fill = theme().card;
        shape.base.stroke = enabled() ? accent : theme().accentDisabled;
        shape.base.strokeWidth = 2;
        shape.base.visible = enabled();
    });
}

void RangeSlider::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);

    bool changed = false;
    float lowTarget = 0.0f;
    float highTarget = 0.0f;
    if (hoverThumb_ == Thumb::Low || dragThumb_ == Thumb::Low) lowTarget = 1.0f;
    if (hoverThumb_ == Thumb::High || dragThumb_ == Thumb::High) highTarget = 1.0f;

    changed = animateTowards(lowHover_, lowTarget, 20.0f, dtSeconds) || changed;
    changed = animateTowards(highHover_, highTarget, 20.0f, dtSeconds) || changed;
    if (changed && laidOut()) updateVisuals();
}

bool RangeSlider::onEvent(const Event& ev) {
    if (!enabled()) return false;

    const int32_t trackX = 9;
    const int32_t span = std::max(1, frameWidth() - 18);

    switch (ev.type) {
    case EventType::MouseMove: {
        if (dragging_ && dragThumb_ != Thumb::None) {
            applyFromLocalX(ev.x, dragThumb_, true);
            return true;
        }
        const Thumb hover = thumbAt(ev.x, trackX, span);
        if (hover != hoverThumb_) {
            hoverThumb_ = hover;
            if (laidOut()) updateVisuals();
        }
        return false;
    }

    case EventType::MouseDown: {
        dragThumb_ = thumbAt(ev.x, trackX, span);
        if (dragThumb_ == Thumb::None) {
            // 点击轨道空白：移动更近的滑块
            const float t = clampValue(
                static_cast<float>(ev.x - trackX) / static_cast<float>(span), 0.0f, 1.0f);
            const float value = minimum_ + t * (maximum_ - minimum_);
            const float distLow = std::fabs(value - low_);
            const float distHigh = std::fabs(value - high_);
            dragThumb_ = distLow <= distHigh ? Thumb::Low : Thumb::High;
        }
        activeThumb_ = dragThumb_;
        dragging_ = true;
        applyFromLocalX(ev.x, dragThumb_, true);
        return true;
    }

    case EventType::MouseUp:
        if (!dragging_) return false;
        applyFromLocalX(ev.x, dragThumb_, true);
        dragging_ = false;
        dragThumb_ = Thumb::None;
        return true;

    case EventType::KeyDown: {
        const float unit = step_ > 0.0f ? step_ : (maximum_ - minimum_) / 100.0f;
        const bool lowActive = activeThumb_ != Thumb::High;
        if (ev.key == kKeyLeft || ev.key == kKeyDown) {
            if (lowActive) commitValues(low_ - unit, high_, true);
            else           commitValues(low_, high_ - unit, true);
            return true;
        }
        if (ev.key == kKeyRight || ev.key == kKeyUp) {
            if (lowActive) commitValues(low_ + unit, high_, true);
            else           commitValues(low_, high_ + unit, true);
            return true;
        }
        if (ev.key == kKeyHome) {
            commitValues(minimum_, high_, true);
            return true;
        }
        if (ev.key == kKeyEnd) {
            commitValues(low_, maximum_, true);
            return true;
        }
        if (ev.key == kKeySpace || ev.key == kKeyEnter) {
            activeThumb_ = lowActive ? Thumb::High : Thumb::Low;
            if (laidOut()) updateVisuals();
            return true;
        }
        return FluentControl::onEvent(ev);
    }

    default:
        return FluentControl::onEvent(ev);
    }
}

void RangeSlider::onStateChanged() {
    if (!hovered() && !dragging_ && hoverThumb_ != Thumb::None) {
        hoverThumb_ = Thumb::None;
        if (laidOut()) updateVisuals();
    }
}

// ===========================================================================
// ComboBoxPopup — View 根级浮出层
// ===========================================================================
class ComboBoxPopup final : public Widget {
public:
    ComboBoxPopup(std::shared_ptr<ComboBoxModel> model, int32_t itemHeight)
        : model_(std::move(model)), itemHeight_(itemHeight) {
        build();
    }

    Vec2i onMeasure(const Vec2i& available) const override {
        (void)available;
        return Vec2i{0, 0};
    }

    void onLayout(const Rect& frame) override {
        (void)frame;
        if (!laidOut_) {
            laidOut_ = true;
        }
        updateVisuals();
    }

    void onTick(float dtSeconds) override {
        (void)dtSeconds;
        if (!model_ || !model_->ownerAlive || dismissed_) {
            if (!hidden_) {
                for (uint32_t id : itemBackgroundIds_) {
                    canvas().updateShape(id, [](Shape& shape) { shape.base.visible = false; });
                }
                for (uint32_t id : itemTextIds_) {
                    canvas().updateShape(id, [](Shape& shape) { shape.base.visible = false; });
                }
                canvas().updateShape(backgroundId_, [](Shape& shape) {
                    shape.base.visible = false;
                });
                canvas().updateShape(blurId_, [](Shape& shape) {
                    shape.base.visible = false;
                });
                hidden_ = true;
            }
            return;
        }

        if (!hovered() && hoverIndex_ >= 0) {
            hoverIndex_ = -1;
            updateVisuals();
        }
    }

    bool onEvent(const Event& ev) override {
        if (dismissed_ || hidden_ || !model_) return false;

        switch (ev.type) {
        case EventType::MouseMove: {
            const int32_t index = itemIndexAt(ev.y);
            if (index != hoverIndex_) {
                hoverIndex_ = index;
                updateVisuals();
            }
            return true;
        }

        case EventType::MouseDown: {
            const int32_t index = itemIndexAt(ev.y);
            if (index >= 0) {
                hoverIndex_ = index;
                updateVisuals();
                return true;
            }
            return false;
        }

        case EventType::MouseUp: {
            const int32_t index = itemIndexAt(ev.y);
            if (index >= 0 && index < static_cast<int32_t>(model_->items.size())) {
                model_->selected = index;
                ++model_->revision;
                dismissed_ = true;
                return true;
            }
            return false;
        }

        default:
            return false;
        }
    }

    bool hitTest(const Vec2i& localPoint) const override {
        if (dismissed_ || hidden_ || !model_ || !model_->ownerAlive) return false;
        return Widget::hitTest(localPoint);
    }

    bool dismissed() const {
        return dismissed_ || hidden_ || !model_ || !model_->ownerAlive;
    }

private:
    int32_t itemIndexAt(int32_t y) const {
        if (y < 4) return -1;
        const int32_t index = (y - 4) / itemHeight_;
        if (index < 0 || index >= static_cast<int32_t>(itemBackgroundIds_.size())) return -1;
        const int32_t rowY = 4 + index * itemHeight_;
        if (y >= rowY + itemHeight_ - 2) return -1;
        return index;
    }

    void build() {
        // 浮出层是内部控件，不参与 .css 选择器，沿用全局主题
        // 1) Blur Behind：先放模糊层，背景 / 列表项都绘制在它上面
        Shape blur = makeBlurShape(Rect{0, 0, 1, 1}, kPopupBlurSigma,
                                   currentTheme().radiusOverlay);
        blurId_ = canvas().addShape(std::move(blur));

        // 2) 半透明蒙层（让下层模糊透出，同时保持 Fluent 的层次感）
        Shape background = makeRRectShape(
            Rect{0, 0, 1, 1}, currentTheme().radiusOverlay,
            withOpacity(currentTheme().overlay, kPopupTintOpacity),
            currentTheme().strokeStrong, 1);
        backgroundId_ = canvas().addShape(std::move(background));

        const size_t count = model_ ? model_->items.size() : 0;
        itemBackgroundIds_.reserve(count);
        itemTextIds_.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            Shape row = makeRRectShape(Rect{0, 0, 1, 1}, 4.0f, Color::nullColor());
            itemBackgroundIds_.push_back(canvas().addShape(std::move(row)));
            Shape text = makeTextShape(model_->items[i], 14, currentTheme().textPrimary);
            itemTextIds_.push_back(canvas().addShape(std::move(text)));
        }
    }

    void updateVisuals() {
        if (!model_) return;
        const int32_t width = frame().w;
        const int32_t height = frame().h;
        if (width <= 0 || height <= 0) return;

        canvas().updateShape(blurId_, [&](Shape& shape) {
            shape.data = BlurShape{0, 0, width, height, kPopupBlurSigma,
                                   currentTheme().radiusOverlay};
            shape.base.visible = true;
        });

        canvas().updateShape(backgroundId_, [&](Shape& shape) {
            shape.data = RRectShape{0, 0, width, height,
                                    currentTheme().radiusOverlay, currentTheme().radiusOverlay};
            shape.base.fill = withOpacity(currentTheme().overlay, kPopupTintOpacity);
            shape.base.stroke = currentTheme().strokeStrong;
            shape.base.strokeWidth = 1;
            shape.base.visible = true;
        });

        for (size_t i = 0; i < itemBackgroundIds_.size(); ++i) {
            const int32_t rowY = 4 + static_cast<int32_t>(i) * itemHeight_;
            const bool selected = static_cast<int32_t>(i) == model_->selected;
            const bool hover = static_cast<int32_t>(i) == hoverIndex_;
            if (rowY + itemHeight_ > height) break;

            Color fill = Color::nullColor();
            if (selected) fill = withOpacity(currentTheme().accent, 0.18f);
            if (hover) fill = currentTheme().subtleHover;

            canvas().updateShape(itemBackgroundIds_[i], [&](Shape& shape) {
                shape.data = RRectShape{4, rowY, width - 8, itemHeight_ - 2, 4.0f, 4.0f};
                shape.base.fill = fill;
                shape.base.visible = fill.a > 0;
            });

            const std::string text = model_->items[i];
            const Rect measured = measureTextLine(canvas(), text, 14);
            const int32_t baseline = rowY + (itemHeight_ - 2 - measured.h) / 2 - measured.y;
            canvas().updateShape(itemTextIds_[i], [&](Shape& shape) {
                setShapeText(shape, text, 14,
                        selected ? currentTheme().textPrimary : currentTheme().textSecondary,
                        12, baseline);
            });
        }
    }

    std::shared_ptr<ComboBoxModel> model_;
    int32_t itemHeight_ = 32;
    int32_t hoverIndex_ = -1;
    bool dismissed_ = false;
    bool hidden_ = false;
    bool laidOut_ = false;
    uint32_t blurId_ = 0;
    uint32_t backgroundId_ = 0;
    std::vector<uint32_t> itemBackgroundIds_;
    std::vector<uint32_t> itemTextIds_;
};

// ===========================================================================
// ComboBox
// ===========================================================================
ComboBox::ComboBox(std::vector<std::string> items)
    : model_(std::make_shared<ComboBoxModel>()) {
    model_->items = std::move(items);
    if (!model_->items.empty()) model_->selected = 0;
    seenRevision_ = model_->revision;

    Shape background = makeRRectShape(
        Rect{0, 0, 1, 1}, theme().radiusControl,
        theme().controlFill, theme().stroke, 1);
    backgroundId_ = canvas().addShape(std::move(background));

    Shape text = makeTextShape(placeholder_, fontSize_, theme().textTertiary);
    labelId_ = canvas().addShape(std::move(text));

    Shape chevron = makeChevronShape(0, 0, 9, 5, true, theme().textSecondary, 2);
    chevronId_ = canvas().addShape(std::move(chevron));
}

ComboBox::~ComboBox() {
    // 浮出层持有 shared model；ownerAlive=false 让它在下一帧自行隐藏。
    model_->ownerAlive = false;
}

void ComboBox::setItems(std::vector<std::string> items) {
    model_->items = std::move(items);
    if (model_->selected >= static_cast<int32_t>(model_->items.size())) {
        model_->selected = model_->items.empty() ? -1 : static_cast<int32_t>(model_->items.size()) - 1;
    }
    ++model_->revision;
    seenRevision_ = model_->revision;
    measured_ = Rect::null();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

const std::vector<std::string>& ComboBox::items() const {
    return model_->items;
}

void ComboBox::setSelectedIndex(int32_t index) {
    if (index < -1 || index >= static_cast<int32_t>(model_->items.size())) return;
    if (index == model_->selected) return;
    selectIndex(index);
    // 程序化设置不触发 onSelectionChanged（与用户操作区分）
    seenRevision_ = model_->revision;
    if (laidOut()) updateVisuals();
}

void ComboBox::selectIndex(int32_t index) {
    model_->selected = index;
    ++model_->revision;
}

std::string ComboBox::selectedText() const {
    if (model_->selected < 0 ||
        model_->selected >= static_cast<int32_t>(model_->items.size())) {
        return placeholder_;
    }
    return model_->items[static_cast<size_t>(model_->selected)];
}

void ComboBox::setPlaceholder(std::string placeholder) {
    placeholder_ = std::move(placeholder);
    measured_ = Rect::null();
    if (laidOut()) updateVisuals();
}

Vec2i ComboBox::onMeasure(const Vec2i& available) const {
    (void)available;
    int32_t widest = measureString(placeholder_, fontSize_).w;
    for (const std::string& item : model_->items) {
        widest = std::max(widest, measureString(item, fontSize_).w);
    }
    measured_ = measureString(selectedText(), fontSize_);
    return Vec2i{std::max(120, widest + 56), theme().controlHeight};
}

void ComboBox::updateVisuals() {
    if (!laidOut()) return;
    measured_ = measureString(selectedText(), fontSize_);

    const Color fill = enabled()
        ? interactiveFill(theme().controlFill, theme().controlFillHover, theme().controlFillPressed)
        : theme().controlFillDisabled;
    Color stroke = focusStroke(theme().stroke);
    if (!enabled()) stroke = theme().stroke;

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, frameWidth(), frameHeight(),
                                theme().radiusControl, theme().radiusControl};
        shape.base.fill = fill;
        shape.base.stroke = stroke;
        shape.base.strokeWidth = focusAmount() > 0.15f ? 2 : 1;
    });

    const bool hasSelection = model_->selected >= 0;
    const std::string shownText = ellipsizeText(canvas(), selectedText(), fontSize_,
                                                std::max(0, frameWidth() - 48));
    measured_ = measureString(shownText, fontSize_);
    const int32_t baseline = baselineForCenter(measured_, frameHeight());
    const Color textColor = enabled()
        ? (hasSelection ? theme().textPrimary : theme().textTertiary)
        : theme().textDisabled;
    canvas().updateShape(labelId_, [&](Shape& shape) {
        setShapeText(shape, shownText, fontSize_, textColor, 12, baseline);
    });

    canvas().updateShape(chevronId_, [&](Shape& shape) {
        const int32_t w = 9;
        const int32_t h = 5;
        shape = makeChevronShape(frameWidth() - 22, (frameHeight() - h) / 2 + 1,
                                 w, h, true, textColor, 2);
    });
}

void ComboBox::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);

    if (model_->revision != seenRevision_) {
        seenRevision_ = model_->revision;
        if (laidOut()) updateVisuals();
        if (onSelectionChanged) onSelectionChanged(model_->selected);
    }

    if (popup_) {
        if (popup_->dismissed()) {
            closePopup();
        } else if (popup_->parent() == nullptr) {
            popup_ = nullptr;
        }
    }
}

bool ComboBox::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseDown:
        return true;

    case EventType::MouseUp:
        if (clicked()) {
            if (popup_) closePopup();
            else openPopup();
        }
        return true;

    case EventType::KeyDown: {
        if (ev.key == kKeySpace || ev.key == kKeyEnter) {
            if (popup_) closePopup();
            else openPopup();
            return true;
        }
        if (ev.key == kKeyDown || ev.key == kKeyUp) {
            const int32_t count = static_cast<int32_t>(model_->items.size());
            if (count <= 0) return true;
            const int32_t next = ev.key == kKeyDown
                ? std::min(count - 1, model_->selected + 1)
                : std::max(0, model_->selected - 1);
            selectIndex(next);
            return true;
        }
        if (ev.key == kKeyEscape) {
            if (popup_) {
                closePopup();
                return true;
            }
            return false;
        }
        return false;
    }

    default:
        return false;
    }
}

void ComboBox::openPopup() {
    if (popup_ || model_->items.empty() || !view()) return;

    const int32_t itemHeight = 32;
    const int32_t width = std::max(120, frameWidth());
    const int32_t height = static_cast<int32_t>(model_->items.size()) * itemHeight + 8;

    auto* popup = new ComboBoxPopup(model_, itemHeight);

    const Rect absolute = absoluteFrame();
    View* v = view();
    const int32_t padLeft = v->style().padding.left;
    const int32_t padTop = v->style().padding.top;

    int32_t y = absolute.y + absolute.h + 4 - padTop;
    if (y + height > v->height() - 8) {
        y = absolute.y - height - 4 - padTop;
    }
    y = std::max(0, y);

    popup->style().position = Position::Absolute;
    popup->style().x = std::max(0, absolute.x - padLeft);
    popup->style().y = y;
    popup->style().width = Dimension::px(width);
    popup->style().height = Dimension::px(height);
    popup->style().clip = true;

    v->addChild(std::unique_ptr<Widget>(popup));
    popup_ = popup;
}

void ComboBox::closePopup() {
    if (!popup_) return;
    View* v = view();
    if (v && popup_->parent() == v) {
        // ComboBox 可能在 onTick 中关闭浮出层；此时 View 正在遍历 children_，
        // 必须延迟到遍历结束后再 removeChild，避免迭代器失效。
        v->deferRemoveWidget(popup_);
    }
    popup_ = nullptr;
}

// ===========================================================================
// RatingControl
// ===========================================================================
RatingControl::RatingControl(float value, int32_t maxRating)
    : value_(clampValue(value, 0.0f, static_cast<float>(maxRating))),
      maxRating_(std::max(1, maxRating)) {
    rebuildStars();
}

void RatingControl::rebuildStars() {
    // 星级在构造 / 改变 maxRating 时重建
    canvas().clear();
    starIds_.clear();

    for (int32_t i = 0; i < maxRating_; ++i) {
        const int32_t cx = 10 + i * 24;
        Shape star;
        star.data = StarShape{cx, 10, 9.0f, 4.0f, 5, 0.0f};
        star.base.fill = theme().controlFill;
        star.base.stroke = theme().stroke;
        star.base.strokeWidth = 1;
        starIds_.push_back(canvas().addShape(std::move(star)));
    }
}

void RatingControl::setValue(float value) {
    const float next = clampValue(value, 0.0f, static_cast<float>(maxRating_));
    if (std::fabs(next - value_) < 1.0e-6f) return;
    value_ = next;
    if (laidOut()) updateVisuals();
}

void RatingControl::setMaxRating(int32_t maxRating) {
    const int32_t next = std::max(1, maxRating);
    if (next == maxRating_) return;
    maxRating_ = next;
    value_ = clampValue(value_, 0.0f, static_cast<float>(maxRating_));
    rebuildStars();
    invalidateLayout();
}

Vec2i RatingControl::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{maxRating_ * 24 + 2, theme().controlHeight};
}

int32_t RatingControl::starIndexAt(int32_t localX) const {
    if (localX < 0) return -1;
    const int32_t index = localX / 24;
    if (index < 0 || index >= maxRating_) return -1;
    return index;
}

void RatingControl::updateVisuals() {
    if (!laidOut()) return;
    const float shown = hoverStar_ >= 0 ? static_cast<float>(hoverStar_ + 1) : value_;
    const int32_t centerY = frameHeight() / 2;

    for (int32_t i = 0; i < maxRating_ && i < static_cast<int32_t>(starIds_.size()); ++i) {
        const bool filled = static_cast<float>(i) + 1.0f <= shown + 1.0e-4f;
        const Color fill = filled ? theme().accent : theme().controlFill;
        const Color stroke = filled ? focusStroke(theme().accent) : focusStroke(theme().stroke);
        canvas().updateShape(starIds_[static_cast<size_t>(i)], [&](Shape& shape) {
            shape.data = StarShape{10 + i * 24, centerY, 9.0f, 4.0f, 5, 0.0f};
            shape.base.fill = enabled() ? fill : theme().controlFillDisabled;
            shape.base.stroke = enabled() ? stroke : theme().stroke;
            shape.base.strokeWidth = 1;
        });
    }
}

bool RatingControl::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseMove: {
        const int32_t index = starIndexAt(ev.x);
        if (index != hoverStar_) {
            hoverStar_ = index;
            if (laidOut()) updateVisuals();
        }
        return true;
    }
    case EventType::MouseDown: {
        const int32_t index = starIndexAt(ev.x);
        if (index >= 0) {
            hoverStar_ = index;
            if (laidOut()) updateVisuals();
        }
        return true;
    }
    case EventType::MouseUp: {
        const int32_t index = starIndexAt(ev.x);
        if (clicked() && index >= 0) commitValue(static_cast<float>(index + 1));
        return true;
    }
    case EventType::KeyDown: {
        if (ev.key == kKeyLeft || ev.key == kKeyDown) {
            commitValue(value_ - 1.0f);
            return true;
        }
        if (ev.key == kKeyRight || ev.key == kKeyUp) {
            commitValue(value_ + 1.0f);
            return true;
        }
        if (ev.key == kKeyHome) {
            commitValue(0.0f);
            return true;
        }
        if (ev.key == kKeyEnd) {
            commitValue(static_cast<float>(maxRating_));
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

void RatingControl::onStateChanged() {
    if (!hovered() && hoverStar_ >= 0) {
        hoverStar_ = -1;
        if (laidOut()) updateVisuals();
    }
}

void RatingControl::commitValue(float value) {
    const float next = clampValue(value, 0.0f, static_cast<float>(maxRating_));
    if (std::fabs(next - value_) < 1.0e-6f) return;
    value_ = next;
    if (laidOut()) updateVisuals();
    if (onValueChanged) onValueChanged(value_);
}

} // namespace fluent
} // namespace modern_ui
