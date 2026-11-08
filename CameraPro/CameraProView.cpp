// ============================================================================
// CameraProView.cpp
// ============================================================================

#include "CameraProView.h"

#include "AssetPath.h"
#include "Blend2DRenderer.h"
#include "CameraToolbar.h"
#include "Snapshot.h"
#include "Win32LayeredWindow.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <blend2d/blend2d.h>

#ifndef CAMERAPRO_MODEL_DIR
  #define CAMERAPRO_MODEL_DIR "models"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using modern_ui::CircleShape;
using modern_ui::Color;
using modern_ui::Event;
using modern_ui::EventType;
using modern_ui::GroupData;
using modern_ui::ImageShape;
using modern_ui::LinearGradient;
using modern_ui::Paint;
using modern_ui::PathShape;
using modern_ui::PointerType;
using modern_ui::PolylineShape;
using modern_ui::Rect;
using modern_ui::RectShape;
using modern_ui::RRectShape;
using modern_ui::Shape;
using modern_ui::TextShape;
using modern_ui::Vec2f;
using modern_ui::Vec2i;

namespace camerapro {

namespace {

// 绘制采样节流：高频鼠标（1000Hz+）与触控面板都按固定间隔送进笔迹引擎。
// 3 倍采样上限：鼠标 / 橡皮 / 平移 / 手势 ≈375Hz，触控 ≈300Hz，激光 ≈150Hz。
// 使用 QueryPerformanceCounter 微秒计时，避免 GetTickCount64 的 10~16ms 粒度。
constexpr uint64_t kMouseSampleIntervalUs = 2667;
constexpr uint64_t kTouchSampleIntervalUs = 3333;
constexpr uint64_t kEraseSampleIntervalUs = 2667;
constexpr uint64_t kLaserSampleIntervalUs = 6667;
constexpr uint64_t kPanSampleIntervalUs = 2667;
constexpr uint64_t kGestureSampleIntervalUs = 2667;

uint64_t monotonicMicros() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value;
    }();

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    if (frequency.QuadPart <= 0) return 0;
    return static_cast<uint64_t>(
        static_cast<double>(counter.QuadPart) * 1000000.0 /
        static_cast<double>(frequency.QuadPart));
}

float clampf(float value, float lo, float hi) {
    return std::max(lo, std::min(hi, value));
}

Rect normalizedRect(const Vec2f& a, const Vec2f& b) {
    const int32_t x0 = static_cast<int32_t>(std::lround(std::min(a.x, b.x)));
    const int32_t y0 = static_cast<int32_t>(std::lround(std::min(a.y, b.y)));
    const int32_t x1 = static_cast<int32_t>(std::lround(std::max(a.x, b.x)));
    const int32_t y1 = static_cast<int32_t>(std::lround(std::max(a.y, b.y)));
    return Rect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

float distanceOf(const Vec2f& a, const Vec2f& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::string utf8Preview(const std::string& text, std::size_t maxChars) {
    std::size_t count = 0;
    std::size_t index = 0;
    while (index < text.size() && count < maxChars) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t step = 1;
        if (lead >= 0xF0) step = 4;
        else if (lead >= 0xE0) step = 3;
        else if (lead >= 0xC0) step = 2;
        index += step;
        ++count;
    }
    if (index < text.size()) return text.substr(0, index) + "…";
    return text;
}

} // namespace

// ============================================================================
// 初始化
// ============================================================================

CameraProView::CameraProView(int32_t width, int32_t height)
    : modern_ui::View(width, height) {}

CameraProView::~CameraProView() {
    camera_.stop();
    ocr_.shutdown();
}

void CameraProView::initialize(modern_ui::Blend2DRenderer& renderer,
                               modern_ui::Win32LayeredWindow& window,
                               const std::string& iconDirectory) {
    if (initialized_) return;

    renderer_ = &renderer;
    window_ = &window;

    engine_.initialize(renderer_, kStageWidth, kStageHeight);
    createRootShapes();

    toolbar_ = emplaceChild<CameraToolbar>();
    toolbar_->loadIcons(iconDirectory);
    toolbar_->setScreenSize(width(), height());

    toolbar_->onToolChanged = [this](Tool tool) { setTool(tool); };
    toolbar_->onColorChanged = [this](const Color& color) {
        color_ = color;
        applyStyle();
    };
    toolbar_->onWidthChanged = [this](float width) {
        width_ = width;
        applyStyle();
    };
    toolbar_->onInkParametersChanged = [this](float smoothing, float penTip,
                                              float taper) {
        uiSmoothing_ = smoothing;
        uiPenTip_ = penTip;
        uiTaper_ = taper;
        applyStyle();
    };
    toolbar_->onUndo = [this] { handleHotkey(HotkeyUndo); };
    toolbar_->onClear = [this] {
        cancelActiveInput();
        clearOcrHighlights();
        engine_.clearPage();
        canvas().markDirtyAll();
        updateInkShape();
        syncToolbar();
    };
    toolbar_->onSnapshot = [this] { takeSnapshot(); };
    toolbar_->onCameraParamsChanged = [this](const PostParams& params) {
        setCameraParams(params);
    };
    toolbar_->onFrameRateChanged = [this](int32_t fps) {
        camera_.setTargetFps(fps);
        syncToolbar();
    };
    toolbar_->onFreezeChanged = [this](bool frozen) { setFrozen(frozen); };
    toolbar_->onSwitchCamera = [this] { switchCamera(); };
    toolbar_->onResetCamera = [this] {
        setCameraParams(PostParams{});
        syncToolbar();
        showToast("画面参数已恢复默认", 1.8f);
    };
    toolbar_->onZoomIn = [this] {
        if (!drawing_ && !erasing_) {
            zoomAt(1.25f, Vec2f{static_cast<float>(canvas().width()) * 0.5f,
                                static_cast<float>(canvas().height()) * 0.5f});
        }
    };
    toolbar_->onZoomOut = [this] {
        if (!drawing_ && !erasing_) {
            zoomAt(0.8f, Vec2f{static_cast<float>(canvas().width()) * 0.5f,
                               static_cast<float>(canvas().height()) * 0.5f});
        }
    };
    toolbar_->onFit = [this] { fitView(); };
    toolbar_->onAddPage = [this] {
        cancelActiveInput();
        clearOcrHighlights();
        if (!engine_.addPage()) {
            showToast("已达到最大页数", 1.8f);
            return;
        }
        updateInkShape();
        canvas().markDirtyAll();
        syncToolbar();
        showToast("已新建第 " + std::to_string(engine_.pageIndex() + 1) + " 页",
                  1.6f);
    };
    toolbar_->onPrevPage = [this] {
        if (engine_.pageIndex() <= 0) return;
        cancelActiveInput();
        if (engine_.setPage(engine_.pageIndex() - 1)) {
            updateInkShape();
            canvas().markDirtyAll();
            syncToolbar();
        }
    };
    toolbar_->onNextPage = [this] {
        if (engine_.pageIndex() + 1 >= engine_.pageCount()) return;
        cancelActiveInput();
        if (engine_.setPage(engine_.pageIndex() + 1)) {
            updateInkShape();
            canvas().markDirtyAll();
            syncToolbar();
        }
    };
    toolbar_->onClose = [this] {
        if (onQuitRequested) onQuitRequested();
    };

    applyStyle();
    syncToolbar();

    ocr_.initialize(
        modern_ui::resolveAssetDirectory("models", CAMERAPRO_MODEL_DIR));

    lastCanvasWidth_ = canvas().width();
    lastCanvasHeight_ = canvas().height();
    updateViewTransform();

    camera_.start(0);
    retryTimer_ = 3.0f;

    initialized_ = true;
    cursorDirty_ = true;
}

void CameraProView::createRootShapes() {
    const int32_t w = std::max(1, canvas().width());
    const int32_t h = std::max(1, canvas().height());

    // 摄像头画面（最底层）
    {
        Shape shape;
        ImageShape image;
        image.handle = 0;
        image.x = 0;
        image.y = 0;
        image.w = w;
        image.h = h;
        shape.data = image;
        shape.base.z = -10000;
        shape.base.visible = false;
        cameraShapeId_ = canvas().addShape(std::move(shape));
    }

    // 笔迹位图
    {
        Shape shape;
        ImageShape image;
        image.handle = engine_.currentImageHandle();
        image.x = 0;
        image.y = 0;
        image.w = w;
        image.h = h;
        shape.data = image;
        shape.base.z = -9600;
        shape.base.visible = true;
        inkShapeId_ = canvas().addShape(std::move(shape));
        syncedInkHandle_ = image.handle;
    }

    // 激光笔拖尾
    for (size_t i = 0; i < laserShapeIds_.size(); ++i) {
        Shape shape;
        CircleShape circle;
        circle.cx = 0;
        circle.cy = 0;
        circle.r = 1.0f;
        shape.data = circle;
        shape.base.fill = Paint(Color{255, 98, 74, 255});
        shape.base.z = -9000;
        shape.base.visible = false;
        laserShapeIds_[i] = canvas().addShape(std::move(shape));
    }

    {
        Shape shape;
        CircleShape circle;
        circle.cx = 0;
        circle.cy = 0;
        circle.r = 1.0f;
        shape.data = circle;
        shape.base.fill = Paint(Color{255, 238, 222, 255});
        shape.base.z = -8999;
        shape.base.visible = false;
        laserHeadShapeId_ = canvas().addShape(std::move(shape));
    }

    // 笔尖 / 橡皮光标环
    {
        Shape shape;
        CircleShape circle;
        circle.cx = 0;
        circle.cy = 0;
        circle.r = 1.0f;
        shape.data = circle;
        shape.base.stroke = Paint(Color{96, 205, 255, 190});
        shape.base.strokeWidth = 2;
        shape.base.z = -7000;
        shape.base.visible = false;
        cursorShapeId_ = canvas().addShape(std::move(shape));
    }

    // OCR 选区：上 / 下 / 左 / 右 遮罩 + 边框
    for (size_t i = 0; i < selectionShapeIds_.size(); ++i) {
        Shape shape;
        shape.data = RectShape{};
        shape.base.z = -2000;
        shape.base.visible = false;
        if (i < 4) {
            shape.base.fill = Paint(Color{0, 0, 0, 110});
        } else {
            shape.base.fill = Paint();
            shape.base.stroke = Paint(Color{255, 232, 120, 220});
            shape.base.strokeWidth = 2;
        }
        selectionShapeIds_[i] = canvas().addShape(std::move(shape));
    }

    // 无摄像头占位
    {
        Shape shape;
        shape.data = GroupData{};
        shape.base.z = -2500;
        shape.base.visible = false;
        placeholderShapeId_ = canvas().addShape(std::move(shape));
    }

    // HUD
    {
        Shape shape;
        shape.data = GroupData{};
        shape.base.z = -1200;
        shape.base.visible = false;
        hudShapeId_ = canvas().addShape(std::move(shape));
    }

    // Toast
    {
        Shape shape;
        shape.data = GroupData{};
        shape.base.z = -1500;
        shape.base.visible = false;
        toastShapeId_ = canvas().addShape(std::move(shape));
    }
}

