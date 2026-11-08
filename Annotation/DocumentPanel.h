// ============================================================================
// DocumentPanel.h — 文档解析结果浮窗（可拖动 / 可缩放 / 可滚动）
// ============================================================================
//
// 一个窗口式的 Markdown 面板：
//   - 拖动标题栏移动窗口
//   - 拖动右下角手柄调整大小
//   - 标题栏右侧“复制”把 Markdown 原文写入剪贴板，× 关闭
//   - 正文由 std_widgets 的 MarkdownView 渲染（完全可复用）
//
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "Markdown.h"
#include "Widget.h"

namespace annotation {

class DocumentPanel : public modern_ui::ParentWidget {
public:
    DocumentPanel();
    ~DocumentPanel() override = default;

    void setDocument(std::string title, std::string markdown);
    const std::string& markdown() const;

    // 面板外框（相对父容器 / 视图客户区）。只在创建时设置一次；
    // 之后由拖动 / 缩放自行维护。
    void setBounds(const modern_ui::Rect& bounds);
    void setLayer(int32_t layer);

    // 正文字号缩放（1.0 = 100%）。Ctrl+ +/- / 双指捏合会调用。
    void setZoom(float zoom);
    void zoomBy(float delta);
    void resetZoom();
    float zoom() const { return zoom_; }

    std::function<void()> onClose;
    std::function<void()> onCopy;
    std::function<void(const modern_ui::Rect&)> onBoundsChanged;

    // ---- Widget ----
    modern_ui::Vec2i onMeasure(const modern_ui::Vec2i& available) const override;
    void onLayout(const modern_ui::Rect& frame) override;
    bool onEvent(const modern_ui::Event& ev) override;
    void onStateChanged() override;
    bool acceptsFocus() const override { return true; }

private:
    enum class PressTarget : uint8_t { None = 0, Close, Copy };

    static int32_t headerHeight() { return 46; }
    static int32_t footerHeight() { return 18; }

    void rebuildVisuals();
    void updateHover(const modern_ui::Vec2i& point);
    void updateMarkdownGeometry(int32_t panelWidth, int32_t panelHeight);
    void applyZoomTheme();

    // 事件坐标 → 视图客户区绝对坐标（与面板移动无关，用于稳定拖动）。
    modern_ui::Vec2i absolutePoint(const modern_ui::Vec2i& localPoint) const;

    void addTouchPoint(int32_t id, const modern_ui::Vec2i& point);
    void moveTouchPoint(int32_t id, const modern_ui::Vec2i& point);
    void removeTouchPoint(int32_t id);
    void beginPinchIfReady();
    void updatePinch();
    float touchSpread() const;
    bool handleZoomKey(const modern_ui::Event& ev);

    modern_ui::fluent::MarkdownView* markdown_ = nullptr;
    modern_ui::fluent::MarkdownTheme baseTheme_{};
    uint32_t visualGroupId_ = 0;
    int32_t layer_ = -1060;
    std::string title_ = "文档解析";
    float zoom_ = 1.0f;

    modern_ui::Rect headerRect_{};
    modern_ui::Rect closeRect_{};
    modern_ui::Rect copyRect_{};
    modern_ui::Rect gripRect_{};

    bool closeHovered_ = false;
    bool copyHovered_ = false;
    bool gripHovered_ = false;
    bool focusRing_ = false;

    bool dragging_ = false;
    bool resizing_ = false;
    PressTarget pressTarget_ = PressTarget::None;
    modern_ui::Vec2i pressAbsolute_{};
    modern_ui::Rect startFrame_{};

    // 触控：pointerId → 客户区坐标（最多保留两指用于捏合缩放）。
    std::vector<std::pair<int32_t, modern_ui::Vec2i>> touches_;
    bool pinching_ = false;
    float pinchStartDistance_ = 0.0f;
    float pinchStartZoom_ = 1.0f;
};

} // namespace annotation
