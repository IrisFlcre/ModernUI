// ============================================================================
// CameraProView.h — 教学摄像头展台视图
// ============================================================================
//
// 内容模型：
//   - 固定 1920×1080 的"内容舞台"（stage），摄像头画面按等比 contain 放入舞台；
//   - 笔迹同样画在舞台坐标系，缩放 / 平移只改变舞台 → 屏幕的显示变换，
//     因此摄像头与笔迹始终严格对齐；
//   - 右侧沿用原批注程序的浮动工具栏。
// ============================================================================

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "CameraCapture.h"
#include "CameraToolbar.h"
#include "FrameProcessor.h"
#include "InkEngine.h"
#include "OcrEngine.h"
#include "View.h"

namespace modern_ui {
class Blend2DRenderer;
class Win32LayeredWindow;
}

namespace camerapro {

class CameraToolbar;

// 全局热键 ID（main 注册 / handleHotkey 共用）
enum HotkeyId : int {
    HotkeyToggleChrome = 1,
    HotkeyFreeze = 2,
    HotkeyUndo = 3,
    HotkeyCopyOcr = 4,
    HotkeyFit = 5,
    HotkeySnapshot = 6,
    HotkeyQuit = 7,
    HotkeyPrevPage = 8,
    HotkeyNextPage = 9,
    HotkeyZoomIn = 10,
    HotkeyZoomOut = 11,
};

class CameraProView : public modern_ui::View {
public:
    CameraProView(int32_t width, int32_t height);
    ~CameraProView() override;

    void initialize(modern_ui::Blend2DRenderer& renderer,
                    modern_ui::Win32LayeredWindow& window,
                    const std::string& iconDirectory);

    bool handleAppEvent(const modern_ui::Event& event);
    void handleHotkey(int id);

    void onTick(float dtSeconds) override;

    std::function<void()> onQuitRequested;

    static constexpr int32_t kStageWidth = 1920;
    static constexpr int32_t kStageHeight = 1080;

private:
    struct LaserPoint {
        modern_ui::Vec2f pos;
        float age = 0.0f;
    };

    struct TouchSlot {
        int32_t id = -1;
        modern_ui::Vec2f point{};
        bool active = false;
    };

    struct OcrHighlight {
        std::array<modern_ui::Vec2f, 4> quadStage{};
        std::string text;
        modern_ui::Rect hitRectStage{};
        float age = 0.0f;
        float delay = 0.0f;
        float lastOpacity = -1.0f;
        bool settled = false;
        uint32_t shapeId = 0;
    };

    // ---- 初始化 / 场景图 ----
    void createRootShapes();
    void applyStyle();
    void syncToolbar();

    // ---- 舞台变换 ----
    float fitScale() const;
    void updateViewTransform();
    void updateInkShape();
    void updateCameraShape();
    void updateCursorShape();
    void updateSelectionOverlay();
    void updatePlaceholder();
    void updateHud();
    void rebuildOcrHighlights();
    void clearOcrHighlights();

    modern_ui::Vec2f screenToStage(const modern_ui::Vec2f& point) const;
    modern_ui::Vec2f stageToScreen(const modern_ui::Vec2f& point) const;
    bool stagePointInside(const modern_ui::Vec2f& point) const;

    void zoomAt(float factor, const modern_ui::Vec2f& anchor);
    void panBy(const modern_ui::Vec2f& delta);
    void fitView();
    void clampPan();

    // ---- 摄像头 ----
    void startCamera(int32_t index);
    void switchCamera();
    void pullCameraFrame();
    void processCurrentFrame();
    void uploadCameraTexture();
    void updateCameraStageRect();
    void setFrozen(bool frozen);
    void setCameraParams(const PostParams& params);
    void handleCameraState();

    // ---- 输入 ----
    // 统一指针交互（鼠标 / 笔 / 单指触控共用）
    void beginPointerInteraction(const modern_ui::Vec2f& screenPoint,
                                 const modern_ui::Event& event);
    void movePointerInteraction(const modern_ui::Vec2f& screenPoint,
                                const modern_ui::Event& event);
    void endPointerInteraction(const modern_ui::Vec2f& screenPoint,
                               const modern_ui::Event& event);

