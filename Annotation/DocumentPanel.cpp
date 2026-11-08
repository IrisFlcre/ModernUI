// ============================================================================
// DocumentPanel.cpp
// ============================================================================

#include "DocumentPanel.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "View.h"

using modern_ui::CircleShape;
using modern_ui::Color;
using modern_ui::Event;
using modern_ui::EventType;
using modern_ui::GroupData;
using modern_ui::LineShape;
using modern_ui::Paint;
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

    modern_ui::fluent::MarkdownTheme theme;
    theme.text = Color{230, 236, 246, 255};
    theme.textMuted = Color{164, 174, 192, 255};
    theme.accent = kAccent;
    theme.link = Color{110, 196, 255, 255};
    theme.codeBackground = Color{255, 255, 255, 18};
    theme.codeBorder = Color{255, 255, 255, 36};
    theme.tableBorder = Color{255, 255, 255, 32};
    theme.tableHeader = Color{255, 255, 255, 20};
    theme.divider = Color{255, 255, 255, 28};
    theme.quoteBar = Color{96, 205, 255, 150};
    theme.scrollbarTrack = Color{255, 255, 255, 20};
    theme.scrollbarThumb = Color{126, 142, 168, 160};
    theme.baseFontSize = 14;
    theme.codeFontSize = 13;
    theme.padding = 16;
    theme.lineHeight = 22;
    markdown_->setTheme(theme);
    markdown_->setLayer(layer_ + 2);
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
    }

    rebuildVisuals();
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

    // 窗口背景
    addRRect(Rect{0, 0, w, h}, 14.0f, kPanelBackground, kPanelStroke, 1);
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
    if (hovered()) return;
    if (!closeHovered_ && !copyHovered_ && !gripHovered_) return;
    closeHovered_ = false;
    copyHovered_ = false;
    gripHovered_ = false;
    rebuildVisuals();
}

bool DocumentPanel::onEvent(const Event& ev) {
    const Vec2i point{ev.x, ev.y};

    switch (ev.type) {
        case EventType::MouseMove: {
            if (dragging_) {
                int32_t x = startFrame_.x + point.x - pressPoint_.x;
                int32_t y = startFrame_.y + point.y - pressPoint_.y;
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
                int32_t width = startFrame_.w + point.x - pressPoint_.x;
                int32_t height = startFrame_.h + point.y - pressPoint_.y;
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
                pressPoint_ = point;
                startFrame_ = frame();
                return true;
            }
            if (headerRect_.contains(point)) {
                pressTarget_ = PressTarget::None;
                dragging_ = true;
                pressPoint_ = point;
                startFrame_ = frame();
                return true;
            }
            return false;
        }

        case EventType::MouseUp: {
            if (ev.button != modern_ui::MouseButtonLeft) return false;

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

        default:
            return false;
    }
}

} // namespace annotation
