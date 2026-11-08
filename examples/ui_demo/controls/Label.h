// ============================================================================
// controls/Label.h — 单行文本控件
// ============================================================================
//
// 演示：
//   - 构造函数：创建 TextShape（相对坐标）
//   - onMeasure：用后端 / provider 的字形度量自适应固有尺寸
//   - onLayout：按最终 frame 对齐文本（仍然是子 Canvas 内的相对坐标）
//   - setText 改变文本 → invalidateLayout 请求重新排版
// ============================================================================

#pragma once

#include <cstdint>
#include <string>

#include "Widget.h"

namespace demo {

using modern_ui::Color;
using modern_ui::Rect;
using modern_ui::Vec2i;
using modern_ui::Widget;

class Label : public Widget {
public:
    Label(std::string text, int32_t fontSize = 14,
          Color color = Color{206, 214, 236, 255});
    ~Label() override;

    void setText(std::string text);
    void setColor(Color color);
    const std::string& text() const { return text_; }

    Vec2i onMeasure(const Vec2i& available) const override;
    void onLayout(const Rect& frame) override;

private:
    Rect measureText() const;
    void writeGeometry();

    uint32_t shapeId_ = 0;
    std::string text_;
    int32_t fontSize_ = 14;
    Color color_{};
    mutable Rect measured_{};
    Rect lastFrame_{};
};

} // namespace demo
