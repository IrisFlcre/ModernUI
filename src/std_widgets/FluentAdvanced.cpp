// ============================================================================
// FluentAdvanced.cpp — TreeView / CalendarView / ColorPicker / PipsPager
// ============================================================================

#include "FluentAdvanced.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "FluentButtons.h"
#include "FluentCollections.h"
#include "FluentTypography.h"

namespace modern_ui {
namespace fluent {
namespace {

constexpr int32_t kRowHeight = 34;
constexpr int32_t kRowSpacing = 2;
constexpr int32_t kListPadding = 4;
constexpr int32_t kCalendarCell = 30;
constexpr int32_t kCalendarGap = 2;
constexpr int kKeyLeft = 0x25;
constexpr int kKeyUp = 0x26;
constexpr int kKeyRight = 0x27;
constexpr int kKeyDown = 0x28;
constexpr int kKeyHome = 0x24;
constexpr int kKeyEnd = 0x23;
constexpr int kKeyEnter = 0x0D;
constexpr int kKeySpace = 0x20;

// ===========================================================================
// TreeRowButton — 树行的基础组件：复用 Button 的绘制 / 悬停 / 点击，
// 只额外绘制 chevron，并把"展开区点击"与"整行点击"拆成两个回调。
// ===========================================================================
class TreeRowButton final : public Button {
public:
    TreeRowButton(std::string label, int32_t depth, bool hasChildren, bool expanded,
                  bool selected)
        : Button(std::move(label), selected ? Button::Style::Selected : Button::Style::Subtle, 13),
          depth_(std::max(0, depth)),
          hasChildren_(hasChildren),
          expanded_(expanded) {
        setContentAlign(Button::ContentAlign::Left);
        setContentPadding(indent() + 18);
        addClass("tree-row");
        style().width = Dimension::percent(100.0f);
        style().height = Dimension::px(static_cast<float>(kRowHeight));

        Shape chevron = makeChevronRightShape(0, 0, 6, 10, theme().textSecondary, 2);
        chevronId_ = canvas().addShape(std::move(chevron));
    }

    std::function<void()> onToggle;

    void setSelected(bool selected) {
        setButtonStyle(selected ? Button::Style::Selected : Button::Style::Subtle);
    }

    void setExpanded(bool expanded) {
        if (expanded_ == expanded) return;
        expanded_ = expanded;
        if (laidOut()) updateVisuals();
    }

    bool expanded() const { return expanded_; }

    void updateVisuals() override {
        Button::updateVisuals();
        if (!laidOut()) return;

        const int32_t top = frameHeight();
        canvas().updateShape(chevronId_, [&](Shape& shape) {
            if (expanded_) {
                shape = makeChevronShape(indent(), (top - 6) / 2, 10, 6, true,
                                         theme().textSecondary, 2);
            } else {
                shape = makeChevronRightShape(indent(), (top - 10) / 2, 6, 10,
                                              theme().textSecondary, 2);
            }
            shape.base.visible = hasChildren_;
        });
    }

    bool onEvent(const Event& ev) override {
        // 方向键 / Home / End / Enter / Space 都冒泡给 TreeView
        if (ev.type == EventType::KeyDown) return false;

        if (ev.type == EventType::MouseUp && clicked() && hasChildren_) {
            if (ev.x >= indent() - 4 && ev.x <= indent() + 18) {
                if (onToggle) onToggle();
                return true;
            }
        }
        return Button::onEvent(ev);
    }

private:
    int32_t indent() const { return 8 + depth_ * 18; }

