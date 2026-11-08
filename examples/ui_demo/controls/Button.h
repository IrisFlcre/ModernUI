// ============================================================================
// controls/Button.h — 按钮控件
// ============================================================================
//
// 演示：
//   - 构造函数：创建圆角矩形背景 + 文本两个几何体
//   - onMeasure：文本度量 + 内边距得到固有尺寸
//   - onEvent：处理事件并修改几何体（点击回调、状态配色）
//   - onStateChanged：hover / pressed / focused 变化时同步配色
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "Widget.h"

namespace demo {

using modern_ui::Color;
using modern_ui::Event;
using modern_ui::Rect;
using modern_ui::Vec2i;
using modern_ui::Widget;

class Button : public Widget {
public:
    using ClickHandler = std::function<void()>;

    explicit Button(std::string label, int32_t fontSize = 14);
    ~Button() override;

    Button(const Button&) = delete;
    Button& operator=(const Button&) = delete;

    ClickHandler onClick;                 // 点击回调（在 onEvent 内触发）

    void setLabel(std::string label);
    void setColors(Color base, Color hover, Color pressed);

    Vec2i onMeasure(const Vec2i& available) const override;
    void onLayout(const Rect& frame) override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;
    bool acceptsFocus() const override { return true; }

private:
    Rect measureText() const;
    void writeGeometry();
    void updateColors();

    uint32_t backgroundId_ = 0;
    uint32_t labelId_ = 0;
    std::string label_;
    int32_t fontSize_ = 14;
    mutable Rect measured_{};
    Rect lastFrame_{};
    Color fillBase_{64, 118, 255, 255};
    Color fillHover_{88, 142, 255, 255};
    Color fillPressed_{44, 88, 206, 255};
};

} // namespace demo