// ============================================================================
// 样式 / 状态同步
// ============================================================================

void CameraProView::applyStyle() {
    InkParameters parameters = engine_.parameters();
    parameters.smoothingTau = uiSmoothing_ * 0.060f;
    parameters.penTip = clampf(uiPenTip_, 0.0f, 1.0f);
    parameters.taper = clampf(uiTaper_, 0.0f, 1.0f);
    parameters.speedScale = 1400.0f;
    engine_.setParameters(parameters);

    style_.color = color_;
    style_.width = clampf(width_, 1.0f, 40.0f);
    style_.alpha = 1.0f;
    style_.highlighter = (tool_ == Tool::Highlighter);
    engine_.setStyle(style_);

    cursorDirty_ = true;
}

void CameraProView::syncToolbar() {
    if (!toolbar_) return;
    toolbar_->setTool(tool_);
    toolbar_->setColor(color_);
    toolbar_->setWidth(width_);
    toolbar_->setInkParameters(uiSmoothing_, uiPenTip_, uiTaper_);
    toolbar_->setPageInfo(engine_.pageIndex(), engine_.pageCount());
    toolbar_->setHistoryState(engine_.canUndo());
    toolbar_->setZoom(zoom_);
    toolbar_->setCameraParams(processor_.params());
    toolbar_->setFrameRate(camera_.targetFps());
    toolbar_->setFreeze(frozen_);
}

// ============================================================================
// 舞台变换
// ============================================================================

float CameraProView::fitScale() const {
    const int32_t w = std::max(1, canvas().width());
    const int32_t h = std::max(1, canvas().height());
    return std::min(static_cast<float>(w) / static_cast<float>(kStageWidth),
                    static_cast<float>(h) / static_cast<float>(kStageHeight));
}

void CameraProView::clampPan() {
    const int32_t w = std::max(1, canvas().width());
    const int32_t h = std::max(1, canvas().height());
    const float contentW = static_cast<float>(kStageWidth) * viewScale_;
    const float contentH = static_cast<float>(kStageHeight) * viewScale_;

    const float limitX = std::max(0.0f, (contentW - static_cast<float>(w)) * 0.5f);
    const float limitY = std::max(0.0f, (contentH - static_cast<float>(h)) * 0.5f);
    panX_ = clampf(panX_, -limitX, limitX);
    panY_ = clampf(panY_, -limitY, limitY);
}

void CameraProView::updateViewTransform() {
    fitScale_ = fitScale();
    viewScale_ = fitScale_ * zoom_;
    clampPan();

    const int32_t w = std::max(1, canvas().width());
    const int32_t h = std::max(1, canvas().height());
    originX_ = (static_cast<float>(w) -
                static_cast<float>(kStageWidth) * viewScale_) * 0.5f + panX_;
    originY_ = (static_cast<float>(h) -
                static_cast<float>(kStageHeight) * viewScale_) * 0.5f + panY_;

    updateInkShape();
    updateCameraShape();
    rebuildOcrHighlights();
    updateHud();
    cursorDirty_ = true;
}

Vec2f CameraProView::screenToStage(const Vec2f& point) const {
    if (viewScale_ <= 0.0001f) return Vec2f{0.0f, 0.0f};
    return Vec2f{(point.x - originX_) / viewScale_,
                 (point.y - originY_) / viewScale_};
}

Vec2f CameraProView::stageToScreen(const Vec2f& point) const {
    return Vec2f{originX_ + point.x * viewScale_,
                 originY_ + point.y * viewScale_};
}

bool CameraProView::stagePointInside(const Vec2f& point) const {
    return point.x >= 0.0f && point.y >= 0.0f &&
           point.x <= static_cast<float>(kStageWidth) &&
           point.y <= static_cast<float>(kStageHeight);
}

void CameraProView::zoomAt(float factor, const Vec2f& anchor) {
    const Vec2f stagePoint = screenToStage(anchor);
    const float nextZoom = clampf(zoom_ * factor, 0.5f, 8.0f);
    if (std::fabs(nextZoom - zoom_) < 0.0005f) return;

    zoom_ = nextZoom;
    const float newFit = fitScale();
    const float newScale = newFit * zoom_;
    const float baseX = (static_cast<float>(canvas().width()) -
                         static_cast<float>(kStageWidth) * newScale) * 0.5f;
    const float baseY = (static_cast<float>(canvas().height()) -
                         static_cast<float>(kStageHeight) * newScale) * 0.5f;
    panX_ = anchor.x - stagePoint.x * newScale - baseX;
    panY_ = anchor.y - stagePoint.y * newScale - baseY;

    updateViewTransform();
    syncToolbar();
}

void CameraProView::panBy(const Vec2f& delta) {
    panX_ += delta.x;
    panY_ += delta.y;
    updateViewTransform();
}

void CameraProView::fitView() {
    zoom_ = 1.0f;
    panX_ = 0.0f;
    panY_ = 0.0f;
    updateViewTransform();
    syncToolbar();
    showToast("已适应屏幕", 1.2f);
}

// ============================================================================
// 摄像头
// ============================================================================

void CameraProView::updateCameraStageRect() {
    if (frameWidth_ <= 0 || frameHeight_ <= 0) {
        cameraStageX_ = 0.0f;
        cameraStageY_ = 0.0f;
        cameraStageW_ = static_cast<float>(kStageWidth);
        cameraStageH_ = static_cast<float>(kStageHeight);
        return;
    }

    const float scale = std::min(
        static_cast<float>(kStageWidth) / static_cast<float>(frameWidth_),
        static_cast<float>(kStageHeight) / static_cast<float>(frameHeight_));
    cameraStageW_ = static_cast<float>(frameWidth_) * scale;
    cameraStageH_ = static_cast<float>(frameHeight_) * scale;
    cameraStageX_ = (static_cast<float>(kStageWidth) - cameraStageW_) * 0.5f;
    cameraStageY_ = (static_cast<float>(kStageHeight) - cameraStageH_) * 0.5f;
}

void CameraProView::startCamera(int32_t index) {
    frameReady_ = false;
    cameraError_.clear();
    lastReportedError_.clear();
    retryTimer_ = 3.0f;
    clearOcrHighlights();

    camera_.start(index);
    frozen_ = false;
    if (toolbar_) toolbar_->setFreeze(false);
    updateCameraShape();
    placeholderSignature_.clear();
    hudSignature_.clear();
}

void CameraProView::switchCamera() {
    std::vector<std::string> names = camera_.deviceNames();
    if (names.empty()) {
        startCamera(0);
        showToast("正在重新连接摄像头…", 1.8f);
        return;
    }

    int32_t next = camera_.deviceIndex() + 1;
    if (next >= static_cast<int32_t>(names.size())) next = 0;
    startCamera(next);
    showToast("切换摄像头：" + utf8Preview(names[static_cast<size_t>(next)], 18),
              1.8f);
}

void CameraProView::pullCameraFrame() {
    if (frozen_) return;

    VideoFrame frame;
    if (!camera_.takeFrame(&frame)) return;

    const bool sizeChanged =
        frame.width != frameWidth_ || frame.height != frameHeight_;
    frameWidth_ = frame.width;
    frameHeight_ = frame.height;

    if (sizeChanged) {
        updateCameraStageRect();
        clearOcrHighlights();
        hudSignature_.clear();
    }

    frameReady_ = true;
    rawBgra_ = std::move(frame.bgra);
    processCurrentFrame();
}

void CameraProView::processCurrentFrame() {
    if (!renderer_ || frameWidth_ <= 0 || frameHeight_ <= 0 ||
        rawBgra_.empty()) {
        return;
    }

    const size_t bytes = static_cast<size_t>(frameWidth_) * frameHeight_ * 4u;
    if (rawBgra_.size() < bytes) return;

    processedBgra_.resize(bytes);
    std::memcpy(processedBgra_.data(), rawBgra_.data(), bytes);
    processor_.process(processedBgra_.data(), frameWidth_, frameHeight_);

    uploadCameraTexture();
    updateCameraShape();
    updateHud();

    // 画面内容每帧变化，只标脏摄像头可见区域。
    const float x0 = originX_ + cameraStageX_ * viewScale_;
    const float y0 = originY_ + cameraStageY_ * viewScale_;
    const float x1 = originX_ + (cameraStageX_ + cameraStageW_) * viewScale_;
    const float y1 = originY_ + (cameraStageY_ + cameraStageH_) * viewScale_;
    const Rect dirty = clampScreenRect(Rect{
        static_cast<int32_t>(std::floor(x0)),
        static_cast<int32_t>(std::floor(y0)),
        static_cast<int32_t>(std::ceil(x1 - x0)) + 1,
        static_cast<int32_t>(std::ceil(y1 - y0)) + 1});
    if (!dirty.isNull()) canvas().markDirty(dirty);
}

void CameraProView::uploadCameraTexture() {
    if (!renderer_ || frameWidth_ <= 0 || frameHeight_ <= 0) return;

    if (cameraTexture_ == 0) {
        cameraTexture_ = renderer_->createBitmapImage(
            frameWidth_, frameHeight_, Color{0, 0, 0, 255});
        if (cameraTexture_ == 0) return;
    }

    BLImage* destination = renderer_->image(cameraTexture_);
    if (!destination || destination->width() != frameWidth_ ||
        destination->height() != frameHeight_) {
        // 分辨率变化：重建纹理。
        if (cameraTexture_ != 0) renderer_->releaseImage(cameraTexture_);
        cameraTexture_ = renderer_->createBitmapImage(
            frameWidth_, frameHeight_, Color{0, 0, 0, 255});
        destination = cameraTexture_ ? renderer_->image(cameraTexture_) : nullptr;
        if (!destination) return;
    }

    BLImage source;
    source.create_from_data(
        frameWidth_, frameHeight_, BL_FORMAT_PRGB32,
        processedBgra_.data(),
        static_cast<intptr_t>(frameWidth_) * 4,
        BL_DATA_ACCESS_READ);
    BLContext context(*destination);
    if (!context.is_valid()) return;
    context.set_comp_op(BL_COMP_OP_SRC_COPY);
    context.blit_image(
        BLRect(0.0, 0.0, static_cast<double>(frameWidth_),
               static_cast<double>(frameHeight_)),
        source, BLRectI(0, 0, frameWidth_, frameHeight_));
    context.flush(BL_CONTEXT_FLUSH_SYNC);
}

