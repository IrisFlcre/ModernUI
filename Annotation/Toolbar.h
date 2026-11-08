// ============================================================================
// Toolbar.h — 右侧浮动选择栏
// ============================================================================
//
// 单控件自绘（一个 Group 写入子 Canvas），避免每帧重建大量顶层几何体。
//   - 收起：68px 图标列（画笔 / 荧光笔 / 橡皮 / 激光 / 指针 / 截图 …）
//   - 展开：向左滑出属性侧边栏（颜色 / 笔宽 / 橡皮大小 / 界面缩放 / 笔迹参数）
//   - 选中工具后再次点击同一按钮：显示 / 隐藏该工具的属性侧边栏
//   - 图标来自 icons/*.svg，运行时按状态染色（白 / 强调色 / 禁用灰）
// ============================================================================

#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "Canvas.h"
#include "Widget.h"

namespace annotation {

enum class Tool : uint8_t {
    Pen = 0,
    Highlighter,
    Eraser,
    Laser,
    Pointer,
    Screenshot,
    Ocr,
    Ai,
};

class AnnotationToolbar : public modern_ui::Widget {
public:
    AnnotationToolbar();
    ~AnnotationToolbar() override = default;

    // 读取 icons 目录；失败时仍可用（只是不绘制对应图标）
    void loadIcons(const std::string& directory);

    void setScreenSize(int32_t width, int32_t height);

    // ---- 状态同步（不触发回调）----
    void setTool(Tool tool);
    Tool tool() const { return tool_; }
    void setColor(const modern_ui::Color& color);
    const modern_ui::Color& color() const { return color_; }
    void setWidth(float width);
    float width() const { return width_; }
    void setInkParameters(float smoothing01, float penTip01, float taper01);
    void setEraserSize(float diameter);
    void setCardScale(float percent);
    void setPageInfo(int32_t index, int32_t count);
    void setHistoryState(bool canUndo);
    void setScreenshotActive(bool active);
    // 文档解析面板是否打开（用于按钮高亮）。
    void setDocumentActive(bool active);
    bool documentActive() const { return documentActive_; }
    // WPS / PowerPoint 放映联动状态（用于页码着色与提示）。
    void setOfficeState(bool active, bool wps);
    bool expanded() const { return panelMode_ != PanelMode::None; }

    // ---- 回调 ----
    std::function<void(Tool)> onToolChanged;
    std::function<void(const modern_ui::Color&)> onColorChanged;
    std::function<void(float)> onWidthChanged;
    std::function<void(float, float, float)> onInkParametersChanged; // smooth, penTip, taper
    std::function<void(float)> onEraserSizeChanged;                  // 橡皮直径（像素）
    std::function<void(float)> onCardScaleChanged;                   // 卡片缩放系数（1.0 = 100%）
    std::function<void()> onUndo;
    std::function<void()> onClear;
    std::function<void()> onScreenshot;
    std::function<void()> onOcr;
    std::function<void()> onAi;
    std::function<void()> onDocument;
    std::function<void()> onAddPage;
    std::function<void()> onPrevPage;
    std::function<void()> onNextPage;
    std::function<void()> onClose;

    // ---- Widget ----
    modern_ui::Vec2i onMeasure(const modern_ui::Vec2i& available) const override;
    void onLayout(const modern_ui::Rect& frame) override;
    void onTick(float dtSeconds) override;
    bool onEvent(const modern_ui::Event& ev) override;
    void onStateChanged() override;

private:
    enum class PanelMode : uint8_t { None = 0, Brush, Eraser, Settings, Info };

    enum class Action : uint8_t {
        None = 0,
        Pen, Highlighter, Eraser, Laser, Pointer, Screenshot, Ocr, Ai, Document,
        Undo, Clear, Settings, AddPage, PrevPage, NextPage, Close,
    };

    struct HitRegion {
        modern_ui::Rect rect;
        Action action = Action::None;
        int32_t index = -1;
    };

    void rebuildVisuals();
    void updateFrameGeometry();
    void activate(Action action);
    // 点击工具按钮：切换工具；再次点击已选中的工具则显示 / 隐藏属性侧边栏。
    void activateTool(Tool tool);
    PanelMode panelModeForTool(Tool tool) const;
    Action actionAt(const modern_ui::Vec2i& point) const;
    int32_t colorAt(const modern_ui::Vec2i& point) const;
    // 0=笔宽，1..5=设置，6=橡皮大小
    int32_t sliderAt(const modern_ui::Vec2i& point) const;
    void setSliderValue(int32_t slider, int32_t localX);
    void setPanelForTool();
    const char* toolName() const;
    const char* toolShortcut() const;
    std::string iconMarkup(Tool tool, bool accent, bool disabled) const;

    // 150% UI 缩放：在 1080p 及以上取 1.5，小屏按可用高度自适应。
    float scaled(float value) const { return value * uiScale_; }
    int32_t scaledInt(float value) const;

    // 图标（已染色 SVG）
    std::array<std::string, 9> iconWhite_;
    std::array<std::string, 9> iconAccent_;
    std::array<std::string, 9> iconDim_;
    // 0=trash, 1=add, 2=back, 3=forward, 4=quit
    std::array<std::string, 5> utilityWhite_;
    std::array<std::string, 5> utilityAccent_;
    std::array<std::string, 5> utilityDim_;
    std::string settingsIconWhite_;
    std::string settingsIconAccent_;
    std::string settingsIconDim_;

    int32_t screenWidth_ = 1920;
    int32_t screenHeight_ = 1080;

    Tool tool_ = Tool::Pen;
    PanelMode panelMode_ = PanelMode::Brush;
    modern_ui::Color color_{255, 210, 66, 255};
    float width_ = 6.0f;
    float smoothing01_ = 0.35f;
    float penTip01_ = 0.65f;
    float taper01_ = 0.30f;
    float eraserSize_ = 32.0f;          // 橡皮直径（屏幕像素）
    float cardScalePercent_ = 100.0f;   // AI 卡片缩放（百分比）
    float uiScaleSetting_ = 1.5f;       // 目标工具栏缩放
    float appliedScale_ = 1.5f;         // 最近一次实际生效的缩放（拖拽时保持稳定）

    int32_t pageIndex_ = 0;
    int32_t pageCount_ = 1;
    bool canUndo_ = false;
    bool screenshotActive_ = false;
    bool documentActive_ = false;
    bool officeActive_ = false;
    bool officeWps_ = false;

    float uiScale_ = 1.5f;

    float expandProgress_ = 1.0f;
    bool  layoutValid_ = false;
    bool  visualDirty_ = true;
    uint32_t groupId_ = 0;

    Action hoverAction_ = Action::None;
    Action pressedAction_ = Action::None;
    float hoverProgress_ = 0.0f;
    int32_t hoverColor_ = -1;

    // 命中区域（控件局部坐标）
    std::vector<HitRegion> buttonRegions_;
    std::vector<modern_ui::Rect> colorSwatches_;
    modern_ui::Rect widthSliderRect_{};
    modern_ui::Rect eraserSliderRect_{};
    std::array<modern_ui::Rect, 5> settingSliderRects_{};
    modern_ui::Rect panelRect_{};

    int32_t draggingSlider_ = -1; // -1 = 无
};

} // namespace annotation
