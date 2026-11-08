// ============================================================================
// DocumentPanel.cpp
// ============================================================================

#include "DocumentPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "View.h"

using modern_ui::CircleShape;
using modern_ui::Color;
using modern_ui::Event;
using modern_ui::EventType;
using modern_ui::GroupData;
using modern_ui::LineShape;
using modern_ui::ModifierControl;
using modern_ui::Paint;
using modern_ui::PointerType;
using modern_ui::Rect;
using modern_ui::RRectShape;
using modern_ui::Shape;
using modern_ui::TextShape;
using modern_ui::Vec2i;

namespace annotation {

namespace {

const Color kPanelBackground{18, 21, 30, 248};
const Color kPanelStroke{255, 255, 255, 36};
const Color kTitleText{236, 241, 250, 255};
const Color kMutedText{154, 164, 182, 255};
const Color kMutedTextDim{154, 164, 182, 210};
const Color kAccent{96, 205, 255, 255};
const Color kAccentDim{96, 205, 255, 150};

Rect insetRect(const Rect& rect, int32_t inset) {
    return Rect{rect.x + inset, rect.y + inset,
                std::max(0, rect.w - inset * 2), std::max(0, rect.h - inset * 2)};
}

int32_t textBaseline(const modern_ui::Canvas& canvas, const std::string& text,
                     int32_t fontSize, int32_t top, int32_t height) {
    TextShape probe;
    probe.text = text;
    probe.fontSize = fontSize;
    canvas.measureTextShape(probe);
    const Rect metrics = probe.measured && !probe.measuredBounds.isNull()
        ? probe.measuredBounds
        : Rect{0, -fontSize, fontSize * 4, fontSize + 4};
    return top + (height - metrics.h) / 2 - metrics.y;
}

} // namespace

// ============================================================================
// 构造 / 配置
// ============================================================================

DocumentPanel::DocumentPanel() {
    clip(true);
    canvas().setBackgroundColor(Color{0, 0, 0, 0});
    absolute(0, 0);
    size(680, 520);
    visualGroupId_ = canvas().group().add();

    markdown_ = emplaceChild<modern_ui::fluent::MarkdownView>();

    baseTheme_.text = Color{230, 236, 246, 255};
    baseTheme_.textMuted = Color{164, 174, 192, 255};
    baseTheme_.accent = kAccent;
    baseTheme_.link = Color{110, 196, 255, 255};
    baseTheme_.codeBackground = Color{255, 255, 255, 18};
    baseTheme_.codeBorder = Color{255, 255, 255, 36};
    baseTheme_.tableBorder = Color{255, 255, 255, 32};
    baseTheme_.tableHeader = Color{255, 255, 255, 20};
    baseTheme_.divider = Color{255, 255, 255, 28};
    baseTheme_.quoteBar = Color{96, 205, 255, 150};
    baseTheme_.scrollbarTrack = Color{255, 255, 255, 20};
    baseTheme_.scrollbarThumb = Color{126, 142, 168, 160};
    baseTheme_.baseFontSize = 14;
    baseTheme_.codeFontSize = 13;
    baseTheme_.padding = 16;
    baseTheme_.lineHeight = 22;
    applyZoomTheme();
    markdown_->setLayer(layer_ + 2);
}

void DocumentPanel::applyZoomTheme() {
    modern_ui::fluent::MarkdownTheme theme = baseTheme_;
    const float zoom = zoom_;
    theme.baseFontSize =
        std::max(10, static_cast<int32_t>(std::lround(baseTheme_.baseFontSize * zoom)));
    theme.codeFontSize =
        std::max(9, static_cast<int32_t>(std::lround(baseTheme_.codeFontSize * zoom)));
    theme.lineHeight = std::max(
        theme.baseFontSize + 6,
        static_cast<int32_t>(std::lround(baseTheme_.lineHeight * zoom)));
    theme.padding = std::max(
        10, static_cast<int32_t>(std::lround(
                baseTheme_.padding * std::min(zoom, 1.4f))));
    markdown_->setTheme(theme);
}

void DocumentPanel::setZoom(float zoom) {
    const float next = std::max(0.6f, std::min(2.5f, zoom));
    if (std::fabs(next - zoom_) < 0.005f) return;
    zoom_ = next;
    applyZoomTheme();
    rebuildVisuals();
}

void DocumentPanel::zoomBy(float delta) {
    setZoom(zoom_ + delta);
}

void DocumentPanel::resetZoom() {
    setZoom(1.0f);
}

void DocumentPanel::setDocument(std::string title, std::string markdown) {
    title_ = std::move(title);
    markdown_->setMarkdown(std::move(markdown));
    rebuildVisuals();
}

const std::string& DocumentPanel::markdown() const {
    return markdown_->markdown();
}

void DocumentPanel::setBounds(const Rect& bounds) {
    const int32_t width = std::max(320, bounds.w);
    const int32_t height = std::max(240, bounds.h);
    absolute(bounds.x, bounds.y);
    size(width, height);

    // 挂载前就给定正文控件尺寸，避免第一帧出现空白闪烁。
    updateMarkdownGeometry(width, height);
}

void DocumentPanel::updateMarkdownGeometry(int32_t panelWidth,
                                           int32_t panelHeight) {
    if (!markdown_) return;
    const int32_t contentTop = headerHeight() + 1;
    markdown_->absolute(1, contentTop);
    markdown_->size(std::max(1, panelWidth - 2),
                    std::max(1, panelHeight - contentTop - footerHeight()));
}

void DocumentPanel::setLayer(int32_t layer) {
    layer_ = layer;
    markdown_->setLayer(layer_ + 2);
    rebuildVisuals();
}

// ============================================================================
// 布局 / 绘制
// ============================================================================

Vec2i DocumentPanel::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{680, 520};
}