    int32_t depth_ = 0;
    bool hasChildren_ = false;
    bool expanded_ = false;
    uint32_t chevronId_ = 0;
};

} // namespace

// ===========================================================================
// TreeView
// ===========================================================================
TreeView::TreeView() {
    auto panel = std::make_unique<StackPanel>(FlexDirection::Column, kRowSpacing, kListPadding);
    rowsPanel_ = panel.get();

    scroll_ = emplaceChild<ScrollViewer>();
    scroll_->setKeyboardScrolling(false);
    scroll_->style().width = Dimension::percent(100.0f);
    scroll_->style().height = Dimension::percent(100.0f);
    scroll_->setContent(std::move(panel));
}

void TreeView::setNodes(std::vector<TreeViewNode> nodes) {
    nodes_ = std::move(nodes);
    if (selectedNode_ >= static_cast<int32_t>(nodes_.size())) {
        selectedNode_ = nodes_.empty() ? -1 : 0;
    }
    recomputeVisible();
    rebuildRows();
    if (scroll_) scroll_->scrollTo(0.0f);
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void TreeView::setSelectedIndex(int32_t index) {
    const int32_t next = (index < 0 || index >= static_cast<int32_t>(nodes_.size())) ? -1 : index;
    if (next == selectedNode_) return;
    selectedNode_ = next;
    syncRowSelection();
}

void TreeView::recomputeVisible() {
    visible_.clear();
    std::vector<bool> expandedAtDepth;

    for (int32_t i = 0; i < static_cast<int32_t>(nodes_.size()); ++i) {
        const int32_t depth = std::max(0, nodes_[static_cast<size_t>(i)].depth);
        if (depth > 0) {
            if (static_cast<size_t>(depth - 1) >= expandedAtDepth.size() ||
                !expandedAtDepth[static_cast<size_t>(depth - 1)]) {
                continue;
            }
        }

        visible_.push_back(i);
        const size_t needed = static_cast<size_t>(depth + 1);
        if (expandedAtDepth.size() < needed) expandedAtDepth.resize(needed, false);
        expandedAtDepth[static_cast<size_t>(depth)] =
            nodes_[static_cast<size_t>(i)].hasChildren && nodes_[static_cast<size_t>(i)].expanded;
        for (size_t deeper = needed; deeper < expandedAtDepth.size(); ++deeper) {
            expandedAtDepth[deeper] = false;
        }
    }
}

void TreeView::rebuildRows() {
    if (!rowsPanel_) return;

    rowsPanel_->clearChildren();
    rows_.clear();
    rows_.reserve(visible_.size());

    for (int32_t nodeIndex : visible_) {
        const TreeViewNode& node = nodes_[static_cast<size_t>(nodeIndex)];
        TreeRowButton* row = rowsPanel_->emplaceChild<TreeRowButton>(
            node.label, node.depth, node.hasChildren, node.expanded,
            nodeIndex == selectedNode_);
        row->onClick = [this, nodeIndex]() { selectNode(nodeIndex); };
        row->onToggle = [this, nodeIndex]() { toggleNode(nodeIndex); };
        rows_.push_back(row);
    }
}

void TreeView::toggleNode(int32_t nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(nodes_.size())) return;
    TreeViewNode& node = nodes_[static_cast<size_t>(nodeIndex)];
    if (!node.hasChildren) return;
    node.expanded = !node.expanded;
    recomputeVisible();

    // 立即更新被点击行的 chevron；行的增删延后到 onTick，
    // 避免在行控件自己的事件回调里把自身析构掉。
    const int32_t position = rowPosition(nodeIndex);
    if (position >= 0 && position < static_cast<int32_t>(rows_.size())) {
        static_cast<TreeRowButton*>(rows_[static_cast<size_t>(position)])
            ->setExpanded(node.expanded);
    }
    rebuildPending_ = true;
    invalidateLayout();
    if (onExpandedChanged) onExpandedChanged(nodeIndex, node.expanded);
}

void TreeView::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);
    if (!rebuildPending_) return;

    rebuildPending_ = false;
    rebuildRows();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

Vec2i TreeView::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{260, 240};
}

void TreeView::updateVisuals() {
    if (!laidOut()) return;
    syncRowSelection();
}

void TreeView::selectNode(int32_t nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(nodes_.size())) return;
    if (nodeIndex == selectedNode_) return;
    selectedNode_ = nodeIndex;
    syncRowSelection();
    if (onSelectionChanged) onSelectionChanged(nodeIndex);
}

void TreeView::syncRowSelection() {
    for (size_t row = 0; row < rows_.size() && row < visible_.size(); ++row) {
        static_cast<TreeRowButton*>(rows_[row])->setSelected(visible_[row] == selectedNode_);
    }
}

int32_t TreeView::rowPosition(int32_t nodeIndex) const {
    for (size_t i = 0; i < visible_.size(); ++i) {
        if (visible_[i] == nodeIndex) return static_cast<int32_t>(i);
    }
    return -1;
}

void TreeView::ensureRowVisible(int32_t position) {
    if (!scroll_ || position < 0) return;

    const float rowHeight = static_cast<float>(kRowHeight + kRowSpacing);
    const float top = static_cast<float>(kListPadding) + static_cast<float>(position) * rowHeight;
    const float bottom = top + static_cast<float>(kRowHeight);
    const float viewTop = scroll_->scrollOffset();
    const float viewBottom = viewTop + static_cast<float>(frameHeight());

    if (top < viewTop) {
        scroll_->scrollTo(top - static_cast<float>(kListPadding));
    } else if (bottom > viewBottom) {
        scroll_->scrollTo(bottom - static_cast<float>(frameHeight()));
    }
}