void CameraProView::setFrozen(bool frozen) {
    if (frozen_ == frozen) return;
    frozen_ = frozen;
    if (toolbar_) toolbar_->setFreeze(frozen_);
    if (frozen_ && !frameReady_) {
        showToast("画面已冻结 · 等待摄像头连接", 1.8f);
    } else {
        showToast(frozen_ ? "画面已冻结" : "画面已恢复", 1.5f);
    }
    updateHud();
}

void CameraProView::setCameraParams(const PostParams& params) {
    processor_.setParams(params);
    if (frozen_ && frameReady_) {
        processCurrentFrame();
    }
    if (toolbar_) toolbar_->setCameraParams(processor_.params());
}

void CameraProView::handleCameraState() {
    cameraError_ = camera_.error();

    if (!cameraError_.empty()) {
        if (cameraError_ != lastReportedError_) {
            lastReportedError_ = cameraError_;
            if (frameReady_) showToast("摄像头：" + cameraError_, 3.0f);
        }
    }

    if (deviceListTimer_ <= 0.0f) {
        deviceNames_ = camera_.deviceNames();
        deviceListTimer_ = 0.5f;
    }

    if (toolbar_) {
        const int32_t index = camera_.deviceIndex();
        std::string name;
        if (!deviceNames_.empty() && index >= 0 &&
            index < static_cast<int32_t>(deviceNames_.size())) {
            name = deviceNames_[static_cast<size_t>(index)];
        }
        toolbar_->setCameraInfo(name, frameReady_ && cameraError_.empty());
    }

    updatePlaceholder();
    updateHud();

    // 摄像头掉线 / 未插入：每 3 秒静默重连一次，支持热插拔。
    if (cameraError_.empty() && !frameReady_ && !camera_.running()) {
        cameraError_ = "摄像头未连接";
    }

    if (!cameraError_.empty() && !camera_.running()) {
        if (retryTimer_ <= 0.0f) {
            retryTimer_ = 3.0f;
            camera_.start(camera_.deviceIndex());
        }
    }
}

// ============================================================================
// 场景图更新
// ============================================================================

void CameraProView::updateInkShape() {
    if (inkShapeId_ == 0) return;

    const uint64_t handle = engine_.currentImageHandle();
    syncedInkHandle_ = handle;

    const float stageScreenW = static_cast<float>(kStageWidth) * viewScale_;
    const float stageScreenH = static_cast<float>(kStageHeight) * viewScale_;
    const Rect stageScreen{
        static_cast<int32_t>(std::lround(originX_)),
        static_cast<int32_t>(std::lround(originY_)),
        std::max(1, static_cast<int32_t>(std::lround(stageScreenW))),
        std::max(1, static_cast<int32_t>(std::lround(stageScreenH)))};
    const Rect visible = stageScreen.intersect(
        Rect{0, 0, std::max(1, canvas().width()), std::max(1, canvas().height())});
    if (visible.isNull() || handle == 0) {
        canvas().updateShape(inkShapeId_, [](Shape& shape) {
            shape.base.visible = false;
        });
        return;
    }

    const float sx0 = (static_cast<float>(visible.x) - stageScreen.x) /
                      std::max(0.0001f, viewScale_);
    const float sy0 = (static_cast<float>(visible.y) - stageScreen.y) /
                      std::max(0.0001f, viewScale_);
    const float sx1 = sx0 + static_cast<float>(visible.w) /
                      std::max(0.0001f, viewScale_);
    const float sy1 = sy0 + static_cast<float>(visible.h) /
                      std::max(0.0001f, viewScale_);

    int32_t srcX = std::max(0, static_cast<int32_t>(std::floor(sx0)));
    int32_t srcY = std::max(0, static_cast<int32_t>(std::floor(sy0)));
    int32_t srcW = std::max(1, static_cast<int32_t>(std::ceil(sx1)) - srcX);
    int32_t srcH = std::max(1, static_cast<int32_t>(std::ceil(sy1)) - srcY);
    srcW = std::min(srcW, kStageWidth - srcX);
    srcH = std::min(srcH, kStageHeight - srcY);

    canvas().updateShape(inkShapeId_, [&](Shape& shape) {
        if (auto* image = std::get_if<ImageShape>(&shape.data)) {
            image->handle = handle;
            image->x = visible.x;
            image->y = visible.y;
            image->w = visible.w;
            image->h = visible.h;
            image->srcX = srcX;
            image->srcY = srcY;
            image->srcW = srcW;
            image->srcH = srcH;
        }
        shape.base.visible = true;
    });
}

void CameraProView::updateCameraShape() {
    if (cameraShapeId_ == 0) return;

    if (!frameReady_ || cameraTexture_ == 0 || frameWidth_ <= 0 ||
        frameHeight_ <= 0) {
        canvas().updateShape(cameraShapeId_, [](Shape& shape) {
            shape.base.visible = false;
        });
        return;
    }

    const float camScreenX = originX_ + cameraStageX_ * viewScale_;
    const float camScreenY = originY_ + cameraStageY_ * viewScale_;
    const float camScreenW = cameraStageW_ * viewScale_;
    const float camScreenH = cameraStageH_ * viewScale_;

    const Rect camScreen{
        static_cast<int32_t>(std::lround(camScreenX)),
        static_cast<int32_t>(std::lround(camScreenY)),
        std::max(1, static_cast<int32_t>(std::lround(camScreenW))),
        std::max(1, static_cast<int32_t>(std::lround(camScreenH)))};
    const Rect visible = camScreen.intersect(
        Rect{0, 0, std::max(1, canvas().width()), std::max(1, canvas().height())});
    if (visible.isNull()) {
        canvas().updateShape(cameraShapeId_, [](Shape& shape) {
            shape.base.visible = false;
        });
        return;
    }

    const float pixelsPerScreenX =
        static_cast<float>(frameWidth_) / std::max(1.0f, camScreenW);
    const float pixelsPerScreenY =
        static_cast<float>(frameHeight_) / std::max(1.0f, camScreenH);

    const float sx0 = (static_cast<float>(visible.x) - camScreenX) * pixelsPerScreenX;
    const float sy0 = (static_cast<float>(visible.y) - camScreenY) * pixelsPerScreenY;
    const float sx1 = sx0 + static_cast<float>(visible.w) * pixelsPerScreenX;
    const float sy1 = sy0 + static_cast<float>(visible.h) * pixelsPerScreenY;

    int32_t srcX = std::max(0, static_cast<int32_t>(std::floor(sx0)));
    int32_t srcY = std::max(0, static_cast<int32_t>(std::floor(sy0)));
    int32_t srcW = std::max(1, static_cast<int32_t>(std::ceil(sx1)) - srcX);
    int32_t srcH = std::max(1, static_cast<int32_t>(std::ceil(sy1)) - srcY);
    srcW = std::min(srcW, frameWidth_ - srcX);
    srcH = std::min(srcH, frameHeight_ - srcY);

    canvas().updateShape(cameraShapeId_, [&](Shape& shape) {
        if (auto* image = std::get_if<ImageShape>(&shape.data)) {
            image->handle = cameraTexture_;
            image->x = visible.x;
            image->y = visible.y;
            image->w = visible.w;
            image->h = visible.h;
            image->srcX = srcX;
            image->srcY = srcY;
            image->srcW = srcW;
            image->srcH = srcH;
        }
        shape.base.visible = true;
    });
}

void CameraProView::updateCursorShape() {
    if (cursorShapeId_ == 0) return;

    const Vec2i point{static_cast<int32_t>(std::lround(cursorPoint_.x)),
                      static_cast<int32_t>(std::lround(cursorPoint_.y))};

    const bool brushTool =
        (tool_ == Tool::Pen || tool_ == Tool::Highlighter || tool_ == Tool::Eraser);
    const bool show = cursorInCanvas_ && brushTool && !ocrSelecting_;
    const bool overToolbar = pointOverToolbar(point);

    const float radius = (tool_ == Tool::Eraser)
        ? std::max(8.0f, width_ * 2.2f) * viewScale_
        : (engine_.currentBaseWidth() * 0.5f * viewScale_ + 2.0f);

    cursorVisible_ = show && !overToolbar;

    canvas().updateShape(cursorShapeId_, [&](Shape& shape) {
        if (auto* circle = std::get_if<CircleShape>(&shape.data)) {
            circle->cx = point.x;
            circle->cy = point.y;
            circle->r = std::max(1.0f, radius);
        }
        shape.base.visible = cursorVisible_;
        shape.base.stroke = Paint((tool_ == Tool::Eraser)
            ? Color{255, 255, 255, 210}
            : Color{96, 205, 255, 200});
        shape.base.strokeWidth = 2;
    });
}

void CameraProView::updateSelectionOverlay() {
    if (selectionShapeIds_[0] == 0) return;

    const bool show = ocrSelecting_ || selecting_;
    const int32_t w = std::max(1, canvas().width());
    const int32_t h = std::max(1, canvas().height());

    const Rect selection = clampScreenRect(selectionRect_);
    if (!show || selection.isNull()) {
        for (uint32_t id : selectionShapeIds_) {
            canvas().updateShape(id, [](Shape& shape) { shape.base.visible = false; });
        }
        return;
    }

    struct Dim { Rect rect; } dims[4] = {
        {Rect{0, 0, w, selection.y}},
        {Rect{0, selection.bottom(), w, h - selection.bottom()}},
        {Rect{0, selection.y, selection.x, selection.h}},
        {Rect{selection.right(), selection.y, w - selection.right(), selection.h}},
    };

    for (size_t i = 0; i < 4; ++i) {
        const Rect rect = dims[i].rect;
        const bool visible = rect.w > 0 && rect.h > 0;
        canvas().updateShape(selectionShapeIds_[i], [&](Shape& shape) {
            shape.data = RectShape{rect.x, rect.y, rect.w, rect.h};
            shape.base.visible = visible;
        });
    }

    canvas().updateShape(selectionShapeIds_[4], [&](Shape& shape) {
        shape.data = RectShape{selection.x, selection.y, selection.w, selection.h};
        shape.base.visible = true;
    });
}

