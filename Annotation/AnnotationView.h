// ============================================================================
// AnnotationView.h — 全屏批注视图
// ============================================================================
//
// 负责把输入事件 → InkEngine（Bitmap 缓存） / 激光笔 / 截图选区，
// 并把工具栏、实时笔迹、光标环、提示 Toast 合成到根 Canvas。
// ============================================================================

#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "AiCards.h"
#include "AiClient.h"
#include "InkEngine.h"
#include "OcrEngine.h"
#include "OfficeBridge.h"
#include "Toolbar.h"
#include "View.h"

namespace modern_ui {
class Blend2DRenderer;
class GlobalMouseHook;
class Win32LayeredWindow;
}

namespace annotation {

// 全局热键 ID（main 注册 / handleHotkey 共用）
enum HotkeyId : int {
    HotkeyToggleVisible = 1,
    HotkeyPointerMode = 2,
    HotkeyUndo = 3,
    HotkeyPrevPage = 4,
    HotkeyNextPage = 5,
    HotkeyQuit = 6,
    HotkeyCopyOcr = 7,
};

class AnnotationView : public modern_ui::View {
public:
    AnnotationView(int32_t width, int32_t height);
    ~AnnotationView() override = default;

    void initialize(modern_ui::Blend2DRenderer& renderer,
                    modern_ui::Win32LayeredWindow& window,
                    modern_ui::GlobalMouseHook* hook,
                    const std::string& iconDirectory);

    // 供 App::onEvent 调用；返回 true 表示事件已被消费（不再派发给 View 根）
    bool handleAppEvent(const modern_ui::Event& event);
    void handleHotkey(int id);

    void onTick(float dtSeconds) override;

    void setTool(Tool tool);
    Tool tool() const { return tool_; }
    void togglePointerMode();
    void toggleVisible();

    std::function<void()> onQuitRequested;

private:
    struct LaserPoint {
        modern_ui::Vec2f pos;
        float age = 0.0f;
    };

    void createRootShapes();
    void applyStyle();
    void syncToolbar();
    void syncPageImage();

    void beginStrokeAt(const modern_ui::Vec2f& point, double timeSeconds);
    void endStroke();
    void beginEraseAt(const modern_ui::Vec2f& point);
    void endErase();
    void addLaserPoint(const modern_ui::Vec2f& point);
    // 高频鼠标 / 触控采样节流
    void flushStrokeSample();
    void flushEraseSample();
    void applyEraseSample(const modern_ui::Vec2f& point);

    // 实时预览只画简单折线；松笔后由 InkEngine 重绘完整笔迹。
    void beginLivePolyline(const modern_ui::Vec2f& point);
    void appendLivePolyline(const modern_ui::Vec2f& point);
    void clearLivePolyline();
    void cancelActiveInput();

    void beginScreenshotSelection();
    void updateScreenshotSelection(const modern_ui::Vec2f& point);
    void finishScreenshotSelection();
    void startScreenshotMode();
    void cancelScreenshotMode();
    void takeScreenshot(const modern_ui::Rect& region);

    // ---- 文字识别 / 荧光高亮 ----
    void startOcrMode();
    void finishOcrSelection(const modern_ui::Vec2f& point);
    void takeOcr(const modern_ui::Rect& region);
    void applyOcrResult(const OcrResult& result);
    void clearOcrHighlights();
    void updateOcrHighlights(float dtSeconds);
    bool copyHighlightAt(const modern_ui::Vec2i& point);
    void copyAllOcrText();

    // ---- AI 智能辅助教学 ----
    void startAiAssist();
    void handleAiOcrResult(const OcrResult& result);
    void applyAiReply(const AiReply& reply);
    bool pointOverAiCards(const modern_ui::Vec2i& point) const;

    void updateCursor();
    void updateLaser(float dtSeconds);
    void updateToast(float dtSeconds);
    void updateScreenshotOverlay();
    void updateHookPolicy();

    void showToast(const std::string& text, float seconds = 2.2f);
    bool pointOverToolbar(const modern_ui::Vec2i& point) const;
    modern_ui::Rect clampRect(const modern_ui::Rect& rect) const;
    double eventTimeSeconds(const modern_ui::Event& event) const;

    void undo();
    void clearCurrentPage();
    void addPage();
    void gotoPrevPage();
    void gotoNextPage();
    // ---- WPS / Office 放映页码联动 ----
    void updateOfficeSync();
    bool stepPresentationPage(bool next);   // true = 已由放映接管翻页
    void switchAnnotationPage(int32_t index);

    bool initialized_ = false;
    bool visible_ = true;