void DocumentPanel::onLayout(const Rect& frame) {
    const int32_t w = std::max(1, frame.w);
    const int32_t h = std::max(1, frame.h);
    if (canvas().width() != w || canvas().height() != h) {
        canvas().resize(w, h);
        // 只在尺寸变化时重绘标题栏；拖动只改位置，避免每帧重建几何体。
        rebuildVisuals();
    }
}

void DocumentPanel::rebuildVisuals() {
    const int32_t w = std::max(1, frame().w);
    const int32_t h = std::max(1, frame().h);

    headerRect_ = Rect{0, 0, w, headerHeight()};
    const int32_t closeWidth = 38;
    closeRect_ = Rect{w - closeWidth, 0, closeWidth, headerHeight()};
    const int32_t copyWidth = 58;
    copyRect_ = Rect{w - closeWidth - copyWidth, 0, copyWidth, headerHeight()};
    gripRect_ = Rect{w - footerHeight(), h - footerHeight(), footerHeight(),
                     footerHeight()};

    GroupData group;
    int32_t z = 0;
    auto add = [&](Shape shape) {
        shape.base.z = z++;
        group.children.push_back(std::move(shape));
    };

    auto addRRect = [&](const Rect& rect, float radius, const Color& fill,
                        const Color& stroke, int32_t strokeWidth,
                        float opacity = 1.0f) {
        Shape shape;
        shape.data = RRectShape{rect.x, rect.y, rect.w, rect.h, radius, radius};
        shape.base.fill = Paint(fill);
        if (strokeWidth > 0) {
            shape.base.stroke = Paint(stroke);
            shape.base.strokeWidth = strokeWidth;
        }
        shape.base.opacity = opacity;
        add(std::move(shape));
    };

    auto addLine = [&](int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                       const Color& color, int32_t strokeWidth,
                       float opacity = 1.0f) {
        Shape shape;
        shape.data = LineShape{x1, y1, x2, y2};
        shape.base.stroke = Paint(color);
        shape.base.strokeWidth = strokeWidth;
        shape.base.opacity = opacity;
        add(std::move(shape));
    };

    auto addText = [&](const std::string& text, int32_t x, int32_t baseline,
                       int32_t fontSize, const Color& color) {
        if (text.empty()) return;
        Shape shape;
        TextShape label;
        label.text = text;
        label.x = x;
        label.y = baseline;
        label.fontSize = fontSize;
        shape.data = std::move(label);
        shape.base.fill = Paint(color);
        add(std::move(shape));
    };

    // 窗口背景（获得焦点时描边高亮）
    addRRect(Rect{0, 0, w, h}, 14.0f, kPanelBackground,
             focusRing_ ? Color{96, 205, 255, 140} : kPanelStroke,
             focusRing_ ? 2 : 1);
    addRRect(Rect{16, 1, std::max(1, w - 32), 1}, 1.0f,
             Color{255, 255, 255, 26}, Color{0, 0, 0, 0}, 0);

    // 标题栏底部分隔线
    addLine(1, headerHeight(), w - 1, headerHeight(),
            Color{255, 255, 255, 26}, 1);

    // 文档图标
    {
        const int32_t iconX = 16;
        const int32_t iconY = (headerHeight() - 26) / 2;
        addRRect(Rect{iconX, iconY, 20, 26}, 4.0f, Color{96, 205, 255, 28},
                 Color{96, 205, 255, 140}, 1);
        addLine(iconX + 5, iconY + 8, iconX + 15, iconY + 8, kAccentDim, 1);
        addLine(iconX + 5, iconY + 13, iconX + 15, iconY + 13, kAccentDim, 1);
        addLine(iconX + 5, iconY + 18, iconX + 12, iconY + 18, kAccentDim, 1);
    }

    // 标题
    {
        const int32_t titleFont = 15;
        const int32_t baseline = textBaseline(canvas(), title_, titleFont, 0,
                                              headerHeight());
        addText(title_, 46, baseline, titleFont, kTitleText);
    }

    // 缩放比例
    {
        char label[16];
        std::snprintf(label, sizeof(label), "%d%%",
                      static_cast<int>(std::lround(zoom_ * 100.0f)));
        const int32_t fontSize = 11;
        TextShape probe;
        probe.text = label;
        probe.fontSize = fontSize;
        canvas().measureTextShape(probe);
        const int32_t measured =
            probe.measured && !probe.measuredBounds.isNull()
                ? probe.measuredBounds.w
                : fontSize * 3;
        const int32_t x = copyRect_.x - 10 - measured;
        const int32_t baseline = textBaseline(canvas(), label, fontSize,
                                              copyRect_.y, copyRect_.h);
        addText(label, std::max(46, x), baseline, fontSize, kMutedTextDim);
    }

    // 复制按钮
    if (copyHovered_) {
        addRRect(insetRect(copyRect_, 6), 7.0f, Color{255, 255, 255, 18},
                 Color{0, 0, 0, 0}, 0);
    }
    {
        const std::string label = "复制";
        const int32_t fontSize = 12;
        TextShape probe;
        probe.text = label;
        probe.fontSize = fontSize;
        canvas().measureTextShape(probe);
        const int32_t measured =
            probe.measured && !probe.measuredBounds.isNull()
                ? probe.measuredBounds.w
                : fontSize * 2;
        const int32_t x = copyRect_.x + (copyRect_.w - measured) / 2;
        const int32_t baseline = textBaseline(canvas(), label, fontSize,
                                              copyRect_.y, copyRect_.h);
        addText(label, x, baseline, fontSize,
                copyHovered_ ? Color{236, 241, 250, 255} : kMutedText);
    }

    // 关闭按钮（×）
    {
        if (closeHovered_) {
            addRRect(insetRect(closeRect_, 6), 7.0f,
                     Color{255, 92, 92, 30}, Color{255, 92, 92, 90}, 1);
        }
        const int32_t cx = closeRect_.x + closeRect_.w / 2;
        const int32_t cy = closeRect_.y + closeRect_.h / 2;
        const int32_t radius = 5;
        const Color color = closeHovered_ ? Color{255, 190, 190, 255}
                                          : kMutedText;
        addLine(cx - radius, cy - radius, cx + radius, cy + radius, color, 1);
        addLine(cx - radius, cy + radius, cx + radius, cy - radius, color, 1);
    }

    // 右下角缩放手柄（三道斜线）
    {
        const Color color = gripHovered_ ? kAccent : Color{150, 162, 184, 150};
        const int32_t x0 = gripRect_.x + gripRect_.w - 4;
        const int32_t y0 = gripRect_.y + gripRect_.h - 4;
        addLine(x0 - 12, y0, x0, y0 - 12, color, 1);
        addLine(x0 - 8, y0, x0, y0 - 8, color, 1);
        addLine(x0 - 4, y0, x0, y0 - 4, color, 1);
    }

    canvas().updateShape(visualGroupId_, [&](Shape& shape) {
        shape.data = std::move(group);
        shape.base.z = layer_;
        shape.base.visible = true;
    });
}

