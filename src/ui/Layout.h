// ============================================================================
// Layout.h — 布局值类型（Flexbox 排版的输入 / 输出）
// ============================================================================
//
// 约定：
//   - 长度单位 = 整数像素，坐标系 y 向下（与 Canvas 场景图一致）。
//   - box-sizing = border-box：style 中的 width / height / min / max 以及
//     Widget::frame() 描述的都是控件"外框"；padding 向内收缩内容区。
//   - 百分比基准 = 父容器的内容区（主轴 / 交叉轴各对应各自轴）。
//   - 所有类型只描述"怎么排"，不产生任何几何体；几何体只存在于各控件的子 Canvas。
// ============================================================================

#pragma once

#include <cstdint>
#include <limits>

#include "Canvas.h"   // Rect

namespace modern_ui {

// --- Dimension：Auto / 固定像素 / 百分比 ---
struct Dimension {
    enum class Kind : uint8_t { Auto, Points, Percent };

    Kind  kind  = Kind::Auto;
    float value = 0.0f;

    constexpr Dimension() = default;
    constexpr Dimension(Kind k, float v) : kind(k), value(v) {}

    static constexpr Dimension px(float v)      { return {Kind::Points, v}; }
    static constexpr Dimension percent(float p) { return {Kind::Percent, p}; }

    constexpr bool isAuto() const { return kind == Kind::Auto; }

    // 解析为像素：Auto 返回 fallback；Percent 以 percentBase 为 100%
    int32_t resolve(int32_t percentBase, int32_t fallback = 0) const {
        if (kind == Kind::Points)  return static_cast<int32_t>(value);
        if (kind == Kind::Percent) return static_cast<int32_t>(static_cast<float>(percentBase) * value * 0.01f);
        return fallback;
    }
};

// --- Edges：padding / margin ---
struct Edges {
    int32_t top = 0, right = 0, bottom = 0, left = 0;

    constexpr Edges() = default;
    constexpr explicit Edges(int32_t all) : top(all), right(all), bottom(all), left(all) {}
    constexpr Edges(int32_t vertical, int32_t horizontal)
        : top(vertical), right(horizontal), bottom(vertical), left(horizontal) {}
    constexpr Edges(int32_t t, int32_t r, int32_t b, int32_t l)
        : top(t), right(r), bottom(b), left(l) {}

    constexpr int32_t horizontal() const { return left + right; }
    constexpr int32_t vertical()   const { return top + bottom; }
};

// --- Flexbox 枚举 ---
enum class FlexDirection : uint8_t { Row = 0, RowReverse, Column, ColumnReverse };
enum class FlexWrap : uint8_t { NoWrap = 0, Wrap, WrapReverse };
enum class JustifyContent : uint8_t { FlexStart = 0, FlexEnd, Center, SpaceBetween, SpaceAround, SpaceEvenly };
enum class AlignItems : uint8_t { FlexStart = 0, FlexEnd, Center, Stretch, Baseline };
enum class AlignContent : uint8_t { FlexStart = 0, FlexEnd, Center, SpaceBetween, SpaceAround, SpaceEvenly, Stretch };
enum class AlignSelf : uint8_t { Auto = 0, FlexStart, FlexEnd, Center, Stretch, Baseline };
enum class Position : uint8_t { Relative = 0, Absolute };

constexpr bool isRowDirection(FlexDirection d) {
    return d == FlexDirection::Row || d == FlexDirection::RowReverse;
}
constexpr bool isReverseDirection(FlexDirection d) {
    return d == FlexDirection::RowReverse || d == FlexDirection::ColumnReverse;
}

// ============================================================================
// FlexStyle — 一个节点同时描述"我作为容器怎么排子控件"和"我作为子项怎么被排"
// ============================================================================

struct FlexStyle {
    static constexpr int32_t kUnbounded = std::numeric_limits<int32_t>::max() / 4;

    // ---- 作为容器 ----
    FlexDirection  direction    = FlexDirection::Row;
    FlexWrap       wrap         = FlexWrap::NoWrap;
    JustifyContent justify      = JustifyContent::FlexStart;
    AlignItems     alignItems   = AlignItems::FlexStart;
    AlignContent   alignContent = AlignContent::FlexStart;
    Edges          padding{};
    int32_t        gap      = 0;      // 主轴间距
    int32_t        crossGap = -1;     // 交叉轴间距；< 0 表示与 gap 相同

    // ---- 作为子项 ----
    Dimension   width{};              // Auto = 由 flexBasis / 内容固有尺寸决定
    Dimension   height{};
    Dimension   flexBasis{};          // Auto = 用 width/height，其次内容固有尺寸
    int32_t     flexGrow   = 0;
    int32_t     flexShrink = 1;
    AlignSelf   alignSelf  = AlignSelf::Auto;
    Position    position   = Position::Relative;
    int32_t     x = 0, y = 0;         // 仅 Absolute 生效：相对父内容区左上角
    Edges       margin{};
    int32_t     minWidth  = 0,           minHeight  = 0;
    int32_t     maxWidth  = kUnbounded,  maxHeight  = kUnbounded;
    bool        clip = true;          // true = 子内容裁剪到自身矩形

    int32_t crossGapValue() const { return crossGap >= 0 ? crossGap : gap; }
};

} // namespace modern_ui
