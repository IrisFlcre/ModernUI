// ============================================================================
// Canvas.cpp — 画布管理实现（Retain Mode）
// ============================================================================

#include "Canvas.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace modern_ui {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float degToRad(float degrees) {
    return degrees * (kPi / 180.0f);
}

// 角度是否落在 [start, start + sweep] 内（支持负 sweep / 超 360°）
bool angleInSweep(float angleDeg, float startDeg, float sweepDeg) {
    if (std::fabs(sweepDeg) >= 360.0f) return true;
    if (std::fabs(sweepDeg) <= 1e-4f) return false;

    if (sweepDeg > 0.0f) {
        float delta = std::fmod(angleDeg - startDeg, 360.0f);
        if (delta < 0.0f) delta += 360.0f;
        return delta <= sweepDeg + 1e-4f;
    }

    float delta = std::fmod(startDeg - angleDeg, 360.0f);
    if (delta < 0.0f) delta += 360.0f;
    return delta <= -sweepDeg + 1e-4f;
}

Rect floatBounds(float x0, float y0, float x1, float y1) {
    if (!(x1 > x0) || !(y1 > y0)) return Rect::null();
    const int32_t ix0 = static_cast<int32_t>(std::floor(x0));
    const int32_t iy0 = static_cast<int32_t>(std::floor(y0));
    const int32_t ix1 = static_cast<int32_t>(std::ceil(x1));
    const int32_t iy1 = static_cast<int32_t>(std::ceil(y1));
    return {ix0, iy0, ix1 - ix0, iy1 - iy0};
}

// 轴对齐矩形绕自身中心旋转后的轴对齐包围盒
Rect rotatedBounds(const Rect& r, float degrees) {
    if (r.isNull() || std::fabs(degrees) < 1e-4f) return r;

    const float cx = static_cast<float>(r.x) + static_cast<float>(r.w) * 0.5f;
    const float cy = static_cast<float>(r.y) + static_cast<float>(r.h) * 0.5f;
    const float hw = static_cast<float>(r.w) * 0.5f;
    const float hh = static_cast<float>(r.h) * 0.5f;
    const float rad = degToRad(degrees);
    const float c = std::cos(rad);
    const float s = std::sin(rad);

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();

    const float signs[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    for (const auto& sign : signs) {
        const float px = hw * sign[0];
        const float py = hh * sign[1];
        const float rx = cx + px * c - py * s;
        const float ry = cy + px * s + py * c;
        minX = std::min(minX, rx);
        minY = std::min(minY, ry);
        maxX = std::max(maxX, rx);
        maxY = std::max(maxY, ry);
    }

    return floatBounds(minX, minY, maxX, maxY);
}

// Arc / Pie 的精确 AABB：两端点 + 扇形内的坐标轴极点（Pie 还包含圆心）
Rect sectorBounds(int32_t cx, int32_t cy, float rx, float ry,
                  float startDeg, float sweepDeg, bool includeCenter) {
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    if (!(rx > 0.0f) || !(ry > 0.0f)) return Rect::null();
    if (std::fabs(sweepDeg) <= 1e-4f) return Rect::null();

    if (std::fabs(sweepDeg) >= 360.0f) {
        return floatBounds(static_cast<float>(cx) - rx, static_cast<float>(cy) - ry,
                           static_cast<float>(cx) + rx, static_cast<float>(cy) + ry);
    }

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();

    auto addPoint = [&](float angleDeg) {
        const float a = degToRad(angleDeg);
        const float px = static_cast<float>(cx) + rx * std::cos(a);
        const float py = static_cast<float>(cy) + ry * std::sin(a);
        minX = std::min(minX, px);
        minY = std::min(minY, py);
        maxX = std::max(maxX, px);
        maxY = std::max(maxY, py);
    };

    addPoint(startDeg);
    addPoint(startDeg + sweepDeg);
    if (includeCenter) {
        minX = std::min(minX, static_cast<float>(cx));
        minY = std::min(minY, static_cast<float>(cy));
        maxX = std::max(maxX, static_cast<float>(cx));
        maxY = std::max(maxY, static_cast<float>(cy));
    }

    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const float axisAngle = static_cast<float>(quadrant) * 90.0f;
        if (angleInSweep(axisAngle, startDeg, sweepDeg)) addPoint(axisAngle);
    }

    return floatBounds(minX, minY, maxX, maxY);
}

Rect polygonBounds(const PolygonShape& s, int32_t pad) {
    const int32_t sides = std::max<int32_t>(3, s.sides);
    const float radius = std::fabs(s.radius);
    if (!(radius > 0.0f)) return Rect::null();

    const double start = static_cast<double>(degToRad(s.rotation - 90.0f));
    const double step = 2.0 * static_cast<double>(kPi) / sides;

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();

    for (int32_t i = 0; i < sides; ++i) {
        const double angle = start + step * i;
        const float px = static_cast<float>(s.cx) + radius * static_cast<float>(std::cos(angle));
        const float py = static_cast<float>(s.cy) + radius * static_cast<float>(std::sin(angle));
        minX = std::min(minX, px);
        minY = std::min(minY, py);
        maxX = std::max(maxX, px);
        maxY = std::max(maxY, py);
    }

    Rect r = floatBounds(minX, minY, maxX, maxY);
    if (!r.isNull() && pad > 0) r = r.inflate(pad, pad);
    return r;
}

Rect starBounds(const StarShape& s, int32_t pad) {
    const int32_t points = std::max<int32_t>(3, s.points);
    const float outer = std::fabs(s.outerRadius);
    const float inner = std::min(std::fabs(s.innerRadius), outer);
    if (!(outer > 0.0f)) return Rect::null();

    const double start = static_cast<double>(degToRad(s.rotation - 90.0f));
    const double step = static_cast<double>(kPi) / points;

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();

    for (int32_t i = 0; i < points * 2; ++i) {
        const double radius = (i % 2 == 0) ? outer : inner;
        const double angle = start + step * i;
        const float px = static_cast<float>(s.cx) + static_cast<float>(radius * std::cos(angle));
        const float py = static_cast<float>(s.cy) + static_cast<float>(radius * std::sin(angle));
        minX = std::min(minX, px);
        minY = std::min(minY, py);
        maxX = std::max(maxX, px);
        maxY = std::max(maxY, py);
    }

    Rect r = floatBounds(minX, minY, maxX, maxY);
    if (!r.isNull() && pad > 0) r = r.inflate(pad, pad);
    return r;
}

} // namespace

// ============================================================================
// Shape::unrotatedBounds() — 各类型 Shape 的轴对齐包围盒（Shape 自身坐标系）
// ============================================================================