bool TreeView::onEvent(const Event& ev) {
    if (!enabled() || ev.type != EventType::KeyDown || visible_.empty()) return false;

    int32_t position = rowPosition(selectedNode_);
    if (ev.key == kKeyDown) {
        position = std::min(static_cast<int32_t>(visible_.size()) - 1, position + 1);
    } else if (ev.key == kKeyUp) {
        position = std::max(0, position - 1);
    } else if (ev.key == kKeyHome) {
        position = 0;
    } else if (ev.key == kKeyEnd) {
        position = static_cast<int32_t>(visible_.size()) - 1;
    } else if (ev.key == kKeyRight) {
        if (position >= 0) {
            const int32_t nodeIndex = visible_[static_cast<size_t>(position)];
            if (!nodes_[static_cast<size_t>(nodeIndex)].expanded) toggleNode(nodeIndex);
        }
        return true;
    } else if (ev.key == kKeyLeft) {
        if (position >= 0) {
            const int32_t nodeIndex = visible_[static_cast<size_t>(position)];
            if (nodes_[static_cast<size_t>(nodeIndex)].hasChildren &&
                nodes_[static_cast<size_t>(nodeIndex)].expanded) {
                toggleNode(nodeIndex);
            }
        }
        return true;
    } else if (ev.key == kKeyEnter || ev.key == kKeySpace) {
        if (position >= 0) toggleNode(visible_[static_cast<size_t>(position)]);
        return true;
    } else {
        return false;
    }

    if (position >= 0 && position < static_cast<int32_t>(visible_.size())) {
        selectNode(visible_[static_cast<size_t>(position)]);
        ensureRowVisible(position);
    }
    return true;
}