    // 多指手势
    bool handleTouchEvent(const modern_ui::Event& event,
                          const modern_ui::Vec2f& point, bool overToolbar);
    bool addTouch(int32_t id, const modern_ui::Vec2f& point);
    bool updateTouch(int32_t id, const modern_ui::Vec2f& point);
    bool removeTouch(int32_t id);
    void beginTouchGesture();
    void updateTouchGesture();

    void beginStrokeAt(const modern_ui::Vec2f& screenPoint, double timeSeconds);
    void endStroke();
    void beginEraseAt(const modern_ui::Vec2f& screenPoint);
    void endErase();
    void addLaserPoint(const modern_ui::Vec2f& screenPoint);
    // 采样节流 / 补点
    void flushStrokeSample();
    void flushEraseSample();
    void flushPanDelta();
    void applyEraseSample(const modern_ui::Vec2f& stagePoint);
    void markStageRectDirty(const modern_ui::Rect& stageRect);
    void beginLivePolyline(const modern_ui::Vec2f& screenPoint);
    void appendLivePolyline(const modern_ui::Vec2f& screenPoint);
    void clearLivePolyline();
    void cancelActiveInput();
    void setTool(Tool tool);

    void beginOcrSelection(const modern_ui::Vec2f& screenPoint);
    void updateOcrSelection(const modern_ui::Vec2f& screenPoint);
    void finishOcrSelection(const modern_ui::Vec2f& screenPoint);
    void takeOcr(const modern_ui::Rect& screenRegion);
    void applyOcrResult(const OcrResult& result);
    void clickOcrHighlight(const modern_ui::Vec2f& screenPoint);
    void copyAllOcrText();

    void takeSnapshot();
    void updateLaser(float dtSeconds);
    void updateToast(float dtSeconds);
    void updateOcrHighlightAnimation(float dtSeconds);

    void toggleChrome();
    void showToast(const std::string& text, float seconds = 2.2f);

    bool pointOverToolbar(const modern_ui::Vec2i& point) const;
    modern_ui::Rect clampScreenRect(const modern_ui::Rect& rect) const;
    double eventTimeSeconds(const modern_ui::Event& event) const;

    bool initialized_ = false;
    bool chromeVisible_ = true;

    modern_ui::Blend2DRenderer* renderer_ = nullptr;
    modern_ui::Win32LayeredWindow* window_ = nullptr;
    CameraToolbar* toolbar_ = nullptr;

    InkEngine engine_;
    InkStyle style_{};
    Tool tool_ = Tool::Pen;
    Tool lastNonPanTool_ = Tool::Pen;

    modern_ui::Color color_{255, 214, 64, 255};
    float width_ = 6.0f;
    float uiSmoothing_ = 0.35f;
    float uiPenTip_ = 0.65f;
    float uiTaper_ = 0.30f;

    // ---- 摄像头 / 后处理 ----
    CameraCapture camera_;
    FrameProcessor processor_;
    std::vector<uint8_t> rawBgra_;
    std::vector<uint8_t> processedBgra_;
    int32_t frameWidth_ = 0;
    int32_t frameHeight_ = 0;
    uint64_t cameraTexture_ = 0;
    bool frameReady_ = false;
    bool frozen_ = false;
    float retryTimer_ = 0.0f;
    std::string cameraError_;
    std::string lastReportedError_;
    std::string placeholderSignature_;
    std::string hudSignature_;
    std::vector<std::string> deviceNames_;
    float deviceListTimer_ = 0.0f;

    // 摄像头帧在舞台中的位置（contain）
    float cameraStageX_ = 0.0f;
    float cameraStageY_ = 0.0f;
    float cameraStageW_ = 0.0f;
    float cameraStageH_ = 0.0f;

