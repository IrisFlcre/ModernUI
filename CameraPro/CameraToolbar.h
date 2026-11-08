// ============================================================================
// CameraToolbar.h — 右侧浮动选择栏（CameraPro）
// ============================================================================
//
// 布局沿用原 Annotation 工具栏：
//   - 收起：68px 图标列（钢笔 / 荧光笔 / 橡皮 / 激光 / 拖动 / 拍照 /
//     画面调整 / 文字识别）
//   - 展开：向左滑出颜色与笔宽、笔迹参数、画面调整面板
//   - 图标来自 icons/*.svg，运行时按状态染色
// ============================================================================

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Canvas.h"
#include "FrameProcessor.h"
#include "Widget.h"

namespace camerapro {

enum class Tool : uint8_t {
    Pen = 0,
    Highlighter,
    Eraser,
    Laser,
    Pan,
    Ocr,
};

class CameraToolbar : public modern_ui::Widget {
public:
    CameraToolbar();
    ~CameraToolbar() override = default;

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
    void setHistoryState(bool canUndo);
    void setPageInfo(int32_t index, int32_t count);
    void setZoom(float zoom);
    void setCameraParams(const PostParams& params);
    void setFrameRate(int32_t fps);
    int32_t frameRate() const { return frameRate_; }
    void setFreeze(bool frozen);
    void setCameraInfo(const std::string& name, bool connected);
    void setChromeVisible(bool visible);
    bool chromeVisible() const { return chromeVisible_; }

    // ---- 回调 ----
    std::function<void(Tool)> onToolChanged;
    std::function<void(const modern_ui::Color&)> onColorChanged;
    std::function<void(float)> onWidthChanged;
    std::function<void(float, float, float)> onInkParametersChanged;
    std::function<void()> onUndo;
    std::function<void()> onClear;
    std::function<void()> onSnapshot;
    std::function<void(const PostParams&)> onCameraParamsChanged;
    std::function<void(int32_t)> onFrameRateChanged;
    std::function<void(bool)> onFreezeChanged;
    std::function<void()> onSwitchCamera;
    std::function<void()> onResetCamera;
    std::function<void()> onZoomIn;
    std::function<void()> onZoomOut;
    std::function<void()> onFit;
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
    enum class PanelMode : uint8_t { None = 0, Brush, Camera, InkSettings };

    enum class Action : uint8_t {
        None = 0,
        Pen, Highlighter, Eraser, Laser, Pan, Snapshot, CameraPanel, Ocr,
        Undo, Clear, InkSettings,
        ZoomOut, ZoomIn, Fit,
        AutoEnhance, Mirror, Freeze, SwitchCamera, ResetCamera,
        AddPage, PrevPage, NextPage, Close,
    };

    struct HitRegion {
        modern_ui::Rect rect;
        Action action = Action::None;
    };

    bool isToolAction(Action action, Tool tool) const;
    void rebuildVisuals();
    void updateFrameGeometry();
    void activate(Action action);
    Action actionAt(const modern_ui::Vec2i& point) const;
    int32_t colorAt(const modern_ui::Vec2i& point) const;
    int32_t sliderAt(const modern_ui::Vec2i& point) const;
    void setSliderValue(int32_t slider, int32_t localX);
    void setPanelForTool();
    const char* toolName() const;
    const char* toolShortcut() const;
    std::string iconMarkup(Tool tool, bool accent, bool disabled) const;

    float scaled(float value) const { return value * uiScale_; }
    int32_t scaledInt(float value) const;

    // 图标（已染色 SVG）：0..7 对应工具列
    std::array<std::string, 8> iconWhite_;
    std::array<std::string, 8> iconAccent_;
    std::array<std::string, 8> iconDim_;
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
    modern_ui::Color color_{255, 214, 64, 255};
    float width_ = 6.0f;
    float smoothing01_ = 0.35f;
    float penTip01_ = 0.65f;
    float taper01_ = 0.30f;

    int32_t pageIndex_ = 0;
    int32_t pageCount_ = 1;
    bool canUndo_ = false;

    PostParams cameraParams_{};
    int32_t frameRate_ = 10;
    bool freeze_ = false;
    bool cameraConnected_ = false;
    std::string cameraName_;

    float zoom_ = 1.0f;

    float uiScale_ = 1.5f;
    bool chromeVisible_ = true;

    float expandProgress_ = 1.0f;
    bool layoutValid_ = false;
    bool visualDirty_ = true;
    uint32_t groupId_ = 0;

    Action hoverAction_ = Action::None;
    Action pressedAction_ = Action::None;
    float hoverProgress_ = 0.0f;
    int32_t hoverColor_ = -1;

    // 命中区域（控件局部坐标）
    std::vector<HitRegion> buttonRegions_;
    std::vector<modern_ui::Rect> colorSwatches_;
    modern_ui::Rect widthSliderRect_{};
    std::array<modern_ui::Rect, 3> inkSliderRects_{};
    std::array<modern_ui::Rect, 5> cameraSliderRects_{};

    int32_t draggingSlider_ = -1;  // -1 无；0 笔宽；1..3 笔迹；4..8 画面
};

} // namespace camerapro