// ===========================================================================
// CalendarView
// ===========================================================================
CalendarView::CalendarView(int32_t year, int32_t month, int32_t day) {
    year_ = std::max(1, year);
    month_ = std::max(1, std::min(12, month));
    day_ = std::max(1, std::min(day, daysInMonth(year_, month_)));

    style().direction = FlexDirection::Column;
    style().padding = Edges(12);
    style().gap = 6;
    style().alignItems = AlignItems::Stretch;

    // ---- 标题栏：← / 月份标题 / → ----
    StackPanel* header = emplaceChild<StackPanel>(FlexDirection::Row, 4, 0);
    header->style().width = Dimension::percent(100.0f);
    header->style().height = Dimension::px(30.0f);
    header->style().alignItems = AlignItems::Center;

    prevButton_ = header->emplaceChild<Button>("<", Button::Style::Subtle, 16);
    prevButton_->style().width = Dimension::px(28.0f);
    prevButton_->style().height = Dimension::px(28.0f);
    prevButton_->addClass("calendar-nav");
    prevButton_->onClick = [this]() { changeMonth(-1); };

    title_ = header->emplaceChild<TextBlock>("", 14);
    title_->setAlign(TextBlock::Align::Center);
    title_->style().flexGrow = 1;
    title_->style().height = Dimension::px(28.0f);
    title_->addClass("calendar-title");

    nextButton_ = header->emplaceChild<Button>(">", Button::Style::Subtle, 16);
    nextButton_->style().width = Dimension::px(28.0f);
    nextButton_->style().height = Dimension::px(28.0f);
    nextButton_->addClass("calendar-nav");
    nextButton_->onClick = [this]() { changeMonth(1); };

    // ---- 星期栏 ----
    StackPanel* weekdays = emplaceChild<StackPanel>(FlexDirection::Row, kCalendarGap, 0);
    weekdays->style().width = Dimension::percent(100.0f);
    weekdays->style().height = Dimension::px(18.0f);
    weekdays->style().justify = JustifyContent::SpaceBetween;

    const char* labels[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    for (const char* label : labels) {
        TextBlock* text = weekdays->emplaceChild<TextBlock>(label, 10);
        text->setAlign(TextBlock::Align::Center);
        text->style().width = Dimension::px(static_cast<float>(kCalendarCell));
        text->style().height = Dimension::px(18.0f);
        text->addClass("calendar-weekday");
    }

    // ---- 6×7 日期网格（日期按钮 + 空白占位，由 Flexbox wrap 排版）----
    dayGrid_ = emplaceChild<StackPanel>(FlexDirection::Row, kCalendarGap, 0);
    dayGrid_->style().wrap = FlexWrap::Wrap;
    dayGrid_->style().crossGap = kCalendarGap;
    dayGrid_->style().justify = JustifyContent::SpaceBetween;
    dayGrid_->style().alignContent = AlignContent::FlexStart;
    dayGrid_->style().width = Dimension::percent(100.0f);
    dayGrid_->style().flexGrow = 1;

    refreshTitle();
    rebuildDays();
}

int32_t CalendarView::daysInMonth(int32_t year, int32_t month) {
    static const int32_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 31;
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return days[month - 1];
}

int32_t CalendarView::firstWeekday(int32_t year, int32_t month) {
    // 0 = Monday … 6 = Sunday
    static const int32_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int32_t y = year;
    if (month < 3) --y;
    const int32_t sunday = (y + y / 4 - y / 100 + y / 400 + t[month - 1] + 1) % 7;
    return (sunday + 6) % 7;
}

const char* CalendarView::monthName(int32_t month) {
    static const char* names[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    if (month < 1 || month > 12) return "";
    return names[month - 1];
}

void CalendarView::setDate(int32_t year, int32_t month, int32_t day) {
    const int32_t nextYear = std::max(1, year);
    const int32_t nextMonth = std::max(1, std::min(12, month));
    const int32_t nextDay = std::max(1, std::min(day, daysInMonth(nextYear, nextMonth)));
    const bool rebuild = nextYear != year_ || nextMonth != month_;

    year_ = nextYear;
    month_ = nextMonth;
    day_ = nextDay;

    refreshTitle();
    if (rebuild) {
        rebuildDays();
        invalidateLayout();
    } else {
        syncDaySelection();
    }
    if (laidOut()) updateVisuals();
}

Vec2i CalendarView::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{262, 292};
}

void CalendarView::changeMonth(int32_t delta) {
    month_ += delta;
    if (month_ < 1) { month_ = 12; --year_; }
    if (month_ > 12) { month_ = 1; ++year_; }
    day_ = std::max(1, std::min(day_, daysInMonth(year_, month_)));

    refreshTitle();
    rebuildDays();
    invalidateLayout();
    if (laidOut()) updateVisuals();
    if (onDateChanged) onDateChanged(year_, month_, day_);
}

void CalendarView::rebuildDays() {
    if (!dayGrid_) return;

    dayGrid_->clearChildren();
    dayCells_.clear();

    const int32_t total = daysInMonth(year_, month_);
    const int32_t offset = firstWeekday(year_, month_);
    dayByNumber_.assign(static_cast<size_t>(total) + 1, nullptr);
    dayCells_.reserve(42);

    for (int32_t index = 0; index < 42; ++index) {
        const int32_t dayNumber = index - offset + 1;
        if (dayNumber >= 1 && dayNumber <= total) {
            ToggleButton* button = dayGrid_->emplaceChild<ToggleButton>(
                std::to_string(dayNumber), Button::Style::Subtle, 12);
            button->style().width = Dimension::px(static_cast<float>(kCalendarCell));
            button->style().height = Dimension::px(static_cast<float>(kCalendarCell));
            button->addClass("calendar-day");
            button->setChecked(dayNumber == day_);
            button->onToggled = [this, dayNumber](bool checked) {
                if (checked) {
                    commitDate(dayNumber);
                    return;
                }
                // 点击已选日期：保持选中，不触发日期变化
                if (dayNumber != day_) return;
                const size_t slot = static_cast<size_t>(dayNumber);
                if (slot < dayByNumber_.size()) {
                    if (auto* selected = static_cast<ToggleButton*>(dayByNumber_[slot])) {
                        selected->setChecked(true);
                    }
                }
            };
            dayCells_.push_back(button);
            dayByNumber_[static_cast<size_t>(dayNumber)] = button;
        } else {
            TextBlock* placeholder = dayGrid_->emplaceChild<TextBlock>("", 12);
            placeholder->style().width = Dimension::px(static_cast<float>(kCalendarCell));
            placeholder->style().height = Dimension::px(static_cast<float>(kCalendarCell));
            dayCells_.push_back(placeholder);
        }
    }
}

void CalendarView::refreshTitle() {
    if (!title_) return;
    title_->setText(std::string(monthName(month_)) + " " + std::to_string(year_));
}

void CalendarView::syncDaySelection() {
    for (size_t day = 1; day < dayByNumber_.size(); ++day) {
        if (auto* button = static_cast<ToggleButton*>(dayByNumber_[day])) {
            button->setChecked(static_cast<int32_t>(day) == day_);
        }
    }
}

void CalendarView::updateVisuals() {
    if (!laidOut()) return;
    syncDaySelection();
}

void CalendarView::commitDate(int32_t day) {
    const int32_t next = std::max(1, std::min(day, daysInMonth(year_, month_)));
    if (next == day_) return;
    day_ = next;
    syncDaySelection();
    if (onDateChanged) onDateChanged(year_, month_, day_);
}

bool CalendarView::onEvent(const Event& ev) {
    if (!enabled() || ev.type != EventType::KeyDown) return false;

    const int32_t total = daysInMonth(year_, month_);
    if (ev.key == kKeyLeft) { commitDate(day_ - 1); return true; }
    if (ev.key == kKeyRight) { commitDate(day_ + 1); return true; }
    if (ev.key == kKeyUp) { commitDate(day_ - 7); return true; }
    if (ev.key == kKeyDown) { commitDate(day_ + 7); return true; }
    if (ev.key == kKeyHome) { commitDate(1); return true; }
    if (ev.key == kKeyEnd) { commitDate(total); return true; }
    return false;
}

// ===========================================================================
// ColorPicker
// ===========================================================================
ColorPicker::ColorPicker() {
    colors_ = {
        Color{0, 120, 212}, Color{0, 95, 184}, Color{96, 205, 255}, Color{76, 194, 255},
        Color{135, 100, 184}, Color{177, 70, 194}, Color{194, 57, 179}, Color{227, 0, 140},
        Color{202, 80, 16}, Color{247, 99, 12}, Color{255, 185, 0}, Color{252, 225, 0},
        Color{16, 124, 16}, Color{108, 203, 95}, Color{11, 106, 11}, Color{0, 178, 148},
        Color{3, 131, 135}, Color{0, 183, 195}, Color{93, 90, 88}, Color{154, 136, 136}
    };
    rebuild();
    if (laidOut()) updateVisuals();
}

void ColorPicker::setColors(std::vector<Color> colors) {
    colors_ = std::move(colors);
    selectedIndex_ = colors_.empty() ? -1 : std::max(0, std::min(selectedIndex_,
                                                                static_cast<int32_t>(colors_.size()) - 1));
    hoverIndex_ = -1;
    rebuild();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void ColorPicker::setSelectedIndex(int32_t index) {
    const int32_t next = (index < 0 || index >= static_cast<int32_t>(colors_.size())) ? -1 : index;
    if (next == selectedIndex_) return;
    selectedIndex_ = next;
    if (laidOut()) updateVisuals();
}

Color ColorPicker::selectedColor() const {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int32_t>(colors_.size())) {
        return Color::nullColor();
    }
    return colors_[static_cast<size_t>(selectedIndex_)];
}

void ColorPicker::rebuild() {
    for (uint32_t id : swatchIds_) canvas().removeShape(id);
    swatchIds_.clear();
    swatchIds_.reserve(colors_.size());

    for (const Color& color : colors_) {
        (void)color;
        swatchIds_.push_back(canvas().addShape(
            makeRRectShape(Rect{0, 0, 1, 1}, 6.0f, color)));
    }
}

Vec2i ColorPicker::onMeasure(const Vec2i& available) const {
    (void)available;
    const int32_t columns = this->columns();
    const int32_t rows = std::max(1, (static_cast<int32_t>(colors_.size()) + columns - 1) / columns);
    return Vec2i{columns * 34 + 24, rows * 34 + 24};
}

int32_t ColorPicker::indexAt(int32_t localX, int32_t localY) const {
    const int32_t x = localX - 12;
    const int32_t y = localY - 12;
    if (x < 0 || y < 0) return -1;
    const int32_t column = x / 34;
    const int32_t row = y / 34;
    if (column >= columns()) return -1;
    if (x % 34 >= 26 || y % 34 >= 26) return -1;
    const int32_t index = row * columns() + column;
    if (index < 0 || index >= static_cast<int32_t>(colors_.size())) return -1;
    return index;
}

void ColorPicker::updateVisuals() {
    if (!laidOut()) return;
    if (swatchIds_.size() != colors_.size()) rebuild();

    for (size_t i = 0; i < swatchIds_.size(); ++i) {
        const int32_t column = static_cast<int32_t>(i) % columns();
        const int32_t row = static_cast<int32_t>(i) / columns();
        const int32_t x = 12 + column * 34;
        const int32_t y = 12 + row * 34;
        const bool selected = static_cast<int32_t>(i) == selectedIndex_;
        const bool hover = static_cast<int32_t>(i) == hoverIndex_;

        canvas().updateShape(swatchIds_[i], [&](Shape& shape) {
            shape.data = RRectShape{x, y, 26, 26, 6.0f, 6.0f};
            shape.base.fill = colors_[i];
            shape.base.stroke = selected ? theme().focusStroke
                : (hover ? theme().textPrimary : theme().stroke);
            shape.base.strokeWidth = selected ? 2 : 1;
            shape.base.visible = true;
        });
    }
}

bool ColorPicker::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseMove: {
        const int32_t index = indexAt(ev.x, ev.y);
        if (index != hoverIndex_) {
            hoverIndex_ = index;
            if (laidOut()) updateVisuals();
        }
        return true;
    }
    case EventType::MouseDown:
        return true;

    case EventType::MouseUp: {
        if (!clicked()) return true;
        const int32_t index = indexAt(ev.x, ev.y);
        if (index >= 0 && index != selectedIndex_) {
            selectedIndex_ = index;
            if (laidOut()) updateVisuals();
            if (onColorChanged) onColorChanged(colors_[static_cast<size_t>(index)]);
        }
        return true;
    }

    case EventType::KeyDown: {
        if (colors_.empty()) return false;
        int32_t next = selectedIndex_;
        if (ev.key == kKeyLeft) next = std::max(0, next - 1);
        else if (ev.key == kKeyRight) next = std::min(static_cast<int32_t>(colors_.size()) - 1, next + 1);
        else if (ev.key == kKeyUp) next = std::max(0, next - columns());
        else if (ev.key == kKeyDown) next = std::min(static_cast<int32_t>(colors_.size()) - 1,
                                                      next + columns());
        else if (ev.key == kKeyHome) next = 0;
        else if (ev.key == kKeyEnd) next = static_cast<int32_t>(colors_.size()) - 1;
        else return false;

        if (next != selectedIndex_) {
            selectedIndex_ = next;
            if (laidOut()) updateVisuals();
            if (onColorChanged) onColorChanged(colors_[static_cast<size_t>(next)]);
        }
        return true;
    }

    default:
        return false;
    }
}

