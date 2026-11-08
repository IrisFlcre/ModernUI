// ============================================================================
// Flexbox.cpp — 单遍 Flexbox 排版算法实现
// ============================================================================

#include "Flexbox.h"

#include <algorithm>
#include <vector>

namespace modern_ui {
namespace {

// --- 排版过程中的临时项 ---
struct Item {
    Widget* widget = nullptr;
    int32_t baseMain = 0;      // 主轴 base 尺寸（border-box）
    int32_t mainOuter = 0;     // 含 margin 的主轴尺寸（伸缩后）
    int32_t crossSize = 0;     // 交叉轴尺寸（border-box，Stretch 后）
    int32_t crossOuter = 0;    // 含 margin 的交叉轴尺寸
    int32_t mainPos = 0;       // 行内主轴起点（内容区坐标，不含 margin）
    int32_t crossPos = 0;      // 行内交叉轴起点（内容区坐标，不含 margin）
    int32_t grow = 0;
    int32_t shrink = 1;
    bool    autoCross = true;  // 交叉轴尺寸是否 Auto（决定 Stretch 是否生效）
    bool    stretch = false;
};

struct Line {
    std::vector<Item> items;
    int32_t mainUsed = 0;      // 含 gap
    int32_t crossSize = 0;     // 行内最大 crossOuter
    int32_t crossPos = 0;      // align-content 决定的交叉轴起点
};

int32_t clampValue(int32_t value, int32_t lo, int32_t hi) {
    if (hi < lo) return lo;
    return std::max(lo, std::min(hi, value));
}

int32_t mainMargin(const Edges& e, bool row)  { return row ? e.horizontal() : e.vertical(); }
int32_t crossMargin(const Edges& e, bool row) { return row ? e.vertical() : e.horizontal(); }
int32_t mainMarginStart(const Edges& e, bool row)  { return row ? e.left : e.top; }
int32_t crossMarginStart(const Edges& e, bool row) { return row ? e.top : e.left; }

const Dimension& axisDim(const FlexStyle& st, bool mainAxis, bool row) {
    if (mainAxis) return row ? st.width : st.height;
    return row ? st.height : st.width;
}
int32_t axisMin(const FlexStyle& st, bool mainAxis, bool row) {
    return mainAxis ? (row ? st.minWidth : st.minHeight) : (row ? st.minHeight : st.minWidth);
}
int32_t axisMax(const FlexStyle& st, bool mainAxis, bool row) {
    return mainAxis ? (row ? st.maxWidth : st.maxHeight) : (row ? st.maxHeight : st.maxWidth);
}
int32_t axisIntrinsic(const Vec2i& intrinsic, bool mainAxis, bool row) {
    return mainAxis ? (row ? intrinsic.x : intrinsic.y) : (row ? intrinsic.y : intrinsic.x);
}

// 解析某个轴上的 border-box 尺寸（Points / Percent 直接用，Auto 用固有尺寸）+ min/max 夹取
int32_t resolveBorderSize(const FlexStyle& st, bool mainAxis, bool row,
                          int32_t percentBase, const Vec2i& intrinsic) {
    const int32_t size = axisDim(st, mainAxis, row).resolve(percentBase, axisIntrinsic(intrinsic, mainAxis, row));
    return clampValue(size, axisMin(st, mainAxis, row), axisMax(st, mainAxis, row));
}

AlignItems resolveAlign(AlignSelf self, AlignItems fallback) {
    switch (self) {
    case AlignSelf::Auto:      return fallback;
    case AlignSelf::FlexStart: return AlignItems::FlexStart;
    case AlignSelf::FlexEnd:   return AlignItems::FlexEnd;
    case AlignSelf::Center:    return AlignItems::Center;
    case AlignSelf::Stretch:   return AlignItems::Stretch;
    default:                   return AlignItems::Baseline;   // 退化为 FlexStart
    }
}

// 主轴 base 尺寸（flexBasis 优先，其次 width/height，再次内容固有尺寸）
int32_t resolveBaseMain(const Widget& child, bool row, int32_t contentMain, const Vec2i& intrinsic) {
    const FlexStyle& cst = child.style();
    if (!cst.flexBasis.isAuto()) {
        return clampValue(cst.flexBasis.resolve(contentMain, 0),
                          axisMin(cst, true, row), axisMax(cst, true, row));
    }
    return resolveBorderSize(cst, true, row, contentMain, intrinsic);
}

} // namespace

// ============================================================================
// measureIntrinsic — 固有内容尺寸（供 Auto 尺寸 / 容器自适应使用）
// ============================================================================

Vec2i Flexbox::measureIntrinsic(const Widget& node, const Vec2i& available) {
    const FlexStyle& st = node.style();
    const bool row = isRowDirection(st.direction);

    const int32_t padMain  = row ? st.padding.horizontal() : st.padding.vertical();
    const int32_t padCross = row ? st.padding.vertical()   : st.padding.horizontal();
    const int32_t availMain  = std::max(0, (row ? available.x : available.y) - padMain);
    const int32_t availCross = std::max(0, (row ? available.y : available.x) - padCross);
    const int32_t gap = std::max(0, st.gap);
    const int32_t crossGap = std::max(0, st.crossGapValue());

    int32_t lineMain = 0, lineCross = 0, lineItems = 0;
    int32_t usedMain = 0, usedCross = 0;

    for (const auto& childPtr : node.children()) {
        const Widget& child = *childPtr;
        const FlexStyle& cst = child.style();
        if (cst.position == Position::Absolute) continue;

        const Edges& m = cst.margin;
        const Vec2i childAvail{
            row ? std::max(0, availMain - m.horizontal()) : std::max(0, availCross - m.vertical()),
            row ? std::max(0, availCross - m.vertical())  : std::max(0, availMain - m.horizontal())
        };
        const Vec2i intrinsic = child.onMeasure(childAvail);

        const int32_t mainOuter = std::max(0, resolveBaseMain(child, row, availMain, intrinsic)) +
                                  mainMargin(m, row);
        int32_t crossOuter = 0;
        if (axisDim(cst, false, row).isAuto()) {
            crossOuter = axisIntrinsic(intrinsic, false, row);
        } else {
            crossOuter = resolveBorderSize(cst, false, row, availCross, intrinsic);
        }
        crossOuter += crossMargin(m, row);

        if (st.wrap != FlexWrap::NoWrap && lineItems > 0 && lineMain + gap + mainOuter > availMain) {
            usedMain = std::max(usedMain, lineMain);
            usedCross += lineCross + crossGap;
            lineMain = 0;
            lineCross = 0;
            lineItems = 0;
        }

        if (lineItems > 0) lineMain += gap;
        lineMain += mainOuter;
        lineCross = std::max(lineCross, crossOuter);
        ++lineItems;
    }

    usedMain = std::max(usedMain, lineMain);
    if (lineItems > 0) usedCross += lineCross;

    const int32_t mainResult  = usedMain + padMain;
    const int32_t crossResult = usedCross + padCross;
    return Vec2i{ row ? mainResult : crossResult, row ? crossResult : mainResult };
}

// ============================================================================
// layout — 排版子树（node.frame() 必须已经是最终尺寸）
// ============================================================================

void Flexbox::layout(Widget& node) {
    const FlexStyle& st = node.style();
    const Rect& frame = node.frame();

    const int32_t contentW = std::max(0, frame.w - st.padding.horizontal());
    const int32_t contentH = std::max(0, frame.h - st.padding.vertical());

    const bool row = isRowDirection(st.direction);
    const bool reverse = isReverseDirection(st.direction);
    const int32_t contentMain  = row ? contentW : contentH;
    const int32_t contentCross = row ? contentH : contentW;
    const int32_t gap = std::max(0, st.gap);
    const int32_t crossGap = std::max(0, st.crossGapValue());

    // ---------- 1. 收集子项 + 计算 base / 交叉轴尺寸 ----------
    std::vector<Item> flowItems;
    std::vector<Widget*> absoluteChildren;

    for (auto& childPtr : node.children_) {
        Widget* child = childPtr.get();
        const FlexStyle& cst = child->style();
        if (cst.position == Position::Absolute) {
            absoluteChildren.push_back(child);
            continue;
        }

        const Edges& m = cst.margin;
        const Vec2i childAvail{ std::max(0, contentW - m.horizontal()),
                                std::max(0, contentH - m.vertical()) };
        const Vec2i intrinsic = child->onMeasure(childAvail);

        Item item;
        item.widget    = child;
        item.baseMain  = std::max(0, resolveBaseMain(*child, row, contentMain, intrinsic));
        item.mainOuter = item.baseMain + mainMargin(m, row);
        item.grow      = std::max(0, cst.flexGrow);
        item.shrink    = std::max(0, cst.flexShrink);
        item.autoCross = axisDim(cst, false, row).isAuto();
        item.crossSize = item.autoCross
            ? std::max(0, axisIntrinsic(intrinsic, false, row))
            : std::max(0, resolveBorderSize(cst, false, row, contentCross, intrinsic));
        item.crossOuter = item.crossSize + crossMargin(m, row);
        item.stretch    = item.autoCross && resolveAlign(cst.alignSelf, st.alignItems) == AlignItems::Stretch;

        flowItems.push_back(item);
    }

    // ---------- 2. 分行（wrap）----------
    std::vector<Line> lines;
    {
        Line current;
        for (Item& item : flowItems) {
            const bool wrapHere = st.wrap != FlexWrap::NoWrap && !current.items.empty() &&
                                  current.mainUsed + gap + item.mainOuter > contentMain;
            if (wrapHere) {
                lines.push_back(std::move(current));
                current = Line{};
            }
            if (!current.items.empty()) current.mainUsed += gap;
            current.mainUsed += item.mainOuter;
            current.items.push_back(std::move(item));
        }
        if (!current.items.empty()) lines.push_back(std::move(current));
    }

    // ---------- 3. 主轴伸缩分配 + min/max 夹取 ----------
    for (Line& line : lines) {
        const int32_t free = contentMain - line.mainUsed;

        if (free > 0) {
            int32_t totalGrow = 0;
            for (const Item& item : line.items) totalGrow += item.grow;
            if (totalGrow > 0) {
                for (Item& item : line.items) {
                    if (item.grow > 0) item.mainOuter += free * item.grow / totalGrow;
                }
            }
        } else if (free < 0) {
            int32_t totalScaled = 0;
            for (const Item& item : line.items) totalScaled += item.shrink * item.baseMain;
            if (totalScaled > 0) {
                for (Item& item : line.items) {
                    if (item.shrink <= 0) continue;
                    item.mainOuter = std::max(0, item.mainOuter - (-free) * (item.shrink * item.baseMain) / totalScaled);
                }
            }
        }

        for (Item& item : line.items) {
            const FlexStyle& cst = item.widget->style();
            int32_t border = item.mainOuter - mainMargin(cst.margin, row);
            border = clampValue(border, axisMin(cst, true, row), axisMax(cst, true, row));
            item.mainOuter = std::max(0, border) + mainMargin(cst.margin, row);
        }

        // 重新统计行主轴占用（伸缩 / 夹取之后）
        int32_t used = 0;
        for (const Item& item : line.items) used += item.mainOuter;
        used += gap * std::max<int32_t>(0, static_cast<int32_t>(line.items.size()) - 1);
        line.mainUsed = used;
    }

    // ---------- 4. 交叉轴：先定行高，再按 Stretch 撑满 ----------
    for (Line& line : lines) {
        int32_t cross = 0;
        for (const Item& item : line.items) cross = std::max(cross, item.crossOuter);
        // 单行容器：行占满容器内容区（与 CSS 单行 flex 容器的行交叉尺寸一致）
        if (lines.size() == 1) cross = std::max(cross, contentCross);
        line.crossSize = cross;

        for (Item& item : line.items) {
            if (!item.stretch) continue;
            const FlexStyle& cst = item.widget->style();
            const int32_t inner = std::max(0, line.crossSize - crossMargin(cst.margin, row));
            item.crossSize = clampValue(inner, axisMin(cst, false, row), axisMax(cst, false, row));
            item.crossOuter = item.crossSize + crossMargin(cst.margin, row);
        }

        // min/max 夹取可能改变实际行高
        cross = 0;
        for (const Item& item : line.items) cross = std::max(cross, item.crossOuter);
        if (lines.size() == 1) cross = std::max(cross, contentCross);
        line.crossSize = cross;
    }

    // ---------- 5. align-content：多行在交叉轴上的分布 ----------
    int32_t linesCross = 0;
    for (const Line& line : lines) linesCross += line.crossSize;
    if (lines.size() > 1) linesCross += crossGap * static_cast<int32_t>(lines.size() - 1);

    if (st.alignContent == AlignContent::Stretch && !lines.empty() && contentCross > linesCross) {
        const int32_t extra = (contentCross - linesCross) / static_cast<int32_t>(lines.size());
        for (Line& line : lines) line.crossSize += extra;
        linesCross = 0;
        for (const Line& line : lines) linesCross += line.crossSize;
        if (lines.size() > 1) linesCross += crossGap * static_cast<int32_t>(lines.size() - 1);
    }

    {
        const int32_t crossFree = std::max(0, contentCross - linesCross);
        const int32_t lineCount = static_cast<int32_t>(lines.size());
        int32_t cursor = 0;
        int32_t betweenLines = crossGap;

        switch (st.alignContent) {
        case AlignContent::FlexEnd:   cursor = crossFree; break;
        case AlignContent::Center:    cursor = crossFree / 2; break;
        case AlignContent::SpaceBetween:
            if (lineCount > 1) betweenLines = crossGap + crossFree / (lineCount - 1);
            break;
        case AlignContent::SpaceAround:
            if (lineCount > 0) {
                const int32_t unit = crossFree / (2 * lineCount);
                cursor = unit;
                betweenLines = crossGap + 2 * unit;
            }
            break;
        case AlignContent::SpaceEvenly:
            if (lineCount > 0) {
                const int32_t unit = crossFree / (lineCount + 1);
                cursor = unit;
                betweenLines = crossGap + unit;
            }
            break;
        default:
            break;   // FlexStart / Stretch
        }

        for (Line& line : lines) {
            line.crossPos = cursor;
            cursor += line.crossSize + betweenLines;
        }
    }

    // ---------- 6. 行内主轴分布（justify-content）+ 交叉轴对齐（align-items）----------
    for (Line& line : lines) {
        const int32_t lineFree = std::max(0, contentMain - line.mainUsed);
        const int32_t count = static_cast<int32_t>(line.items.size());

        int32_t cursor = 0;
        int32_t betweenItems = gap;
        switch (st.justify) {
        case JustifyContent::FlexEnd: cursor = lineFree; break;
        case JustifyContent::Center:  cursor = lineFree / 2; break;
        case JustifyContent::SpaceBetween:
            if (count > 1) betweenItems = gap + lineFree / (count - 1);
            break;
        case JustifyContent::SpaceAround:
            if (count > 0) {
                const int32_t unit = lineFree / (2 * count);
                cursor = unit;
                betweenItems = gap + 2 * unit;
            }
            break;
        case JustifyContent::SpaceEvenly:
            if (count > 0) {
                const int32_t unit = lineFree / (count + 1);
                cursor = unit;
                betweenItems = gap + unit;
            }
            break;
        default:
            break;   // FlexStart
        }

        for (Item& item : line.items) {
            item.mainPos = cursor;
            cursor += item.mainOuter + betweenItems;

            const FlexStyle& cst = item.widget->style();
            const AlignItems align = resolveAlign(cst.alignSelf, st.alignItems);
            switch (align) {
            case AlignItems::FlexEnd:
                item.crossPos = line.crossPos + line.crossSize - item.crossOuter;
                break;
            case AlignItems::Center:
                item.crossPos = line.crossPos + (line.crossSize - item.crossOuter) / 2;
                break;
            default:   // FlexStart / Stretch / Baseline(退化)
                item.crossPos = line.crossPos;
                break;
            }
        }
    }

    // ---------- 7. 写 frame + 递归子树 ----------
    const int32_t mainOffset  = row ? st.padding.left : st.padding.top;
    const int32_t crossOffset = row ? st.padding.top  : st.padding.left;

    for (Line& line : lines) {
        for (Item& item : line.items) {
            const FlexStyle& cst = item.widget->style();
            const Edges& m = cst.margin;

            int32_t mainPos = item.mainPos;
            int32_t crossPos = item.crossPos;
            if (reverse) mainPos = contentMain - mainPos - item.mainOuter;
            if (st.wrap == FlexWrap::WrapReverse) crossPos = contentCross - crossPos - item.crossOuter;

            const int32_t mainBorder  = std::max(0, item.mainOuter - mainMargin(m, row));
            const int32_t crossBorder = std::max(0, item.crossSize);

            const int32_t alongMain  = mainOffset + mainPos + mainMarginStart(m, row);
            const int32_t alongCross = crossOffset + crossPos + crossMarginStart(m, row);

            item.widget->frame_ = row
                ? Rect{alongMain, alongCross, mainBorder, crossBorder}
                : Rect{alongCross, alongMain, crossBorder, mainBorder};

            Flexbox::layout(*item.widget);
        }
    }

    // ---------- 8. 绝对定位子项（相对父内容区左上角）----------
    for (Widget* child : absoluteChildren) {
        const FlexStyle& cst = child->style();
        const Edges& m = cst.margin;
        const Vec2i childAvail{ std::max(0, contentW - m.horizontal()),
                                std::max(0, contentH - m.vertical()) };
        const Vec2i intrinsic = child->onMeasure(childAvail);

        const int32_t w = clampValue(cst.width.resolve(contentW, intrinsic.x), cst.minWidth, cst.maxWidth);
        const int32_t h = clampValue(cst.height.resolve(contentH, intrinsic.y), cst.minHeight, cst.maxHeight);

        child->frame_ = Rect{ st.padding.left + cst.x + m.left,
                              st.padding.top  + cst.y + m.top,
                              std::max(0, w), std::max(0, h) };
        Flexbox::layout(*child);
    }
}

} // namespace modern_ui