// ============================================================================
// 交互
// ============================================================================

void DocumentPanel::updateHover(const Vec2i& point) {
    const bool closeHover = closeRect_.contains(point);
    const bool copyHover = !closeHover && copyRect_.contains(point);
    const bool gripHover = gripRect_.contains(point);

    if (closeHover == closeHovered_ && copyHover == copyHovered_ &&
        gripHover == gripHovered_) {
        return;
    }
    closeHovered_ = closeHover;
    copyHovered_ = copyHover;
    gripHovered_ = gripHover;
    rebuildVisuals();
}

void DocumentPanel::onStateChanged() {
    bool dirty = false;

    const bool focusNow = focused();
    if (focusNow != focusRing_) {
        focusRing_ = focusNow;
        dirty = true;
    }

    if (!hovered() &&
        (closeHovered_ || copyHovered_ || gripHovered_)) {
        closeHovered_ = false;
        copyHovered_ = false;
        gripHovered_ = false;
        dirty = true;
    }

    if (dirty) rebuildVisuals();
}

// ============================================================================
// 坐标 / 触控手势
// ============================================================================

Vec2i DocumentPanel::absolutePoint(const Vec2i& localPoint) const {
    const Rect& abs = absoluteFrame();
    return Vec2i{abs.x + localPoint.x, abs.y + localPoint.y};
}

