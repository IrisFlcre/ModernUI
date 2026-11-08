// ============================================================================
// FluentCollections.h — 布局 / 集合 / 导航类控件
// ============================================================================
//
//   StackPanel     — 纵向 / 横向堆叠容器（Flexbox 语法糖）
//   Card           — 8px 圆角卡片容器
//   AcrylicPanel   — 亚克力质感容器（渐变 + 描边）
//   Chip           — 胶囊标签 / 可选中 / 可关闭
//   Expander       — 可展开卡片（标题栏点击 / 内容区自动排版）
//   ScrollViewer   — 单内容滚动容器（滚轮 / 键盘 / 自绘滚动条）
//   ListView       — 行式列表（悬停 / 选中 / 滚轮 / 键盘）
//   GridView       — 磁贴网格（自动列数 / 选中 / 滚轮）
//   NavigationView — 左侧导航 + 内容区（完整 / 紧凑两种模式）
//   TabView        — 标签条（选中 / 悬停 / 关闭回调预留）
//   BreadcrumbBar  — 面包屑导航
//
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "FluentControl.h"

namespace modern_ui {
namespace fluent {

// ===========================================================================
class StackPanel : public ParentWidget {
public:
    explicit StackPanel(FlexDirection direction = FlexDirection::Column,
                        int32_t spacing = 0, int32_t padding = 0);
};

// ===========================================================================
class Card : public FluentControl {
public:
    Card();   // 跟随主题的卡片
    Card(Color fill, Color stroke, float radius = -1.0f, int32_t padding = 16);

    void setFill(Color fill);
    void setStroke(Color stroke);
    void setRadius(float radius);
    void setPadding(int32_t padding);

    void updateVisuals() override;
    bool acceptsFocus() const override { return false; }

private:
    bool useThemeFill_ = true;
    bool useThemeStroke_ = true;
    Color fill_{};
    Color stroke_{};
    float radius_ = -1.0f;
    uint32_t backgroundId_ = 0;
};

// ===========================================================================
class AcrylicPanel : public FluentControl {
public:
    explicit AcrylicPanel(Color tint = Color::nullColor(), float radius = 8.0f);

    void setTint(Color tint);
    void setRadius(float radius);

    void updateVisuals() override;
    bool acceptsFocus() const override { return false; }

private:
    Color tint_;
    float radius_ = 8.0f;
    uint32_t backgroundId_ = 0;
};

// ===========================================================================
class Chip : public FluentControl {
public:
    explicit Chip(std::string text = {}, bool selected = false);

    void setText(std::string text);
    const std::string& text() const { return text_; }

    void setSelected(bool selected);
    bool selected() const { return selected_; }

    void setClosable(bool closable);
    bool closable() const { return closable_; }

    // false = 纯展示标签（点击不切换选中态）
    void setSelectable(bool selectable) { selectable_ = selectable; }
    bool selectable() const { return selectable_; }

    std::function<void(bool)> onSelectedChanged;
    std::function<void()> onClose;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

protected:
    void activate() override;

private:
    bool closeHit(int32_t x, int32_t y) const;

    std::string text_;
    int32_t fontSize_ = 12;
    bool selected_ = false;
    bool closable_ = false;
    bool selectable_ = true;
    bool closeHover_ = false;
    mutable Rect measured_{};

    uint32_t backgroundId_ = 0;
    uint32_t textId_ = 0;
    uint32_t closeBackgroundId_ = 0;
    uint32_t closeGlyphId_ = 0;
};

// ===========================================================================
class Expander : public FluentControl {
public:
    explicit Expander(std::string header = "Expander", bool expanded = false);

    void setHeader(std::string header);
    const std::string& header() const { return header_; }

    void setContent(std::unique_ptr<Widget> content);
    Widget* content() const;

    void setExpanded(bool expanded);
    bool expanded() const { return expanded_; }

    std::function<void(bool)> onExpandedChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    void toggle();

    std::string header_;
    bool expanded_ = false;
    bool headerHover_ = false;
    int32_t headerHeight_ = 48;
    mutable Rect headerMeasured_{};
    uint32_t backgroundId_ = 0;
    uint32_t headerHoverId_ = 0;
    uint32_t titleId_ = 0;
    uint32_t chevronId_ = 0;
    uint32_t dividerId_ = 0;
};

// ===========================================================================
class ScrollViewer : public FluentControl {
public:
    ScrollViewer();

    void setContent(std::unique_ptr<Widget> content);
    Widget* content() const { return content_; }

    void scrollTo(float offset);
    void scrollBy(float delta);
    float scrollOffset() const { return scrollOffset_; }
    float contentHeight() const { return contentHeight_; }

    // false = 不消费方向键 / Home / End，交给外层控件（如 TreeView）处理
    void setKeyboardScrolling(bool enabled) { keyboardScrolling_ = enabled; }
    bool keyboardScrolling() const { return keyboardScrolling_; }

    Vec2i onMeasure(const Vec2i& available) const override;
    void onLayout(const Rect& frame) override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;

private:
    float maxScroll() const;
    void applyScrollOffset();

