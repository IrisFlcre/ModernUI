// ============================================================================
// Flexbox.h — 单遍 Flexbox 排版算法（无具体控件依赖）
// ============================================================================
//
// 职责：把 Widget::style() 里的 flex 配置解析成每个子控件的 frame()，
//       并递归排版整棵子树。算法本身不产生几何体，只写 frame()。
//
// 覆盖范围：
//   direction(row/row-reverse/column/column-reverse) / wrap(nowrap/wrap/wrap-reverse)
//   justify-content / align-items / align-self / align-content
//   flex-basis / flex-grow / flex-shrink / gap / cross-gap
//   width / height / min / max（像素 + 百分比）/ margin / padding
//   position: absolute（相对父内容区右上角偏移）
//   box-sizing: border-box
//
// 与 CSS 的简化差异（有意为之，保证单遍收敛）：
//   - align-items: Baseline 退化为 FlexStart
//   - flex-shrink 只按 base × shrink 比例一次分配，不做多轮最小尺寸迭代
//   - Auto 尺寸使用 onMeasure() 的固有尺寸，不做 min-content / max-content 探测
// ============================================================================

#pragma once

#include "Layout.h"
#include "Widget.h"

namespace modern_ui {

class Flexbox {
public:
    // 排版 node 的子树；调用前 node.frame() 必须已经是最终外框尺寸
    static void layout(Widget& node);

    // 计算 node 作为容器时的固有内容尺寸（border-box，含 padding）
    static Vec2i measureIntrinsic(const Widget& node, const Vec2i& available);
};

} // namespace modern_ui