void ColorPicker::onStateChanged() {
    if (!hovered() && hoverIndex_ != -1) {
        hoverIndex_ = -1;
        if (laidOut()) updateVisuals();
    }
}

// ===========================================================================
// PipsPager
// ===========================================================================
PipsPager::PipsPager(int32_t numberOfPages, int32_t selectedPage)
    : numberOfPages_(std::max(1, numberOfPages)),
      selectedPage_(std::max(0, std::min(selectedPage, numberOfPages - 1))) {
    Shape prev = makeChevronRightShape(0, 0, 6, 10, theme().textSecondary, 2);
    prevId_ = canvas().addShape(std::move(prev));
    Shape next = makeChevronRightShape(0, 0, 6, 10, theme().textSecondary, 2);
    nextId_ = canvas().addShape(std::move(next));
    rebuild();
}

void PipsPager::setNumberOfPages(int32_t count) {
    const int32_t next = std::max(1, count);
    if (next == numberOfPages_) return;
    numberOfPages_ = next;
    selectedPage_ = std::min(selectedPage_, numberOfPages_ - 1);
    rebuild();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void PipsPager::setSelectedPage(int32_t page) {
    const int32_t next = std::max(0, std::min(page, numberOfPages_ - 1));
    if (next == selectedPage_) return;
    selectedPage_ = next;
    if (laidOut()) updateVisuals();
}

void PipsPager::rebuild() {
    for (uint32_t id : pipIds_) canvas().removeShape(id);
    pipIds_.clear();
    pipIds_.reserve(static_cast<size_t>(numberOfPages_));
    for (int32_t i = 0; i < numberOfPages_; ++i) {
        (void)i;
        pipIds_.push_back(canvas().addShape(
            makeCircleShape(0, 0, 3.0f, withOpacity(theme().textSecondary, 0.55f))));
    }
}

Vec2i PipsPager::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{48 + 16 + numberOfPages_ * 12, 32};
}