Rect Shape::unrotatedBounds() const {
    if (unrotatedCached_) return cachedUnrotated_;

    Rect r = Rect::null();
    // Stroke 会让图形超出几何包围盒，需要额外留出半宽 + AA 余量
    const int32_t strokePad = base.stroke.isVisible()
        ? std::max<int32_t>(1, base.strokeWidth / 2 + 1)
        : 0;

    switch (type()) {
    case Type::Rect: {
        const auto& s = std::get<RectShape>(data);
        r = {s.x, s.y, s.w, s.h};
        if (!r.isNull() && strokePad > 0) r = r.inflate(strokePad, strokePad);
        break;
    }
    case Type::RRect: {
        const auto& s = std::get<RRectShape>(data);
        r = {s.x, s.y, s.w, s.h};
        if (!r.isNull() && strokePad > 0) r = r.inflate(strokePad, strokePad);
        break;
    }
    case Type::Circle: {
        const auto& s = std::get<CircleShape>(data);
        const int32_t ir = static_cast<int32_t>(std::ceil(std::fabs(s.r)));
        r = {s.cx - ir, s.cy - ir, ir * 2, ir * 2};
        if (!r.isNull() && strokePad > 0) r = r.inflate(strokePad, strokePad);
        break;
    }
    case Type::Ellipse: {
        const auto& s = std::get<EllipseShape>(data);
        const int32_t rx = static_cast<int32_t>(std::ceil(std::fabs(s.rx)));
        const int32_t ry = static_cast<int32_t>(std::ceil(std::fabs(s.ry)));
        r = {s.cx - rx, s.cy - ry, rx * 2, ry * 2};
        if (!r.isNull() && strokePad > 0) r = r.inflate(strokePad, strokePad);
        break;
    }
    case Type::Arc: {
        const auto& s = std::get<ArcShape>(data);
        r = sectorBounds(s.cx, s.cy, s.rx, s.ry, s.startAngle, s.sweepAngle, false);
        // 圆弧必为开放路径，描边至少占 1px（fill 会作为备用画刷）。
        if (!r.isNull()) {
            const int32_t pad = base.stroke.isVisible()
                ? std::max<int32_t>(1, base.strokeWidth / 2 + 1)
                : 1;
            r = r.inflate(pad, pad);
        }
        break;
    }
    case Type::Pie: {
        const auto& s = std::get<PieShape>(data);
        r = sectorBounds(s.cx, s.cy, s.rx, s.ry, s.startAngle, s.sweepAngle, true);
        if (!r.isNull() && strokePad > 0) r = r.inflate(strokePad, strokePad);
        break;
    }
    case Type::Ring: {
        const auto& s = std::get<RingShape>(data);
        const int32_t rx = static_cast<int32_t>(std::ceil(std::fabs(s.rx)));
        const int32_t ry = static_cast<int32_t>(std::ceil(std::fabs(s.ry)));
        r = {s.cx - rx, s.cy - ry, rx * 2, ry * 2};
        if (!r.isNull() && strokePad > 0) r = r.inflate(strokePad, strokePad);
        break;
    }
    case Type::Star: {
        r = starBounds(std::get<StarShape>(data), strokePad);
        break;
    }
    case Type::Polygon: {
        r = polygonBounds(std::get<PolygonShape>(data), strokePad);
        break;
    }
    case Type::Line: {
        const auto& s = std::get<LineShape>(data);
        int32_t x0 = std::min(s.x1, s.x2);
        int32_t y0 = std::min(s.y1, s.y2);
        int32_t x1 = std::max(s.x1, s.x2);
        int32_t y1 = std::max(s.y1, s.y2);
        int32_t pad = base.strokeWidth > 0 ? base.strokeWidth / 2 + 1 : 1;
        r = {x0 - pad, y0 - pad, (x1 - x0) + pad * 2, (y1 - y0) + pad * 2};
        break;
    }
    case Type::Polyline: {
        const auto& s = std::get<PolylineShape>(data);
        if (s.points.empty()) break;
        int32_t x0 = std::numeric_limits<int32_t>::max();
        int32_t y0 = std::numeric_limits<int32_t>::max();
        int32_t x1 = std::numeric_limits<int32_t>::min();
        int32_t y1 = std::numeric_limits<int32_t>::min();
        for (const auto& p : s.points) {
            x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
        }
        int32_t pad = base.strokeWidth > 0 ? base.strokeWidth / 2 + 1 : 1;
        r = {x0 - pad, y0 - pad, (x1 - x0) + pad * 2, (y1 - y0) + pad * 2};
        break;
    }
    case Type::Path: {
        const auto& s = std::get<PathShape>(data);
        if (s.steps.empty()) break;
        float fx0 = std::numeric_limits<float>::max();
        float fy0 = std::numeric_limits<float>::max();
        float fx1 = std::numeric_limits<float>::lowest();
        float fy1 = std::numeric_limits<float>::lowest();
        for (const auto& step : s.steps) {
            int n = 1; // MoveTo
            if (step.cmd == PathShape::Cmd::LineTo)      n = 1;
            else if (step.cmd == PathShape::Cmd::QuadTo)  n = 2;
            else if (step.cmd == PathShape::Cmd::CubicTo) n = 3;
            for (int i = 0; i < n; ++i) {
                fx0 = std::min(fx0, step.pts[i].x);
                fy0 = std::min(fy0, step.pts[i].y);
                fx1 = std::max(fx1, step.pts[i].x);
                fy1 = std::max(fy1, step.pts[i].y);
            }
        }
        int32_t ix0 = static_cast<int32_t>(std::floor(fx0));
        int32_t iy0 = static_cast<int32_t>(std::floor(fy0));
        int32_t ix1 = static_cast<int32_t>(std::ceil(fx1));
        int32_t iy1 = static_cast<int32_t>(std::ceil(fy1));
        int32_t pad = base.strokeWidth > 0 ? base.strokeWidth / 2 + 1 : 1;
        r = {ix0 - pad, iy0 - pad, (ix1 - ix0) + pad * 2, (iy1 - iy0) + pad * 2};
        break;
    }
    case Type::Text: {
        const auto& s = std::get<TextShape>(data);
        if (s.measured && !s.measuredBounds.isNull()) {
            r = s.measuredBounds;
        } else {
            // 粗略估计 — 后端应通过 measureText 提供真实值
            int32_t charW = std::max(1, static_cast<int32_t>(s.fontSize * 0.6f));
            int32_t h     = s.fontSize + 2;
            r = {s.x - 1, s.y - h, static_cast<int32_t>(s.text.size()) * charW + 2, h};
        }
        break;
    }
    case Type::Image: {
        const auto& s = std::get<ImageShape>(data);
        r = {s.x, s.y, s.w, s.h};
        break;
    }
    case Type::Svg: {
        // SVG 的解析由后端负责；Canvas 以布局矩形参与脏区计算
        const auto& s = std::get<SvgShape>(data);
        r = {s.x, s.y, s.w, s.h};
        break;
    }
    case Type::Blur: {
        // Blur 只影响自身矩形区域：采样卷积由后端实现，包围盒 = 几何矩形
        const auto& s = std::get<BlurShape>(data);
        r = {s.x, s.y, s.w, s.h};
        break;
    }
    case Type::Group: {
        const auto& g = std::get<GroupData>(data);
        Rect acc = Rect::null();
        for (const auto& child : g.children) {
            Rect cb = child.bounds();
            // 子节点局部坐标 + scrollOffset = Group 局部坐标系下的位置
            if (g.scrollOffset.x != 0 || g.scrollOffset.y != 0) {
                cb = cb.translated(g.scrollOffset);
            }
            if (!cb.isNull()) {
                acc = acc.isNull() ? cb : acc.unite(cb);
            }
        }
        // clipRect 与子节点同属 Group 局部坐标系：视口固定，scrollOffset 只移动内容
        if (g.clipRect.has_value() && !acc.isNull()) {
            acc = acc.intersect(g.clipRect.value());
        }
        r = acc;
        break;
    }
    } // switch

    cachedUnrotated_ = r;
    unrotatedCached_ = true;
    return r;
}