float DocumentPanel::touchSpread() const {
    if (touches_.size() < 2) return 0.0f;
    const Vec2i a = touches_[0].second;
    const Vec2i b = touches_[1].second;
    const float dx = static_cast<float>(b.x - a.x);
    const float dy = static_cast<float>(b.y - a.y);
    return std::sqrt(dx * dx + dy * dy);
}

void DocumentPanel::addTouchPoint(int32_t id, const Vec2i& point) {
    for (auto& touch : touches_) {
        if (touch.first == id) {
            touch.second = point;
            return;
        }
    }
    if (touches_.size() >= 2) return;
    touches_.emplace_back(id, point);
    beginPinchIfReady();
}

void DocumentPanel::moveTouchPoint(int32_t id, const Vec2i& point) {
    for (auto& touch : touches_) {
        if (touch.first == id) {
            touch.second = point;
            return;
        }
    }
}

void DocumentPanel::removeTouchPoint(int32_t id) {
    for (auto it = touches_.begin(); it != touches_.end(); ++it) {
        if (it->first != id) continue;
        touches_.erase(it);
        if (touches_.size() < 2) pinching_ = false;
        return;
    }
}

void DocumentPanel::beginPinchIfReady() {
    if (touches_.size() < 2) return;

    // 双指落下：终止单指拖动 / 调整大小，进入捏合缩放。
    dragging_ = false;
    resizing_ = false;
    pressTarget_ = PressTarget::None;
    pinching_ = true;
    pinchStartDistance_ = touchSpread();
    pinchStartZoom_ = zoom_;
}

void DocumentPanel::updatePinch() {
    if (touches_.size() < 2 || pinchStartDistance_ <= 1.0f) return;
    const float spread = touchSpread();
    if (spread <= 1.0f) return;
    setZoom(pinchStartZoom_ * spread / pinchStartDistance_);
}

bool DocumentPanel::handleZoomKey(const Event& ev) {
    if (ev.type != EventType::KeyDown) return false;
    if ((ev.modifiers & ModifierControl) == 0) return false;

    switch (ev.key) {
        case 0xBB:   // '=' / '+'（Ctrl+= 也是多数应用的放大键）
        case 0x6B:   // 小键盘 +
            zoomBy(0.1f);
            return true;
        case 0xBD:   // '-' / '_'
        case 0x6D:   // 小键盘 -
            zoomBy(-0.1f);
            return true;
        case 0x30:   // '0'
        case 0x60:   // 小键盘 0
            resetZoom();
            return true;
        default:
            return false;
    }
}

