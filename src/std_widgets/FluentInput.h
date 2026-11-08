// ============================================================================
// FluentInput.h — 文本输入类控件
// ============================================================================
//
//   TextBox     — 单行文本输入（聚焦下划线、闪烁光标、UTF-8 字符输入）
//   PasswordBox — TextBox 的密码模式（按码点显示圆点）
//   NumberBox   — 数值输入（左右增减区 / 键盘上下 / 步长与范围）
//
// 说明：
//   - 光标固定在文本末尾；输入通过 Event::Char 的 Unicode 码点完成。
//   - 文本度量依赖 View 下发的 TextMetricProvider，没有时退化为估算。
//
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "FluentControl.h"

namespace modern_ui {
namespace fluent {

class TextBox : public FluentControl {
public:
    explicit TextBox(std::string placeholder = "Enter text", int32_t fontSize = 14);

    void setText(std::string text);
    const std::string& text() const { return text_; }

    void setPlaceholder(std::string placeholder);
    const std::string& placeholder() const { return placeholder_; }

    void setFontSize(int32_t fontSize);

    std::function<void(const std::string&)> onTextChanged;
    std::function<void(const std::string&)> onSubmit;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

protected:
    // PasswordBox 覆写：返回用于显示的文本
    virtual std::string visualText() const { return text_; }
    const std::string& rawText() const { return text_; }

private:
    void notifyChanged();
    void resetCaretBlink();

    std::string text_;
    std::string placeholder_;
    int32_t fontSize_ = 14;

    float caretTimer_ = 0.0f;
    bool caretVisible_ = true;
    bool wasFocused_ = false;

    mutable Rect measured_{};

    uint32_t backgroundId_ = 0;
    uint32_t textId_ = 0;
    uint32_t caretId_ = 0;
    uint32_t underlineId_ = 0;
};

class PasswordBox : public TextBox {
public:
    explicit PasswordBox(std::string placeholder = "Enter password");

protected:
    std::string visualText() const override;
};

class NumberBox : public FluentControl {
public:
    explicit NumberBox(double value = 0.0, double minimum = -1.0e9, double maximum = 1.0e9,
                       double step = 1.0);

    void setValue(double value);
    double value() const { return value_; }

    void setRange(double minimum, double maximum);
    void setStep(double step) { step_ = step > 0.0 ? step : 1.0; }
    void setDecimals(int32_t decimals);

    std::function<void(double)> onValueChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    void commitValue(double value);
    std::string formattedValue() const;
    int32_t sideAt(int32_t localX) const;   // -1 减 / 0 中 / 1 加

    double value_ = 0.0;
    double minimum_ = -1.0e9;
    double maximum_ = 1.0e9;
    double step_ = 1.0;
    int32_t decimals_ = 0;
    int32_t hoverSide_ = 0;

    mutable Rect measured_{};

    uint32_t backgroundId_ = 0;
    uint32_t minusAreaId_ = 0;
    uint32_t plusAreaId_ = 0;
    uint32_t minusIconId_ = 0;
    uint32_t plusIconId_ = 0;
    uint32_t valueId_ = 0;
};

} // namespace fluent
} // namespace modern_ui
