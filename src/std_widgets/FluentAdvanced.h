// ============================================================================
// FluentAdvanced.h — 树 / 日历 / 颜色 / 分页控件
// ============================================================================
//
//   TreeView     — 扁平节点 + depth 的层级树（展开 / 折叠 / 选中 / 滚动）
//   CalendarView — 月历（上/下月、今天、选中日期）
//   ColorPicker  — Fluent 调色板（悬停 / 选中 / 键盘）
//   PipsPager    — 分页圆点 + 左右箭头
//   Sparkline    — 迷你折线 / 面积图（渐变填充 + 平滑数据过渡）
//
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "FluentControl.h"

namespace modern_ui {
namespace fluent {

class Button;
class ScrollViewer;
class StackPanel;
class TextBlock;
class ToggleButton;

// ===========================================================================
struct TreeViewNode {
    std::string label;
    int32_t     depth = 0;
    bool        hasChildren = false;
    bool        expanded = false;
};

// ===========================================================================
// TreeView — 组合式实现：ScrollViewer + 行 Button
// ===========================================================================
class TreeView : public FluentControl {
public:
    TreeView();

    void setNodes(std::vector<TreeViewNode> nodes);
    const std::vector<TreeViewNode>& nodes() const { return nodes_; }

    // 节点在 nodes() 中的下标
    void setSelectedIndex(int32_t index);
    int32_t selectedIndex() const { return selectedNode_; }

    std::function<void(int32_t)> onSelectionChanged;
    std::function<void(int32_t, bool)> onExpandedChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;
    bool onEvent(const Event& ev) override;

private:
    void recomputeVisible();
    void rebuildRows();
    void toggleNode(int32_t nodeIndex);
    void selectNode(int32_t nodeIndex);
    void syncRowSelection();
    void ensureRowVisible(int32_t position);
    int32_t rowPosition(int32_t nodeIndex) const;

    std::vector<TreeViewNode> nodes_;
    std::vector<int32_t> visible_;
    std::vector<Widget*> rows_;
    int32_t selectedNode_ = -1;
    bool rebuildPending_ = false;

    ScrollViewer* scroll_ = nullptr;
    StackPanel*   rowsPanel_ = nullptr;
};

// ===========================================================================
// CalendarView — 组合式实现：标题栏 Button/TextBlock + 7×6 日期 ToggleButton
// ===========================================================================
class CalendarView : public FluentControl {
public:
    explicit CalendarView(int32_t year = 2026, int32_t month = 10, int32_t day = 26);

    void setDate(int32_t year, int32_t month, int32_t day);
    int32_t year() const { return year_; }
    int32_t month() const { return month_; }
    int32_t day() const { return day_; }

    std::function<void(int32_t, int32_t, int32_t)> onDateChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;

    bool acceptsFocus() const override { return true; }

private:
    static int32_t daysInMonth(int32_t year, int32_t month);
    static int32_t firstWeekday(int32_t year, int32_t month);   // 0 = Monday
    static const char* monthName(int32_t month);

    void rebuildDays();
    void refreshTitle();
    void syncDaySelection();
    void changeMonth(int32_t delta);
    void commitDate(int32_t day);

    int32_t year_ = 2026;
    int32_t month_ = 10;
    int32_t day_ = 26;

    TextBlock* title_ = nullptr;
    Button*    prevButton_ = nullptr;
    Button*    nextButton_ = nullptr;
    StackPanel* dayGrid_ = nullptr;
    std::vector<Widget*> dayCells_;      // 42 个格子（日期按钮或空白占位）
    std::vector<Widget*> dayByNumber_;   // 1..daysInMonth → ToggleButton
};

// ===========================================================================
class ColorPicker : public FluentControl {
public:
    ColorPicker();

    void setColors(std::vector<Color> colors);
    const std::vector<Color>& colors() const { return colors_; }

    void setSelectedIndex(int32_t index);
    int32_t selectedIndex() const { return selectedIndex_; }
    Color selectedColor() const;

    std::function<void(Color)> onColorChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    void rebuild();
    int32_t indexAt(int32_t localX, int32_t localY) const;
    int32_t columns() const { return 8; }

    std::vector<Color> colors_;
    std::vector<uint32_t> swatchIds_;
    int32_t selectedIndex_ = 0;
    int32_t hoverIndex_ = -1;
};

// ===========================================================================
class PipsPager : public FluentControl {
public:
    explicit PipsPager(int32_t numberOfPages = 5, int32_t selectedPage = 0);

    void setNumberOfPages(int32_t count);
    int32_t numberOfPages() const { return numberOfPages_; }

    void setSelectedPage(int32_t page);
    int32_t selectedPage() const { return selectedPage_; }

    std::function<void(int32_t)> onSelectedPageChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    bool acceptsFocus() const override { return false; }

private:
    void rebuild();

    int32_t numberOfPages_ = 5;
    int32_t selectedPage_ = 0;
    std::vector<uint32_t> pipIds_;
    uint32_t prevId_ = 0;
    uint32_t nextId_ = 0;
};

// ===========================================================================
class Sparkline : public FluentControl {
public:
    explicit Sparkline(std::vector<float> values = {});

    void setValues(std::vector<float> values);
    void addValue(float value);
    const std::vector<float>& values() const { return values_; }

    void setLineWidth(int32_t width) {
        lineWidth_ = std::max(1, width);
        if (laidOut()) updateVisuals();
    }
    void setShowArea(bool show) { showArea_ = show; if (laidOut()) updateVisuals(); }
    void setShowDot(bool show) { showDot_ = show; if (laidOut()) updateVisuals(); }

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;
    bool acceptsFocus() const override { return false; }

private:
    std::vector<float> values_;
    std::vector<float> display_;
    int32_t lineWidth_ = 2;
    bool showArea_ = true;
    bool showDot_ = true;
    bool animating_ = false;

    uint32_t areaId_ = 0;
    uint32_t lineId_ = 0;
    uint32_t dotId_ = 0;
    uint32_t glowId_ = 0;
};

} // namespace fluent
} // namespace modern_ui
