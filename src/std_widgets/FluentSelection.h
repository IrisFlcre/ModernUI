// ============================================================================
// FluentSelection.h — 选择类控件
// ============================================================================
//
//   CheckBox      — 20px 圆角勾选框 + 标签（选中进度动画）
//   RadioButton   — 圆形单选（支持 group 互斥）
//   ToggleSwitch  — 40×20 开关（滑块位移动画）
//   SegmentedControl — 分段选择（滑块胶囊平滑移动）
//   Slider        — 轨道 / 已填充段 / 可拖拽滑块
//   RangeSlider   — 双滑块区间选择（低 / 高值 + 悬停放大）
//   ComboBox      — 下拉选择（浮出列表由 View 根级 Popup 承载）
//   RatingControl — 五星评分（悬停预览 + 点击）
//
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "FluentControl.h"

namespace modern_ui {
namespace fluent {

// ComboBox 的共享数据：浮出列表与控件通过 shared_ptr 解耦生命周期
struct ComboBoxModel {
    std::vector<std::string> items;
    int32_t selected = -1;
    uint32_t revision = 0;
    bool ownerAlive = true;
};

class ComboBoxPopup;

// ===========================================================================
class CheckBox : public FluentControl {
public:
    using CheckedHandler = std::function<void(bool)>;

    explicit CheckBox(std::string label = {}, bool checked = false);

    void setLabel(std::string label);
    const std::string& label() const { return label_; }

    void setChecked(bool checked);
    bool checked() const { return checked_; }

    CheckedHandler onCheckedChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;

protected:
    void activate() override;

private:
    std::string label_;
    int32_t fontSize_ = 14;
    bool checked_ = false;
    float checkProgress_ = 0.0f;
    mutable Rect measured_{};
    uint32_t boxId_ = 0;
    uint32_t checkId_ = 0;
    uint32_t labelId_ = 0;
};

// ===========================================================================
class RadioButton : public FluentControl {
public:
    explicit RadioButton(std::string label = {}, bool checked = false);
    ~RadioButton() override;

    void setLabel(std::string label);
    const std::string& label() const { return label_; }

    void setChecked(bool checked);
    bool checked() const { return checked_; }

    // 同一 group 内互斥（空字符串 = 不分组）
    void setGroup(std::string group);
    const std::string& group() const { return group_; }

    std::function<void(bool)> onCheckedChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;

protected:
    void activate() override;

private:
    void uncheckGroupSiblings();

    std::string label_;
    std::string group_;
    int32_t fontSize_ = 14;
    bool checked_ = false;
    float checkProgress_ = 0.0f;
    mutable Rect measured_{};
    uint32_t ringId_ = 0;
    uint32_t dotId_ = 0;
    uint32_t labelId_ = 0;
};

// ===========================================================================
class ToggleSwitch : public FluentControl {
public:
    explicit ToggleSwitch(std::string label = {}, bool checked = false);

    void setLabel(std::string label);
    const std::string& label() const { return label_; }

    void setChecked(bool checked);
    bool checked() const { return checked_; }

    std::function<void(bool)> onToggled;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;

protected:
    void activate() override;

private:
    std::string label_;
    int32_t fontSize_ = 14;
    bool checked_ = false;
    float knobProgress_ = 0.0f;
    mutable Rect measured_{};
    uint32_t trackId_ = 0;
    uint32_t knobId_ = 0;
    uint32_t labelId_ = 0;
};

// ===========================================================================
class SegmentedControl : public FluentControl {
public:
    explicit SegmentedControl(std::vector<std::string> items = {});

    void setItems(std::vector<std::string> items);
    const std::vector<std::string>& items() const { return items_; }

    void setSelectedIndex(int32_t index);
    int32_t selectedIndex() const { return selectedIndex_; }

    std::function<void(int32_t)> onSelectionChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    void rebuildLabels();
    void computeSegment(int32_t totalWidth, int32_t count, int32_t segment,
                        int32_t& x, int32_t& width) const;
    int32_t indexAt(int32_t localX) const;
    void commitSelection(int32_t index, bool notify);

    std::vector<std::string> items_;
    std::vector<uint32_t> labelIds_;
    int32_t selectedIndex_ = 0;
    int32_t hoverIndex_ = -1;
    int32_t fontSize_ = 13;

    float indicatorX_ = 0.0f;
    float indicatorWidth_ = 0.0f;
    bool  indicatorReady_ = false;