void PipsPager::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (width <= 0 || height <= 0) return;
    if (pipIds_.size() != static_cast<size_t>(numberOfPages_)) rebuild();

    canvas().updateShape(prevId_, [&](Shape& shape) {
        shape = makeChevronRightShape(8, height / 2 - 5, 6, 10,
                                      theme().textSecondary, 2);
        shape.base.visible = selectedPage_ > 0;
    });
    canvas().updateShape(nextId_, [&](Shape& shape) {
        shape = makeChevronRightShape(width - 14, height / 2 - 5, 6, 10,
                                      theme().textSecondary, 2);
        shape.base.visible = selectedPage_ + 1 < numberOfPages_;
    });

    const int32_t totalWidth = numberOfPages_ * 12;
    const int32_t startX = (width - totalWidth) / 2 + 6;
    for (int32_t i = 0; i < numberOfPages_; ++i) {
        const bool selected = i == selectedPage_;
        const float radius = selected ? 4.0f : 3.0f;
        canvas().updateShape(pipIds_[static_cast<size_t>(i)], [&](Shape& shape) {
            shape.data = CircleShape{startX + i * 12, height / 2, radius};
            shape.base.fill = selected ? theme().accent
                                      : withOpacity(theme().textSecondary, 0.55f);
            shape.base.visible = true;
        });
    }
}