    // ---- 缩放 / 平移 ----
    float zoom_ = 1.0f;
    float fitScale_ = 1.0f;
    float viewScale_ = 1.0f;
    float originX_ = 0.0f;
    float originY_ = 0.0f;
    float panX_ = 0.0f;
    float panY_ = 0.0f;
    bool panning_ = false;
    modern_ui::Vec2f lastPanPoint_{};

    // ---- 绘制状态 ----
    bool drawing_ = false;
    bool erasing_ = false;
    bool laserActive_ = false;

    // 绘制采样节流（限制高频鼠标 / 触控的事件进入笔迹引擎）
    uint64_t lastStrokeSampleUs_ = 0;
    uint64_t lastEraseSampleUs_ = 0;
    uint64_t lastLaserSampleUs_ = 0;
    uint64_t lastPanSampleUs_ = 0;
    bool hasPendingStrokeSample_ = false;
    modern_ui::Vec2f pendingStrokeStage_{};
    modern_ui::Vec2f pendingStrokeScreen_{};
    double pendingStrokeTime_ = 0.0;
    bool hasPendingEraseSample_ = false;
    modern_ui::Vec2f pendingEraseStage_{};
    modern_ui::Vec2f pendingPanDelta_{};

    // 触控：最多跟踪两个触点，双指进入缩放 / 平移手势
    std::array<TouchSlot, 2> touchSlots_{};
    int32_t activeTouchCount_ = 0;
    bool touchGesture_ = false;
    bool touchGestureLocked_ = false;
    modern_ui::Vec2f gestureAnchorStage_{};
    float gestureStartDistance_ = 1.0f;
    float gestureStartZoom_ = 1.0f;
    uint64_t lastGestureSampleUs_ = 0;

    uint32_t cameraShapeId_ = 0;
    uint32_t inkShapeId_ = 0;
    uint32_t cursorShapeId_ = 0;
    uint32_t placeholderShapeId_ = 0;
    uint32_t hudShapeId_ = 0;
    uint32_t toastShapeId_ = 0;
    uint64_t syncedInkHandle_ = 0;
    std::array<uint32_t, 5> selectionShapeIds_{};
    std::array<uint32_t, 28> laserShapeIds_{};
    uint32_t laserHeadShapeId_ = 0;

    std::vector<uint32_t> livePolylineIds_;
    std::vector<modern_ui::Vec2f> livePolylinePoints_;
    modern_ui::Color liveStrokeColor_{255, 255, 255, 255};
    float liveStrokeWidth_ = 6.0f;
    bool liveChunkHasShape_ = false;

    modern_ui::Vec2f cursorPoint_{};
    bool cursorInCanvas_ = false;
    bool cursorVisible_ = false;
    bool cursorDirty_ = true;

    std::vector<LaserPoint> laserPoints_;
    bool laserWasVisible_ = false;
    std::size_t laserVisibleSlots_ = 0;
    bool laserHeadVisible_ = false;

    // ---- OCR ----
    OcrEngine ocr_;
    std::vector<OcrHighlight> ocrHighlights_;
    int32_t ocrRequestId_ = 0;
    bool ocrSelecting_ = false;
    bool selecting_ = false;
    modern_ui::Vec2f selectionStart_{};
    modern_ui::Rect selectionRect_{};
    bool selectionDirty_ = false;
    int32_t ocrCropX_ = 0;
    int32_t ocrCropY_ = 0;
    int32_t ocrCropW_ = 0;
    int32_t ocrCropH_ = 0;

    // ---- Toast / 尺寸 ----
    std::string toastText_;
    float toastRemaining_ = 0.0f;
    float toastDuration_ = 0.0f;
    float toastLastAlpha_ = -1.0f;
    bool toastBuilt_ = false;

    int32_t lastCanvasWidth_ = 0;
    int32_t lastCanvasHeight_ = 0;
    uint64_t lastClickTick_ = 0;
    modern_ui::Vec2i lastClickPoint_{0, 0};
};

} // namespace camerapro
