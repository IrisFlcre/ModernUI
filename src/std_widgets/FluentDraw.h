// ============================================================================
// FluentDraw.h — 控件绘制的轻量 Shape 构造器
// ============================================================================
//
// 只做"用 Canvas 图元描述 Fluent 视觉"的小工具，不持有任何状态：
//   makeRect / makeRRect / makeCircle / makeArc / makeLine / makeText
//   makePolyline / makePath / makeCheck / makeChevron / makeX / makePlus / makeMinus
//   measureTextLine / baselineForCenter
//
// ============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Canvas.h"
#include "FluentTheme.h"

namespace modern_ui {
namespace fluent {

inline Shape makeRectShape(const Rect& rect, const Color& fill,
                           const Color& stroke = Color::nullColor(),
                           int32_t strokeWidth = 0) {
    Shape shape;
    shape.base.fill = fill;
    shape.base.stroke = stroke;
    shape.base.strokeWidth = strokeWidth;
    shape.data = RectShape{rect.x, rect.y, rect.w, rect.h};
    return shape;
}

inline Shape makeRRectShape(const Rect& rect, float radius, const Color& fill,
                            const Color& stroke = Color::nullColor(),
                            int32_t strokeWidth = 0) {
    Shape shape;
    shape.base.fill = fill;
    shape.base.stroke = stroke;
    shape.base.strokeWidth = strokeWidth;
    shape.data = RRectShape{rect.x, rect.y, rect.w, rect.h, radius, radius};
    return shape;
}

inline Shape makeCircleShape(int32_t cx, int32_t cy, float radius, const Color& fill,
                             const Color& stroke = Color::nullColor(),
                             int32_t strokeWidth = 0) {
    Shape shape;
    shape.base.fill = fill;
    shape.base.stroke = stroke;
    shape.base.strokeWidth = strokeWidth;
    shape.data = CircleShape{cx, cy, radius};
    return shape;
}

// Blur Behind：由渲染后端回读下层像素做高斯模糊（radius = σ），
// cornerRadius 与前景背景圆角保持一致，模糊不会漏到圆角之外。
inline Shape makeBlurShape(const Rect& rect, float radius = 16.0f,
                           float cornerRadius = 0.0f) {
    Shape shape;
    shape.data = BlurShape{rect.x, rect.y, rect.w, rect.h, radius, cornerRadius};
    return shape;
}

inline Shape makeArcShape(int32_t cx, int32_t cy, float radius,
                          float startAngle, float sweepAngle, const Color& stroke,
                          int32_t strokeWidth = 2) {
    Shape shape;
    shape.base.fill = Paint{};
    shape.base.stroke = stroke;
    shape.base.strokeWidth = strokeWidth;
    shape.data = ArcShape{cx, cy, radius, radius, startAngle, sweepAngle};
    return shape;
}

inline Shape makeLineShape(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                           const Color& stroke, int32_t strokeWidth = 1) {
    Shape shape;
    shape.base.stroke = stroke;
    shape.base.strokeWidth = strokeWidth;
    shape.data = LineShape{x1, y1, x2, y2};
    return shape;
}

inline Shape makeTextShape(std::string text, int32_t fontSize, const Color& color,
                           int32_t x = 0, int32_t baselineY = 0) {
    Shape shape;
    shape.base.fill = color;
    TextShape textShape;
    textShape.text = std::move(text);
    textShape.fontSize = fontSize;
    textShape.x = x;
    textShape.y = baselineY;
    shape.data = std::move(textShape);
    return shape;
}

inline void setShapeText(Shape& shape, const std::string& text, int32_t fontSize,
                         const Color& color, int32_t x, int32_t baselineY) {
    if (auto* textShape = std::get_if<TextShape>(&shape.data)) {
        textShape->text = text;
        textShape->fontSize = fontSize;
        textShape->x = x;
        textShape->y = baselineY;
        textShape->measured = false;
        textShape->measuredBounds = Rect::null();
    }
    shape.base.fill = color;
    shape.base.visible = !text.empty();
}

inline Shape makePolylineShape(std::vector<Vec2i> points, const Color& stroke,
                               int32_t strokeWidth = 2, bool closed = false) {
    Shape shape;
    shape.base.fill = Paint{};
    shape.base.stroke = stroke;
    shape.base.strokeWidth = strokeWidth;
    PolylineShape polyline;
    polyline.points = std::move(points);
    polyline.closed = closed;
    shape.data = std::move(polyline);
    return shape;
}

inline Shape makePathShape(const std::vector<std::vector<Vec2f>>& contours,
                           const Color& stroke, int32_t strokeWidth = 2) {
    Shape shape;
    shape.base.fill = Paint{};
    shape.base.stroke = stroke;
    shape.base.strokeWidth = strokeWidth;

    PathShape path;
    for (const auto& contour : contours) {
        if (contour.empty()) continue;
        PathShape::Step move;
        move.cmd = PathShape::Cmd::MoveTo;
        move.pts[0] = contour.front();
        path.steps.push_back(move);

        for (size_t i = 1; i < contour.size(); ++i) {
            PathShape::Step line;
            line.cmd = PathShape::Cmd::LineTo;
            line.pts[0] = contour[i];
            path.steps.push_back(line);
        }
    }
    shape.data = std::move(path);
    return shape;
}

// ---- 常用图标（折线坐标相对绝对位置）----

inline Shape makeCheckShape(int32_t x, int32_t y, int32_t size, const Color& color,
                            int32_t strokeWidth = 2) {
    const float sx = static_cast<float>(x);
    const float sy = static_cast<float>(y);
    const float s = static_cast<float>(size);
    return makePolylineShape(
        {
            Vec2i{static_cast<int32_t>(sx + s * 0.20f), static_cast<int32_t>(sy + s * 0.53f)},
            Vec2i{static_cast<int32_t>(sx + s * 0.42f), static_cast<int32_t>(sy + s * 0.74f)},
            Vec2i{static_cast<int32_t>(sx + s * 0.82f), static_cast<int32_t>(sy + s * 0.28f)}
        },
        color, strokeWidth);
}

inline Shape makeChevronShape(int32_t x, int32_t y, int32_t w, int32_t h,
                              bool down, const Color& color, int32_t strokeWidth = 2) {
    if (down) {
        return makePolylineShape(
            {
                Vec2i{x, y},
                Vec2i{x + w / 2, y + h},
                Vec2i{x + w, y}
            },
            color, strokeWidth);
    }
    return makePolylineShape(
        {
            Vec2i{x, y + h},
            Vec2i{x + w / 2, y},
            Vec2i{x + w, y + h}
        },
        color, strokeWidth);
}

inline Shape makeChevronRightShape(int32_t x, int32_t y, int32_t w, int32_t h,
                                   const Color& color, int32_t strokeWidth = 2) {
    return makePolylineShape(
        {
            Vec2i{x, y},
            Vec2i{x + w, y + h / 2},
            Vec2i{x, y + h}
        },
        color, strokeWidth);
}

inline Shape makeChevronLeftShape(int32_t x, int32_t y, int32_t w, int32_t h,
                                  const Color& color, int32_t strokeWidth = 2) {
    return makePolylineShape(
        {
            Vec2i{x + w, y},
            Vec2i{x, y + h / 2},
            Vec2i{x + w, y + h}
        },
        color, strokeWidth);
}

inline Shape makeXShape(int32_t x, int32_t y, int32_t size, const Color& color,
                        int32_t strokeWidth = 1) {
    const float s = static_cast<float>(size);
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    return makePathShape(
        {
            {Vec2f{fx, fy}, Vec2f{fx + s, fy + s}},
            {Vec2f{fx + s, fy}, Vec2f{fx, fy + s}}
        },
        color, strokeWidth);
}

inline Shape makePlusShape(int32_t cx, int32_t cy, int32_t size, const Color& color,
                           int32_t strokeWidth = 1) {
    const int32_t half = size / 2;
    const float fx = static_cast<float>(cx);
    const float fy = static_cast<float>(cy);
    const float h = static_cast<float>(half);
    return makePathShape(
        {
            {Vec2f{fx - h, fy}, Vec2f{fx + h, fy}},
            {Vec2f{fx, fy - h}, Vec2f{fx, fy + h}}
        },
        color, strokeWidth);
}

inline Shape makeMinusShape(int32_t cx, int32_t cy, int32_t size, const Color& color,
                            int32_t strokeWidth = 1) {
    const int32_t half = size / 2;
    const float fx = static_cast<float>(cx);
    const float fy = static_cast<float>(cy);
    const float h = static_cast<float>(half);
    return makePathShape({{Vec2f{fx - h, fy}, Vec2f{fx + h, fy}}}, color, strokeWidth);
}

// ---- 文本度量 ----

inline Rect measureTextLine(const Canvas& canvas, const std::string& text, int32_t fontSize) {
    if (text.empty()) return Rect{0, -fontSize, 0, fontSize};
    TextShape probe;
    probe.text = text;
    probe.fontSize = fontSize;
    canvas.measureTextShape(probe);
    if (probe.measured && !probe.measuredBounds.isNull()) return probe.measuredBounds;

    const int32_t advance = std::max(1, static_cast<int32_t>(static_cast<float>(fontSize) * 0.6f));
    return Rect{
        0,
        -fontSize,
        std::max(1, static_cast<int32_t>(text.size()) * advance + 2),
        fontSize + 4
    };
}

// 让单行文本在 boxHeight 的容器中垂直居中：返回基线 y
inline int32_t baselineForCenter(const Rect& measured, int32_t boxHeight) {
    return (boxHeight - measured.h) / 2 - measured.y;
}

// 单行省略：超出 maxWidth 时按 UTF-8 码点截断并追加省略号
inline std::string ellipsizeText(const Canvas& canvas, const std::string& text,
                                 int32_t fontSize, int32_t maxWidth) {
    if (text.empty() || maxWidth <= 0) return text;
    if (measureTextLine(canvas, text, fontSize).w <= maxWidth) return text;

    const std::string ellipsis = "\xE2\x80\xA6";   // U+2026 …
    size_t length = text.size();
    while (length > 0) {
        --length;
        while (length > 0 &&
               (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u) {
            --length;
        }
        const std::string candidate = text.substr(0, length) + ellipsis;
        if (measureTextLine(canvas, candidate, fontSize).w <= maxWidth) return candidate;
    }
    return ellipsis;
}

} // namespace fluent
} // namespace modern_ui
