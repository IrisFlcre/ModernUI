// ============================================================================
// FluentTypography.h — 文本类控件
// ============================================================================
//
//   TextBlock — WinUI TextBlock 的单行实现（字号 / 颜色 / 自动主题色）
//   Divider   — 1px 分割线（水平 / 垂直）
//
// ============================================================================

#pragma once

#include <cstdint>
#include <string>

#include "FluentControl.h"

namespace modern_ui {
namespace fluent {

class TextBlock : public FluentControl {
public:
    explicit TextBlock(std::string text = {}, int32_t fontSize = 14,
                       Color color = Color::nullColor());

    void setText(std::string text);
    const std::string& text() const { return text_; }

    void setFontSize(int32_t fontSize);
    int32_t fontSize() const { return fontSize_; }

    // alpha = 0 表示跟随主题 textPrimary
    void setColor(Color color);

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool acceptsFocus() const override { return false; }

    // 文本对齐
    enum class Align : uint8_t { Left = 0, Center, Right };
    void setAlign(Align align) { align_ = align; }

private:
    std::string text_;
    int32_t fontSize_ = 14;
    Color color_;
    Align align_ = Align::Left;
    mutable Rect measured_{};
    uint32_t shapeId_ = 0;
};

class Divider : public FluentControl {
public:
    explicit Divider(bool vertical = false);

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool acceptsFocus() const override { return false; }

private:
    bool vertical_ = false;
    uint32_t lineId_ = 0;
};

} // namespace fluent
} // namespace modern_ui
