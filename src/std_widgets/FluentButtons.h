// ============================================================================
// FluentButtons.h — Windows 11 Fluent 按钮族
// ============================================================================
//
//   Button           — Standard / Accent / Subtle 三种外观
//   ToggleButton     — 可保持选中态（选中时使用强调色）
//   RepeatButton     — 按住连续触发
//   HyperlinkButton  — 强调色文本 + 下划线
//   DropDownButton   — 右侧 chevron（用于展开浮出层）
//   SplitButton      — 主按钮 + 右侧下拉区域，双回调
//
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "FluentControl.h"

namespace modern_ui {
namespace fluent {

class Button : public FluentControl {
public:
    enum class Style : uint8_t { Standard = 0, Accent, Subtle, Selected };
    enum class ContentAlign : uint8_t { Center = 0, Left };

    explicit Button(std::string label = "Button",
                    Style style = Style::Standard,
                    int32_t fontSize = 14);

    void setLabel(std::string label);
    const std::string& label() const { return label_; }

    void setButtonStyle(Style style);
    Style buttonStyle() const { return style_; }

    // 文本水平对齐 / 左侧内缩（组合控件如树行、列表行使用）
    void setContentAlign(ContentAlign align);
    ContentAlign contentAlign() const { return contentAlign_; }
    void setContentPadding(int32_t padding);
    int32_t contentPadding() const { return contentPadding_; }

    void setFontSize(int32_t fontSize);

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;

protected:
    void activate() override;
    virtual bool isToggled() const { return false; }

    // DropDownButton 使用：为 chevron 预留宽度
    void setShowChevron(bool show);
    bool showChevron() const { return showChevron_; }

    std::string label_;
    int32_t fontSize_ = 14;
    Style style_ = Style::Standard;
    ContentAlign contentAlign_ = ContentAlign::Center;
    int32_t contentPadding_ = 16;

private:
    mutable Rect measured_{};
    uint32_t backgroundId_ = 0;
    uint32_t labelId_ = 0;
    uint32_t chevronId_ = 0;
    bool showChevron_ = false;
};

class ToggleButton : public Button {
public:
    using ToggleHandler = std::function<void(bool)>;

    explicit ToggleButton(std::string label = "Toggle",
                          Style style = Style::Standard,
                          int32_t fontSize = 14);

    ToggleHandler onToggled;

    void setChecked(bool checked);
    bool checked() const { return checked_; }

protected:
    void activate() override;
    bool isToggled() const override { return checked_; }

private:
    bool checked_ = false;
};

class RepeatButton : public Button {
public:
    explicit RepeatButton(std::string label = "Repeat");

    bool onEvent(const Event& ev) override;
    void onTick(float dtSeconds) override;

protected:
    void activate() override {}   // 由按住状态自行触发

private:
    void fire();

    bool repeating_ = false;
    bool fired_ = false;
    float elapsed_ = 0.0f;
};

class HyperlinkButton : public FluentControl {
public:
    explicit HyperlinkButton(std::string label = "Hyperlink", int32_t fontSize = 14);

    void setLabel(std::string label);
    const std::string& label() const { return label_; }

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;

private:
    std::string label_;
    int32_t fontSize_ = 14;
    mutable Rect measured_{};
    uint32_t textId_ = 0;
    uint32_t underlineId_ = 0;
};

class DropDownButton : public Button {
public:
    explicit DropDownButton(std::string label = "DropDown");
    using DropDownHandler = std::function<void()>;
    DropDownHandler onDropDown;

protected:
    void activate() override;
};

class SplitButton : public FluentControl {
public:
    explicit SplitButton(std::string label = "Split");

    std::function<void()> onPrimaryClick;
    std::function<void()> onSecondaryClick;

    void setLabel(std::string label);
    const std::string& label() const { return label_; }

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;

private:
    void primary();
    void secondary();

    std::string label_;
    int32_t fontSize_ = 14;
    mutable Rect measured_{};
    uint32_t backgroundId_ = 0;
    uint32_t labelId_ = 0;
    uint32_t dividerId_ = 0;
    uint32_t chevronId_ = 0;
};

} // namespace fluent
} // namespace modern_ui