// ============================================================================
// 事件
// ============================================================================

bool DocumentPanel::onEvent(const Event& ev) {
    const Vec2i point{ev.x, ev.y};
    const Vec2i absolutePointNow = absolutePoint(point);
    const bool touch = ev.pointerType == PointerType::Touch;

    switch (ev.type) {
        case EventType::MouseWheel: {
            // Ctrl + 滚轮 / 触控板捏合（系统会转成 Ctrl+滚轮）：缩放正文。
            if ((ev.modifiers & ModifierControl) != 0) {
                zoomBy(ev.wheelDelta * 0.1f);
                return true;
            }
            return false;
        }

        case EventType::MouseMove: {
            if (touch) {
                moveTouchPoint(ev.pointerId, absolutePointNow);
                if (pinching_) {
                    updatePinch();
                    return true;
                }
            }

            // 拖动 / 缩放全部基于客户区绝对坐标：
            // 面板自身移动后局部坐标会跟着变，用局部坐标会造成抖动。
            const Vec2i delta{absolutePointNow.x - pressAbsolute_.x,
                              absolutePointNow.y - pressAbsolute_.y};

            if (dragging_) {
                int32_t x = startFrame_.x + delta.x;
                int32_t y = startFrame_.y + delta.y;
                if (view()) {
                    x = std::max(8, std::min(
                        std::max(8, view()->width() - startFrame_.w - 8), x));
                    y = std::max(8, std::min(
                        std::max(8, view()->height() - startFrame_.h - 8), y));
                } else {
                    x = std::max(8, x);
                    y = std::max(8, y);
                }
                absolute(x, y);
                return true;
            }

            if (resizing_) {
                int32_t width = startFrame_.w + delta.x;
                int32_t height = startFrame_.h + delta.y;
                width = std::max(320, width);
                height = std::max(240, height);
                if (view()) {
                    width = std::min(
                        width, std::max(320, view()->width() - startFrame_.x - 8));
                    height = std::min(
                        height, std::max(240, view()->height() - startFrame_.y - 8));
                }
                size(width, height);
                updateMarkdownGeometry(width, height);
                return true;
            }

            updateHover(point);
            return true;
        }

        case EventType::MouseDown: {
            if (ev.button != modern_ui::MouseButtonLeft) return false;

            if (touch) {
                addTouchPoint(ev.pointerId, absolutePointNow);
                if (pinching_) return true;
            } else if (!touches_.empty()) {
                // 触控序列丢失（例如某一指在面板外抬起）时用鼠标事件复位。
                touches_.clear();
                pinching_ = false;
            }

            if (closeRect_.contains(point)) {
                pressTarget_ = PressTarget::Close;
                return true;
            }
            if (copyRect_.contains(point)) {
                pressTarget_ = PressTarget::Copy;
                return true;
            }
            if (gripRect_.contains(point)) {
                pressTarget_ = PressTarget::None;
                resizing_ = true;
                pressAbsolute_ = absolutePointNow;
                startFrame_ = frame();
                return true;
            }
            if (headerRect_.contains(point)) {
                pressTarget_ = PressTarget::None;
                dragging_ = true;
                pressAbsolute_ = absolutePointNow;
                startFrame_ = frame();
                return true;
            }
            return false;
        }

        case EventType::MouseUp: {
            if (ev.button != modern_ui::MouseButtonLeft) return false;

            const bool wasPinching = pinching_;
            if (touch) removeTouchPoint(ev.pointerId);
            if (wasPinching) return true;

            if (dragging_ || resizing_) {
                dragging_ = false;
                resizing_ = false;
                if (onBoundsChanged) onBoundsChanged(frame());
                return true;
            }

            const PressTarget target = pressTarget_;
            pressTarget_ = PressTarget::None;
            if (target == PressTarget::Close && closeRect_.contains(point)) {
                if (onClose) onClose();
                return true;
            }
            if (target == PressTarget::Copy && copyRect_.contains(point)) {
                if (onCopy) onCopy();
                return true;
            }
            return true;
        }

        case EventType::KeyDown:
            return handleZoomKey(ev);

        default:
            return false;
    }
}

} // namespace annotation