// 视觉包围盒 = 未旋转包围盒再按 rotation 扩展到旋转后的轴对齐包围盒
Rect Shape::bounds() const {
    if (boundsCached_) return cachedBounds_;

    const Rect r = unrotatedBounds();
    const Rect result =
        (r.isNull() || base.rotation == 0.0f) ? r : rotatedBounds(r, base.rotation);
    cachedBounds_ = result;
    boundsCached_ = true;
    return result;
}

bool Shape::intersectsRect(const Rect& rect) const {
    if (!rect.intersects(bounds())) return false;
    return true;
}

// ============================================================================
// Canvas — 构造 / 尺寸 / 背景
// ============================================================================

const Rect Canvas::kInfiniteRect = Rect{
    std::numeric_limits<int32_t>::min() / 2,
    std::numeric_limits<int32_t>::min() / 2,
    std::numeric_limits<int32_t>::max(),
    std::numeric_limits<int32_t>::max()
};

Canvas::Canvas(int32_t width, int32_t height)
    : width_(std::max<int32_t>(1, width)), height_(std::max<int32_t>(1, height)) {
    clipStack_.push_back(kInfiniteRect);
    markDirtyAll();
}

Canvas::~Canvas() = default;

void Canvas::resize(int32_t w, int32_t h) {
    width_  = std::max<int32_t>(1, w);
    height_ = std::max<int32_t>(1, h);
    markDirtyAll();
}

// ============================================================================
// Retain Mode 场景图管理
// ============================================================================

int32_t Canvas::findShapeIndex(uint32_t id) const {
    auto it = topLevelIndex_.find(id);
    if (it == topLevelIndex_.end()) return -1;
    const int32_t index = it->second;
    if (index < 0 || static_cast<size_t>(index) >= shapes_.size() ||
        shapes_[static_cast<size_t>(index)].base.id != id) {
        return -1;
    }
    return index;
}

void Canvas::reindexTopLevel(size_t from) {
    for (size_t i = from; i < shapes_.size(); ++i) {
        topLevelIndex_[shapes_[i].base.id] = static_cast<int32_t>(i);
    }
}

Shape* Canvas::findShape(uint32_t id) {
    int32_t idx = findShapeIndex(id);
    return idx >= 0 ? &shapes_[idx] : nullptr;
}

// --- 递归查找（包括 Group children）---
static Shape* findInGroup(std::vector<Shape>& children, uint32_t id) {
    for (auto& c : children) {
        if (c.base.id == id) return &c;
        if (c.type() == Shape::Type::Group) {
            auto& g = std::get<GroupData>(c.data);
            Shape* found = findInGroup(g.children, id);
            if (found) return found;
        }
    }
    return nullptr;
}
static const Shape* findInGroupConst(const std::vector<Shape>& children, uint32_t id) {
    for (const auto& c : children) {
        if (c.base.id == id) return &c;
        if (c.type() == Shape::Type::Group) {
            const auto& g = std::get<GroupData>(c.data);
            const Shape* found = findInGroupConst(g.children, id);
            if (found) return found;
        }
    }
    return nullptr;
}

Shape* Canvas::findShapeRecursive(uint32_t id) {
    Shape* top = findShape(id);
    if (top) return top;
    for (auto& shape : shapes_) {
        if (shape.type() == Shape::Type::Group) {
            auto& g = std::get<GroupData>(shape.data);
            Shape* found = findInGroup(g.children, id);
            if (found) return found;
        }
    }
    return nullptr;
}

const Shape* Canvas::findShapeRecursive(uint32_t id) const {
    int32_t idx = findShapeIndex(id);
    if (idx >= 0) return &shapes_[idx];
    for (const auto& shape : shapes_) {
        if (shape.type() == Shape::Type::Group) {
            const auto& g = std::get<GroupData>(shape.data);
            const Shape* found = findInGroupConst(g.children, id);
            if (found) return found;
        }
    }
    return nullptr;
}

// --- 一次遍历定位 + 祖先链（顶层 → 目标）---
static Shape* locateInChildren(std::vector<Shape>& children, uint32_t id,
                               std::vector<Shape*>& chain) {
    for (auto& child : children) {
        chain.push_back(&child);
        if (child.base.id == id) return &child;
        if (child.type() == Shape::Type::Group) {
            if (Shape* found = locateInChildren(std::get<GroupData>(child.data).children,
                                                id, chain)) {
                return found;
            }
        }
        chain.pop_back();
    }
    return nullptr;
}

Shape* Canvas::locateShape(uint32_t id, std::vector<Shape*>& chain) {
    chain.clear();
    int32_t idx = findShapeIndex(id);
    if (idx >= 0) {
        chain.push_back(&shapes_[static_cast<size_t>(idx)]);
        return chain.back();
    }
    for (auto& shape : shapes_) {
        chain.push_back(&shape);
        if (shape.type() == Shape::Type::Group) {
            if (Shape* found = locateInChildren(std::get<GroupData>(shape.data).children,
                                                id, chain)) {
                return found;
            }
        }
        chain.pop_back();
    }
    return nullptr;
}

Vec2i Canvas::chainOffset(const std::vector<Shape*>& chain) {
    Vec2i acc{0, 0};
    if (chain.size() < 2) return acc;
    // 最后一项是目标自身：只有祖先（Group）的 scrollOffset 参与累积
    for (size_t i = 0; i + 1 < chain.size(); ++i) {
        const Shape* node = chain[i];
        if (node && node->type() == Shape::Type::Group) {
            acc = acc + std::get<GroupData>(node->data).scrollOffset;
        }
    }
    return acc;
}