    mutable std::vector<Rect> measured_;
    uint32_t trackId_ = 0;
    uint32_t indicatorId_ = 0;
};

// ===========================================================================
class Slider : public FluentControl {
public:
    explicit Slider(float value = 0.0f, float minimum = 0.0f, float maximum = 1.0f,
                    float step = 0.0f);

    void setValue(float value);
    float value() const { return value_; }

    void setRange(float minimum, float maximum);
    void setStep(float step) { step_ = std::max(0.0f, step); }

    std::function<void(float)> onValueChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;

private:
    void applyFromLocalX(int32_t localX, bool notify);
    void commitValue(float next, bool notify);
    float normalized() const;

    float value_ = 0.0f;
    float minimum_ = 0.0f;
    float maximum_ = 1.0f;
    float step_ = 0.0f;
    bool dragging_ = false;
    int32_t fontSize_ = 12;

    uint32_t trackId_ = 0;
    uint32_t fillId_ = 0;
    uint32_t knobId_ = 0;
};

// ===========================================================================
class RangeSlider : public FluentControl {
public:
    explicit RangeSlider(float low = 0.25f, float high = 0.75f,
                         float minimum = 0.0f, float maximum = 1.0f,
                         float step = 0.0f);

    void setValues(float low, float high);
    float low() const { return low_; }
    float high() const { return high_; }

    void setRange(float minimum, float maximum);
    void setStep(float step) { step_ = std::max(0.0f, step); }

    // (low, high)
    std::function<void(float, float)> onRangeChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    enum class Thumb : uint8_t { None = 0, Low, High };

    float normalizedLow() const;
    float normalizedHigh() const;
    int32_t thumbX(float normalized, int32_t trackX, int32_t span) const;
    Thumb thumbAt(int32_t localX, int32_t trackX, int32_t span) const;
    void applyFromLocalX(int32_t localX, Thumb thumb, bool notify);
    void commitValues(float low, float high, bool notify);

    float low_ = 0.25f;
    float high_ = 0.75f;
    float minimum_ = 0.0f;
    float maximum_ = 1.0f;
    float step_ = 0.0f;

    Thumb activeThumb_ = Thumb::None;
    Thumb dragThumb_ = Thumb::None;
    Thumb hoverThumb_ = Thumb::None;
    float lowHover_ = 0.0f;
    float highHover_ = 0.0f;
    bool dragging_ = false;

    uint32_t trackId_ = 0;
    uint32_t fillId_ = 0;
    uint32_t lowKnobId_ = 0;
    uint32_t highKnobId_ = 0;
    uint32_t lowGlowId_ = 0;
    uint32_t highGlowId_ = 0;
};

// ===========================================================================
class ComboBox : public FluentControl {
public:
    explicit ComboBox(std::vector<std::string> items = {});
    ~ComboBox() override;

    void setItems(std::vector<std::string> items);
    const std::vector<std::string>& items() const;

    void setSelectedIndex(int32_t index);
    int32_t selectedIndex() const { return model_->selected; }
    std::string selectedText() const;

    void setPlaceholder(std::string placeholder);

    std::function<void(int32_t)> onSelectionChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;
    bool onEvent(const Event& ev) override;

private:
    void openPopup();
    void closePopup();
    void selectIndex(int32_t index);

    std::shared_ptr<ComboBoxModel> model_;
    ComboBoxPopup* popup_ = nullptr;
    uint32_t seenRevision_ = 0;
    std::string placeholder_ = "Select an item";
    int32_t fontSize_ = 14;

    mutable Rect measured_{};
    uint32_t backgroundId_ = 0;
    uint32_t labelId_ = 0;
    uint32_t chevronId_ = 0;
};

// ===========================================================================
class RatingControl : public FluentControl {
public:
    explicit RatingControl(float value = 0.0f, int32_t maxRating = 5);

    void setValue(float value);
    float value() const { return value_; }

    void setMaxRating(int32_t maxRating);
    int32_t maxRating() const { return maxRating_; }

    std::function<void(float)> onValueChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    void rebuildStars();
    int32_t starIndexAt(int32_t localX) const;
    void commitValue(float value);

    float value_ = 0.0f;
    int32_t maxRating_ = 5;
    int32_t hoverStar_ = -1;
    std::vector<uint32_t> starIds_;
};

} // namespace fluent
} // namespace modern_ui