void CameraProView::updatePlaceholder() {
    if (placeholderShapeId_ == 0) return;

    std::string signature;
    if (!frameReady_) {
        signature = cameraError_.empty() ? "正在连接摄像头…" : cameraError_;
    }
    if (signature == placeholderSignature_) return;
    placeholderSignature_ = signature;

    if (signature.empty()) {
        canvas().updateShape(placeholderShapeId_, [](Shape& shape) {
            shape.base.visible = false;
        });
        return;
    }

    const int32_t w = std::max(1, canvas().width());
    const int32_t h = std::max(1, canvas().height());

    const std::string hint =
        "请检查摄像头连接，或在右侧「画面调整」中切换设备";

    TextShape title;
    title.text = signature;
    title.fontSize = 20;
    canvas().measureTextShape(title);
    TextShape hintShape;
    hintShape.text = hint;
    hintShape.fontSize = 11;
    canvas().measureTextShape(hintShape);

    const int32_t titleW = title.measuredBounds.isNull()
        ? static_cast<int32_t>(signature.size()) * 12
        : title.measuredBounds.w;
    const int32_t hintW = hintShape.measuredBounds.isNull()
        ? static_cast<int32_t>(hint.size()) * 7
        : hintShape.measuredBounds.w;
    const int32_t pillW = std::max(520, std::max(titleW, hintW) + 72);
    const int32_t pillH = 108;
    const int32_t x = (w - pillW) / 2;
    const int32_t y = (h - pillH) / 2 - 24;

    GroupData group;
    {
        Shape background;
        background.data = RRectShape{x, y, pillW, pillH, 18.0f, 18.0f};
        background.base.fill = Paint(Color{10, 12, 18, 220});
        background.base.stroke = Paint(Color{96, 205, 255, 52});
        background.base.strokeWidth = 1;
        background.base.z = 0;
        group.children.push_back(std::move(background));
    }
    {
        Shape text;
        TextShape label;
        label.text = signature;
        label.x = x + (pillW - titleW) / 2;
        label.y = y + 48;
        label.fontSize = 20;
        text.data = std::move(label);
        text.base.fill = Paint(Color{238, 242, 250, 255});
        text.base.z = 1;
        group.children.push_back(std::move(text));
    }
    {
        Shape text;
        TextShape label;
        label.text = hint;
        label.x = x + (pillW - hintW) / 2;
        label.y = y + 76;
        label.fontSize = 11;
        text.data = std::move(label);
        text.base.fill = Paint(Color{160, 168, 184, 255});
        text.base.z = 2;
        group.children.push_back(std::move(text));
    }

    canvas().updateShape(placeholderShapeId_, [&](Shape& shape) {
        shape.data = std::move(group);
        shape.base.visible = true;
    });
}

void CameraProView::updateHud() {
    if (hudShapeId_ == 0) return;

    if (!chromeVisible_ || !frameReady_ || frameWidth_ <= 0 || frameHeight_ <= 0) {
        if (!hudSignature_.empty()) {
            hudSignature_.clear();
            canvas().updateShape(hudShapeId_, [](Shape& shape) {
                shape.base.visible = false;
            });
        }
        return;
    }

    std::string cameraName = "摄像头";
    if (!deviceNames_.empty()) {
        const int32_t index = camera_.deviceIndex();
        if (index >= 0 && index < static_cast<int32_t>(deviceNames_.size())) {
            cameraName = utf8Preview(deviceNames_[static_cast<size_t>(index)], 20);
        }
    }

    char info[180];
    std::snprintf(info, sizeof(info),
                  "%dx%d  ·  %d fps  ·  缩放 %d%%%s%s",
                  static_cast<int>(frameWidth_), static_cast<int>(frameHeight_),
                  camera_.targetFps(),
                  static_cast<int>(std::lround(zoom_ * 100.0f)),
                  frozen_ ? "  ·  已冻结" : "",
                  cameraError_.empty() ? "" : "  ·  连接中断");

    const std::string signature = cameraName + "|" + info;
    if (signature == hudSignature_) return;
    hudSignature_ = signature;

    TextShape title;
    title.text = "CameraPro  ·  " + cameraName;
    title.fontSize = 13;
    canvas().measureTextShape(title);
    TextShape sub;
    sub.text = info;
    sub.fontSize = 10;
    canvas().measureTextShape(sub);

    const int32_t titleW = title.measuredBounds.isNull()
        ? 160 : title.measuredBounds.w;
    const int32_t subW = sub.measuredBounds.isNull() ? 160 : sub.measuredBounds.w;
    const int32_t pillW = std::max(220, std::max(titleW, subW) + 32);
    const int32_t pillH = 48;
    const int32_t x = 18;
    const int32_t y = 16;

    GroupData group;
    {
        Shape background;
        background.data = RRectShape{x, y, pillW, pillH, 12.0f, 12.0f};
        background.base.fill = Paint(Color{10, 12, 18, 214});
        background.base.stroke = Paint(Color{255, 255, 255, 20});
        background.base.strokeWidth = 1;
        background.base.z = 0;
        group.children.push_back(std::move(background));
    }
    {
        Shape text;
        TextShape label;
        label.text = "CameraPro  ·  " + cameraName;
        label.x = x + 16;
        label.y = y + 22;
        label.fontSize = 13;
        text.data = std::move(label);
        text.base.fill = Paint(Color{235, 244, 255, 255});
        text.base.z = 1;
        group.children.push_back(std::move(text));
    }
    {
        Shape text;
        TextShape label;
        label.text = info;
        label.x = x + 16;
        label.y = y + 39;
        label.fontSize = 10;
        text.data = std::move(label);
        text.base.fill = Paint(Color{150, 168, 190, 255});
        text.base.z = 2;
        group.children.push_back(std::move(text));
    }

    canvas().updateShape(hudShapeId_, [&](Shape& shape) {
        shape.data = std::move(group);
        shape.base.visible = true;
    });
}

// ============================================================================
// OCR 高亮
// ============================================================================

void CameraProView::rebuildOcrHighlights() {
    for (OcrHighlight& highlight : ocrHighlights_) {
        if (highlight.shapeId != 0) {
            canvas().removeShape(highlight.shapeId);
            highlight.shapeId = 0;
        }
    }

    if (ocrHighlights_.empty()) return;

    int32_t z = -3000;
    for (OcrHighlight& highlight : ocrHighlights_) {
        const Vec2f tl = stageToScreen(highlight.quadStage[0]);
        const Vec2f tr = stageToScreen(highlight.quadStage[1]);
        const Vec2f br = stageToScreen(highlight.quadStage[2]);
        const Vec2f bl = stageToScreen(highlight.quadStage[3]);

        const float lineWidth = (distanceOf(tl, tr) + distanceOf(bl, br)) * 0.5f;
        const float lineHeight = (distanceOf(tl, bl) + distanceOf(tr, br)) * 0.5f;
        if (lineWidth < 2.0f || lineHeight < 2.0f) continue;

        const float centerX = (tl.x + tr.x + br.x + bl.x) * 0.25f;
        const float centerY = (tl.y + tr.y + br.y + bl.y) * 0.25f;
        const float angle = std::atan2(tr.y - tl.y, tr.x - tl.x) *
                            (180.0f / 3.14159265358979323846f);

        const float coreW = lineWidth + 6.0f;
        const float coreH = lineHeight + 4.0f;
        const float haloW = lineWidth + 14.0f;
        const float haloH = lineHeight + 12.0f;
        const float radius = std::max(2.0f, coreH * 0.42f);

        GroupData group;

        Shape halo;
        halo.data = RRectShape{
            static_cast<int32_t>(std::lround(centerX - haloW * 0.5f)),
            static_cast<int32_t>(std::lround(centerY - haloH * 0.5f)),
            static_cast<int32_t>(std::lround(haloW)),
            static_cast<int32_t>(std::lround(haloH)),
            radius + 3.0f, radius + 3.0f};
        halo.base.fill = Paint(Color{255, 236, 140, 38});
        halo.base.z = 0;
        halo.base.rotation = angle;
        group.children.push_back(std::move(halo));

        Shape core;
        LinearGradient gradient;
        gradient.from = Vec2f{centerX, centerY - coreH * 0.5f};
        gradient.to = Vec2f{centerX, centerY + coreH * 0.5f};
        gradient.stops.push_back({0.0f, Color{255, 249, 198, 66}});
        gradient.stops.push_back({0.55f, Color{255, 234, 112, 120}});
        gradient.stops.push_back({1.0f, Color{255, 214, 64, 92}});
        core.data = RRectShape{
            static_cast<int32_t>(std::lround(centerX - coreW * 0.5f)),
            static_cast<int32_t>(std::lround(centerY - coreH * 0.5f)),
            static_cast<int32_t>(std::lround(coreW)),
            static_cast<int32_t>(std::lround(coreH)),
            radius, radius};
        core.base.fill = Paint(std::move(gradient));
        core.base.stroke = Paint(Color{255, 255, 240, 64});
        core.base.strokeWidth = 1;
        core.base.z = 1;
        core.base.rotation = angle;
        group.children.push_back(std::move(core));

        float opacity = 1.0f;
        if (!highlight.settled && highlight.age > highlight.delay) {
            const float t = clampf((highlight.age - highlight.delay) / 0.26f,
                                   0.0f, 1.0f);
            const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
            opacity = eased;
            highlight.lastOpacity = eased;
        }

        Shape shape;
        shape.data = std::move(group);
        shape.base.z = z++;
        shape.base.opacity = opacity;
        shape.base.visible = true;
        highlight.shapeId = canvas().addShape(std::move(shape));
    }
}

void CameraProView::clearOcrHighlights() {
    for (OcrHighlight& highlight : ocrHighlights_) {
        if (highlight.shapeId != 0) canvas().removeShape(highlight.shapeId);
    }
    ocrHighlights_.clear();
    ocrRequestId_ = 0;
}

// ============================================================================
// 帧驱动
// ============================================================================

void CameraProView::onTick(float dtSeconds) {
    if (!initialized_) return;

    retryTimer_ -= dtSeconds;
    deviceListTimer_ -= dtSeconds;

    if (lastCanvasWidth_ != canvas().width() ||
        lastCanvasHeight_ != canvas().height()) {
        lastCanvasWidth_ = canvas().width();
        lastCanvasHeight_ = canvas().height();
        if (toolbar_) toolbar_->setScreenSize(canvas().width(), canvas().height());
        placeholderSignature_.clear();
        hudSignature_.clear();
        updateViewTransform();
    }

    if (toolbar_) toolbar_->setScreenSize(canvas().width(), canvas().height());

    pullCameraFrame();
    handleCameraState();

    updateLaser(dtSeconds);
    updateToast(dtSeconds);
    updateOcrHighlightAnimation(dtSeconds);

    OcrResult ocrResult;
    if (ocr_.poll(&ocrResult) && ocrResult.requestId == ocrRequestId_) {
        applyOcrResult(ocrResult);
    }

    if (selectionDirty_) {
        updateSelectionOverlay();
        selectionDirty_ = false;
    }

    if (cursorDirty_) {
        updateCursorShape();
        cursorDirty_ = false;
    }

    syncToolbar();
}

// ============================================================================
// 输入
// ============================================================================