    Widget* content_ = nullptr;
    float scrollOffset_ = 0.0f;
    float contentHeight_ = 0.0f;
    bool keyboardScrolling_ = true;

    uint32_t trackId_ = 0;
    uint32_t thumbId_ = 0;
};

// ===========================================================================
class ListView : public FluentControl {
public:
    struct RowGeometry {
        uint32_t background = 0;
        uint32_t accentBar = 0;
        uint32_t text = 0;
    };

    ListView();

    void setItems(std::vector<std::string> items);
    void addItem(std::string item);
    const std::vector<std::string>& items() const { return items_; }

    void setSelectedIndex(int32_t index);
    int32_t selectedIndex() const { return selectedIndex_; }

    std::function<void(int32_t)> onSelectionChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    void rebuildRows();
    int32_t indexAt(int32_t localY) const;
    void scrollTo(float offset);
    void ensureVisible(int32_t index);
    float maxScroll() const;

    std::vector<std::string> items_;
    std::vector<RowGeometry> rows_;
    int32_t selectedIndex_ = -1;
    int32_t hoverIndex_ = -1;
    float scrollOffset_ = 0.0f;

    uint32_t scrollTrackId_ = 0;
    uint32_t scrollThumbId_ = 0;
};

// ===========================================================================
class GridView : public FluentControl {
public:
    struct TileGeometry {
        uint32_t tile = 0;
        uint32_t icon = 0;
        uint32_t text = 0;
    };

    GridView();

    void setItems(std::vector<std::string> items);
    const std::vector<std::string>& items() const { return items_; }

    void setSelectedIndex(int32_t index);
    int32_t selectedIndex() const { return selectedIndex_; }

    std::function<void(int32_t)> onSelectionChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    void rebuildTiles();
    int32_t tileIndexAt(int32_t localX, int32_t localY) const;
    int32_t columnsForWidth(int32_t width) const;

    std::vector<std::string> items_;
    std::vector<TileGeometry> tiles_;
    int32_t selectedIndex_ = -1;
    int32_t hoverIndex_ = -1;
    float scrollOffset_ = 0.0f;
    float contentHeight_ = 0.0f;
};

// ===========================================================================
class NavigationView : public FluentControl {
public:
    struct ItemGeometry {
        uint32_t background = 0;
        uint32_t accentBar = 0;
        uint32_t icon = 0;
        uint32_t iconText = 0;
        uint32_t label = 0;
    };

    NavigationView();

    void addItem(std::string label);
    void setItems(std::vector<std::string> items);
    const std::vector<std::string>& items() const { return items_; }

    void setSelectedIndex(int32_t index);
    int32_t selectedIndex() const { return selectedIndex_; }

    void setPaneTitle(std::string title);
    void setCompact(bool compact);
    bool compact() const { return compact_; }

    void setContent(std::unique_ptr<Widget> content);
    Widget* content() const { return content_; }

    std::function<void(int32_t)> onSelectionChanged;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    int32_t paneWidth() const;
    void rebuildItems();
    int32_t itemIndexAt(int32_t localY) const;

    std::vector<std::string> items_;
    std::vector<ItemGeometry> itemShapes_;
    int32_t selectedIndex_ = -1;
    int32_t hoverIndex_ = -1;
    bool compact_ = false;
    std::string paneTitle_ = "ModernUI";
    Widget* content_ = nullptr;

    uint32_t paneBackgroundId_ = 0;
    uint32_t paneDividerId_ = 0;
    uint32_t hamburgerId_ = 0;
    uint32_t titleId_ = 0;
};

// ===========================================================================
class TabView : public FluentControl {
public:
    struct TabGeometry {
        uint32_t background = 0;
        uint32_t indicator = 0;
        uint32_t text = 0;
    };

    TabView();

    int32_t addTab(std::string title);
    void setTabs(std::vector<std::string> tabs);
    const std::vector<std::string>& tabs() const { return tabs_; }

    void setSelectedIndex(int32_t index);
    int32_t selectedIndex() const { return selectedIndex_; }
    void closeTab(int32_t index);

    std::function<void(int32_t)> onTabChanged;
    std::function<void(int32_t)> onTabClosed;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    void rebuildTabs();

    std::vector<std::string> tabs_;
    std::vector<TabGeometry> tabShapes_;
    std::vector<int32_t> tabWidths_;
    int32_t selectedIndex_ = 0;
    int32_t hoverIndex_ = -1;
};

// ===========================================================================
class BreadcrumbBar : public FluentControl {
public:
    BreadcrumbBar();

    void setItems(std::vector<std::string> items);
    const std::vector<std::string>& items() const { return items_; }

    void setSelectedIndex(int32_t index);
    int32_t selectedIndex() const { return selectedIndex_; }

    std::function<void(int32_t)> onItemClicked;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    void rebuild();

    std::vector<std::string> items_;
    std::vector<uint32_t> textIds_;
    std::vector<uint32_t> chevronIds_;
    std::vector<int32_t> itemX_;
    std::vector<int32_t> itemWidth_;
    int32_t selectedIndex_ = 0;
    int32_t hoverIndex_ = -1;
};

} // namespace fluent
} // namespace modern_ui