bool PipsPager::onEvent(const Event& ev) {
    if (!enabled()) return false;

    if (ev.type == EventType::MouseDown) return true;
    if (ev.type != EventType::MouseUp || !clicked()) return false;

    if (ev.x < 28) {
        if (selectedPage_ > 0) {
            setSelectedPage(selectedPage_ - 1);
            if (onSelectedPageChanged) onSelectedPageChanged(selectedPage_);
            return true;
        }
        return true;
    }
    if (ev.x > frameWidth() - 28) {
        if (selectedPage_ + 1 < numberOfPages_) {
            setSelectedPage(selectedPage_ + 1);
            if (onSelectedPageChanged) onSelectedPageChanged(selectedPage_);
            return true;
        }
        return true;
    }

    const int32_t totalWidth = numberOfPages_ * 12;
    const int32_t startX = (frameWidth() - totalWidth) / 2;
    const int32_t index = (ev.x - startX) / 12;
    if (index >= 0 && index < numberOfPages_) {
        if (index != selectedPage_) {
            setSelectedPage(index);
            if (onSelectedPageChanged) onSelectedPageChanged(index);
        }
        return true;
    }
    return false;
}

// ===========================================================================
// Sparkline
// ===========================================================================
Sparkline::Sparkline(std::vector<float> values)
    : values_(std::move(values)), display_(values_) {
    Shape area = makePathShape({}, withOpacity(currentTheme().accent, 0.24f), 0);
    areaId_ = canvas().addShape(std::move(area));

    Shape line = makePolylineShape({}, currentTheme().accent, lineWidth_);
    lineId_ = canvas().addShape(std::move(line));

    Shape glow = makeCircleShape(0, 0, 1.0f, withOpacity(currentTheme().accent, 0.20f));
    glowId_ = canvas().addShape(std::move(glow));

    Shape dot = makeCircleShape(0, 0, 1.0f, currentTheme().accentHover);
    dotId_ = canvas().addShape(std::move(dot));
}

void Sparkline::setValues(std::vector<float> values) {
    values_ = std::move(values);
    if (display_.size() != values_.size()) {
        display_ = values_;
        animating_ = false;
    } else {
        animating_ = true;
    }
    if (laidOut()) updateVisuals();
}

void Sparkline::addValue(float value) {
    values_.push_back(value);
    if (display_.size() + 1 == values_.size()) {
        display_.push_back(value);
    } else {
        display_ = values_;
    }
    animating_ = false;
    if (laidOut()) updateVisuals();
}

Vec2i Sparkline::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{220, 56};
}