bool CameraProView::handleAppEvent(const Event& event) {
    if (event.type == EventType::Hotkey) {
        handleHotkey(event.hotkeyId);
        return true;
    }

    if (!initialized_) return false;

    if (event.type == EventType::MouseLeave) {
        cursorInCanvas_ = false;
        cursorDirty_ = true;
        return false;
    }

    if (!event.isMouseEvent()) return false;

    const Vec2f point{static_cast<float>(event.x), static_cast<float>(event.y)};
    const Vec2i integerPoint{event.x, event.y};
    const bool insideCanvas = (integerPoint.x >= 0 && integerPoint.y >= 0 &&
                               integerPoint.x < canvas().width() &&
                               integerPoint.y < canvas().height());

    cursorPoint_ = point;
    cursorInCanvas_ = insideCanvas;
    cursorDirty_ = true;

    const bool overToolbar = pointOverToolbar(integerPoint);

    // 触控走独立路径：单指绘制 / 单指拖动，双指缩放 + 平移。
    if (event.pointerType == PointerType::Touch) {
        return handleTouchEvent(event, point, overToolbar);
    }

    if (event.type == EventType::MouseWheel) {
        if (overToolbar) return false;   // 交给工具栏（笔宽滚轮）
        if (drawing_ || erasing_) return true;
        zoomAt(std::pow(1.16f, event.wheelDelta), point);
        return true;
    }

    if (event.type == EventType::MouseDown &&
        event.button == modern_ui::MouseButtonMiddle) {
        if (overToolbar) return false;
        cancelActiveInput();
        panning_ = true;
        lastPanPoint_ = point;
        pendingPanDelta_ = Vec2f{0.0f, 0.0f};
        lastPanSampleUs_ = 0;
        return true;
    }

    if (event.type == EventType::MouseDown) {
        if (event.button != modern_ui::MouseButtonLeft) return false;
        if (overToolbar) return false;
        beginPointerInteraction(point, event);
        return true;
    }

    if (event.type == EventType::MouseMove) {
        if (panning_ || drawing_ || erasing_ || laserActive_ || ocrSelecting_) {
            movePointerInteraction(point, event);
            return true;
        }
        return false;
    }

    if (event.type == EventType::MouseUp) {
        if (event.button == modern_ui::MouseButtonMiddle && panning_) {
            flushPanDelta();
            panning_ = false;
            return true;
        }
        if (event.button == modern_ui::MouseButtonLeft &&
            (panning_ || drawing_ || erasing_ || laserActive_ || ocrSelecting_)) {
            endPointerInteraction(point, event);
            return true;
        }
    }

    return false;
}

// ----------------------------------------------------------------------------
// 统一指针交互（鼠标 / 笔 / 单指触控）
// ----------------------------------------------------------------------------

void CameraProView::beginPointerInteraction(const Vec2f& screenPoint,
                                            const Event& event) {
    if (tool_ == Tool::Ocr) {
        beginOcrSelection(screenPoint);
        return;
    }

    if (tool_ == Tool::Pan) {
        // 双击适应屏幕（触控双指手势优先，不做双击判定）
        if (event.pointerType != PointerType::Touch) {
            const Vec2i integerPoint{static_cast<int32_t>(std::lround(screenPoint.x)),
                                     static_cast<int32_t>(std::lround(screenPoint.y))};
            const uint64_t now = GetTickCount64();
            const int32_t dx = integerPoint.x - lastClickPoint_.x;
            const int32_t dy = integerPoint.y - lastClickPoint_.y;
            if (now - lastClickTick_ < 400 && dx * dx + dy * dy < 36) {
                lastClickTick_ = 0;
                fitView();
                return;
            }
            lastClickTick_ = now;
            lastClickPoint_ = integerPoint;
        }

        panning_ = true;
        lastPanPoint_ = screenPoint;
        pendingPanDelta_ = Vec2f{0.0f, 0.0f};
        lastPanSampleUs_ = 0;
        return;
    }

    switch (tool_) {
        case Tool::Pen:
        case Tool::Highlighter:
            beginStrokeAt(screenPoint, eventTimeSeconds(event));
            break;
        case Tool::Eraser:
            beginEraseAt(screenPoint);
            break;
        case Tool::Laser:
            laserActive_ = true;
            addLaserPoint(screenPoint);
            lastLaserSampleUs_ = 0;
            break;
        default:
            break;
    }
}

void CameraProView::movePointerInteraction(const Vec2f& screenPoint,
                                           const Event& event) {
    const uint64_t now = monotonicMicros();

    if (panning_) {
        pendingPanDelta_.x += screenPoint.x - lastPanPoint_.x;
        pendingPanDelta_.y += screenPoint.y - lastPanPoint_.y;
        lastPanPoint_ = screenPoint;
        if (lastPanSampleUs_ == 0 ||
            now - lastPanSampleUs_ >= kPanSampleIntervalUs) {
            flushPanDelta();
            lastPanSampleUs_ = now;
        }
        return;
    }

    if (ocrSelecting_) {
        updateOcrSelection(screenPoint);
        return;
    }

    if (drawing_) {
        const Vec2f stagePoint = screenToStage(screenPoint);
        const double timeSeconds = eventTimeSeconds(event);
        const uint64_t interval =
            (event.pointerType == PointerType::Touch)
                ? kTouchSampleIntervalUs : kMouseSampleIntervalUs;

        if (lastStrokeSampleUs_ == 0 ||
            now - lastStrokeSampleUs_ >= interval) {
            engine_.addPoint(stagePoint, timeSeconds);
            lastStrokeSampleUs_ = now;
            hasPendingStrokeSample_ = false;
            appendLivePolyline(screenPoint);
        } else {
            pendingStrokeStage_ = stagePoint;
            pendingStrokeScreen_ = screenPoint;
            pendingStrokeTime_ = timeSeconds;
            hasPendingStrokeSample_ = true;
        }
        return;
    }

    if (erasing_) {
        const Vec2f stagePoint = screenToStage(screenPoint);
        const uint64_t interval =
            (event.pointerType == PointerType::Touch)
                ? kTouchSampleIntervalUs : kEraseSampleIntervalUs;
        if (lastEraseSampleUs_ == 0 ||
            now - lastEraseSampleUs_ >= interval) {
            applyEraseSample(stagePoint);
            lastEraseSampleUs_ = now;
            hasPendingEraseSample_ = false;
        } else {
            pendingEraseStage_ = stagePoint;
            hasPendingEraseSample_ = true;
        }
        return;
    }

    if (laserActive_) {
        if (lastLaserSampleUs_ == 0 ||
            now - lastLaserSampleUs_ >= kLaserSampleIntervalUs) {
            addLaserPoint(screenPoint);
            lastLaserSampleUs_ = now;
        }
        return;
    }
}

void CameraProView::endPointerInteraction(const Vec2f& screenPoint,
                                          const Event& event) {
    (void)event;

    if (ocrSelecting_) {
        finishOcrSelection(screenPoint);
        return;
    }
    if (panning_) {
        flushPanDelta();
        panning_ = false;
        return;
    }
    if (drawing_) {
        flushStrokeSample();
        endStroke();
        return;
    }
    if (erasing_) {
        flushEraseSample();
        endErase();
        return;
    }
    if (laserActive_) {
        addLaserPoint(screenPoint);
        laserActive_ = false;
        return;
    }
}

// ----------------------------------------------------------------------------
// 触控：单指绘制 / 拖动，双指缩放 + 平移
// ----------------------------------------------------------------------------

bool CameraProView::addTouch(int32_t id, const Vec2f& point) {
    for (TouchSlot& slot : touchSlots_) {
        if (slot.active) continue;
        slot.id = id;
        slot.point = point;
        slot.active = true;
        ++activeTouchCount_;
        return true;
    }
    return false;
}

bool CameraProView::updateTouch(int32_t id, const Vec2f& point) {
    for (TouchSlot& slot : touchSlots_) {
        if (slot.active && slot.id == id) {
            slot.point = point;
            return true;
        }
    }
    return false;
}

bool CameraProView::removeTouch(int32_t id) {
    for (TouchSlot& slot : touchSlots_) {
        if (slot.active && slot.id == id) {
            slot.active = false;
            slot.id = -1;
            activeTouchCount_ = std::max(0, activeTouchCount_ - 1);
            return true;
        }
    }
    return false;
}