void Canvas::invalidateBoundsRecursive(Shape& shape) {
    shape.invalidateBoundsCache();
    if (shape.type() == Shape::Type::Group) {
        for (auto& child : std::get<GroupData>(shape.data).children) {
            invalidateBoundsRecursive(child);
        }
    }
}

// --- 递归分配 ID + 建立 parentMap_ ---
uint32_t Canvas::assignIdsRecursive(Shape& shape, uint32_t parentId) {
    uint32_t myId = nextShapeId_++;
    shape.base.id = myId;
    if (parentId != 0) {
        parentMap_[myId] = parentId;
    }
    if (shape.type() == Shape::Type::Group) {
        auto& g = std::get<GroupData>(shape.data);
        for (auto& child : g.children) {
            assignIdsRecursive(child, myId);
        }
    }
    return myId;
}

// --- 从 id 向上累积 scrollOffset 得到绝对偏移 ---
Vec2i Canvas::getAbsoluteOffset(uint32_t id) const {
    Vec2i acc{0, 0};
    uint32_t cur = id;
    while (true) {
        auto it = parentMap_.find(cur);
        if (it == parentMap_.end()) break;   // 顶层
        uint32_t parentId = it->second;
        const Shape* parent = findShapeRecursive(parentId);
        if (!parent || parent->type() != Shape::Type::Group) break;
        const auto& g = std::get<GroupData>(parent->data);
        acc = acc + g.scrollOffset;
        cur = parentId;
    }
    return acc;
}

uint32_t Canvas::addShape(Shape shape) {
    // Shape 可能来自其它 Canvas / 已测量过的副本：先丢弃旧的字形度量缓存。
    resetTextMetrics(shape);

    // 递归分配 ID + 建 parentMap_
    uint32_t id = assignIdsRecursive(shape, 0);

    topLevelIndex_[id] = static_cast<int32_t>(shapes_.size());
    shapes_.push_back(std::move(shape));

    // 先测量 Text（真实 bounds 参与脏区域计算）
    Shape& stored = shapes_.back();
    measureShapeTexts(stored);

    // 逐几何体脏记录 + 结构变化（上层需要重建镜像结构）
    structureDirty_ = true;
    allShapesDirty_ = false;
    noteShapeTouched(id);

    // 顶层节点：绝对坐标 = 局部坐标
    markDirty(stored.bounds());
    return id;
}

const Shape* Canvas::getShape(uint32_t id) const {
    return findShapeRecursive(id);
}

// updateShape 模板在 Canvas.h 中定义，它会：
//   1. findShapeRecursive 找 Shape
//   2. getAbsoluteOffset 算绝对偏移
//   3. 修改前 markDirty(旧绝对 bounds)
//   4. 执行 func 修改
//   5. 修改后 markDirty(新绝对 bounds)

bool Canvas::removeShape(uint32_t id) {
    // 一次遍历拿到目标与祖先链（祖先链用于删除后清空包围盒缓存）
    std::vector<Shape*>& chain = locateScratch_;
    Shape* target = locateShape(id, chain);
    if (!target) return false;

    Vec2i offset = getAbsoluteOffset(id);
    Rect oldBounds = target->absoluteBounds(offset);
    markDirty(oldBounds);

    // 清理 parentMap_ 中这个节点及其所有 descendants
    // 注意：顶层 Group 自身不在 parentMap_ 中，但它的 children 在，必须无条件下清理
    std::vector<uint32_t> toRemove{id};
    if (target->type() == Shape::Type::Group) {
        std::function<void(const std::vector<Shape>&)> collect = [&](const std::vector<Shape>& children) {
            for (const auto& c : children) {
                toRemove.push_back(c.base.id);
                if (c.type() == Shape::Type::Group) {
                    collect(std::get<GroupData>(c.data).children);
                }
            }
        };
        collect(std::get<GroupData>(target->data).children);
    }
    for (uint32_t rid : toRemove) parentMap_.erase(rid);

    const bool nested = chain.size() > 1;
    if (nested) {
        // 父 Group 的包围盒缓存必须作废（少了一个子节点）
        for (size_t i = 0; i + 1 < chain.size(); ++i) chain[i]->invalidateBoundsCache();
        Shape* parentGroup = chain[chain.size() - 2];
        auto& children = std::get<GroupData>(parentGroup->data).children;
        children.erase(std::remove_if(children.begin(), children.end(),
                                      [id](const Shape& child) { return child.base.id == id; }),
                       children.end());
        structureDirty_ = true;
        allShapesDirty_ = false;
        return true;
    }

    // 顶层
    int32_t idx = findShapeIndex(id);
    if (idx >= 0) {
        topLevelIndex_.erase(id);
        shapes_.erase(shapes_.begin() + idx);
        reindexTopLevel(static_cast<size_t>(idx));
        bumpDirtyStamp();   // 下标整体前移：旧的时间戳不再可信
        structureDirty_ = true;
        allShapesDirty_ = false;
        return true;
    }
    return false;
}

// ============================================================================
// Text Metric
// ============================================================================

void Canvas::measureTextShape(TextShape& ts) const {
    Rect measured = Rect::null();

    // 方式 1：IRenderer::measureText
    if (renderer_) {
        auto opt = renderer_->measureText(ts);
        if (opt.has_value()) {
            measured = opt.value();
        }
    }

    // 方式 2：setTextMetricProvider 回调（优先，因为用户显式设置）
    if (textMetricProvider_) {
        measured = textMetricProvider_(ts);
    }

    if (!measured.isNull()) {
        ts.measuredBounds = measured;
        ts.measured = true;
    }
}

bool Canvas::measureShapeTexts(Shape& shape) {
    bool measuredAny = false;
    if (shape.type() == Shape::Type::Text) {
        auto& ts = std::get<TextShape>(shape.data);
        if (!ts.measured) {
            measureTextShape(ts);
            if (ts.measured) {
                shape.invalidateBoundsCache();   // 真实尺寸替换了估算尺寸
                measuredAny = true;
            } else {
                // 测量失败（例如字体尚未加载）时保留挂起标记，下一帧重试
                textMeasurePending_ = true;
            }
        }
    } else if (shape.type() == Shape::Type::Group) {
        auto& g = std::get<GroupData>(shape.data);
        for (auto& child : g.children) {
            measuredAny = measureShapeTexts(child) || measuredAny;
        }
    }
    return measuredAny;
}

void Canvas::measureTexts() {
    // 没有任何测量源：不再逐帧空转，等 provider / renderer 变更时重新挂起
    if (!renderer_ && !textMetricProvider_) {
        textMeasurePending_ = false;
        return;
    }

    textMeasurePending_ = false;
    bool measuredAny = false;
    for (auto& shape : shapes_) {
        measuredAny = measureShapeTexts(shape) || measuredAny;
    }

    // 文本真实尺寸迟到（字体后加载 / provider 后注入）会改变包围盒：
    // 之前基于估算尺寸缓存的 Group 包围盒必须整体作废，并整屏标脏一次。
    if (measuredAny) {
        for (auto& shape : shapes_) invalidateBoundsRecursive(shape);
        markDirtyAll();
    }
}

