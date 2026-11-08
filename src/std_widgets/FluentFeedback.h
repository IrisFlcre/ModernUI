// ============================================================================
// FluentFeedback.h — 反馈 / 浮层 / 菜单控件
// ============================================================================
//
//   TeachingTip   — 教学提示卡片
//   ToolTip       — 轻量气泡提示
//   ContentDialog — 模态对话框卡片（主 / 次 / 关闭）
//   MenuFlyout    — 菜单浮出层（支持分隔线 / 键盘导航）
//   MenuBar       — 横向菜单栏
//   CommandBar    — 命令条（组合 Subtle Button）
//
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "FluentButtons.h"
#include "FluentControl.h"

namespace modern_ui {
namespace fluent {

// ===========================================================================
class TeachingTip : public FluentControl {
public:
    explicit TeachingTip(std::string title = "Tip",
                         std::string message = {},
                         bool closable = true);

    void setTitle(std::string title);
    void setMessage(std::string message);

    std::function<void()> onClose;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    std::string title_;
    std::string message_;
    bool closable_ = true;
    bool closeHover_ = false;
    mutable Rect titleMeasured_{};
    mutable Rect messageMeasured_{};

    uint32_t backgroundId_ = 0;
    uint32_t iconId_ = 0;
    uint32_t iconGlyphId_ = 0;
    uint32_t titleId_ = 0;
    uint32_t messageId_ = 0;
    uint32_t closeBackgroundId_ = 0;
    uint32_t closeGlyphId_ = 0;
};

// ===========================================================================
class ToolTip : public FluentControl {
public:
    explicit ToolTip(std::string text = {});

    void setText(std::string text);
    const std::string& text() const { return text_; }

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool acceptsFocus() const override { return false; }

private:
    std::string text_;
    mutable Rect measured_{};
    uint32_t backgroundId_ = 0;
    uint32_t textId_ = 0;
};

// ===========================================================================
class ContentDialog : public FluentControl {
public:
    explicit ContentDialog(std::string title = {},
                           std::string message = {},
                           std::string primaryText = "OK",
                           std::string secondaryText = {});

    void setTitle(std::string title);
    void setMessage(std::string message);
    void setPrimaryText(std::string text);
    void setSecondaryText(std::string text);

    std::function<void()> onPrimary;
    std::function<void()> onSecondary;
    std::function<void()> onClosed;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    int32_t hitButton(int32_t x, int32_t y) const;   // 1 主 / 2 次 / 3 关闭

    std::string title_;
    std::string message_;
    std::string primaryText_;
    std::string secondaryText_;

    int32_t hoverButton_ = 0;
    int32_t pressedButton_ = 0;
    Rect primaryRect_{};
    Rect secondaryRect_{};
    Rect closeRect_{};

    uint32_t backgroundId_ = 0;
    uint32_t titleId_ = 0;
    uint32_t messageId_ = 0;
    uint32_t primaryBackgroundId_ = 0;
    uint32_t primaryTextId_ = 0;
    uint32_t secondaryBackgroundId_ = 0;
    uint32_t secondaryTextId_ = 0;
    uint32_t closeId_ = 0;
};

// ===========================================================================
class MenuFlyout : public FluentControl {
public:
    MenuFlyout();

    void addItem(std::string label, std::function<void()> action = {});
    void addSeparator();
    void clearItems();
    size_t itemCount() const { return items_.size(); }

    std::function<void()> onDismiss;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    struct Item {
        std::string label;
        std::function<void()> action;
        bool separator = false;
    };
    struct RowGeometry {
        uint32_t background = 0;
        uint32_t text = 0;
    };

    void rebuild();
    int32_t rowIndexAt(int32_t localY) const;
    int32_t rowTop(int32_t index) const;
    int32_t rowHeight(int32_t index) const;

    std::vector<Item> items_;
    std::vector<RowGeometry> rows_;
    int32_t hoverIndex_ = -1;
    uint32_t backgroundId_ = 0;
};

// ===========================================================================
class MenuBar : public FluentControl {
public:
    MenuBar();

    int32_t addItem(std::string label);
    void setItems(std::vector<std::string> items);

    std::function<void(int32_t)> onItemClicked;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;

private:
    void rebuild();

    std::vector<std::string> items_;
    std::vector<uint32_t> backgrounds_;
    std::vector<uint32_t> texts_;
    std::vector<int32_t> itemX_;
    std::vector<int32_t> itemWidth_;
    int32_t hoverIndex_ = -1;
};

// ===========================================================================
class CommandBar : public FluentControl {
public:
    CommandBar();

    Button* addCommand(std::string label, std::function<void()> action = {});
    size_t commandCount() const { return children().size(); }

    void updateVisuals() override;

private:
    uint32_t backgroundId_ = 0;
};

} // namespace fluent
} // namespace modern_ui