void Sparkline::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (width <= 0 || height <= 0) return;

    if (values_.empty()) {
        for (uint32_t id : {areaId_, lineId_, glowId_, dotId_}) {
            canvas().updateShape(id, [](Shape& shape) { shape.base.visible = false; });
        }
        return;
    }
    if (display_.size() != values_.size()) display_ = values_;

    float minimum = values_[0];
    float maximum = values_[0];
    for (float value : values_) {
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    if (maximum - minimum < 1.0e-5f) {
        minimum -= 1.0f;
        maximum += 1.0f;
    }
    const float range = maximum - minimum;

    const int32_t padX = 5;
    const int32_t padY = 6;
    const float plotW = static_cast<float>(std::max(1, width - padX * 2));
    const float plotH = static_cast<float>(std::max(1, height - padY * 2));
    const size_t count = display_.size();

    std::vector<Vec2i> points;
    points.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const float t = count <= 1 ? 0.0f
            : static_cast<float>(i) / static_cast<float>(count - 1);
        const float x = static_cast<float>(padX) + t * plotW;
        const float normalized = (display_[i] - minimum) / range;
        const float y = static_cast<float>(height - padY) - normalized * plotH;
        points.emplace_back(static_cast<int32_t>(std::lround(x)),
                            static_cast<int32_t>(std::lround(y)));
    }

    // 面积：折线 + 底边闭合，填充竖向渐变
    if (showArea_ && count >= 2) {
        PathShape path;
        PathShape::Step move;
        move.cmd = PathShape::Cmd::MoveTo;
        move.pts[0] = Vec2f{static_cast<float>(points.front().x),
                            static_cast<float>(points.front().y)};
        path.steps.push_back(move);

        for (size_t i = 1; i < points.size(); ++i) {
            PathShape::Step lineStep;
            lineStep.cmd = PathShape::Cmd::LineTo;
            lineStep.pts[0] = Vec2f{static_cast<float>(points[i].x),
                                   static_cast<float>(points[i].y)};
            path.steps.push_back(lineStep);
        }

        PathShape::Step toBottom;
        toBottom.cmd = PathShape::Cmd::LineTo;
        toBottom.pts[0] = Vec2f{static_cast<float>(points.back().x),
                                static_cast<float>(height - padY)};
        path.steps.push_back(toBottom);

        PathShape::Step acrossBottom;
        acrossBottom.cmd = PathShape::Cmd::LineTo;
        acrossBottom.pts[0] = Vec2f{static_cast<float>(points.front().x),
                                    static_cast<float>(height - padY)};
        path.steps.push_back(acrossBottom);

        PathShape::Step close;
        close.cmd = PathShape::Cmd::Close;
        path.steps.push_back(close);

        LinearGradient gradient;
        gradient.from = Vec2f{0.0f, static_cast<float>(padY)};
        gradient.to = Vec2f{0.0f, static_cast<float>(height)};
        gradient.stops = {
            GradientStop{0.0f, withOpacity(theme().accent, 0.34f)},
            GradientStop{1.0f, withOpacity(theme().accent, 0.015f)}
        };

        canvas().updateShape(areaId_, [&](Shape& shape) {
            shape.data = std::move(path);
            shape.base.fill = Paint::linear(gradient);
            shape.base.stroke = Paint{};
            shape.base.visible = true;
        });
    } else {
        canvas().updateShape(areaId_, [&](Shape& shape) { shape.base.visible = false; });
    }

    canvas().updateShape(lineId_, [&](Shape& shape) {
        shape.data = PolylineShape{points, false};
        shape.base.fill = Paint{};
        shape.base.stroke = enabled() ? theme().accent : theme().accentDisabled;
        shape.base.strokeWidth = lineWidth_;
        shape.base.visible = count >= 2;
    });

    const Vec2i last = points.back();
    if (showDot_) {
        canvas().updateShape(glowId_, [&](Shape& shape) {
            shape.data = CircleShape{last.x, last.y, 7.0f};
            shape.base.fill = withOpacity(theme().accent, 0.18f);
            shape.base.visible = true;
        });
        canvas().updateShape(dotId_, [&](Shape& shape) {
            shape.data = CircleShape{last.x, last.y, 2.5f};
            shape.base.fill = enabled() ? theme().accentHover : theme().accentDisabled;
            shape.base.visible = true;
        });
    } else {
        canvas().updateShape(glowId_, [&](Shape& shape) { shape.base.visible = false; });
        canvas().updateShape(dotId_, [&](Shape& shape) { shape.base.visible = false; });
    }
}

void Sparkline::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);
    if (!animating_ || display_.size() != values_.size()) return;

    bool changed = false;
    for (size_t i = 0; i < display_.size(); ++i) {
        changed = animateTowards(display_[i], values_[i], 11.0f, dtSeconds) || changed;
    }
    if (changed) {
        if (laidOut()) updateVisuals();
    } else {
        animating_ = false;
    }
}

} // namespace fluent
} // namespace modern_ui