void Canvas::resetTextMetrics(Shape& shape) {
    shape.invalidateBoundsCache();
    if (shape.type() == Shape::Type::Text) {
        auto& ts = std::get<TextShape>(shape.data);
        ts.measured = false;
        ts.measuredBounds = Rect::null();
    } else if (shape.type() == Shape::Type::Group) {
        auto& g = std::get<GroupData>(shape.data);
        for (auto& child : g.children) resetTextMetrics(child);
    }
}

void Canvas::invalidateTextMetrics() {
    for (auto& shape : shapes_) {
        resetTextMetrics(shape);
        invalidateBoundsRecursive(shape);
    }
    textMeasurePending_ = true;
    markDirtyAll();
}

namespace {

void registerSubtree(Shape& shape, uint32_t parentId,
                     std::unordered_map<uint32_t, uint32_t>& map) {
    map[shape.base.id] = parentId;
    if (shape.type() == Shape::Type::Group) {
        for (auto& child : std::get<GroupData>(shape.data).children) {
            registerSubtree(child, shape.base.id, map);
        }
    }
}

} // namespace

void Canvas::rebuildParentMap() {
    parentMap_.clear();
    for (auto& shape : shapes_) {
        if (shape.type() != Shape::Type::Group) continue;
        for (auto& child : std::get<GroupData>(shape.data).children) {
            registerSubtree(child, shape.base.id, parentMap_);
        }
    }
    parentMapRebuildPending_ = false;
}

void Canvas::clear() {
    shapes_.clear();
    topLevelIndex_.clear();
    parentMap_.clear();
    parentMapRebuildPending_ = false;
    dirtyShapeIds_.clear();
    dirtyShapeStamps_.clear();
    dirtyStamp_ = 1;
    structureDirty_ = true;
    allShapesDirty_ = true;
    invalidateTextMetrics();   // 度量缓存作废 + 整体标脏
}

void Canvas::noteShapeTouched(uint32_t id) {
    dirty_ = true;
    if (id == 0) {
        structureDirty_ = true;
        return;
    }

    // O(1) 去重：按顶层 index 记录"本轮已登记"的时间戳。
    // 动画帧里同一控件可能连续 updateShape 多次；用时间戳数组代替哈希表，
    // 稳定状态下的标脏不产生任何堆分配。
    const int32_t index = topLevelIndexOf(id);
    if (index < 0) {
        // 父映射待重建 / 刚被摘除：无法定位顶层节点，交给脏区域兜底
        dirtyShapeIds_.push_back(id);
        return;
    }

    if (dirtyShapeStamps_.size() < shapes_.size()) {
        dirtyShapeStamps_.resize(shapes_.size(), 0);
    }
    if (dirtyShapeStamps_[static_cast<size_t>(index)] == dirtyStamp_) return;
    dirtyShapeStamps_[static_cast<size_t>(index)] = dirtyStamp_;
    dirtyShapeIds_.push_back(id);
}

void Canvas::bumpDirtyStamp() {
    ++dirtyStamp_;
    if (dirtyStamp_ == 0) {
        // 回绕：清空全部时间戳，避免旧值与新一轮相同被误判为"已登记"
        dirtyStamp_ = 1;
        dirtyShapeStamps_.assign(dirtyShapeStamps_.size(), 0);
    }
}

int32_t Canvas::topLevelIndexOf(uint32_t id) const {
    uint32_t current = id;
    // 沿 parentMap_ 向上找到顶层节点（防御性上限，避免异常映射导致死循环）
    for (int guard = 0; guard < 1024; ++guard) {
        auto it = parentMap_.find(current);
        if (it == parentMap_.end()) break;
        current = it->second;
    }
    return findShapeIndex(current);
}

void Canvas::consumeDirty() {
    dirty_ = false;
    structureDirty_ = false;
    allShapesDirty_ = false;
    dirtyShapeIds_.clear();

    // 时间戳前进一位即可让全部旧登记失效（无需逐项清理）
    bumpDirtyStamp();

    // 帧内渲染脏区域也一并消费：子 Canvas 从不直接渲染，
    // 根 Canvas 在 drawFrame() 完成后调用本函数。
    dirtyRects_.clear();
    dirtyAll_ = false;
}

void Canvas::forEachDirtyShape(
    const std::function<void(size_t index, const Shape& shape)>& fn) {
    if (parentMapRebuildPending_) rebuildParentMap();
    if (!dirty_ || !fn) return;

    // 结构变化 / 整块失效：没有稳定的逐几何体对应关系，回退为全量
    if (structureDirty_ || allShapesDirty_) {
        for (size_t i = 0; i < shapes_.size(); ++i) fn(i, shapes_[i]);
        return;
    }

    // 复用去重数组（动画帧里每帧都会进入这里，避免反复分配）
    dirtyVisitScratch_.assign(shapes_.size(), uint8_t{0});

    // 1) 显式登记的几何体
    for (uint32_t id : dirtyShapeIds_) {
        const int32_t index = topLevelIndexOf(id);
        if (index < 0 || dirtyVisitScratch_[static_cast<size_t>(index)]) continue;
        dirtyVisitScratch_[static_cast<size_t>(index)] = 1;
        fn(static_cast<size_t>(index), shapes_[static_cast<size_t>(index)]);
    }

    // 2) 未被显式登记、但包围盒与脏区域相交的几何体
    if (dirtyRects_.empty()) return;
    for (size_t i = 0; i < shapes_.size(); ++i) {
        if (dirtyVisitScratch_[i]) continue;
        const Shape& shape = shapes_[i];
        bool hit = false;
        for (const Rect& dirty : dirtyRects_) {
            if (!dirty.isNull() && shape.intersectsRect(dirty)) { hit = true; break; }
        }
        if (hit) fn(i, shape);
    }
}

// ============================================================================
// Group 数据被整体替换后，为 id=0 的新节点补分配 ID，并刷新 parentMap_
// ============================================================================

bool Canvas::fixupChildIds(Shape& group, uint32_t parentId) {
    bool assigned = false;
    auto& g = std::get<GroupData>(group.data);
    for (auto& child : g.children) {
        if (child.base.id == 0) {
            // Group 用递归分配（同时处理它自己的 children）
            assignIdsRecursive(child, parentId);
            assigned = true;
        } else {
            parentMap_[child.base.id] = parentId;
            if (child.type() == Shape::Type::Group) {
                assigned = fixupChildIds(child, child.base.id) || assigned;
            }
        }
    }
    return assigned;
}

