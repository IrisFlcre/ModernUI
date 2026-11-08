// ============================================================================
// AnnotationView.cpp
// ============================================================================

#include "AnnotationView.h"

#include "AssetPath.h"
#include "Blend2DRenderer.h"
#include "DocumentPanel.h"
#include "DocumentParser.h"
#include "GlobalMouseHook.h"
#include "Screenshot.h"
#include "Win32LayeredWindow.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef ANNOTATION_MODEL_DIR
  #define ANNOTATION_MODEL_DIR "models"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
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
using modern_ui::PolylineShape;
using modern_ui::Rect;
using modern_ui::RectShape;
using modern_ui::RRectShape;
using modern_ui::Shape;
using modern_ui::TextShape;
using modern_ui::Vec2f;
using modern_ui::Vec2i;

namespace annotation {

namespace {

// 绘制采样节流：高频鼠标（1000Hz+）与触控事件按固定间隔进入笔迹引擎。
// 3 倍上限：鼠标 / 橡皮 ≈375Hz，触控 ≈300Hz，激光 ≈150Hz。
constexpr uint64_t kStrokeSampleIntervalUs = 2667;
constexpr uint64_t kTouchSampleIntervalUs = 3333;
constexpr uint64_t kEraseSampleIntervalUs = 2667;
constexpr uint64_t kLaserSampleIntervalUs = 6667;

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

// 按 Unicode 码点截断 UTF-8，避免拆开多字节字符。
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

// 去掉首尾 ASCII 空白（OCR 行文本清理）。
std::string trimAsciiText(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (begin < end && isSpace(text[begin])) ++begin;
    while (end > begin && isSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

float distanceOf(const Vec2f& a, const Vec2f& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

// ============================================================================
// 初始化
// ============================================================================

AnnotationView::AnnotationView(int32_t width, int32_t height)
    : modern_ui::View(width, height) {}

void AnnotationView::initialize(modern_ui::Blend2DRenderer& renderer,
                                modern_ui::Win32LayeredWindow& window,
                                modern_ui::GlobalMouseHook* hook,
                                const std::string& iconDirectory) {
    if (initialized_) return;

    renderer_ = &renderer;
    window_ = &window;
    hook_ = hook;

    canvas().setBackgroundColor(Color{0, 0, 0, 0});

    engine_.initialize(renderer_, width(), height());
    pageImageHandle_ = engine_.currentImageHandle();
    syncedPageIndex_ = engine_.pageIndex();

    createRootShapes();

    toolbar_ = emplaceChild<AnnotationToolbar>();
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
    toolbar_->onInkParametersChanged = [this](float smoothing, float penTip, float taper) {
        uiSmoothing_ = smoothing;
        uiPenTip_ = penTip;
        uiTaper_ = taper;
        applyStyle();
    };
    toolbar_->onEraserSizeChanged = [this](float diameter) {
        eraserSize_ = clampf(diameter, 8.0f, 160.0f);
        cursorDirty_ = true;
    };
    toolbar_->onCardScaleChanged = [this](float factor) {
        cardScale_ = clampf(factor, 0.6f, 2.0f);
        aiCards_.setUserScale(cardScale_);
    };
    toolbar_->onUndo = [this] { undo(); };
    toolbar_->onClear = [this] { clearCurrentPage(); };
    toolbar_->onScreenshot = [this] { startScreenshotMode(); };
    toolbar_->onOcr = [this] { startOcrMode(); };
    toolbar_->onAi = [this] { startAiAssist(); };
    toolbar_->onDocument = [this] { startDocumentParse(); };
    toolbar_->onAddPage = [this] { addPage(); };
    toolbar_->onPrevPage = [this] { gotoPrevPage(); };
    toolbar_->onNextPage = [this] { gotoNextPage(); };
    toolbar_->onClose = [this] {
        if (onQuitRequested) onQuitRequested();
    };

    applyStyle();
    syncToolbar();

    ocr_.initialize(
        modern_ui::resolveAssetDirectory("models", ANNOTATION_MODEL_DIR));

    aiCards_.initialize(&canvas(), iconDirectory);
    aiCards_.setUserScale(cardScale_);
    aiMarks_.initialize(&canvas());
    ai_.initialize();
    office_.initialize();   // 后台轮询 WPS / PowerPoint 放映页码

    initialized_ = true;
    cursorDirty_ = true;
}

void AnnotationView::createRootShapes() {
    const int32_t w = std::max(1, canvas().width());
    const int32_t h = std::max(1, canvas().height());

    // 笔迹 Bitmap（最底层）
    {
        Shape shape;
        ImageShape image;
        image.handle = pageImageHandle_;
        image.x = 0;
        image.y = 0;
        image.w = w;
        image.h = h;
        shape.data = image;
        shape.base.z = -10000;
        shape.base.opaque = false;
        shape.base.visible = pageImageHandle_ != 0;
        imageShapeId_ = canvas().addShape(std::move(shape));
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
        shape.base.z = -8000;
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
        shape.base.z = -7999;
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

    // 截图选区：上 / 下 / 左 / 右 遮罩 + 边框
    for (size_t i = 0; i < screenshotShapeIds_.size(); ++i) {
        Shape shape;
        shape.data = RectShape{};
        shape.base.z = -2000;
        shape.base.visible = false;
        if (i < 4) {
            shape.base.fill = Paint(Color{0, 0, 0, 110});
        } else {
            shape.base.fill = Paint();
            shape.base.stroke = Paint(Color{96, 205, 255, 220});
            shape.base.strokeWidth = 2;
        }
        screenshotShapeIds_[i] = canvas().addShape(std::move(shape));
    }

    // Toast
    {
        Shape shape;
        shape.data = GroupData{};
        shape.base.z = -900;
        shape.base.visible = false;
        toastShapeId_ = canvas().addShape(std::move(shape));
    }
}

// ============================================================================
// 状态同步
// ============================================================================

void AnnotationView::applyStyle() {
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

void AnnotationView::syncToolbar() {
    if (!toolbar_) return;
    toolbar_->setTool(tool_);
    toolbar_->setColor(color_);
    toolbar_->setWidth(width_);
    toolbar_->setInkParameters(uiSmoothing_, uiPenTip_, uiTaper_);
    toolbar_->setEraserSize(eraserSize_);
    toolbar_->setCardScale(cardScale_ * 100.0f);

    // 放映联动时页码显示使用演示文稿的页码，而不是批注页序号。
    if (officeConnected_ && officePage_ > 0) {
        toolbar_->setPageInfo(officePage_ - 1,
                              officeTotal_ > 0 ? officeTotal_ : engine_.pageCount());
    } else {
        toolbar_->setPageInfo(engine_.pageIndex(), engine_.pageCount());
    }
    toolbar_->setHistoryState(engine_.canUndo());
    toolbar_->setScreenshotActive(screenshotMode_);
    toolbar_->setOfficeState(officeConnected_, officeWps_);
}

void AnnotationView::syncPageImage() {
    if (imageShapeId_ == 0) return;

    // 只读取已存在的位图：翻页浏览空白页不会分配整屏 Bitmap。
    const uint64_t handle = engine_.existingImageHandle();
    if (engine_.pageIndex() == syncedPageIndex_ && handle == pageImageHandle_) {
        return;
    }

    syncedPageIndex_ = engine_.pageIndex();
    pageImageHandle_ = handle;

    const int32_t w = std::max(1, canvas().width());
    const int32_t h = std::max(1, canvas().height());

    canvas().updateShape(imageShapeId_, [&](Shape& shape) {
        if (auto* image = std::get_if<ImageShape>(&shape.data)) {
            image->handle = handle;
            image->x = 0;
            image->y = 0;
            image->w = w;
            image->h = h;
            image->srcX = 0;
            image->srcY = 0;
            image->srcW = 0;
            image->srcH = 0;
        }
        shape.base.visible = handle != 0;
    });
    canvas().markDirtyAll();
}

// ============================================================================
// 帧驱动
// ============================================================================

void AnnotationView::onTick(float dtSeconds) {
    if (!initialized_) return;

    // 页位图按需创建：每帧比对句柄，首次落笔后及时显示新 Bitmap。
    syncPageImage();

    updateCursor();
    updateLaser(dtSeconds);
    updateToast(dtSeconds);
    updateOcrHighlights(dtSeconds);
    aiMarks_.tick(dtSeconds);

    OcrResult ocrResult;
    while (ocr_.poll(&ocrResult)) {
        if (documentAwaitingOcr_ && ocrResult.requestId == documentOcrRequestId_) {
            documentAwaitingOcr_ = false;
            handleDocumentOcrResult(ocrResult);
        } else if (aiAwaitingOcr_ && ocrResult.requestId == aiOcrRequestId_) {
            aiAwaitingOcr_ = false;
            handleAiOcrResult(ocrResult);
        } else if (ocrResult.requestId == ocrRequestId_) {
            applyOcrResult(ocrResult);
        }
    }

    AiReply aiReply;
    while (ai_.poll(&aiReply)) {
        if (aiReply.requestId != 0 && aiReply.requestId == aiRequestId_) {
            aiRequestId_ = 0;
            applyAiReply(aiReply);
        } else if (aiReply.requestId != 0 &&
                   aiReply.requestId == documentRequestId_) {
            documentRequestId_ = 0;
            applyDocumentReply(aiReply);
        }
    }

    if (screenshotDirty_) {
        updateScreenshotOverlay();
        screenshotDirty_ = false;
    }

    updateOfficeSync();
    syncToolbar();
    updateHookPolicy();
}

void AnnotationView::updateCursor() {
    if (!cursorDirty_ || cursorShapeId_ == 0) return;
    cursorDirty_ = false;

    const Vec2i point{static_cast<int32_t>(std::lround(cursorPoint_.x)),
                      static_cast<int32_t>(std::lround(cursorPoint_.y))};

    const bool brushTool =
        (tool_ == Tool::Pen || tool_ == Tool::Highlighter || tool_ == Tool::Eraser);

    const bool show = visible_ && cursorInCanvas_ && !screenshotMode_ &&
                      brushTool && !pointOverToolbar(point) &&
                      !pointOverAiCards(point) &&
                      !pointOverDocumentPanel(point);

    const float radius = (tool_ == Tool::Eraser)
        ? std::max(4.0f, eraserSize_ * 0.5f)
        : engine_.currentBaseWidth() * 0.5f + 2.0f;

    cursorVisible_ = show;
    cursorRadius_ = static_cast<int32_t>(std::lround(radius));

    canvas().updateShape(cursorShapeId_, [&](Shape& shape) {
        if (auto* circle = std::get_if<CircleShape>(&shape.data)) {
            circle->cx = point.x;
            circle->cy = point.y;
            circle->r = radius;
        }
        shape.base.visible = show;
        shape.base.stroke = Paint((tool_ == Tool::Eraser)
            ? Color{255, 255, 255, 210}
            : Color{96, 205, 255, 200});
        shape.base.strokeWidth = 2;
    });
}

void AnnotationView::updateLaser(float dtSeconds) {
    if (laserPoints_.empty() && !laserActive_) {
        if (laserWasVisible_) {
            for (size_t i = 0; i < laserVisibleSlots_; ++i) {
                canvas().updateShape(laserShapeIds_[i], [&](Shape& shape) {
                    shape.base.visible = false;
                    shape.base.opacity = 0.0f;
                });
            }
            if (laserHeadVisible_) {
                canvas().updateShape(laserHeadShapeId_, [&](Shape& shape) {
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

    // 只更新"当前可见槽 + 刚熄灭的槽"，不再每帧遍历全部 28 个圆。
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

    const bool showHead = laserActive_ && visible_ && cursorInCanvas_ &&
                          !screenshotMode_;
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
        canvas().updateShape(laserHeadShapeId_, [&](Shape& shape) {
            shape.base.visible = false;
        });
        laserHeadVisible_ = false;
    }

    laserWasVisible_ = (newCount > 0) || showHead;
}

void AnnotationView::updateToast(float dtSeconds) {
    if (toastShapeId_ == 0) return;

    if (toastRemaining_ <= 0.0f) {
        if (toastLastAlpha_ != 0.0f || toastBuilt_) {
            canvas().updateShape(toastShapeId_, [&](Shape& shape) {
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
    const float alpha = clampf(std::min(1.0f, toastRemaining_ / 0.35f), 0.0f, 1.0f);

    if (toastRemaining_ <= 0.0f) {
        toastRemaining_ = 0.0f;
        canvas().updateShape(toastShapeId_, [&](Shape& shape) {
            shape.base.visible = false;
            shape.base.opacity = 0.0f;
        });
        toastLastAlpha_ = 0.0f;
        toastBuilt_ = false;
        toastText_.clear();
        return;
    }

    // 文本 / 圆角背景只构建一次；后续淡出只更新 Group opacity。
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

void AnnotationView::updateScreenshotOverlay() {
    if (screenshotShapeIds_[0] == 0) return;

    const bool ocrMode = (tool_ == Tool::Ocr);
    const bool show = (screenshotMode_ && selecting_) ||
                      (ocrMode && ocrSelecting_);
    const int32_t w = std::max(1, canvas().width());
    const int32_t h = std::max(1, canvas().height());

    Rect selection = clampRect(selectionRect_);
    if (selection.isNull()) {
        for (uint32_t id : screenshotShapeIds_) {
            canvas().updateShape(id, [&](Shape& shape) { shape.base.visible = false; });
        }
        return;
    }

    struct Dim {
        Rect rect;
    } dims[4] = {
        {Rect{0, 0, w, selection.y}},
        {Rect{0, selection.bottom(), w, h - selection.bottom()}},
        {Rect{0, selection.y, selection.x, selection.h}},
        {Rect{selection.right(), selection.y, w - selection.right(), selection.h}},
    };

    for (size_t i = 0; i < 4; ++i) {
        const Rect rect = dims[i].rect;
        const bool visible = show && rect.w > 0 && rect.h > 0;
        canvas().updateShape(screenshotShapeIds_[i], [&](Shape& shape) {
            shape.data = RectShape{rect.x, rect.y, rect.w, rect.h};
            shape.base.visible = visible;
        });
    }

    canvas().updateShape(screenshotShapeIds_[4], [&](Shape& shape) {
        shape.data = RectShape{selection.x, selection.y, selection.w, selection.h};
        shape.base.visible = show && !selection.isNull();
        shape.base.stroke = Paint(ocrMode ? Color{255, 232, 120, 235}
                                          : Color{96, 205, 255, 220});
        shape.base.strokeWidth = 2;
    });
}

// ============================================================================
// 输入
// ============================================================================

bool AnnotationView::handleAppEvent(const Event& event) {
    if (event.type == EventType::Hotkey) {
        handleHotkey(event.hotkeyId);
        return true;
    }

    if (!initialized_ || !visible_) return false;

    if (event.type == EventType::MouseLeave) {
        cursorInCanvas_ = false;
        cursorDirty_ = true;
        return false;
    }

    const bool pointerEvent =
        (event.type == EventType::MouseMove || event.type == EventType::MouseDown ||
         event.type == EventType::MouseUp || event.type == EventType::MouseWheel);
    if (!pointerEvent) return false;

    const Vec2f point{static_cast<float>(event.x), static_cast<float>(event.y)};
    const Vec2i integerPoint{event.x, event.y};
    const bool insideCanvas = (integerPoint.x >= 0 && integerPoint.y >= 0 &&
                               integerPoint.x < canvas().width() &&
                               integerPoint.y < canvas().height());

    cursorPoint_ = point;
    cursorInCanvas_ = insideCanvas;
    cursorDirty_ = true;

    const bool overToolbar = pointOverToolbar(integerPoint);
    const bool overDocument = pointOverDocumentPanel(integerPoint);

    // 文档面板优先：内部交互（拖动 / 缩放 / 滚动 / 按钮）由控件树处理。
    if (overDocument) return false;

    // AI 卡片优先于绘制 / 选区处理；指针模式下卡片区域由 Hook 吞掉左键，
    // 因此卡片始终可交互（拖动移动，短按展开 / 收起 / 关闭）。
    if (aiCards_.hasCards()) {
        if (event.type == EventType::MouseMove) {
            if (aiCards_.handleDrag(integerPoint)) return true;
            aiCards_.updateHover(integerPoint);
        } else if (event.type == EventType::MouseDown &&
                   event.button == modern_ui::MouseButtonLeft) {
            if (!overToolbar && aiCards_.handlePress(integerPoint)) {
                return true;
            }
        } else if (event.type == EventType::MouseUp &&
                   event.button == modern_ui::MouseButtonLeft) {
            if (aiCards_.handleRelease(integerPoint)) {
                return true;
            }
        } else if (event.type == EventType::MouseWheel) {
            if (!overToolbar &&
                aiCards_.handleWheel(integerPoint, event.wheelDelta)) {
                return true;
            }
        }
    }

    if (tool_ == Tool::Ocr) {
        if (event.type == EventType::MouseDown &&
            event.button == modern_ui::MouseButtonLeft) {
            if (overToolbar) return false;
            ocrSelecting_ = true;
            selectionStart_ = point;
            selectionRect_ = Rect::null();
            screenshotDirty_ = true;
            return true;
        }
        if (event.type == EventType::MouseMove && ocrSelecting_) {
            updateScreenshotSelection(point);
            return true;
        }
        if (event.type == EventType::MouseUp &&
            event.button == modern_ui::MouseButtonLeft && ocrSelecting_) {
            finishOcrSelection(point);
            return true;
        }
        return false;
    }

    if (screenshotMode_) {
        if (event.type == EventType::MouseDown &&
            event.button == modern_ui::MouseButtonLeft) {
            if (overToolbar) return false;
            beginScreenshotSelection();
            return true;
        }
        if (event.type == EventType::MouseMove && selecting_) {
            updateScreenshotSelection(point);
            return true;
        }
        if (event.type == EventType::MouseUp &&
            event.button == modern_ui::MouseButtonLeft && selecting_) {
            finishScreenshotSelection();
            return true;
        }
        return false;
    }

    if (event.type == EventType::MouseDown) {
        if (event.button != modern_ui::MouseButtonLeft) return false;
        if (overToolbar) return false;

        if (tool_ == Tool::Pointer) return false;

        if (tool_ == Tool::Screenshot) {
            startScreenshotMode();
            beginScreenshotSelection();
            return true;
        }

        switch (tool_) {
            case Tool::Pen:
            case Tool::Highlighter:
                beginStrokeAt(point, eventTimeSeconds(event));
                break;

            case Tool::Eraser:
                beginEraseAt(point);
                break;

            case Tool::Laser:
                laserActive_ = true;
                addLaserPoint(point);
                break;

            default:
                break;
        }
        return false;
    }

    if (event.type == EventType::MouseMove) {
        const uint64_t now = monotonicMicros();
        const uint64_t touchInterval =
            (event.pointerType == modern_ui::PointerType::Touch)
                ? kTouchSampleIntervalUs : kStrokeSampleIntervalUs;

        if (drawing_) {
            if (lastStrokeSampleUs_ == 0 ||
                now - lastStrokeSampleUs_ >= touchInterval) {
                engine_.addPoint(point, eventTimeSeconds(event));
                appendLivePolyline(point);
                lastStrokeSampleUs_ = now;
                hasPendingStrokeSample_ = false;
            } else {
                pendingStrokeStage_ = point;
                pendingStrokeScreen_ = point;
                pendingStrokeTime_ = eventTimeSeconds(event);
                hasPendingStrokeSample_ = true;
            }
            return false;
        }
        if (erasing_) {
            const uint64_t eraseInterval =
                (event.pointerType == modern_ui::PointerType::Touch)
                    ? kTouchSampleIntervalUs : kEraseSampleIntervalUs;
            if (lastEraseSampleUs_ == 0 ||
                now - lastEraseSampleUs_ >= eraseInterval) {
                applyEraseSample(point);
                lastEraseSampleUs_ = now;
                hasPendingEraseSample_ = false;
            } else {
                pendingEraseStage_ = point;
                hasPendingEraseSample_ = true;
            }
            return false;
        }
        if (laserActive_) {
            if (lastLaserSampleUs_ == 0 ||
                now - lastLaserSampleUs_ >= kLaserSampleIntervalUs) {
                addLaserPoint(point);
                lastLaserSampleUs_ = now;
            }
            return false;
        }
        return false;
    }

    if (event.type == EventType::MouseUp &&
        event.button == modern_ui::MouseButtonLeft) {
        if (drawing_) {
            endStroke();
            return false;
        }
        if (erasing_) {
            endErase();
            return false;
        }
        if (laserActive_) {
            addLaserPoint(point);
            laserActive_ = false;
            return false;
        }
    }

    return false;
}

void AnnotationView::handleHotkey(int id) {
    switch (id) {
        case HotkeyToggleVisible: toggleVisible(); break;
        case HotkeyPointerMode: togglePointerMode(); break;
        case HotkeyUndo: undo(); break;
        case HotkeyPrevPage: gotoPrevPage(); break;
        case HotkeyNextPage: gotoNextPage(); break;
        case HotkeyCopyOcr: copyAllOcrText(); break;
        case HotkeyDocumentZoomIn:
            if (documentPanel_) documentPanel_->zoomBy(0.1f);
            break;
        case HotkeyDocumentZoomOut:
            if (documentPanel_) documentPanel_->zoomBy(-0.1f);
            break;
        case HotkeyDocumentZoomReset:
            if (documentPanel_) documentPanel_->resetZoom();
            break;
        case HotkeyQuit:
            if (onQuitRequested) onQuitRequested();
            break;
        default:
            break;
    }
}

void AnnotationView::setTool(Tool tool) {
    if (tool == Tool::Screenshot) {
        startScreenshotMode();
        return;
    }

    if (screenshotMode_) cancelScreenshotMode();
    if (ocrSelecting_) {
        ocrSelecting_ = false;
        selectionRect_ = Rect::null();
        screenshotDirty_ = true;
    }
    cancelActiveInput();

    if (tool != tool_) {
        if (tool_ != Tool::Pointer && tool_ != Tool::Screenshot) {
            lastNonPointerTool_ = tool_;
        }
        tool_ = tool;
    }

    if (tool_ != Tool::Laser) laserActive_ = false;

    if (toolbar_) toolbar_->setTool(tool_);
    applyStyle();
    cursorDirty_ = true;
}

void AnnotationView::togglePointerMode() {
    if (tool_ == Tool::Pointer) {
        setTool(lastNonPointerTool_ == Tool::Screenshot ? Tool::Pen
                                                        : lastNonPointerTool_);
    } else {
        if (tool_ != Tool::Screenshot) lastNonPointerTool_ = tool_;
        setTool(Tool::Pointer);
    }
}

void AnnotationView::toggleVisible() {
    if (visible_) {
        if (screenshotMode_) {
            cancelScreenshotMode();
            if (tool_ == Tool::Screenshot) setTool(lastNonPointerTool_);
        }
        if (ocrSelecting_) {
            ocrSelecting_ = false;
            selectionRect_ = Rect::null();
            screenshotDirty_ = true;
        }
        cancelActiveInput();
        visible_ = false;
        hookPolicyInitialized_ = false;
        hookPolicyCardCount_ = 0;
        if (hook_) {
            hook_->setLeftButtonPolicy(false, Rect{});
            hook_->setWheelSuppressRect(Rect{});
            hook_->setCardSuppressRects(nullptr, 0);
        }
        if (window_) window_->hide();
    } else {
        visible_ = true;
        cursorDirty_ = true;
        canvas().markDirtyAll();
        drawFrame();
        if (window_) window_->show();
    }
}

void AnnotationView::beginStrokeAt(const Vec2f& point, double timeSeconds) {
    if (!engine_.ready()) return;
    drawing_ = true;
    hasPendingStrokeSample_ = false;
    lastStrokeSampleUs_ = 0;
    engine_.beginStroke(point, timeSeconds);
    beginLivePolyline(point);
}

void AnnotationView::endStroke() {
    if (!drawing_) return;
    flushStrokeSample();
    drawing_ = false;

    Rect dirty;
    if (engine_.endStroke(&dirty) && !dirty.isNull()) {
        canvas().markDirty(dirty);
    }
    // 新页首次落笔会按需创建 Bitmap，立即挂到画布避免预览消失一帧。
    syncPageImage();
    hasPendingStrokeSample_ = false;
    lastStrokeSampleUs_ = 0;
    // 松笔后移除折线预览，露出 Bitmap 中的完整笔迹（Bezier + 笔锋）。
    clearLivePolyline();
    syncToolbar();
}

void AnnotationView::beginEraseAt(const Vec2f& point) {
    cancelActiveInput();
    erasing_ = true;
    hasPendingEraseSample_ = false;
    lastEraseSampleUs_ = 0;

    const float radius = std::max(4.0f, eraserSize_ * 0.5f);
    Rect dirty;
    engine_.beginErase(point, radius, &dirty);
    if (!dirty.isNull()) canvas().markDirty(dirty);
    // AI 批注与笔迹一样可被橡皮擦擦除（整条批注消失）。
    aiMarks_.eraseAt(point, radius);
    syncToolbar();
}

void AnnotationView::endErase() {
    if (!erasing_) return;
    flushEraseSample();
    erasing_ = false;
    hasPendingEraseSample_ = false;
    lastEraseSampleUs_ = 0;
    engine_.endErase();
    syncToolbar();
}

void AnnotationView::flushStrokeSample() {
    if (!hasPendingStrokeSample_) return;
    engine_.addPoint(pendingStrokeStage_, pendingStrokeTime_);
    appendLivePolyline(pendingStrokeScreen_);
    hasPendingStrokeSample_ = false;
}

void AnnotationView::flushEraseSample() {
    if (!hasPendingEraseSample_) return;
    applyEraseSample(pendingEraseStage_);
    hasPendingEraseSample_ = false;
}

void AnnotationView::applyEraseSample(const Vec2f& point) {
    const float radius = std::max(4.0f, eraserSize_ * 0.5f);
    Rect dirty;
    engine_.eraseTo(point, radius, &dirty);
    if (!dirty.isNull()) canvas().markDirty(dirty);
    aiMarks_.eraseAt(point, radius);
}

void AnnotationView::addLaserPoint(const Vec2f& point) {
    laserPoints_.push_back(LaserPoint{point, 0.0f});
    if (laserPoints_.size() > laserShapeIds_.size()) {
        laserPoints_.erase(laserPoints_.begin());
    }
}

// ----------------------------------------------------------------------------
// 实时预览：只画简单折线（分块，每块最多 32 点），松笔后再光栅化完整笔迹。
// ----------------------------------------------------------------------------

void AnnotationView::beginLivePolyline(const Vec2f& point) {
    clearLivePolyline();

    liveStrokeColor_ = color_;
    if (tool_ == Tool::Highlighter) {
        liveStrokeColor_.a = 90;
        liveStrokeWidth_ = width_ * 2.4f;
    } else {
        liveStrokeColor_.a = 255;
        liveStrokeWidth_ = width_;
    }

    livePolylinePoints_.clear();
    livePolylinePoints_.push_back(point);
}

void AnnotationView::appendLivePolyline(const Vec2f& point) {
    if (livePolylinePoints_.empty()) {
        livePolylinePoints_.push_back(point);
        return;
    }

    const Vec2f& previous = livePolylinePoints_.back();
    const float dx = point.x - previous.x;
    const float dy = point.y - previous.y;
    if (dx * dx + dy * dy < 4.0f) return;   // 2px 内不新增折线段

    livePolylinePoints_.push_back(point);

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
        shape.base.z = -9000 + static_cast<int32_t>(livePolylineIds_.size());
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

    // 当前块写满后，用最后一点作为下一块的起点，保持折线连续。
    if (livePolylinePoints_.size() >= 32) {
        const Vec2f tail = livePolylinePoints_.back();
        livePolylinePoints_.assign(1, tail);
        liveChunkHasShape_ = false;
    }
}

void AnnotationView::clearLivePolyline() {
    for (uint32_t id : livePolylineIds_) {
        canvas().removeShape(id);
    }
    livePolylineIds_.clear();
    livePolylinePoints_.clear();
    liveChunkHasShape_ = false;
}

void AnnotationView::cancelActiveInput() {
    if (drawing_) {
        engine_.cancelStroke();
        drawing_ = false;
    }
    if (erasing_) {
        engine_.endErase();
        erasing_ = false;
    }
    clearLivePolyline();
    laserActive_ = false;

    hasPendingStrokeSample_ = false;
    hasPendingEraseSample_ = false;
    lastStrokeSampleUs_ = 0;
    lastEraseSampleUs_ = 0;
    lastLaserSampleUs_ = 0;
}

// ============================================================================
// 截图
// ============================================================================

void AnnotationView::startScreenshotMode() {
    if (screenshotMode_) return;

    cancelActiveInput();

    if (tool_ != Tool::Pointer && tool_ != Tool::Screenshot) {
        lastNonPointerTool_ = tool_;
    }

    tool_ = Tool::Screenshot;
    if (toolbar_) toolbar_->setTool(tool_);

    screenshotMode_ = true;
    selecting_ = false;
    selectionRect_ = Rect::null();
    screenshotDirty_ = true;
    cursorDirty_ = true;

    if (toolbar_) toolbar_->setScreenshotActive(true);
    showToast("拖拽框选区域，松开后复制并保存 PNG", 2.4f);
}

void AnnotationView::cancelScreenshotMode() {
    screenshotMode_ = false;
    selecting_ = false;
    selectionRect_ = Rect::null();
    screenshotDirty_ = true;
    cursorDirty_ = true;
    if (toolbar_) toolbar_->setScreenshotActive(false);
}

void AnnotationView::beginScreenshotSelection() {
    selecting_ = true;
    selectionStart_ = cursorPoint_;
    selectionRect_ = Rect::null();
    screenshotDirty_ = true;
}

void AnnotationView::updateScreenshotSelection(const Vec2f& point) {
    cursorPoint_ = point;
    cursorDirty_ = true;
    Rect next = normalizedRect(selectionStart_, point);
    next = clampRect(next);
    if (!(next == selectionRect_)) {
        selectionRect_ = next;
        screenshotDirty_ = true;
    }
}

void AnnotationView::finishScreenshotSelection() {
    if (!selecting_) return;
    selecting_ = false;

    Rect region = clampRect(selectionRect_);
    if (region.w < 4 || region.h < 4) {
        region = Rect{0, 0, std::max(1, canvas().width()),
                      std::max(1, canvas().height())};
    }

    screenshotDirty_ = true;
    takeScreenshot(region);
    cancelScreenshotMode();
    setTool(lastNonPointerTool_ == Tool::Screenshot ? Tool::Pen
                                                    : lastNonPointerTool_);
}

void AnnotationView::takeScreenshot(const Rect& region) {
    if (!window_ || !renderer_) return;

    const Vec2i origin = window_->clientOrigin();
    const uint64_t handle = engine_.currentImageHandle();
    const BLImage* ink = renderer_->image(handle);

    window_->hide();
    Sleep(35);

    const CaptureResult result = captureRegionToClipboardAndPng(
        origin.x + region.x, origin.y + region.y, region.w, region.h,
        region.x, region.y, ink);

    canvas().markDirtyAll();
    drawFrame();
    window_->show();

    if (result.ok) {
        if (result.copiedToClipboard && result.savedToFile) {
            showToast("已复制到剪贴板，并保存 PNG", 2.2f);
        } else if (result.copiedToClipboard) {
            showToast("已复制到剪贴板", 2.0f);
        } else {
            showToast("已保存 PNG", 2.0f);
        }
    } else {
        showToast("截图失败：" + result.error, 2.5f);
    }
}

// ============================================================================
// 文字识别 / 荧光高亮
// ============================================================================

void AnnotationView::startOcrMode() {
    if (!initialized_) return;
    if (aiAwaitingOcr_) {
        showToast("AI 正在识别画面文字，请稍候…", 2.0f);
        return;
    }
    if (documentBusy_) {
        showToast("文档解析进行中，请稍候…", 2.0f);
        return;
    }

    if (screenshotMode_) {
        cancelScreenshotMode();
        if (toolbar_) toolbar_->setScreenshotActive(false);
    }
    ocrSelecting_ = false;
    selectionRect_ = Rect::null();
    screenshotDirty_ = true;
    cancelActiveInput();

    if (tool_ != Tool::Ocr) {
        if (tool_ != Tool::Pointer && tool_ != Tool::Screenshot) {
            lastNonPointerTool_ = tool_;
        }
        tool_ = Tool::Ocr;
    }

    if (toolbar_) toolbar_->setTool(tool_);
    cursorDirty_ = true;
    showToast("拖拽框选文字区域，松开后自动识别", 2.4f);
}

void AnnotationView::finishOcrSelection(const Vec2f& point) {
    if (!ocrSelecting_) return;
    ocrSelecting_ = false;

    Rect region = clampRect(selectionRect_);
    selectionRect_ = Rect::null();
    screenshotDirty_ = true;

    if (region.w < 4 || region.h < 4) {
        // 单击：命中高亮则复制该行文字，否则提示重新框选。
        const Vec2i click{static_cast<int32_t>(std::lround(point.x)),
                          static_cast<int32_t>(std::lround(point.y))};
        if (!copyHighlightAt(click)) {
            showToast("拖拽框选区域后可识别文字", 2.0f);
        }
        return;
    }

    takeOcr(region);
}

void AnnotationView::takeOcr(const Rect& region) {
    if (!window_ || !ocr_.initialized()) {
        showToast("OCR 引擎未初始化", 2.4f);
        return;
    }

    const Vec2i origin = window_->clientOrigin();

    window_->hide();
    Sleep(35);

    CapturedPixels capture = captureScreenRegionRgb(
        origin.x + region.x, origin.y + region.y, region.w, region.h);

    canvas().markDirtyAll();
    drawFrame();
    window_->show();

    if (!capture.ok || capture.rgb.empty()) {
        showToast("截图失败：" + capture.error, 2.5f);
        return;
    }

    ocrRequestId_ = ocr_.recognize(region.x, region.y,
                                  capture.width, capture.height,
                                  std::move(capture.rgb));
    if (ocrRequestId_ == 0) {
        showToast("OCR 任务提交失败", 2.2f);
        return;
    }
    showToast("正在识别文字…", 6.0f);
}

void AnnotationView::applyOcrResult(const OcrResult& result) {
    if (!result.ok) {
        showToast("识别失败：" + result.error, 3.0f);
        return;
    }

    clearOcrHighlights();

    if (result.lines.empty()) {
        showToast("未识别到文字", 2.2f);
        return;
    }

    int32_t z = -3000;
    for (const OcrLine& line : result.lines) {
        const Vec2f& tl = line.quad[0];
        const Vec2f& tr = line.quad[1];
        const Vec2f& br = line.quad[2];
        const Vec2f& bl = line.quad[3];

        const float lineWidth = (distanceOf(tl, tr) + distanceOf(bl, br)) * 0.5f;
        const float lineHeight = (distanceOf(tl, bl) + distanceOf(tr, br)) * 0.5f;
        if (lineWidth < 2.0f || lineHeight < 2.0f) continue;

        const float centerX = (tl.x + tr.x + br.x + bl.x) * 0.25f;
        const float centerY = (tl.y + tr.y + br.y + bl.y) * 0.25f;
        const float angle = std::atan2(tr.y - tl.y, tr.x - tl.x) *
                            (180.0f / 3.14159265358979323846f);

        // macOS Live Text 风格：外层柔光 + 内层荧光渐变，文字区域四周留一点呼吸空间。
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

        const float minX = std::min(std::min(tl.x, tr.x), std::min(br.x, bl.x));
        const float maxX = std::max(std::max(tl.x, tr.x), std::max(br.x, bl.x));
        const float minY = std::min(std::min(tl.y, tr.y), std::min(br.y, bl.y));
        const float maxY = std::max(std::max(tl.y, tr.y), std::max(br.y, bl.y));

        TextHighlight highlight;
        highlight.text = line.text;
        highlight.hitRect = Rect{
            static_cast<int32_t>(std::floor(minX)) - 2,
            static_cast<int32_t>(std::floor(minY)) - 2,
            static_cast<int32_t>(std::ceil(maxX - minX)) + 4,
            static_cast<int32_t>(std::ceil(maxY - minY)) + 4};
        highlight.delay = static_cast<float>(
            std::min<size_t>(ocrHighlights_.size(), 40) * 0.022f);
        highlight.lastOpacity = 0.0f;

        Shape shape;
        shape.data = std::move(group);
        shape.base.z = z++;
        shape.base.opacity = 0.0f;
        shape.base.visible = true;
        highlight.shapeId = canvas().addShape(std::move(shape));
        ocrHighlights_.push_back(std::move(highlight));
    }

    showToast("已识别 " + std::to_string(result.lines.size()) +
              " 行文字 · 单击高亮复制", 2.8f);
}

void AnnotationView::clearOcrHighlights() {
    for (const TextHighlight& highlight : ocrHighlights_) {
        if (highlight.shapeId != 0) canvas().removeShape(highlight.shapeId);
    }
    ocrHighlights_.clear();
}

void AnnotationView::updateOcrHighlights(float dtSeconds) {
    for (TextHighlight& highlight : ocrHighlights_) {
        if (highlight.settled) continue;

        highlight.age += dtSeconds;
        if (highlight.age < highlight.delay) continue;

        const float t = clampf(
            (highlight.age - highlight.delay) / 0.26f, 0.0f, 1.0f);
        const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);

        if (std::fabs(eased - highlight.lastOpacity) > 0.01f ||
            (t >= 1.0f && !highlight.settled)) {
            highlight.lastOpacity = eased;
            canvas().updateShape(highlight.shapeId, [&](Shape& shape) {
                shape.base.opacity = eased;
            });
            if (t >= 1.0f) highlight.settled = true;
        }
    }
}

bool AnnotationView::copyHighlightAt(const Vec2i& point) {
    for (const TextHighlight& highlight : ocrHighlights_) {
        if (!highlight.hitRect.contains(point)) continue;

        if (copyTextToClipboard(highlight.text)) {
            showToast("已复制：" + utf8Preview(highlight.text, 20), 2.4f);
        } else {
            showToast("复制到剪贴板失败", 2.0f);
        }
        return true;
    }
    return false;
}

void AnnotationView::copyAllOcrText() {
    std::string all;
    for (const TextHighlight& highlight : ocrHighlights_) {
        if (highlight.text.empty()) continue;
        if (!all.empty()) all += '\n';
        all += highlight.text;
    }

    if (all.empty()) {
        showToast("尚未识别文字", 1.8f);
        return;
    }

    if (copyTextToClipboard(all)) {
        showToast("已复制 " + std::to_string(ocrHighlights_.size()) +
                  " 行文字", 2.2f);
    } else {
        showToast("复制到剪贴板失败", 2.0f);
    }
}

// ============================================================================
// AI 智能辅助教学
// ============================================================================

void AnnotationView::startAiAssist() {
    if (!initialized_) return;
    if (aiBusy_ || documentBusy_) {
        showToast("AI 正在分析，请稍候…", 1.8f);
        return;
    }
    if (!ai_.ready()) {
        showToast("未找到 API_KEY.txt：请在本程序目录写入 DeepSeek API 密钥", 5.0f);
        return;
    }
    if (!window_ || !ocr_.initialized()) {
        showToast("OCR 引擎未初始化", 2.4f);
        return;
    }

    if (screenshotMode_) {
        cancelScreenshotMode();
        if (toolbar_) toolbar_->setScreenshotActive(false);
    }
    if (ocrSelecting_) {
        ocrSelecting_ = false;
        selectionRect_ = Rect::null();
        screenshotDirty_ = true;
    }
    cancelActiveInput();

    aiBusy_ = true;
    aiAwaitingOcr_ = false;

    const Vec2i origin = window_->clientOrigin();
    const int32_t width = std::max(1, canvas().width());
    const int32_t height = std::max(1, canvas().height());

    // 隐藏批注层后抓取整屏，避免把笔迹和工具栏混进画面。
    window_->hide();
    Sleep(35);

    CapturedPixels capture = captureScreenRegionRgb(
        origin.x, origin.y, width, height);

    canvas().markDirtyAll();
    drawFrame();
    window_->show();

    if (!capture.ok || capture.rgb.empty()) {
        aiBusy_ = false;
        showToast("全屏截图失败：" + capture.error, 3.0f);
        return;
    }

    aiOcrRequestId_ = ocr_.recognize(0, 0, capture.width, capture.height,
                                    std::move(capture.rgb));
    if (aiOcrRequestId_ == 0) {
        aiBusy_ = false;
        showToast("OCR 任务提交失败", 2.2f);
        return;
    }

    aiAwaitingOcr_ = true;
    showToast("正在截取全屏并识别文字…", 10.0f);
}

void AnnotationView::handleAiOcrResult(const OcrResult& result) {
    if (!result.ok) {
        aiBusy_ = false;
        showToast("文字识别失败：" + result.error, 4.0f);
        return;
    }

    const float canvasWidth = static_cast<float>(std::max(1, canvas().width()));
    const float canvasHeight = static_cast<float>(std::max(1, canvas().height()));

    std::string text;
    text.reserve(2048);
    aiOcrLineRects_.clear();
    for (const OcrLine& line : result.lines) {
        const std::string value = trimAsciiText(line.text);
        if (value.empty()) continue;

        // 附带每行文字在屏幕上的位置 / 尺寸（百分比），供 AI 选择空白区域放卡片 / 批注。
        float minX = line.quad[0].x;
        float maxX = line.quad[0].x;
        float minY = line.quad[0].y;
        float maxY = line.quad[0].y;
        for (int32_t corner = 1; corner < 4; ++corner) {
            minX = std::min(minX, line.quad[static_cast<size_t>(corner)].x);
            maxX = std::max(maxX, line.quad[static_cast<size_t>(corner)].x);
            minY = std::min(minY, line.quad[static_cast<size_t>(corner)].y);
            maxY = std::max(maxY, line.quad[static_cast<size_t>(corner)].y);
        }

        const int32_t rectX = static_cast<int32_t>(std::floor(minX));
        const int32_t rectY = static_cast<int32_t>(std::floor(minY));
        const Rect lineRect{
            rectX, rectY,
            std::max(1, static_cast<int32_t>(std::ceil(maxX - minX))),
            std::max(1, static_cast<int32_t>(std::ceil(maxY - minY)))};
        aiOcrLineRects_.push_back(lineRect);

        char prefix[128];
        std::snprintf(prefix, sizeof(prefix),
                      "#%d [%.1f%%, %.1f%%, %.1f%%, %.1f%%] ",
                      static_cast<int>(aiOcrLineRects_.size()),
                      minX / canvasWidth * 100.0f,
                      minY / canvasHeight * 100.0f,
                      (maxX - minX) / canvasWidth * 100.0f,
                      (maxY - minY) / canvasHeight * 100.0f);

        text += prefix;
        text += value;
        text += '\n';
        if (text.size() > 7000) break;
    }

    if (text.empty()) {
        aiBusy_ = false;
        showToast("画面中未识别到文字，AI 无法分析", 3.2f);
        return;
    }

    char header[200];
    std::snprintf(header, sizeof(header),
                  "屏幕分辨率：%d×%d\n"
                  "OCR 文本（每行格式：#编号 [x%%, y%%, 宽%%, 高%%] 文本，坐标为屏幕占比）：\n",
                  canvas().width(), canvas().height());

    aiRequestId_ = ai_.ask(aiCardSystemPrompt(), std::string(header) + text);
    if (aiRequestId_ == 0) {
        aiBusy_ = false;
        showToast("AI 请求提交失败", 2.4f);
        return;
    }
    showToast("AI 正在分析画面…", 40.0f);
}

void AnnotationView::applyAiReply(const AiReply& reply) {
    aiBusy_ = false;

    if (!reply.ok) {
        showToast("AI 请求失败：" + utf8Preview(reply.error, 60), 5.0f);
        return;
    }

    std::vector<AiCardSpec> cards =
        parseAiCardsXml(reply.text, canvas().width(), canvas().height());
    std::vector<AiMarkSpec> marks =
        parseAiMarksXml(reply.text, canvas().width(), canvas().height(),
                        aiOcrLineRects_);

    if (cards.empty() && marks.empty()) {
        showToast("AI 回复无法解析为卡片或批注", 3.2f);
        return;
    }

    const size_t cardCount = cards.size();
    const size_t markCount = marks.size();
    aiCards_.setCards(std::move(cards));
    aiMarks_.setMarks(std::move(marks));
    canvas().markDirtyAll();
    updateHookPolicy();

    if (cardCount > 0 && markCount > 0) {
        showToast("AI 已生成 " + std::to_string(cardCount) + " 张卡片 · " +
                  std::to_string(markCount) + " 处批注", 3.2f);
    } else if (cardCount > 0) {
        showToast("AI 已生成 " + std::to_string(cardCount) +
                  " 张卡片 · 点击标题展开", 3.2f);
    } else {
        showToast("AI 已在画面上添加 " + std::to_string(markCount) +
                  " 处批注", 3.0f);
    }
}

bool AnnotationView::pointOverAiCards(const Vec2i& point) const {
    return aiCards_.hitTest(point);
}

// ============================================================================
// 文档解析（OCR 原文 → AI 整理 → Markdown 面板）
// ============================================================================

void AnnotationView::startDocumentParse() {
    if (!initialized_) return;
    if (aiBusy_ || documentBusy_) {
        showToast("AI 正在处理，请稍候…", 1.8f);
        return;
    }
    if (!ai_.ready()) {
        showToast("未找到 API_KEY.txt：请在本程序目录写入 DeepSeek API 密钥", 5.0f);
        return;
    }
    if (!window_ || !ocr_.initialized()) {
        showToast("OCR 引擎未初始化", 2.4f);
        return;
    }
    if (ocr_.busy()) {
        showToast("OCR 正在识别，请稍候…", 2.0f);
        return;
    }

    if (screenshotMode_) {
        cancelScreenshotMode();
        if (toolbar_) toolbar_->setScreenshotActive(false);
    }
    if (ocrSelecting_) {
        ocrSelecting_ = false;
        selectionRect_ = Rect::null();
        screenshotDirty_ = true;
    }
    cancelActiveInput();

    documentBusy_ = true;
    documentAwaitingOcr_ = false;
    documentRequestId_ = 0;

    const Vec2i origin = window_->clientOrigin();
    const int32_t width = std::max(1, canvas().width());
    const int32_t height = std::max(1, canvas().height());

    window_->hide();
    Sleep(35);

    CapturedPixels capture = captureScreenRegionRgb(
        origin.x, origin.y, width, height);

    canvas().markDirtyAll();
    drawFrame();
    window_->show();

    if (!capture.ok || capture.rgb.empty()) {
        documentBusy_ = false;
        showToast("全屏截图失败：" + capture.error, 3.0f);
        return;
    }

    documentOcrRequestId_ = ocr_.recognize(0, 0, capture.width, capture.height,
                                          std::move(capture.rgb));
    if (documentOcrRequestId_ == 0) {
        documentBusy_ = false;
        showToast("OCR 任务提交失败", 2.2f);
        return;
    }

    documentAwaitingOcr_ = true;
    showToast("正在截取文档并识别文字…", 10.0f);
}

void AnnotationView::handleDocumentOcrResult(const OcrResult& result) {
    if (!result.ok) {
        documentBusy_ = false;
        showToast("文字识别失败：" + result.error, 4.0f);
        return;
    }

    if (result.lines.empty()) {
        documentBusy_ = false;
        showToast("画面中未识别到文字，无法解析文档", 3.2f);
        return;
    }

    std::string payload = buildDocumentOcrPayload(
        canvas().width(), canvas().height(), result.lines);
    if (payload.empty()) {
        documentBusy_ = false;
        showToast("OCR 结果为空", 2.4f);
        return;
    }

    documentRequestId_ =
        ai_.ask(documentParserSystemPrompt(), payload);
    if (documentRequestId_ == 0) {
        documentBusy_ = false;
        showToast("AI 请求提交失败", 2.4f);
        return;
    }

    showToast("AI 正在还原文档原文（不增删内容）…", 40.0f);
}

void AnnotationView::applyDocumentReply(const AiReply& reply) {
    documentBusy_ = false;

    if (!reply.ok) {
        showToast("文档解析失败：" + utf8Preview(reply.error, 60), 5.0f);
        return;
    }

    const std::string markdown = extractMarkdownFromReply(reply.text);
    if (markdown.empty()) {
        showToast("AI 没有返回文档内容", 3.2f);
        return;
    }

    showDocumentPanel(markdown);
    showToast("文档已还原 · 可拖动 / 缩放 / 滚动", 3.2f);
}

void AnnotationView::showDocumentPanel(const std::string& markdown) {
    if (!documentPanel_) {
        documentPanel_ = emplaceChild<DocumentPanel>();
        documentPanel_->setLayer(-1060);

        Rect bounds = documentPanelRect_;
        if (bounds.isNull() || bounds.w < 320 || bounds.h < 240) {
            const int32_t canvasWidth = std::max(1, canvas().width());
            const int32_t canvasHeight = std::max(1, canvas().height());
            const int32_t width = std::max(
                320, std::min(canvasWidth - 24,
                              static_cast<int32_t>(canvasWidth * 0.48f)));
            const int32_t height = std::max(
                240, std::min(canvasHeight - 24,
                              static_cast<int32_t>(canvasHeight * 0.66f)));
            bounds = Rect{(canvasWidth - width) / 2,
                          std::max(16, (canvasHeight - height) / 2 - 20),
                          width, height};
        }
        documentPanel_->setBounds(bounds);

        documentPanel_->onClose = [this] { closeDocumentPanel(); };
        documentPanel_->onCopy = [this] {
            if (!documentPanel_) return;
            if (copyTextToClipboard(documentPanel_->markdown())) {
                showToast("已复制 Markdown 原文", 2.0f);
            } else {
                showToast("复制到剪贴板失败", 2.0f);
            }
        };
        documentPanel_->onBoundsChanged = [this](const Rect& rect) {
            documentPanelRect_ = rect;
        };

        // 覆盖层无法获得键盘焦点（no-activate + 点击穿透），
        // 面板打开期间用动态全局热键提供 Ctrl + +/-/0 缩放。
        if (window_) {
            window_->registerHotkey(HotkeyDocumentZoomIn,
                                    modern_ui::ModifierControl, VK_OEM_PLUS);
            window_->registerHotkey(HotkeyDocumentZoomInShift,
                                    modern_ui::ModifierControl | modern_ui::ModifierShift,
                                    VK_OEM_PLUS);
            window_->registerHotkey(HotkeyDocumentZoomOut,
                                    modern_ui::ModifierControl, VK_OEM_MINUS);
            window_->registerHotkey(HotkeyDocumentZoomReset,
                                    modern_ui::ModifierControl, '0');
        }
    }

    documentPanel_->setDocument("文档解析", markdown);
    if (toolbar_) toolbar_->setDocumentActive(true);
    canvas().markDirtyAll();
    updateHookPolicy();
}

void AnnotationView::closeDocumentPanel() {
    if (!documentPanel_) return;

    documentPanelRect_ = documentPanel_->frame();
    DocumentPanel* panel = documentPanel_;
    documentPanel_ = nullptr;
    deferRemoveWidget(panel);

    // 释放面板专属缩放热键，避免长期占用系统快捷键。
    if (window_) {
        window_->unregisterHotkey(HotkeyDocumentZoomIn);
        window_->unregisterHotkey(HotkeyDocumentZoomInShift);
        window_->unregisterHotkey(HotkeyDocumentZoomOut);
        window_->unregisterHotkey(HotkeyDocumentZoomReset);
    }

    if (toolbar_) toolbar_->setDocumentActive(false);
    canvas().markDirtyAll();
    updateHookPolicy();
}

bool AnnotationView::pointOverDocumentPanel(const Vec2i& point) const {
    if (!documentPanel_) return false;
    // 面板是根视图的直接子控件，frame() 即客户区坐标；拖动当帧立即生效。
    const Rect frame = documentPanel_->frame();
    return frame.w > 0 && frame.h > 0 && frame.contains(point);
}

// ============================================================================
// 撤销 / 页面
// ============================================================================

void AnnotationView::undo() {
    if (!initialized_ || drawing_ || erasing_) return;
    Rect dirty;
    if (engine_.undo(&dirty)) {
        if (!dirty.isNull()) canvas().markDirty(dirty);
        syncToolbar();
    }
}

void AnnotationView::clearCurrentPage() {
    if (!initialized_) return;
    cancelActiveInput();
    clearOcrHighlights();
    aiMarks_.clear();
    engine_.clearPage();
    canvas().markDirtyAll();
    syncToolbar();
    showToast("已清空当前页", 1.8f);
}

void AnnotationView::addPage() {
    if (!initialized_) return;
    if (officeConnected_) {
        showToast("放映联动中，批注页随演示页码自动切换", 2.0f);
        return;
    }
    cancelActiveInput();
    if (!engine_.addPage()) {
        showToast("已达到最大页数", 1.8f);
        return;
    }
    syncPageImage();
    canvas().markDirtyAll();
    syncToolbar();
    showToast("已新建第 " + std::to_string(engine_.pageIndex() + 1) + " 页", 1.8f);
}

void AnnotationView::gotoPrevPage() {
    if (!initialized_) return;
    if (stepPresentationPage(false)) return;
    if (engine_.pageIndex() <= 0) return;
    switchAnnotationPage(engine_.pageIndex() - 1);
}

void AnnotationView::gotoNextPage() {
    if (!initialized_) return;
    if (stepPresentationPage(true)) return;
    if (engine_.pageIndex() + 1 >= engine_.pageCount()) return;
    switchAnnotationPage(engine_.pageIndex() + 1);
}

// ----------------------------------------------------------------------------
// WPS / PowerPoint 放映页码联动
// ----------------------------------------------------------------------------

void AnnotationView::switchAnnotationPage(int32_t index) {
    if (index < 0) return;
    if (engine_.pageIndex() == index) {
        syncPageImage();
        syncToolbar();
        return;
    }

    cancelActiveInput();
    if (!engine_.setPage(index)) {
        engine_.ensurePageCount(index + 1);
        if (!engine_.setPage(index)) {
            showToast("批注页数已达上限", 2.0f);
            return;
        }
    }
    syncPageImage();
    canvas().markDirtyAll();
    syncToolbar();
}

bool AnnotationView::stepPresentationPage(bool next) {
    // 已知存在放映时，翻页操作始终保持与放映页码一致；
    // 尚未连接时则仅在明确检测到前台放映的情况下接管。
    if (!officeConnected_ && !office_.canControlPresentation()) return false;

    const OfficeBridge::Snapshot snap = office_.snapshot();
    const uint64_t nowMs = monotonicMicros() / 1000;

    // 快速连点时 COM 轮询可能还没更新：以本地乐观页码为基准，避免丢步。
    int32_t basePage = snap.page;
    if (officeConnected_ && syncedOfficePage_ > 0 && nowMs < officeGraceUntilMs_) {
        basePage = syncedOfficePage_;
    }

    if (basePage > 0 && snap.total > 0) {
        if (next && basePage >= snap.total) return true;
        if (!next && basePage <= 1) return true;
    }

    if (!office_.sendPageKey(next)) {
        if (officeConnected_) {
            showToast("请先切回放映窗口再翻页", 1.8f);
            return true;
        }
        return false;
    }

    officeGraceUntilMs_ = nowMs + 600;

    if (basePage > 0) {
        int32_t target = basePage + (next ? 1 : -1);
        if (snap.total > 0) {
            target = std::min(std::max(target, 1), snap.total);
        } else {
            target = std::max(target, 1);
        }

        officeConnected_ = true;
        officeWps_ = (snap.app == OfficeBridge::App::Wps);
        officePage_ = target;
        officeTotal_ = snap.total;
        syncedOfficePage_ = target;
        switchAnnotationPage(target - 1);
        return true;
    }

    // 页码暂时不可用：先让批注页跟随按钮，后续轮询会自动校正。
    if (next) {
        switchAnnotationPage(engine_.pageIndex() + 1);
    } else if (engine_.pageIndex() > 0) {
        switchAnnotationPage(engine_.pageIndex() - 1);
    }
    return true;
}

void AnnotationView::updateOfficeSync() {
    const OfficeBridge::Snapshot snap = office_.snapshot();
    const uint64_t nowMs = monotonicMicros() / 1000;

    if (!snap.presenting) {
        // 短暂读不到放映状态时先保持连接，避免拖动 / 切换窗口导致页码抖动。
        if (officeConnected_ && nowMs - officeLastActiveMs_ > 1200) {
            officeConnected_ = false;
            officeWps_ = false;
            officePage_ = 0;
            officeTotal_ = 0;
            syncedOfficePage_ = 0;
            showToast("演示已结束，恢复批注分页", 2.0f);
        }
        return;
    }

    officeLastActiveMs_ = nowMs;

    if (!officeConnected_) {
        officeConnected_ = true;
        officeWps_ = (snap.app == OfficeBridge::App::Wps);
        officePage_ = snap.page;
        officeTotal_ = snap.total;
        syncedOfficePage_ = 0;
        showToast(officeWps_
            ? "已连接 WPS 演示 · 批注页随放映页码同步"
            : "已连接 PowerPoint 放映 · 批注页随放映页码同步", 3.0f);
    } else {
        officeWps_ = (snap.app == OfficeBridge::App::Wps);
        officeTotal_ = snap.total;
        // 乐观翻页等待放映确认期间，不把页码回退成轮询的旧值。
        if (snap.page > 0 && nowMs >= officeGraceUntilMs_) officePage_ = snap.page;
    }

    if (snap.page <= 0) return;
    if (snap.page == syncedOfficePage_) {
        officePage_ = snap.page;
        return;
    }
    if (nowMs < officeGraceUntilMs_) return;

    officePage_ = snap.page;
    syncedOfficePage_ = snap.page;
    switchAnnotationPage(snap.page - 1);
}

// ============================================================================
// 辅助
// ============================================================================

void AnnotationView::showToast(const std::string& text, float seconds) {
    toastText_ = text;
    toastDuration_ = std::max(0.2f, seconds);
    toastRemaining_ = toastDuration_;
    toastLastAlpha_ = -1.0f;
    toastBuilt_ = false;
}

bool AnnotationView::pointOverToolbar(const Vec2i& point) const {
    if (!toolbar_) return false;
    return toolbar_->absoluteFrame().contains(point);
}

Rect AnnotationView::clampRect(const Rect& rect) const {
    return rect.intersect(Rect{0, 0, std::max(1, canvas().width()),
                               std::max(1, canvas().height())});
}

double AnnotationView::eventTimeSeconds(const Event& event) const {
    // 只有 Hook 输入的时间戳可靠；窗口 / 触控事件改用高精度计时。
    if (event.source == modern_ui::InputSource::Hook && event.timestampMs != 0) {
        return static_cast<double>(event.timestampMs) / 1000.0;
    }
    return static_cast<double>(monotonicMicros()) / 1000000.0;
}

void AnnotationView::updateHookPolicy() {
    if (!hook_) return;

    Rect toolbarScreenRect{};
    std::array<Rect, 8> cardRects{};
    int32_t cardCount = 0;

    if (visible_ && window_) {
        const Vec2i origin = window_->clientOrigin();
        if (toolbar_) {
            const Rect frame = toolbar_->absoluteFrame();
            toolbarScreenRect = Rect{frame.x + origin.x, frame.y + origin.y,
                                     frame.w, frame.h};
        }

        // 文档面板同样需要吞掉左键与滚轮，保证拖动 / 缩放 / 滚动不泄漏。
        // 用 frame()（父容器局部坐标 = 客户区）而不是 absoluteFrame()，
        // 拖动后当帧就能同步吞键矩形，不会滞后一帧。
        if (documentPanel_) {
            const Rect frame = documentPanel_->frame();
            if (frame.w > 0 && frame.h > 0) {
                cardRects[static_cast<size_t>(cardCount++)] =
                    Rect{frame.x + origin.x, frame.y + origin.y,
                         frame.w, frame.h};
            }
        }

        // 滚轮与左键都按每张卡片的精确矩形吞掉，卡片之间空隙不再拦截。
        for (const Rect& frame : aiCards_.frames()) {
            if (cardCount >= static_cast<int32_t>(cardRects.size())) break;
            cardRects[static_cast<size_t>(cardCount++)] =
                Rect{frame.x + origin.x, frame.y + origin.y,
                     frame.w, frame.h};
        }
    }

    const bool suppressAll =
        visible_ && (screenshotMode_ || tool_ != Tool::Pointer);

    bool cardRectsSame = (cardCount == hookPolicyCardCount_);
    if (cardRectsSame) {
        for (int32_t i = 0; i < cardCount; ++i) {
            if (!(cardRects[static_cast<size_t>(i)] ==
                  hookPolicyCardRects_[static_cast<size_t>(i)])) {
                cardRectsSame = false;
                break;
            }
        }
    }

    if (hookPolicyInitialized_ && suppressAll == hookPolicySuppressAll_ &&
        toolbarScreenRect == hookPolicyRect_ && cardRectsSame) {
        return;
    }
    hookPolicyInitialized_ = true;
    hookPolicySuppressAll_ = suppressAll;
    hookPolicyRect_ = toolbarScreenRect;
    hookPolicyCardRects_ = cardRects;
    hookPolicyCardCount_ = cardCount;
    hook_->setLeftButtonPolicy(suppressAll, toolbarScreenRect);
    hook_->setWheelSuppressRect(Rect{});
    hook_->setCardSuppressRects(cardCount > 0 ? cardRects.data() : nullptr,
                                cardCount);
}

} // namespace annotation