    OfficeBridge office_;
    bool officeConnected_ = false;
    bool officeWps_ = false;
    int32_t officePage_ = 0;
    int32_t officeTotal_ = 0;
    int32_t syncedOfficePage_ = 0;
    uint64_t officeLastActiveMs_ = 0;
    uint64_t officeGraceUntilMs_ = 0;

    modern_ui::Blend2DRenderer* renderer_ = nullptr;
    modern_ui::Win32LayeredWindow* window_ = nullptr;
    modern_ui::GlobalMouseHook* hook_ = nullptr;
    AnnotationToolbar* toolbar_ = nullptr;

    InkEngine engine_;
    InkStyle style_{};

    OcrEngine ocr_;
    struct TextHighlight {
        uint32_t shapeId = 0;
        std::string text;
        modern_ui::Rect hitRect{};
        float age = 0.0f;
        float delay = 0.0f;
        float lastOpacity = -1.0f;
        bool settled = false;
    };
    std::vector<TextHighlight> ocrHighlights_;
    int ocrRequestId_ = 0;
    bool ocrSelecting_ = false;

    // AI 智能辅助教学
    AiClient ai_;
    AiCardLayer aiCards_;
    AiAnnotationLayer aiMarks_;
    std::vector<modern_ui::Rect> aiOcrLineRects_;
    int aiRequestId_ = 0;
    int aiOcrRequestId_ = 0;
    bool aiBusy_ = false;
    bool aiAwaitingOcr_ = false;

    Tool tool_ = Tool::Pen;
    Tool lastNonPointerTool_ = Tool::Pen;

    modern_ui::Color color_{255, 214, 64, 255};
    float width_ = 6.0f;
    float eraserSize_ = 32.0f;      // 橡皮直径（屏幕像素）
    float cardScale_ = 1.0f;        // AI 卡片缩放系数
    float uiSmoothing_ = 0.35f;
    float uiPenTip_ = 0.65f;
    float uiTaper_ = 0.30f;

    bool drawing_ = false;
    bool erasing_ = false;
    bool laserActive_ = false;

    // 绘制采样节流（3 倍上限：鼠标 ≈375Hz，触控 ≈300Hz，激光 ≈150Hz）
    uint64_t lastStrokeSampleUs_ = 0;
    uint64_t lastEraseSampleUs_ = 0;
    uint64_t lastLaserSampleUs_ = 0;
    bool hasPendingStrokeSample_ = false;
    modern_ui::Vec2f pendingStrokeStage_{};
    modern_ui::Vec2f pendingStrokeScreen_{};
    double pendingStrokeTime_ = 0.0;
    bool hasPendingEraseSample_ = false;
    modern_ui::Vec2f pendingEraseStage_{};

    uint32_t imageShapeId_ = 0;
    uint32_t cursorShapeId_ = 0;
    uint32_t toastShapeId_ = 0;
    std::vector<uint32_t> livePolylineIds_;
    std::vector<modern_ui::Vec2f> livePolylinePoints_;
    modern_ui::Color liveStrokeColor_{255, 255, 255, 255};
    float liveStrokeWidth_ = 6.0f;
    bool liveChunkHasShape_ = false;
    std::array<uint32_t, 5> screenshotShapeIds_{};
    std::array<uint32_t, 28> laserShapeIds_{};
    uint32_t laserHeadShapeId_ = 0;
    uint64_t pageImageHandle_ = 0;

    modern_ui::Vec2f cursorPoint_{};
    bool cursorInCanvas_ = false;
    bool cursorVisible_ = false;
    bool cursorDirty_ = true;
    int32_t cursorRadius_ = 0;

    // 激光笔
    std::vector<LaserPoint> laserPoints_;
    bool laserWasVisible_ = false;
    std::size_t laserVisibleSlots_ = 0;
    bool laserHeadVisible_ = false;

    // 截图选区
    bool screenshotMode_ = false;
    bool selecting_ = false;
    modern_ui::Vec2f selectionStart_{};
    modern_ui::Rect selectionRect_{};
    bool screenshotDirty_ = false;

    // Toast
    std::string toastText_;
    float toastRemaining_ = 0.0f;
    float toastDuration_ = 0.0f;
    float toastLastAlpha_ = -1.0f;
    bool  toastBuilt_ = false;

    int32_t syncedPageIndex_ = -1;
    bool hookPolicyInitialized_ = false;
    bool hookPolicySuppressAll_ = false;
    modern_ui::Rect hookPolicyRect_{};
    std::array<modern_ui::Rect, 4> hookPolicyCardRects_{};
    int32_t hookPolicyCardCount_ = 0;
};

} // namespace annotation