// ============================================================================
// Clip Stack（deprecated，保留 markDirty 裁剪用）
// ============================================================================

void Canvas::pushClip(const Rect& rect) {
    dirty_ = true;
    if (invalidateCallback_) invalidateCallback_();
    Rect oldClip = clipStack_.back();
    Rect newClip = oldClip.intersect(rect);
    if (newClip.isNull()) newClip = rect;
    clipStack_.push_back(newClip);

    if (!dirtyAll_) {
        Rect canvasRect{0, 0, width_, height_};
        Rect affected = oldClip.unite(newClip).intersect(canvasRect);
        if (!affected.isNull()) dirtyRects_.push_back(affected);
    }
}

void Canvas::popClip() {
    dirty_ = true;
    if (invalidateCallback_) invalidateCallback_();
    if (clipStack_.size() > 1) {
        Rect oldClip = clipStack_.back();
        clipStack_.pop_back();
        Rect newClip = clipStack_.back();
        if (!dirtyAll_) {
            Rect canvasRect{0, 0, width_, height_};
            Rect affected = oldClip.unite(newClip).intersect(canvasRect);
            if (!affected.isNull()) dirtyRects_.push_back(affected);
        }
    }
}

void Canvas::clearClip() {
    dirty_ = true;
    if (invalidateCallback_) invalidateCallback_();
    if (clipStack_.size() > 1) {
        Rect oldClip = clipStack_.back();
        clipStack_.clear();
        clipStack_.push_back(kInfiniteRect);
        if (!dirtyAll_) {
            Rect canvasRect{0, 0, width_, height_};
            if (!oldClip.isNull()) dirtyRects_.push_back(oldClip.intersect(canvasRect));
        }
    }
}

const Rect& Canvas::currentClip() const {
    return clipStack_.back();
}

// ============================================================================
// 脏区域标记
// ============================================================================
//
// dirtyRects_ 是"本帧待合并的脏矩形"。动画帧里同一个几何体常常连续标脏多次
// （旧包围盒 + 新包围盒 + 逐子项），因此：
//   1. 已经落在既有脏矩形内部的直接跳过（最常见的重复标记）；
//   2. 数量超过上限时合并成一个包围矩形，避免列表无限增长与后续 O(n²) 走查。

namespace {
constexpr size_t kMaxDirtyRects = 32;
}

void Canvas::markDirty(const Rect& rect) {
    dirty_ = true;
    if (invalidateCallback_) invalidateCallback_();
    if (rect.isNull() || dirtyAll_) return;

    Rect clipped = rect.intersect(Rect{0, 0, width_, height_});
    if (clipped.isNull()) return;

    // 兼容全局 clip stack（deprecated 但 markDirty 仍用它裁剪）
    clipped = clipped.intersect(currentClip());
    if (clipped.isNull()) return;

    // 已被既有脏矩形完全覆盖：无需重复记录
    for (const Rect& existing : dirtyRects_) {
        if (existing.contains(clipped.x, clipped.y) &&
            existing.contains(clipped.right() - 1, clipped.bottom() - 1)) {
            return;
        }
    }

    if (dirtyRects_.size() >= kMaxDirtyRects) {
        Rect merged = clipped;
        for (const Rect& existing : dirtyRects_) merged = merged.unite(existing);
        dirtyRects_.clear();
        dirtyRects_.push_back(merged);
        return;
    }

    dirtyRects_.push_back(clipped);
}

void Canvas::markDirtyAll() {
    dirty_ = true;
    allShapesDirty_ = true;
    dirtyAll_ = true;
    dirtyRects_.clear();
    dirtyRects_.push_back(Rect{0, 0, width_, height_});
    if (invalidateCallback_) invalidateCallback_();
}

void Canvas::markShapeDirty(uint32_t id) {
    const Shape* s = findShapeRecursive(id);
    if (!s) return;
    Vec2i offset = getAbsoluteOffset(id);
    markDirty(s->absoluteBounds(offset));
}

void Canvas::markShapeDirty(const Shape& shape) {
    // 只有绝对坐标未知时（没 id 或没 parentMap_ 记录）才用 local bounds
    markDirty(shape.bounds());
}

// ============================================================================
// 脏区域算法 — Tile 位图 + 贪心极大矩形提取
// ============================================================================
//
// 旧实现把每个脏矩形切成 Tile 后 sort + unique，再把"整行只有一条区间"的行
// 纵向合并。它对"同一行有多条区间"的区域（例如两列并排的脏块）依然会产生
// 每行一条的碎片。
//
// 新实现：
//   1. 按画布尺寸开一张 Tile 位图（1280×820 仅 130 字节），逐矩形标记；
//   2. 贪心提取极大矩形：从最上最左的未覆盖 Tile 出发，先向右扩展到本行
//      连续区间，再在"整段区间都脏"的前提下向下扩展 —— 得到零额外像素的
//      大矩形；每个矩形提取后标记已覆盖；
//   3. 预算内合并：把两个几乎相邻的矩形合成一个（额外像素 ≤ 预算比例），
//      直接减少 DrawCall / Clip 次数。预算由 setDirtyMergeBudget() 控制，
//      默认 0.25；设为 0 则严格不重绘任何额外像素。
//
// 热帧零排序、零堆分配（缓冲跨帧复用）。

namespace {
int64_t rectArea(const Rect& r) {
    return r.w > 0 && r.h > 0 ? static_cast<int64_t>(r.w) * r.h : 0;
}
} // namespace