void CameraProView::beginTouchGesture() {
    cancelActiveInput();
    if (ocrSelecting_) {
        ocrSelecting_ = false;
        selecting_ = false;
        selectionRect_ = Rect::null();
        selectionDirty_ = true;
    }

    Vec2f a{0.0f, 0.0f};
    Vec2f b{0.0f, 0.0f};
    int32_t found = 0;
    for (const TouchSlot& slot : touchSlots_) {
        if (!slot.active) continue;
        if (found == 0) a = slot.point;
        else if (found == 1) b = slot.point;
        ++found;
    }
    if (found < 2) return;

    gestureAnchorStage_ = screenToStage(Vec2f{
        (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f});
    gestureStartDistance_ = std::max(1.0f, distanceOf(a, b));
    gestureStartZoom_ = zoom_;
    touchGesture_ = true;
    touchGestureLocked_ = true;
    lastGestureSampleUs_ = 0;
    showToast("双指缩放 / 平移画面", 1.4f);
}

void CameraProView::updateTouchGesture() {
    if (activeTouchCount_ < 2) return;

    const uint64_t now = monotonicMicros();
    if (lastGestureSampleUs_ != 0 &&
        now - lastGestureSampleUs_ < kGestureSampleIntervalUs) {
        return;
    }
    lastGestureSampleUs_ = now;

    Vec2f a{0.0f, 0.0f};
    Vec2f b{0.0f, 0.0f};
    int32_t found = 0;
    for (const TouchSlot& slot : touchSlots_) {
        if (!slot.active) continue;
        if (found == 0) a = slot.point;
        else if (found == 1) b = slot.point;
        ++found;
    }
    if (found < 2) return;

    const Vec2f center{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    const float distance = distanceOf(a, b);
    const float targetZoom = clampf(
        gestureStartZoom_ * (distance / gestureStartDistance_), 0.5f, 8.0f);
    if (std::fabs(targetZoom - zoom_) < 0.002f &&
        std::fabs(center.x - lastPanPoint_.x) < 0.5f &&
        std::fabs(center.y - lastPanPoint_.y) < 0.5f) {
        return;
    }

    zoom_ = targetZoom;
    const float scale = fitScale() * zoom_;
    const float baseX = (static_cast<float>(canvas().width()) -
                         static_cast<float>(kStageWidth) * scale) * 0.5f;
    const float baseY = (static_cast<float>(canvas().height()) -
                         static_cast<float>(kStageHeight) * scale) * 0.5f;
    panX_ = center.x - gestureAnchorStage_.x * scale - baseX;
    panY_ = center.y - gestureAnchorStage_.y * scale - baseY;
    lastPanPoint_ = center;

    updateViewTransform();
    syncToolbar();
}

bool CameraProView::handleTouchEvent(const Event& event, const Vec2f& point,
                                     bool overToolbar) {
    const bool isDown = (event.type == EventType::MouseDown &&
                         event.button == modern_ui::MouseButtonLeft);
    const bool isUp = (event.type == EventType::MouseUp &&
                       event.button == modern_ui::MouseButtonLeft);

    if (isDown) {
        // 触摸工具栏交给控件系统处理；画布上的触摸才进入笔迹 / 手势。
        if (overToolbar) return false;
        if (touchGesture_ || touchGestureLocked_) return true;

        if (activeTouchCount_ == 0) {
            if (!addTouch(event.pointerId, point)) return true;
            beginPointerInteraction(point, event);
            return true;
        }

        if (activeTouchCount_ == 1) {
            if (!addTouch(event.pointerId, point)) return true;
            beginTouchGesture();
            return true;
        }

        return true;
    }

    if (event.type == EventType::MouseMove) {
        if (!updateTouch(event.pointerId, point)) {
            return activeTouchCount_ > 0 || touchGesture_ || touchGestureLocked_;
        }
        if (touchGesture_) {
            updateTouchGesture();
            return true;
        }
        if (touchGestureLocked_) return true;

        if (activeTouchCount_ == 1) {
            movePointerInteraction(point, event);
            return true;
        }
        return true;
    }

    if (isUp) {
        if (!removeTouch(event.pointerId)) {
            return activeTouchCount_ > 0 || touchGesture_ || touchGestureLocked_;
        }

        if (touchGesture_) {
            // 手势中抬起一指：停止缩放，剩余手指不画线，直到全部离开。
            touchGesture_ = false;
            touchGestureLocked_ = true;
        } else if (!touchGestureLocked_ && activeTouchCount_ == 0) {
            endPointerInteraction(point, event);
        }

        if (activeTouchCount_ == 0) {
            touchGesture_ = false;
            touchGestureLocked_ = false;
        }
        return true;
    }

    return false;
}

void CameraProView::handleHotkey(int id) {
    switch (id) {
        case HotkeyToggleChrome: toggleChrome(); break;
        case HotkeyFreeze: setFrozen(!frozen_); break;
        case HotkeyUndo: {
            if (drawing_ || erasing_) return;
            Rect dirty;
            if (engine_.undo(&dirty) && !dirty.isNull()) {
                const Rect screenDirty = clampScreenRect(Rect{
                    static_cast<int32_t>(std::floor(originX_ +
                        static_cast<float>(dirty.x) * viewScale_)) - 2,
                    static_cast<int32_t>(std::floor(originY_ +
                        static_cast<float>(dirty.y) * viewScale_)) - 2,
                    static_cast<int32_t>(std::ceil(
                        static_cast<float>(dirty.w) * viewScale_)) + 4,
                    static_cast<int32_t>(std::ceil(
                        static_cast<float>(dirty.h) * viewScale_)) + 4});
                if (!screenDirty.isNull()) canvas().markDirty(screenDirty);
            }
            syncToolbar();
            break;
        }
        case HotkeyCopyOcr: copyAllOcrText(); break;
        case HotkeyFit: fitView(); break;
        case HotkeySnapshot: takeSnapshot(); break;
        case HotkeyZoomIn:
            if (!drawing_ && !erasing_) {
                zoomAt(1.25f, Vec2f{static_cast<float>(canvas().width()) * 0.5f,
                                    static_cast<float>(canvas().height()) * 0.5f});
            }
            break;
        case HotkeyZoomOut:
            if (!drawing_ && !erasing_) {
                zoomAt(0.8f, Vec2f{static_cast<float>(canvas().width()) * 0.5f,
                                   static_cast<float>(canvas().height()) * 0.5f});
            }
            break;
        case HotkeyPrevPage:
            if (engine_.pageIndex() > 0) {
                cancelActiveInput();
                if (engine_.setPage(engine_.pageIndex() - 1)) {
                    updateInkShape();
                    canvas().markDirtyAll();
                    syncToolbar();
                }
            }
            break;
        case HotkeyNextPage:
            if (engine_.pageIndex() + 1 < engine_.pageCount()) {
                cancelActiveInput();
                if (engine_.setPage(engine_.pageIndex() + 1)) {
                    updateInkShape();
                    canvas().markDirtyAll();
                    syncToolbar();
                }
            }
            break;
        case HotkeyQuit:
            if (onQuitRequested) onQuitRequested();
            break;
        default:
            break;
    }
}

void CameraProView::setTool(Tool tool) {
    if (tool == tool_) {
        if (toolbar_) toolbar_->setTool(tool_);
        return;
    }

    cancelActiveInput();
    if (ocrSelecting_) {
        ocrSelecting_ = false;
        selectionRect_ = Rect::null();
        selectionDirty_ = true;
    }

    tool_ = tool;
    if (toolbar_) toolbar_->setTool(tool_);
    applyStyle();
    cursorDirty_ = true;
}

void CameraProView::toggleChrome() {
    chromeVisible_ = !chromeVisible_;
    if (toolbar_) toolbar_->setChromeVisible(chromeVisible_);
    hudSignature_.clear();
    updateHud();
    showToast(chromeVisible_ ? "工具栏已显示" : "工具栏已隐藏 · F9 恢复", 1.6f);
}

bool CameraProView::pointOverToolbar(const Vec2i& point) const {
    if (!toolbar_) return false;
    return toolbar_->absoluteFrame().contains(point);
}

Rect CameraProView::clampScreenRect(const Rect& rect) const {
    return rect.intersect(Rect{0, 0, std::max(1, canvas().width()),
                               std::max(1, canvas().height())});
}

double CameraProView::eventTimeSeconds(const Event& event) const {
    // 只有 Hook 输入的时间戳可靠；窗口 / 触控事件改用高精度计时。
    if (event.source == modern_ui::InputSource::Hook && event.timestampMs != 0) {
        return static_cast<double>(event.timestampMs) / 1000.0;
    }
    return static_cast<double>(monotonicMicros()) / 1000000.0;
}

// ============================================================================
// 笔迹 / 橡皮 / 激光
// ============================================================================

void CameraProView::beginStrokeAt(const Vec2f& screenPoint, double timeSeconds) {
    if (!engine_.ready()) return;
    const Vec2f stagePoint = screenToStage(screenPoint);
    if (!stagePointInside(stagePoint)) return;

    drawing_ = true;
    hasPendingStrokeSample_ = false;
    lastStrokeSampleUs_ = 0;
    engine_.beginStroke(stagePoint, timeSeconds);
    beginLivePolyline(screenPoint);
}

void CameraProView::endStroke() {
    if (!drawing_) return;
    drawing_ = false;

    Rect dirty;
    if (engine_.endStroke(&dirty)) markStageRectDirty(dirty);
    hasPendingStrokeSample_ = false;
    lastStrokeSampleUs_ = 0;
    clearLivePolyline();
    syncToolbar();
}

void CameraProView::beginEraseAt(const Vec2f& screenPoint) {
    cancelActiveInput();
    const Vec2f stagePoint = screenToStage(screenPoint);
    if (!stagePointInside(stagePoint)) return;

    erasing_ = true;
    hasPendingEraseSample_ = false;
    lastEraseSampleUs_ = 0;

    Rect dirty;
    engine_.beginErase(stagePoint, std::max(8.0f, width_ * 2.2f), &dirty);
    markStageRectDirty(dirty);
    syncToolbar();
}

void CameraProView::endErase() {
    if (!erasing_) return;
    erasing_ = false;
    hasPendingEraseSample_ = false;
    lastEraseSampleUs_ = 0;
    engine_.endErase();
    syncToolbar();
}

// 把累计的最后一个采样点补进引擎，避免快速抬笔丢失尾巴。
void CameraProView::flushStrokeSample() {
    if (!hasPendingStrokeSample_) return;
    engine_.addPoint(pendingStrokeStage_, pendingStrokeTime_);
    appendLivePolyline(pendingStrokeScreen_);
    hasPendingStrokeSample_ = false;
}

void CameraProView::flushEraseSample() {
    if (!hasPendingEraseSample_) return;
    applyEraseSample(pendingEraseStage_);
    hasPendingEraseSample_ = false;
}

void CameraProView::flushPanDelta() {
    if (std::fabs(pendingPanDelta_.x) < 0.01f &&
        std::fabs(pendingPanDelta_.y) < 0.01f) {
        pendingPanDelta_ = Vec2f{0.0f, 0.0f};
        return;
    }
    panBy(pendingPanDelta_);
    pendingPanDelta_ = Vec2f{0.0f, 0.0f};
}

void CameraProView::applyEraseSample(const Vec2f& stagePoint) {
    Rect dirty;
    engine_.eraseTo(stagePoint, std::max(8.0f, width_ * 2.2f), &dirty);
    markStageRectDirty(dirty);
}

void CameraProView::markStageRectDirty(const Rect& stageRect) {
    if (stageRect.isNull()) return;
    const int32_t x = static_cast<int32_t>(std::floor(
        originX_ + static_cast<float>(stageRect.x) * viewScale_)) - 2;
    const int32_t y = static_cast<int32_t>(std::floor(
        originY_ + static_cast<float>(stageRect.y) * viewScale_)) - 2;
    const int32_t w = static_cast<int32_t>(std::ceil(
        static_cast<float>(stageRect.w) * viewScale_)) + 4;
    const int32_t h = static_cast<int32_t>(std::ceil(
        static_cast<float>(stageRect.h) * viewScale_)) + 4;
    canvas().markDirty(clampScreenRect(Rect{x, y, w, h}));
}

void CameraProView::addLaserPoint(const Vec2f& screenPoint) {
    laserPoints_.push_back(LaserPoint{screenPoint, 0.0f});
    if (laserPoints_.size() > laserShapeIds_.size()) {
        laserPoints_.erase(laserPoints_.begin());
    }
}

void CameraProView::beginLivePolyline(const Vec2f& screenPoint) {
    clearLivePolyline();

    liveStrokeColor_ = color_;
    if (tool_ == Tool::Highlighter) {
        liveStrokeColor_.a = 90;
        liveStrokeWidth_ = width_ * 2.4f * viewScale_;
    } else {
        liveStrokeColor_.a = 255;
        liveStrokeWidth_ = width_ * viewScale_;
    }

    livePolylinePoints_.clear();
    livePolylinePoints_.push_back(screenPoint);
}

void CameraProView::appendLivePolyline(const Vec2f& screenPoint) {
    if (livePolylinePoints_.empty()) {
        livePolylinePoints_.push_back(screenPoint);
        return;
    }

    const Vec2f& previous = livePolylinePoints_.back();
    const float dx = screenPoint.x - previous.x;
    const float dy = screenPoint.y - previous.y;
    if (dx * dx + dy * dy < 4.0f) return;

    livePolylinePoints_.push_back(screenPoint);

    PolylineShape polyline;
    polyline.closed = false;
    polyline.points.reserve(livePolylinePoints_.size());
    for (const Vec2f& item : livePolylinePoints_) {
        polyline.points.push_back(Vec2i{
            static_cast<int32_t>(std::lround(item.x)),
            static_cast<int32_t>(std::lround(item.y))});
    }

    const int32_t strokeWidth =
        std::max(1, static_cast<int32_t>(std::lround(liveStrokeWidth_)));

    if (!liveChunkHasShape_) {
        Shape shape;
        shape.data = std::move(polyline);
        shape.base.stroke = Paint(liveStrokeColor_);
        shape.base.strokeWidth = strokeWidth;
        shape.base.z = -9400 + static_cast<int32_t>(livePolylineIds_.size());
        livePolylineIds_.push_back(canvas().addShape(std::move(shape)));
        liveChunkHasShape_ = true;
    } else {
        const uint32_t id = livePolylineIds_.back();
        canvas().updateShape(id, [&](Shape& shape) {
            shape.data = std::move(polyline);
            shape.base.stroke = Paint(liveStrokeColor_);
            shape.base.strokeWidth = strokeWidth;
        });
    }

    if (livePolylinePoints_.size() >= 32) {
        const Vec2f tail = livePolylinePoints_.back();
        livePolylinePoints_.assign(1, tail);
        liveChunkHasShape_ = false;
    }
}

void CameraProView::clearLivePolyline() {
    for (uint32_t id : livePolylineIds_) {
        canvas().removeShape(id);
    }
    livePolylineIds_.clear();
    livePolylinePoints_.clear();
    liveChunkHasShape_ = false;
}

void CameraProView::cancelActiveInput() {
    if (drawing_) {
        engine_.cancelStroke();
        drawing_ = false;
    }
    if (erasing_) {
        engine_.endErase();
        erasing_ = false;
    }
    panning_ = false;
    clearLivePolyline();
    laserActive_ = false;

    hasPendingStrokeSample_ = false;
    hasPendingEraseSample_ = false;
    pendingPanDelta_ = Vec2f{0.0f, 0.0f};
    lastStrokeSampleUs_ = 0;
    lastEraseSampleUs_ = 0;
    lastLaserSampleUs_ = 0;
    lastPanSampleUs_ = 0;
}

// ============================================================================
// OCR
// ============================================================================

void CameraProView::beginOcrSelection(const Vec2f& screenPoint) {
    cancelActiveInput();
    ocrSelecting_ = true;
    selecting_ = true;
    selectionStart_ = screenPoint;
    selectionRect_ = Rect::null();
    selectionDirty_ = true;
}

void CameraProView::updateOcrSelection(const Vec2f& screenPoint) {
    cursorPoint_ = screenPoint;
    cursorDirty_ = true;
    Rect next = normalizedRect(selectionStart_, screenPoint);
    next = clampScreenRect(next);
    if (!(next == selectionRect_)) {
        selectionRect_ = next;
        selectionDirty_ = true;
    }
}

void CameraProView::finishOcrSelection(const Vec2f& screenPoint) {
    if (!ocrSelecting_) return;
    ocrSelecting_ = false;
    selecting_ = false;

    Rect region = clampScreenRect(selectionRect_);
    selectionRect_ = Rect::null();
    selectionDirty_ = true;

    if (region.w < 4 || region.h < 4) {
        clickOcrHighlight(screenPoint);
        return;
    }

    takeOcr(region);
}

void CameraProView::takeOcr(const Rect& screenRegion) {
    if (!frameReady_ || processedBgra_.empty()) {
        showToast("摄像头尚未就绪", 2.0f);
        return;
    }
    if (!ocr_.initialized()) {
        showToast("OCR 引擎未初始化", 2.2f);
        return;
    }

    // 屏幕选区 → 舞台 → 摄像头像素。
    const Vec2f tl = screenToStage(Vec2f{
        static_cast<float>(screenRegion.x),
        static_cast<float>(screenRegion.y)});
    const Vec2f br = screenToStage(Vec2f{
        static_cast<float>(screenRegion.x + screenRegion.w),
        static_cast<float>(screenRegion.y + screenRegion.h)});

    const float stageX0 = clampf(std::min(tl.x, br.x),
                                 cameraStageX_, cameraStageX_ + cameraStageW_);
    const float stageY0 = clampf(std::min(tl.y, br.y),
                                 cameraStageY_, cameraStageY_ + cameraStageH_);
    const float stageX1 = clampf(std::max(tl.x, br.x),
                                 cameraStageX_, cameraStageX_ + cameraStageW_);
    const float stageY1 = clampf(std::max(tl.y, br.y),
                                 cameraStageY_, cameraStageY_ + cameraStageH_);

    if (stageX1 - stageX0 < 1.0f || stageY1 - stageY0 < 1.0f) {
        showToast("选区不在摄像头画面内", 2.0f);
        return;
    }

    const float pixelsPerStageX =
        static_cast<float>(frameWidth_) / std::max(1.0f, cameraStageW_);
    const float pixelsPerStageY =
        static_cast<float>(frameHeight_) / std::max(1.0f, cameraStageH_);

    const int32_t x0 = std::max(0, static_cast<int32_t>(std::floor(
        (stageX0 - cameraStageX_) * pixelsPerStageX)));
    const int32_t y0 = std::max(0, static_cast<int32_t>(std::floor(
        (stageY0 - cameraStageY_) * pixelsPerStageY)));
    const int32_t x1 = std::min(frameWidth_, static_cast<int32_t>(std::ceil(
        (stageX1 - cameraStageX_) * pixelsPerStageX)));
    const int32_t y1 = std::min(frameHeight_, static_cast<int32_t>(std::ceil(
        (stageY1 - cameraStageY_) * pixelsPerStageY)));

    const int32_t cropW = x1 - x0;
    const int32_t cropH = y1 - y0;
    if (cropW < 4 || cropH < 4) {
        showToast("选区太小，请重新框选", 2.0f);
        return;
    }

    std::vector<uint8_t> rgb(
        static_cast<size_t>(cropW) * cropH * 3u);
    for (int32_t y = 0; y < cropH; ++y) {
        const uint8_t* srcRow = processedBgra_.data() +
            static_cast<size_t>(y0 + y) * frameWidth_ * 4u;
        uint8_t* dstRow = rgb.data() + static_cast<size_t>(y) * cropW * 3u;
        for (int32_t x = 0; x < cropW; ++x) {
            const uint8_t* pixel = srcRow +
                static_cast<size_t>(x0 + x) * 4u;
            dstRow[static_cast<size_t>(x) * 3u + 0u] = pixel[2];
            dstRow[static_cast<size_t>(x) * 3u + 1u] = pixel[1];
            dstRow[static_cast<size_t>(x) * 3u + 2u] = pixel[0];
        }
    }

    ocrCropX_ = x0;
    ocrCropY_ = y0;
    ocrCropW_ = cropW;
    ocrCropH_ = cropH;

    ocrRequestId_ = ocr_.recognize(0, 0, cropW, cropH, std::move(rgb));
    if (ocrRequestId_ == 0) {
        showToast("OCR 任务提交失败", 2.0f);
        return;
    }
    showToast("正在识别画面文字…", 6.0f);
}

void CameraProView::applyOcrResult(const OcrResult& result) {
    if (!result.ok) {
        showToast("识别失败：" + result.error, 3.0f);
        return;
    }

    clearOcrHighlights();

    if (result.lines.empty()) {
        showToast("未识别到文字", 2.2f);
        return;
    }

    const float scaleX = cameraStageW_ /
        static_cast<float>(std::max(1, frameWidth_));
    const float scaleY = cameraStageH_ /
        static_cast<float>(std::max(1, frameHeight_));

    for (const OcrLine& line : result.lines) {
        OcrHighlight highlight;
        highlight.text = line.text;

        Vec2f minPoint{std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max()};
        Vec2f maxPoint{-std::numeric_limits<float>::max(),
                       -std::numeric_limits<float>::max()};

        for (int32_t corner = 0; corner < 4; ++corner) {
            const Vec2f& source = line.quad[static_cast<size_t>(corner)];
            const Vec2f stage{
                cameraStageX_ + (static_cast<float>(ocrCropX_) + source.x) * scaleX,
                cameraStageY_ + (static_cast<float>(ocrCropY_) + source.y) * scaleY};
            highlight.quadStage[static_cast<size_t>(corner)] = stage;
            minPoint.x = std::min(minPoint.x, stage.x);
            minPoint.y = std::min(minPoint.y, stage.y);
            maxPoint.x = std::max(maxPoint.x, stage.x);
            maxPoint.y = std::max(maxPoint.y, stage.y);
        }

        highlight.hitRectStage = Rect{
            static_cast<int32_t>(std::floor(minPoint.x)) - 2,
            static_cast<int32_t>(std::floor(minPoint.y)) - 2,
            static_cast<int32_t>(std::ceil(maxPoint.x - minPoint.x)) + 4,
            static_cast<int32_t>(std::ceil(maxPoint.y - minPoint.y)) + 4};
        highlight.delay = static_cast<float>(
            std::min<size_t>(ocrHighlights_.size(), 40) * 0.022f);

        ocrHighlights_.push_back(std::move(highlight));
    }

    rebuildOcrHighlights();
    showToast("已识别 " + std::to_string(result.lines.size()) +
              " 行文字 · 单击高亮复制", 2.8f);
}

void CameraProView::clickOcrHighlight(const Vec2f& screenPoint) {
    const Vec2f stagePoint = screenToStage(screenPoint);
    const Vec2i point{static_cast<int32_t>(std::lround(stagePoint.x)),
                      static_cast<int32_t>(std::lround(stagePoint.y))};

    for (const OcrHighlight& highlight : ocrHighlights_) {
        if (!highlight.hitRectStage.contains(point)) continue;
        if (copyTextToClipboard(highlight.text)) {
            showToast("已复制：" + utf8Preview(highlight.text, 18), 2.4f);
        } else {
            showToast("复制到剪贴板失败", 2.0f);
        }
        return;
    }

    showToast("拖拽框选文字区域，松开后自动识别", 2.0f);
}

void CameraProView::copyAllOcrText() {
    std::string all;
    for (const OcrHighlight& highlight : ocrHighlights_) {
        if (highlight.text.empty()) continue;
        if (!all.empty()) all += '\n';
        all += highlight.text;
    }

    if (all.empty()) {
        showToast("尚未识别文字", 1.8f);
        return;
    }

    if (copyTextToClipboard(all)) {
        showToast("已复制 " + std::to_string(ocrHighlights_.size()) + " 行文字",
                  2.2f);
    } else {
        showToast("复制到剪贴板失败", 2.0f);
    }
}

void CameraProView::updateOcrHighlightAnimation(float dtSeconds) {
    for (OcrHighlight& highlight : ocrHighlights_) {
        if (highlight.settled) continue;

        highlight.age += dtSeconds;
        if (highlight.age < highlight.delay) continue;

        const float t = clampf((highlight.age - highlight.delay) / 0.26f,
                               0.0f, 1.0f);
        const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);

        if (std::fabs(eased - highlight.lastOpacity) > 0.01f ||
            (t >= 1.0f && !highlight.settled)) {
            highlight.lastOpacity = eased;
            if (highlight.shapeId != 0) {
                canvas().updateShape(highlight.shapeId, [&](Shape& shape) {
                    shape.base.opacity = eased;
                });
            }
            if (t >= 1.0f) highlight.settled = true;
        }
    }
}

// ============================================================================
// 拍照
// ============================================================================

void CameraProView::takeSnapshot() {
    if (!frameReady_ || processedBgra_.empty() ||
        frameWidth_ <= 0 || frameHeight_ <= 0) {
        showToast("摄像头尚未就绪", 2.0f);
        return;
    }

    const int32_t w = frameWidth_;
    const int32_t h = frameHeight_;

    BLImage cameraImage;
    cameraImage.create_from_data(w, h, BL_FORMAT_PRGB32,
                                 processedBgra_.data(),
                                 static_cast<intptr_t>(w) * 4,
                                 BL_DATA_ACCESS_READ);
    BLImage output(w, h, BL_FORMAT_PRGB32);
    BLContext context(output);
    if (!context.is_valid()) {
        showToast("生成拍照图像失败", 2.2f);
        return;
    }

    context.set_comp_op(BL_COMP_OP_SRC_COPY);
    context.fill_all(BLRgba32(0, 0, 0, 255));
    context.blit_image(BLRect(0.0, 0.0, static_cast<double>(w),
                              static_cast<double>(h)),
                       cameraImage, BLRectI(0, 0, w, h));

    const uint64_t inkHandle = engine_.currentImageHandle();
    const BLImage* inkImage = renderer_ ? renderer_->image(inkHandle) : nullptr;
    if (inkImage) {
        int32_t ix = std::max(0, static_cast<int32_t>(std::lround(cameraStageX_)));
        int32_t iy = std::max(0, static_cast<int32_t>(std::lround(cameraStageY_)));
        int32_t iw = std::max(1, static_cast<int32_t>(std::lround(cameraStageW_)));
        int32_t ih = std::max(1, static_cast<int32_t>(std::lround(cameraStageH_)));
        iw = std::min(iw, kStageWidth - ix);
        ih = std::min(ih, kStageHeight - iy);

        context.set_comp_op(BL_COMP_OP_SRC_OVER);
        context.blit_image(
            BLRect(0.0, 0.0, static_cast<double>(w), static_cast<double>(h)),
            *inkImage, BLRectI(ix, iy, iw, ih));
    }

    context.flush(BL_CONTEXT_FLUSH_SYNC);

    BLImageData data{};
    if (output.get_data(&data) != BL_SUCCESS || !data.pixel_data) {
        showToast("读取拍照图像失败", 2.2f);
        return;
    }

    std::vector<uint8_t> pixels(
        static_cast<size_t>(w) * h * 4u);
    for (int32_t y = 0; y < h; ++y) {
        std::memcpy(
            pixels.data() + static_cast<size_t>(y) * w * 4u,
            static_cast<const uint8_t*>(data.pixel_data) +
                static_cast<ptrdiff_t>(y) * data.stride,
            static_cast<size_t>(w) * 4u);
        for (int32_t x = 0; x < w; ++x) {
            pixels[static_cast<size_t>(y) * w * 4u +
                   static_cast<size_t>(x) * 4u + 3u] = 255;
        }
    }

    const SnapshotResult result = saveBgraToClipboardAndPng(
        w, h, pixels.data());

    if (result.ok) {
        if (result.copiedToClipboard && result.savedToFile) {
            showToast("已拍照：复制到剪贴板并保存 PNG", 2.4f);
        } else if (result.copiedToClipboard) {
            showToast("已拍照并复制到剪贴板", 2.0f);
        } else {
            showToast("已拍照并保存 PNG", 2.0f);
        }
    } else {
        showToast("拍照失败：" + result.error, 2.5f);
    }
}

// ============================================================================
// 激光 / Toast
// ============================================================================

void CameraProView::updateLaser(float dtSeconds) {
    if (laserPoints_.empty() && !laserActive_) {
        if (laserWasVisible_) {
            for (size_t i = 0; i < laserVisibleSlots_; ++i) {
                canvas().updateShape(laserShapeIds_[i], [&](Shape& shape) {
                    shape.base.visible = false;
                    shape.base.opacity = 0.0f;
                });
            }
            if (laserHeadVisible_) {
                canvas().updateShape(laserHeadShapeId_, [](Shape& shape) {
                    shape.base.visible = false;
                });
            }
            laserVisibleSlots_ = 0;
            laserHeadVisible_ = false;
            laserWasVisible_ = false;
        }
        return;
    }

    for (LaserPoint& point : laserPoints_) point.age += dtSeconds;
    laserPoints_.erase(
        std::remove_if(laserPoints_.begin(), laserPoints_.end(),
                       [](const LaserPoint& point) { return point.age > 0.68f; }),
        laserPoints_.end());

    const size_t newCount = laserPoints_.size();
    const size_t oldCount = laserVisibleSlots_;
    const size_t updateCount = std::max(oldCount, newCount);

    for (size_t i = 0; i < updateCount && i < laserShapeIds_.size(); ++i) {
        const bool show = i < newCount;
        canvas().updateShape(laserShapeIds_[i], [&](Shape& shape) {
            if (auto* circle = std::get_if<CircleShape>(&shape.data)) {
                if (show) {
                    const LaserPoint& point = laserPoints_[i];
                    const float t = clampf(point.age / 0.68f, 0.0f, 1.0f);
                    circle->cx = static_cast<int32_t>(std::lround(point.pos.x));
                    circle->cy = static_cast<int32_t>(std::lround(point.pos.y));
                    circle->r = std::max(2.5f, 8.0f - t * 7.0f);
                    shape.base.opacity = (1.0f - t) * 0.90f;
                } else {
                    circle->r = 1.0f;
                    shape.base.opacity = 0.0f;
                }
            }
            shape.base.visible = show;
        });
    }
    laserVisibleSlots_ = newCount;

    const bool showHead = laserActive_ && cursorInCanvas_ && !ocrSelecting_;
    if (showHead) {
        canvas().updateShape(laserHeadShapeId_, [&](Shape& shape) {
            if (auto* circle = std::get_if<CircleShape>(&shape.data)) {
                circle->cx = static_cast<int32_t>(std::lround(cursorPoint_.x));
                circle->cy = static_cast<int32_t>(std::lround(cursorPoint_.y));
                circle->r = 8.0f;
            }
            shape.base.visible = true;
            shape.base.opacity = 0.95f;
        });
        laserHeadVisible_ = true;
    } else if (laserHeadVisible_) {
        canvas().updateShape(laserHeadShapeId_, [](Shape& shape) {
            shape.base.visible = false;
        });
        laserHeadVisible_ = false;
    }

    laserWasVisible_ = (newCount > 0) || showHead;
}

void CameraProView::updateToast(float dtSeconds) {
    if (toastShapeId_ == 0) return;

    if (toastRemaining_ <= 0.0f) {
        if (toastLastAlpha_ != 0.0f || toastBuilt_) {
            canvas().updateShape(toastShapeId_, [](Shape& shape) {
                shape.base.visible = false;
                shape.base.opacity = 0.0f;
            });
            toastLastAlpha_ = 0.0f;
            toastBuilt_ = false;
            toastText_.clear();
        }
        return;
    }

    toastRemaining_ -= dtSeconds;
    const float alpha = clampf(std::min(1.0f, toastRemaining_ / 0.35f),
                               0.0f, 1.0f);

    if (toastRemaining_ <= 0.0f) {
        toastRemaining_ = 0.0f;
        canvas().updateShape(toastShapeId_, [](Shape& shape) {
            shape.base.visible = false;
            shape.base.opacity = 0.0f;
        });
        toastLastAlpha_ = 0.0f;
        toastBuilt_ = false;
        toastText_.clear();
        return;
    }

    if (toastBuilt_) {
        if (std::fabs(alpha - toastLastAlpha_) < 0.002f) return;
        toastLastAlpha_ = alpha;
        canvas().updateShape(toastShapeId_, [&](Shape& shape) {
            shape.base.opacity = alpha;
        });
        return;
    }

    TextShape measured;
    measured.text = toastText_;
    measured.fontSize = 13;
    canvas().measureTextShape(measured);
    const int32_t textWidth = measured.measuredBounds.isNull()
        ? static_cast<int32_t>(toastText_.size()) * 8
        : measured.measuredBounds.w;

    const int32_t canvasW = std::max(1, canvas().width());
    const int32_t canvasH = std::max(1, canvas().height());
    const int32_t pillW = std::max(180, std::min(canvasW - 80, textWidth + 52));
    const int32_t pillH = 40;
    const int32_t x = (canvasW - pillW) / 2;
    const int32_t y = canvasH - 104;

    GroupData group;
    {
        Shape background;
        background.data = RRectShape{x, y, pillW, pillH, 20.0f, 20.0f};
        background.base.fill = Paint(Color{14, 16, 22, 236});
        background.base.stroke = Paint(Color{96, 205, 255, 70});
        background.base.strokeWidth = 1;
        background.base.z = 0;
        group.children.push_back(std::move(background));
    }
    {
        Shape text;
        TextShape label;
        label.text = toastText_;
        label.x = x + (pillW - textWidth) / 2;
        label.y = y + 26;
        label.fontSize = 13;
        text.data = std::move(label);
        text.base.fill = Paint(Color{238, 242, 250, 255});
        text.base.z = 1;
        group.children.push_back(std::move(text));
    }

    toastBuilt_ = true;
    toastLastAlpha_ = alpha;
    canvas().updateShape(toastShapeId_, [&](Shape& shape) {
        shape.data = std::move(group);
        shape.base.visible = true;
        shape.base.opacity = alpha;
    });
}

void CameraProView::showToast(const std::string& text, float seconds) {
    toastText_ = text;
    toastDuration_ = std::max(0.2f, seconds);
    toastRemaining_ = toastDuration_;
    toastLastAlpha_ = -1.0f;
    toastBuilt_ = false;
}

} // namespace camerapro