const std::vector<Rect>& Canvas::buildTiles(const std::vector<Rect>& dirtyRects,
                                            int32_t tileSize, const Rect& canvasBounds) {
    std::vector<Rect>& out = tileMerged_;
    out.clear();
    if (tileSize <= 0 || canvasBounds.isNull()) return out;

    // 快路径：任意脏矩形覆盖整块画布 → 整屏
    for (const Rect& rect : dirtyRects) {
        if (rect.x <= canvasBounds.x && rect.y <= canvasBounds.y &&
            rect.right() >= canvasBounds.right() && rect.bottom() >= canvasBounds.bottom()) {
            out.push_back(canvasBounds);
            return out;
        }
    }

    const int32_t gridW = (canvasBounds.w + tileSize - 1) / tileSize;
    const int32_t gridH = (canvasBounds.h + tileSize - 1) / tileSize;
    if (gridW <= 0 || gridH <= 0) return out;

    // 防御：超大画布退化为整屏（位图不再经济）
    const size_t tileCount = static_cast<size_t>(gridW) * static_cast<size_t>(gridH);
    if (tileCount > (1u << 18)) {
        out.push_back(canvasBounds);
        return out;
    }

    const size_t words = (tileCount + 63) / 64;
    tileBits_.assign(words, 0ull);
    tileUsed_.assign(words, 0ull);

    auto setBit = [](std::vector<uint64_t>& bits, size_t index) {
        bits[index >> 6] |= (1ull << (index & 63));
    };
    auto testBit = [](const std::vector<uint64_t>& bits, size_t index) {
        return ((bits[index >> 6] >> (index & 63)) & 1ull) != 0;
    };
    auto tileIndex = [gridW](int32_t tx, int32_t ty) {
        return static_cast<size_t>(ty) * static_cast<size_t>(gridW) + static_cast<size_t>(tx);
    };

    for (const Rect& rect : dirtyRects) {
        const Rect r = rect.intersect(canvasBounds);
        if (r.isNull()) continue;
        const int32_t tx0 = (r.x - canvasBounds.x) / tileSize;
        const int32_t ty0 = (r.y - canvasBounds.y) / tileSize;
        const int32_t tx1 = (r.right() - 1 - canvasBounds.x) / tileSize;
        const int32_t ty1 = (r.bottom() - 1 - canvasBounds.y) / tileSize;
        for (int32_t ty = ty0; ty <= ty1; ++ty) {
            for (int32_t tx = tx0; tx <= tx1; ++tx) setBit(tileBits_, tileIndex(tx, ty));
        }
    }

    // 贪心极大矩形：零额外像素，但比"逐行区间"少得多的矩形数量
    for (int32_t ty = 0; ty < gridH; ++ty) {
        for (int32_t tx = 0; tx < gridW; ++tx) {
            const size_t anchor = tileIndex(tx, ty);
            if (!testBit(tileBits_, anchor) || testBit(tileUsed_, anchor)) continue;

            // 1) 向右扩展
            int32_t width = 0;
            while (tx + width < gridW) {
                const size_t index = tileIndex(tx + width, ty);
                if (!testBit(tileBits_, index) || testBit(tileUsed_, index)) break;
                ++width;
            }

            // 2) 向下扩展：整段宽度都必须脏且未覆盖
            int32_t height = 1;
            while (ty + height < gridH) {
                bool full = true;
                for (int32_t k = 0; k < width; ++k) {
                    const size_t index = tileIndex(tx + k, ty + height);
                    if (!testBit(tileBits_, index) || testBit(tileUsed_, index)) {
                        full = false;
                        break;
                    }
                }
                if (!full) break;
                ++height;
            }

            for (int32_t k = 0; k < height; ++k) {
                for (int32_t i = 0; i < width; ++i) {
                    setBit(tileUsed_, tileIndex(tx + i, ty + k));
                }
            }

            out.push_back(Rect{canvasBounds.x + tx * tileSize,
                               canvasBounds.y + ty * tileSize,
                               width * tileSize, height * tileSize});
        }
    }
    if (out.empty()) return out;

    // 边缘 Tile 收进画布范围（最后一行 / 列通常不是整 Tile）
    for (Rect& rect : out) rect = rect.intersect(canvasBounds);
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const Rect& r) { return r.isNull(); }), out.end());

    // 预算内合并 → 更少的矩形 / DrawCall / Clip
    mergeDirtyRects(out);
    return out;
}

void Canvas::mergeDirtyRects(std::vector<Rect>& rects) const {
    if (dirtyMergeBudget_ <= 0.0f || rects.size() < 2) return;
    // 矩形数量很多说明区域很碎，合并会带来大量额外像素，直接跳过
    if (rects.size() > 64) return;

    bool changed = true;
    while (changed && rects.size() > 1) {
        changed = false;

        float bestRatio = dirtyMergeBudget_;
        int64_t bestArea = 0;
        size_t bestI = 0, bestJ = 0;
        Rect bestUnion;

        for (size_t i = 0; i < rects.size(); ++i) {
            for (size_t j = i + 1; j < rects.size(); ++j) {
                const Rect united = rects[i].unite(rects[j]);
                const int64_t unionArea = rectArea(united);
                if (unionArea <= 0) continue;

                const int64_t covered = rectArea(rects[i]) + rectArea(rects[j]) -
                                        rectArea(rects[i].intersect(rects[j]));
                const int64_t extra = unionArea - covered;
                const float ratio = unionArea > 0
                    ? static_cast<float>(extra) / static_cast<float>(unionArea)
                    : 1.0f;
                if (ratio > bestRatio) continue;

                // 同等额外像素比例下，优先合并面积更大的一对（减少的矩形数相同，
                // 但大矩形更可能继续与邻近矩形合并）
                if (ratio < bestRatio || unionArea > bestArea) {
                    bestRatio = ratio;
                    bestArea = unionArea;
                    bestI = i;
                    bestJ = j;
                    bestUnion = united;
                }
            }
        }

        if (bestArea > 0) {
            rects[bestI] = bestUnion;
            rects.erase(rects.begin() + static_cast<ptrdiff_t>(bestJ));
            changed = true;
        }
    }
}

// ============================================================================
// buildDirtyEntries — 场景图展平 + 脏区域过滤
// ============================================================================
//
// 关键语义：
//   - Shape.bounds() 返回 Shape 自身坐标系的 AABB（局部坐标，带缓存）
//   - accumulatedOffset = 从画布根到当前节点的 scrollOffset 累积
//   - 叶子节点绝对坐标 bounds = bounds() + accumulatedOffset
//   - Group.clipRect（如果有）用绝对坐标裁剪
//   - 叶子 clip = 各级 Group clipRect 的绝对交集；区域过滤时再 ∩ region
//
// 性能：
//   1. 只展平与"本帧脏区并集"相交的子树（大场景小改动时接近 O(脏区)）；
//   2. 遍历顺序天然按 z 有序时跳过排序（z 默认全为 0，最常见）；
//   3. 展平缓冲跨帧复用，不再每帧重建 vector。

void Canvas::collectLeaves(const Shape& shape,
                           const Vec2i& parentOffset,
                           const Rect& parentClip,
                           const Rect& cullRect,
                           float parentOpacity,
                           std::vector<LeafEntry>& out,
                           bool& zOrdered,
                           int32_t& lastZ) {
    // Group / 叶子统一遵守 visible
    if (!shape.base.visible) return;

    const float opacity = parentOpacity *
        std::max(0.0f, std::min(1.0f, shape.base.opacity));
    if (opacity <= 0.0f) return;   // 完全透明：不参与绘制，也不参与脏区过滤

    if (shape.type() == Shape::Type::Group) {
        const auto& g = std::get<GroupData>(shape.data);

        // 1. 新的 accumulatedOffset = 父级 + Group 自身 scrollOffset
        //    子节点局部坐标 + newOffset = 画布根坐标系的绝对坐标
        Vec2i newOffset = parentOffset + g.scrollOffset;

        // 2. 新的 clip — clipRect 是 Group 局部坐标（视口本身不随滚动移动），
        //    转成绝对坐标只需加 parentOffset（不能加 scrollOffset）
        Rect newClip = parentClip;
        if (g.clipRect.has_value()) {
            newClip = newClip.intersect(g.clipRect.value().translated(parentOffset));
        }
        if (newClip.isNull()) return;

        // 3. 子树裁剪：整组包围盒（缓存）都不在脏区 → 整棵子树跳过
        const Rect groupAbs = shape.bounds().translated(parentOffset);
        if (groupAbs.isNull() || !groupAbs.intersects(cullRect) ||
            !groupAbs.intersects(newClip)) {
            return;
        }

        // 4. 递归子节点
        for (const auto& child : g.children) {
            collectLeaves(child, newOffset, newClip, cullRect, opacity, out, zOrdered, lastZ);
        }
        return;
    }

    // 叶子节点：bounds() 返回局部坐标，绝对坐标 = bounds() + accumulatedOffset
    Rect absoluteBounds = shape.bounds().translated(parentOffset);
    if (absoluteBounds.isNull() || !absoluteBounds.intersects(parentClip) ||
        !absoluteBounds.intersects(cullRect)) {
        return;
    }

    // 记录遍历顺序是否已经按 z 有序（是则省掉每帧排序）
    if (zOrdered) {
        const int32_t z = shape.base.z;
        if (z < lastZ) zOrdered = false;
        lastZ = z;
    }

    out.push_back(LeafEntry{&shape, parentOffset, parentClip, absoluteBounds, opacity});
}

void Canvas::buildDirtyEntries(const std::vector<Rect>& regions, std::vector<DirtyEntry>& out) {
    // 复用 out 的元素与内部 shapes 容量（跨帧保留，避免每帧重新分配）
    out.resize(regions.size());
    for (DirtyEntry& entry : out) entry.shapes.clear();
    if (regions.empty()) return;

    // 本帧脏区并集：作为展平阶段的子树裁剪范围
    Rect cullRect = Rect::null();
    for (const Rect& region : regions) cullRect = cullRect.unite(region);

    std::vector<LeafEntry>& leaves = leafScratch_;
    leaves.clear();
    bool zOrdered = true;
    int32_t lastZ = std::numeric_limits<int32_t>::min();
    for (const auto& shape : shapes_) {
        collectLeaves(shape, Vec2i{0, 0}, kInfiniteRect, cullRect, 1.0f,
                      leaves, zOrdered, lastZ);
    }
    if (leaves.empty()) {
        out.clear();
        return;
    }

    // z 乱序时才排序（stable：z 相同时保持场景图遍历顺序）
    const bool useOrder = !zOrdered;
    if (useOrder) {
        std::vector<uint32_t>& order = leafOrder_;
        order.resize(leaves.size());
        for (uint32_t k = 0; k < order.size(); ++k) order[k] = k;
        std::stable_sort(order.begin(), order.end(),
            [&leaves](uint32_t a, uint32_t b) {
                return leaves[a].shape->base.z < leaves[b].shape->base.z;
            });
    }

    for (size_t regionIndex = 0; regionIndex < regions.size(); ++regionIndex) {
        const Rect& region = regions[regionIndex];
        DirtyEntry& entry = out[regionIndex];
        entry.region = region;
        entry.shapes.reserve(std::min<size_t>(leaves.size(), 256));

        const auto emit = [&](const LeafEntry& leaf) {
            if (!leaf.bounds.intersects(region)) return;
            const Rect clip = leaf.clip.intersect(region);
            if (clip.isNull()) return;
            entry.shapes.push_back(DirtyShapeRef{leaf.shape, leaf.offset, clip, leaf.opacity});
        };
        if (useOrder) {
            for (uint32_t index : leafOrder_) emit(leaves[index]);
        } else {
            for (const LeafEntry& leaf : leaves) emit(leaf);
        }
    }
}

// ============================================================================
// Canvas::drawFrame — 主帧驱动
// ============================================================================

void Canvas::drawFrame() {
    // 结构变化后延迟重建一次映射（比每次 updateShape 擦除整棵子树更省）
    if (parentMapRebuildPending_) rebuildParentMap();

    // 度量失效后重测；measureTexts 内部会在仍有未测项时重新挂起。
    if (textMeasurePending_) measureTexts();

    if (!renderer_) {
        dirtyRects_.clear();
        dirtyAll_ = false;
        ++frameCounter_;
        return;
    }

    // --- 1. 归一化脏区域（不做两两合并，直接交给 Tile 去重 + 相邻合并）---
    std::vector<Rect> regions;
    Rect totalDirty = Rect::null();

    if (dirtyAll_) {
        // 整屏重绘必须覆盖整块画布；deprecated clip stack 不应截断它，
        // 否则 resize / 背景色变更后会留下未刷新的残留像素。
        regions.push_back(Rect{0, 0, width_, height_});
        dirtyRects_.clear();
    } else {
        regions.swap(dirtyRects_);
    }
    dirtyRects_.clear();
    dirtyAll_ = false;

    if (regions.empty()) {
        CanvasInfo info;
        info.width  = width_;
        info.height = height_;
        info.frame  = ++frameCounter_;
        info.backgroundColor = backgroundColor_;
        info.totalDirty = Rect::null();

        renderer_->beginFrame(info);
        renderer_->renderAll(info, {});
        renderer_->endFrame();
        return;
    }

    // --- 2. Tile 分块（位图 → 行区间 → 纵向合并；全屏收敛为少量大矩形）---
    Rect canvasBounds{0, 0, width_, height_};
    const std::vector<Rect>& tiles = buildTiles(regions, TILE_SIZE, canvasBounds);

    if (tiles.empty()) {
        dirtyAll_ = false;
        return;
    }

    // 实际重绘区域 = Tile 并集（Tile 会对齐网格并可能超出原始脏矩形，
    // 后端增量提交窗口必须覆盖这段区域，否则会留下未提交的像素）。
    totalDirty = Rect::null();
    for (const Rect& tile : tiles) totalDirty = totalDirty.unite(tile);

    // --- 3. 构建 DirtyEntries（只展平与脏区相交的子树 + 按区域过滤）---
    buildDirtyEntries(tiles, entryScratch_);

    // --- 4. 调用后端 ---
    CanvasInfo info;
    info.width  = width_;
    info.height = height_;
    info.frame  = ++frameCounter_;
    info.backgroundColor = backgroundColor_;
    info.totalDirty = totalDirty;

    renderer_->beginFrame(info);
    renderer_->renderAll(info, entryScratch_);
    renderer_->endFrame();
}

// ============================================================================
// ShapeBuilder — 提交到 Canvas
// ============================================================================

uint32_t ShapeBuilder::add() {
    return canvas_.addShape(std::move(shape_));
}

} // namespace modern_ui
