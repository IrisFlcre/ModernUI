// ============================================================================
// Canvas.h — 画布管理层（Retain Mode, 不实现渲染）
// ============================================================================
//
// 设计原则：
//   - Retain Mode：Canvas 持有完整场景图，按 ID 访问/修改，修改自动标脏。
//   - Shape 体系：std::variant 类型安全联合 + Group 递归嵌套（C++17 规则）。
//     内置几何体：Rect / RRect / Circle / Ellipse / Arc / Pie / Ring / Star /
//     Polygon / Line / Polyline / Path / Text / Image / Svg / Blur / Group。
//   - 角度约定：Arc / Pie / Star / Polygon 的角度均为“度”，0° 指向 +X，
//     顺时针为正（屏幕坐标 y 向下）；Star / Polygon 的 rotation = 0 时顶点朝上。
//   - Clip = 场景图属性：GroupData 可设置 clipRect（绝对坐标），
//     废弃全局 pushClip/popClip（保留但 deprecated）。
//   - Scroll Offset = 场景图属性：GroupData.scrollOffset 叠加在子节点坐标上。
//     Scroll Area = Group(clipRect={viewport}, scrollOffset={x,y})。
//   - 脏区域算法：收集 → 对齐 32×32 Tile 网格去重 → 相邻 Tile 合并回大矩形 →
//     场景图展平一次 → 按区域过滤命中 Shape（避免 O(tiles × shapes) 重复遍历）。
//   - Text Metric：后端通过回调测量 TextShape，Canvas 缓存 measuredBounds。
//   - Paint 体系：纯色 / 线性渐变 / 径向渐变，填充与描边统一描述。
//   - SVG：SvgShape 只携带 XML 原文 + 布局矩形；解析、矢量/位图缓存全部由后端实现。
//
// 后端需实现的接口（见 IRenderer 文档注释）：
//   - beginFrame() / endFrame()
//   - renderAll(CanvasInfo, DirtyEntry[]) / renderTile()
//   - measureText(TextShape) → Rect（可选；Canvas 提供 provider 回调方式）
//   - SvgShape 解析与缓存（后端自行管理，Canvas 不感知）
//   - BlurShape 采样 / 高斯卷积 / 结果缓存（后端自行管理；Canvas 只保证
//     该几何体在脏区域中的绘制顺序，以及 revision 反映底层内容是否变化）
//
// 坐标语义：
//   - Shape 坐标默认是画布根坐标系（屏幕绝对像素）。
//   - 子节点在 Group 内时，"屏幕绝对坐标 = Shape 坐标 + 累积 scrollOffset"。
//   - DirtyShapeRef 携带 accumulatedOffset，后端渲染时加到 Shape 坐标上。
//
// ============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace modern_ui {

// ============================================================================
// 基础数学 / 工具类型
// ============================================================================

template <typename T>
struct Vec2 {
    T x{}, y{};
    constexpr Vec2() = default;
    constexpr Vec2(T xv, T yv) : x(xv), y(yv) {}
    constexpr bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
};

using Vec2f = Vec2<float>;
using Vec2i = Vec2<int32_t>;

// AABB 矩形（整数坐标，像素对齐）
struct Rect {
    int32_t x = 0, y = 0;
    int32_t w = 0, h = 0;

    constexpr Rect() = default;
    constexpr Rect(int32_t px, int32_t py, int32_t pw, int32_t ph)
        : x(px), y(py), w(pw), h(ph) {}

    static constexpr Rect null() { return {0, 0, 0, 0}; }
    constexpr bool isNull() const { return w <= 0 || h <= 0; }
    constexpr int32_t left()   const { return x; }
    constexpr int32_t top()    const { return y; }
    constexpr int32_t right()  const { return x + w; }
    constexpr int32_t bottom() const { return y + h; }

    // 点是否在矩形内
    constexpr bool contains(int32_t px, int32_t py) const {
        return px >= x && px < right() && py >= y && py < bottom();
    }
    constexpr bool contains(const Vec2i& p) const { return contains(p.x, p.y); }

    // 与另一个矩形是否相交
    constexpr bool intersects(const Rect& o) const {
        return !(o.x >= right() || o.right() <= x || o.y >= bottom() || o.bottom() <= y);
    }

    // 与另一个矩形的交集（isNull 表示不相交）
    constexpr Rect intersect(const Rect& o) const {
        int32_t nx = x > o.x ? x : o.x;
        int32_t ny = y > o.y ? y : o.y;
        int32_t nr = right() < o.right() ? right() : o.right();
        int32_t nb = bottom() < o.bottom() ? bottom() : o.bottom();
        int32_t nw = nr - nx;
        int32_t nh = nb - ny;
        return nw > 0 && nh > 0 ? Rect{nx, ny, nw, nh} : Rect::null();
    }

    // 与另一个矩形的并集（包围两者的最小矩形）
    constexpr Rect unite(const Rect& o) const {
        if (isNull()) return o;
        if (o.isNull()) return *this;
        int32_t nx = x < o.x ? x : o.x;
        int32_t ny = y < o.y ? y : o.y;
        int32_t nr = right() > o.right() ? right() : o.right();
        int32_t nb = bottom() > o.bottom() ? bottom() : o.bottom();
        return {nx, ny, nr - nx, nb - ny};
    }

    // 扩展矩形（负值 = 收缩）
    constexpr Rect inflate(int32_t dx, int32_t dy) const {
        return {x - dx, y - dy, w + 2 * dx, h + 2 * dy};
    }

    // 平移（加上偏移量）
    constexpr Rect translated(int32_t dx, int32_t dy) const {
        return {x + dx, y + dy, w, h};
    }
    constexpr Rect translated(const Vec2i& v) const { return translated(v.x, v.y); }

    constexpr bool operator==(const Rect& o) const {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
};

// RGBA 颜色（0-255）
struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    constexpr Color() = default;
    constexpr Color(uint8_t rv, uint8_t gv, uint8_t bv, uint8_t av = 255)
        : r(rv), g(gv), b(bv), a(av) {}
    static constexpr Color white()   { return {255, 255, 255, 255}; }
    static constexpr Color black()   { return {  0,   0,   0, 255}; }
    static constexpr Color nullColor() { return {0, 0, 0, 0}; }
};

// ============================================================================
// Paint — 纯色 / 线性渐变 / 径向渐变
// ============================================================================
//
// 渐变坐标全部位于 Shape 自身坐标系（与几何坐标同一空间）。
// Canvas 只描述"画什么"，具体的 BLGradient / 纹理 / 光栅缓存由后端实现。

struct GradientStop {
    float offset = 0.0f;    // [0, 1]
    Color color;
};

enum class SpreadMethod : uint8_t {
    Pad = 0,        // 超出范围取端点色
    Reflect,        // 镜像重复
    Repeat          // 平铺重复
};

struct LinearGradient {
    Vec2f from{0.0f, 0.0f};
    Vec2f to{0.0f, 0.0f};
    std::vector<GradientStop> stops;
    SpreadMethod spread = SpreadMethod::Pad;
};

struct RadialGradient {
    Vec2f center{0.0f, 0.0f};
    float radius = 0.0f;
    Vec2f focal{0.0f, 0.0f};   // 与 center 重合 = 无焦点偏移
    std::vector<GradientStop> stops;
    SpreadMethod spread = SpreadMethod::Pad;
};

enum class PaintType : uint8_t {
    None = 0,
    Solid,
    LinearGradient,
    RadialGradient
};

// Paint 同时用于 fill / stroke；默认 None 表示不绘制。
// Color 可隐式转换为 Paint，保持既有写法：shape.base.fill = Color{...};
struct Paint {
    std::variant<std::monostate, Color, LinearGradient, RadialGradient> data;

    Paint() = default;
    Paint(Color color) : data(color) {}
    Paint(LinearGradient gradient) : data(std::move(gradient)) {}
    Paint(RadialGradient gradient) : data(std::move(gradient)) {}

    static Paint solid(Color color) { return Paint(color); }
    static Paint linear(LinearGradient gradient) { return Paint(std::move(gradient)); }
    static Paint radial(RadialGradient gradient) { return Paint(std::move(gradient)); }

    PaintType type() const {
        switch (data.index()) {
        case 0: return PaintType::None;
        case 1: return PaintType::Solid;
        case 2: return PaintType::LinearGradient;
        default: return PaintType::RadialGradient;
        }
    }

    bool isNone() const { return type() == PaintType::None; }
    bool isSolid() const { return type() == PaintType::Solid; }
    bool isGradient() const {
        const PaintType t = type();
        return t == PaintType::LinearGradient || t == PaintType::RadialGradient;
    }

    const Color* solidColor() const { return std::get_if<Color>(&data); }
    const LinearGradient* linearGradient() const { return std::get_if<LinearGradient>(&data); }
    const RadialGradient* radialGradient() const { return std::get_if<RadialGradient>(&data); }

    // 完全透明（不产生任何像素）：None / alpha=0 纯色 / 所有 stop alpha=0
    bool isTransparent() const {
        if (const Color* color = solidColor()) return color->a == 0;
        const std::vector<GradientStop>* stops = nullptr;
        if (const LinearGradient* g = linearGradient()) stops = &g->stops;
        else if (const RadialGradient* g = radialGradient()) stops = &g->stops;
        if (!stops || stops->empty()) return true;
        for (const GradientStop& stop : *stops) {
            if (stop.color.a > 0) return false;
        }
        return true;
    }

    // 是否有可见输出（None 或完全透明都不算）
    bool isVisible() const { return !isNone() && !isTransparent(); }
};

// ============================================================================
// Shape 体系
// ============================================================================

// 所有 Shape 共享的属性
struct ShapeBase {
    uint32_t id = 0;                // 场景图唯一标识（Canvas 分配）
    int32_t  z  = 0;                // 绘制顺序（小 → 先画）
    bool     visible = true;
    bool     opaque  = false;       // 是否完全不透明（脏区域优化用）
    // 逐形状不透明度 [0, 1]。Group 上设置时向下累积（父 × 子），
    // 后端只需一次 global-alpha 设置即可实现整块淡入淡出。
    float    opacity = 1.0f;
    // 内容版本：Canvas 在 addShape / updateShape 时递增。
    // 后端可用它判断"某个几何体的内容是否真的变化"，避免对像素做全量比对
    // （Blur 几何体的底层内容缓存失效就依赖这个字段）。
    uint32_t revision = 0;
    // 旋转角度（度，顺时针为正，围绕自身轴对齐包围盒中心）。
    // Group 不直接旋转（子节点已展平），需要旋转请对叶子设置。
    float    rotation = 0.0f;
    Paint    fill;
    Paint    stroke;
    int32_t  strokeWidth = 0;
};

// --- 叶子 Shape 数据结构 ---

struct RectShape {
    int32_t x = 0, y = 0, w = 0, h = 0;
};

struct RRectShape {
    int32_t x = 0, y = 0, w = 0, h = 0;
    float   radiusX = 0.0f, radiusY = 0.0f;
};

struct CircleShape {
    int32_t cx = 0, cy = 0;
    float   r = 0.0f;
};

struct EllipseShape {
    int32_t cx = 0, cy = 0;
    float   rx = 0.0f, ry = 0.0f;
};

// 圆弧（开口，仅描边；fill 作为 stroke 的备用画刷）
// 角度单位：度；0° = +X，顺时针为正。
struct ArcShape {
    int32_t cx = 0, cy = 0;
    float   rx = 0.0f, ry = 0.0f;
    float   startAngle = 0.0f;
    float   sweepAngle = 0.0f;
};

// 扇形 / 饼图（从圆心到圆弧两端闭合，可填充可描边）
struct PieShape {
    int32_t cx = 0, cy = 0;
    float   rx = 0.0f, ry = 0.0f;
    float   startAngle = 0.0f;
    float   sweepAngle = 0.0f;
};

// 圆环 / 进度环（偶奇填充，中间镂空）
// thickness <= 0 时退化为实心椭圆；thickness >= min(rx, ry) 时同样为实心。
struct RingShape {
    int32_t cx = 0, cy = 0;
    float   rx = 0.0f, ry = 0.0f;
    float   thickness = 0.0f;
};

// 多角星（评分 / 徽标）：rotation = 0 时第一个顶点朝上
struct StarShape {
    int32_t cx = 0, cy = 0;
    float   outerRadius = 0.0f;
    float   innerRadius = 0.0f;
    int32_t points = 5;               // >= 3
    float   rotation = 0.0f;          // 度
};

// 正多边形（徽标 / 图标底）：rotation = 0 时顶点朝上
struct PolygonShape {
    int32_t cx = 0, cy = 0;
    float   radius = 0.0f;
    int32_t sides = 6;                // >= 3
    float   rotation = 0.0f;          // 度
};

struct LineShape {
    int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

struct PolylineShape {
    std::vector<Vec2i> points;
    bool               closed = false;   // true = Polygon
};

struct PathShape {
    enum class Cmd : uint8_t { MoveTo, LineTo, QuadTo, CubicTo, Close };
    struct Step {
        Cmd                  cmd = Cmd::MoveTo;
        std::array<Vec2f, 3> pts{};
    };
    std::vector<Step> steps;
};

struct TextShape {
    std::string text;
    int32_t     x = 0, y = 0;       // 基线位置
    int32_t     fontSize = 14;
    // 后端需提供字体测量；Canvas 缓存 measuredBounds
    mutable Rect measuredBounds;    // 初始为 null，后端测量后填入
    mutable bool measured = false;
};

// 位图 / Sprite 帧
// srcW/srcH > 0 时只绘制纹理内的源矩形（Sprite 图集）；否则绘制整张纹理。
// 源矩形与目标矩形尺寸不同时会等比缩放（后端 blit_image 缩放）。
struct ImageShape {
    uint64_t  handle = 0;           // 后端纹理句柄
    int32_t   x = 0, y = 0, w = 0, h = 0;
    int32_t   srcX = 0, srcY = 0;   // 源矩形左上角（图集像素坐标）
    int32_t   srcW = 0, srcH = 0;   // 0 = 使用整张纹理
    bool      flipY = false;

    bool hasSourceRect() const { return srcW > 0 && srcH > 0; }
};

// SVG 矢量图 — Canvas 只提供原始数据与布局矩形。
// 解析、路径构建、渐变转换、缓存与光栅化全部由渲染后端完成。
struct SvgShape {
    std::string data;                    // SVG XML 原文（UTF-8）
    int32_t     x = 0, y = 0, w = 0, h = 0;  // 布局矩形（Shape 本地坐标）
    bool        preserveAspectRatio = true;  // true = 等比缩放并居中；false = 拉伸填满
};

// 背景模糊 / Blur Behind — Canvas 只描述"模糊哪块区域、多强、什么圆角"。
// 采样、卷积、缓存与失效全部由渲染后端实现：
//   - 后端把该 Shape 之前（z 更低、先绘制）已经落在目标表面上的像素当作底图，
//     做高斯模糊后覆盖回 Shape 区域（backdrop filter 语义，等价于 CSS blur()）。
//   - 底图内容变化（下层几何体新增 / 删除 / 修改）时缓存必须重建；
//     ShapeBase.revision 为后端提供了 O(1) 的"内容是否变化"依据。
//   - radius 为高斯标准差（像素，<= 0 表示不绘制，底层内容保持原样）。
//   - cornerRadius 用于把模糊区域裁成圆角（弹窗 / 卡片与背景圆角一致）。
//   - fill / stroke / rotation 对 Blur 无效。
struct BlurShape {
    int32_t x = 0, y = 0, w = 0, h = 0;
    float   radius = 16.0f;      // 高斯 σ（像素）
    float   cornerRadius = 0.0f; // 圆角半径（0 = 直角）
};

// 前向声明 — GroupData 需要 vector<Shape>，C++17 允许不完整类型
struct Shape;

struct GroupData {
    std::vector<Shape> children;

    // Clip — 子节点可见区域（Group 局部坐标系，不含本组 scrollOffset）
    // nullopt = 不裁剪
    // 有值 = 视口固定在 Group 局部坐标系中；子节点先叠加 scrollOffset 再被裁剪。
    //        渲染时绝对裁剪区 = clipRect + 父级累积 scrollOffset。
    // Scroll Area 的用法：clipRect = {0, 0, viewportW, viewportH}
    std::optional<Rect> clipRect;

    // Scroll Offset — 子节点整体偏移
    // "屏幕绝对坐标 = 子节点局部坐标 + 累积 scrollOffset 链"
    // 向下滚 100px → scrollOffset.y = -100（内容向上移动）
    Vec2i scrollOffset{0, 0};
};

// --- Shape 完整定义 ---
struct Shape {
    ShapeBase base;
    std::variant<
        RectShape,
        RRectShape,
        CircleShape,
        EllipseShape,
        ArcShape,
        PieShape,
        RingShape,
        StarShape,
        PolygonShape,
        LineShape,
        PolylineShape,
        PathShape,
        TextShape,
        ImageShape,
        SvgShape,
        BlurShape,
        GroupData
    > data;

    enum class Type : uint8_t {
        Rect = 0, RRect, Circle, Ellipse, Arc, Pie, Ring, Star, Polygon,
        Line, Polyline, Path, Text, Image, Svg, Blur, Group
    };
    Type type() const { return static_cast<Type>(data.index()); }

    // 轴对齐包围盒（Shape 自身坐标系，不考虑 Group 累积的 scrollOffset）。
    // 已包含 rotation：返回旋转后的轴对齐包围盒，供脏区 / 裁剪使用。
    // 结果按 (id, 几何数据) 缓存：Canvas 在每次 updateShape 时清空自身与祖先的缓存，
    // 常规动画帧里每帧只算一次，不会重复遍历 Path / Group 子树。
    Rect bounds() const;

    // 未旋转的轴对齐包围盒（渲染时用于求旋转中心）
    Rect unrotatedBounds() const;

    // 子节点在屏幕上的绝对包围盒 = bounds() + accumulatedOffset
    Rect absoluteBounds(const Vec2i& accumulatedOffset) const {
        Rect b = bounds();
        if (accumulatedOffset.x == 0 && accumulatedOffset.y == 0) return b;
        return b.translated(accumulatedOffset);
    }

    // 是否与 rect 相交（粗剪）
    bool intersectsRect(const Rect& rect) const;

    // --- 包围盒缓存（由 Canvas 维护；外部拷贝 Shape 时随数据一起复制）---
    mutable Rect cachedUnrotated_{};
    mutable Rect cachedBounds_{};
    mutable bool unrotatedCached_ = false;
    mutable bool boundsCached_ = false;

    void invalidateBoundsCache() const { unrotatedCached_ = false; boundsCached_ = false; }
};

// ============================================================================
// ShapeBuilder — Canvas 形状创建的链式语法糖
// ============================================================================
//
//   const uint32_t cardId = canvas.rrect(16, 16, 240, 120, 12.0f)
//                               .fill(Color{32, 32, 36, 255})
//                               .stroke(Color{64, 64, 72, 255}, 1)
//                               .z(1)
//                               .add();
//
// add() 提交到 Canvas 并返回分配的 id；所有配置方法都返回自身引用。

class Canvas;

class ShapeBuilder {
public:
    ShapeBuilder(Canvas& canvas, Shape shape) : canvas_(canvas), shape_(std::move(shape)) {}

    ShapeBuilder& fill(Color color) {
        shape_.base.fill = Paint(color);
        return *this;
    }
    ShapeBuilder& fill(LinearGradient gradient) {
        shape_.base.fill = Paint(std::move(gradient));
        return *this;
    }
    ShapeBuilder& fill(RadialGradient gradient) {
        shape_.base.fill = Paint(std::move(gradient));
        return *this;
    }
    ShapeBuilder& stroke(Color color, int32_t width = 1) {
        shape_.base.stroke = Paint(color);
        shape_.base.strokeWidth = width;
        return *this;
    }
    ShapeBuilder& stroke(LinearGradient gradient, int32_t width = 1) {
        shape_.base.stroke = Paint(std::move(gradient));
        shape_.base.strokeWidth = width;
        return *this;
    }
    ShapeBuilder& stroke(RadialGradient gradient, int32_t width = 1) {
        shape_.base.stroke = Paint(std::move(gradient));
        shape_.base.strokeWidth = width;
        return *this;
    }
    ShapeBuilder& z(int32_t value) {
        shape_.base.z = value;
        return *this;
    }
    ShapeBuilder& opacity(float value) {
        shape_.base.opacity = value;
        return *this;
    }
    ShapeBuilder& rotate(float degrees) {
        shape_.base.rotation = degrees;
        return *this;
    }
    ShapeBuilder& visible(bool value) {
        shape_.base.visible = value;
        return *this;
    }
    ShapeBuilder& opaque(bool value) {
        shape_.base.opaque = value;
        return *this;
    }
    // 直接改写几何数据（例如在通用构建器上套用自定义半径）
    template <typename T>
    ShapeBuilder& data(T value) {
        shape_.data = std::move(value);
        return *this;
    }
    // Group：追加子节点
    ShapeBuilder& child(Shape child) {
        if (shape_.type() != Shape::Type::Group) shape_.data = GroupData{};
        std::get<GroupData>(shape_.data).children.push_back(std::move(child));
        return *this;
    }
    ShapeBuilder& clipRect(Rect rect) {
        if (shape_.type() != Shape::Type::Group) shape_.data = GroupData{};
        std::get<GroupData>(shape_.data).clipRect = rect;
        return *this;
    }
    ShapeBuilder& scrollOffset(Vec2i offset) {
        if (shape_.type() != Shape::Type::Group) shape_.data = GroupData{};
        std::get<GroupData>(shape_.data).scrollOffset = offset;
        return *this;
    }

    Shape& shape() { return shape_; }
    const Shape& shape() const { return shape_; }

    // 提交到 Canvas，返回分配的 id
    uint32_t add();
    // 只构建不提交（等价于 shape()）
    Shape build() { return std::move(shape_); }

private:
    Canvas& canvas_;
    Shape shape_;
};

// ============================================================================
// DirtyShapeRef — 后端渲染时拿的 Shape 引用
// ============================================================================

struct DirtyShapeRef {
    const Shape* shape;
    Vec2i        accumulatedOffset;   // Shape 坐标 + 这个 = 屏幕绝对坐标
    Rect         clipRect;            // 绝对坐标下的最终裁剪区（Tile ∩ 各级 Group clipRect）
    float        opacity = 1.0f;      // 各级 Group 与 Shape 自身的不透明度乘积
};

// ============================================================================
// 脏区域条目
// ============================================================================

struct DirtyEntry {
    Rect                        region;   // 需要重绘的区域（绝对坐标；可能由多个相邻 Tile 合并而来）
    std::vector<DirtyShapeRef> shapes;    // 命中此区域的 Shape（带绝对坐标偏移，已按 z 排序）
};

// Canvas 传给后端的每一帧信息
struct CanvasInfo {
    int32_t width  = 0;
    int32_t height = 0;
    uint64_t frame = 0;
    Color    backgroundColor = Color::black();
    Rect     totalDirty;            // 本帧所有脏区域的总并集（绝对坐标）
};

// ============================================================================
// IRenderer — 后端需实现的接口
// ============================================================================
//
// 后端必须实现：
//   1. beginFrame(CanvasInfo)   — 每帧开始（清屏 / 设置 viewport / 等）
//   2. endFrame()               — 每帧结束（交换缓冲区 / 提交命令 / 等）
//
//   后端可选择实现方案 A 或 B：
//   A. renderTile(info, DirtyEntry) — 逐条渲染（简单）
//   B. renderAll(info, entries[])   — 批量渲染（可做批处理优化）
//      renderAll 默认调用 renderTile，覆盖其一即可。
//
//   后端可选实现：
//   - measureText(TextShape) → Rect — 字形测量
//     如果提供，Canvas 在 drawFrame 前会调用它填充 TextShape.measuredBounds。
//     否则 Canvas 用 fontSize*0.6 的粗略估计。
//
// 后端负责的资源管理：
//   - 字体加载 / 缓存（TextShape 只传 fontSize + text）
//   - 图片纹理上传 / 缓存（ImageShape 通过 handle 关联）
//   - GPU Buffer / 命令队列生命周期
//
// 后端渲染注意事项：
//   - DirtyShapeRef 携带 accumulatedOffset，Shape 的 x/y 坐标需要加上这个偏移
//   - DirtyShapeRef.clipRect 是绝对坐标下的最终裁剪区（Tile ∩ Group clipRect），
//     后端必须在绘制该 Shape 时裁剪，否则 Group clip 会失效
//   - Group.clipRect 的过滤在 Canvas 侧完成，但裁剪区域仍需后端按 clipRect 应用
//   - Tile 本身是 clip 后的结果，后端可以直接渲染 Tile.region 内的内容
// ============================================================================

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual void beginFrame(const CanvasInfo& info) = 0;
    virtual void endFrame() = 0;

    // 渲染方案 A：逐条
    virtual void renderTile(const CanvasInfo& /*info*/, const DirtyEntry& /*entry*/) {}

    // 渲染方案 B：批量（可覆盖，默认逐条转发）
    virtual void renderAll(const CanvasInfo& info, const std::vector<DirtyEntry>& entries) {
        (void)info;
        for (const auto& e : entries) renderTile(info, e);
    }

    // 可选：字形测量 — 返回 TextShape 的真实 bounds（基线左上角或包含字形的 AABB）
    // 返回 nullopt 表示 Canvas 使用默认估计
    virtual std::optional<Rect> measureText(const TextShape& /*text*/) { return std::nullopt; }

    // 可选：从磁盘加载资源。
    // 支持的后端返回非 0 句柄 / true；默认实现返回失败，Canvas 的
    // imageFile() / svgFile() 语法糖据此退化为"空白几何体"（不绘制）。
    virtual uint64_t loadImageFile(const std::string& /*path*/, std::string* /*error*/) { return 0; }
    virtual bool imageSize(uint64_t /*handle*/, int32_t& /*width*/, int32_t& /*height*/) const {
        return false;
    }
    // 读取 SVG 文件原文（解析与缓存由后端负责）
    virtual bool loadSvgFile(const std::string& /*path*/, std::string& /*out*/, std::string* /*error*/) {
        return false;
    }
};

// ============================================================================
// Canvas — 主类（Retain Mode）
// ============================================================================

class Canvas {
public:
    static constexpr int32_t TILE_SIZE = 32;

    Canvas(int32_t width, int32_t height);
    ~Canvas();

    // --- 后端绑定 ---
    // 切换后端会让已缓存的 Text 度量失效，并整屏标脏。
    void setRenderer(IRenderer* r) {
        if (renderer_ == r) return;
        renderer_ = r;
        invalidateTextMetrics();
    }
    IRenderer* renderer() const    { return renderer_; }

    // --- 尺寸 ---
    int32_t width()  const { return width_; }
    int32_t height() const { return height_; }
    void resize(int32_t w, int32_t h);

    // --- 背景 ---
    void setBackgroundColor(Color c) { backgroundColor_ = c; markDirtyAll(); }
    Color backgroundColor() const    { return backgroundColor_; }

    // --- 内容脏标记（跨帧保留，供上层组合器使用）---
    //
    // 语义（重要）：
    //   isDirty() 只表示"这个 Canvas 里存在至少一个脏几何体"，
    //   **不表示整个 Canvas 的内容都脏**。上层拿到 true 之后，
    //   应当进入这个 Canvas 按"逐几何体 + 脏区域"的方式增量处理：
    //       if (canvas.isDirty()) {
    //           if (canvas.structureDirty() || canvas.allShapesDirty()) 重建结构;
    //           else canvas.forEachDirtyShape([](size_t index, const Shape& s){ ... });
    //           canvas.consumeDirty();
    //       }
    //
    //   structureDirty()   —— 几何体被增 / 删（层级结构变化，需要重建镜像结构）
    //   allShapesDirty()   —— 整块内容失效（resize / 背景色 / 度量失效 / 清空）
    //   forEachDirtyShape()—— 逐几何体遍历脏项（显式登记的几何体 + 脏区域 ∩ 包围盒）
    //   consumeDirty()     —— 上层处理完毕：清空上述标记与帧内脏区域
    bool isDirty() const { return dirty_; }
    bool structureDirty() const { return structureDirty_; }
    bool allShapesDirty() const { return allShapesDirty_; }
    void consumeDirty();
    void forEachDirtyShape(const std::function<void(size_t index, const Shape& shape)>& fn);

    // ========================================================================
    // Retain Mode 场景图管理
    // ========================================================================

    // 添加 Shape，返回分配的唯一 id（Canvas 持有副本）
    uint32_t addShape(Shape shape);

    // 只读访问（不触发脏区域）
    const Shape* getShape(uint32_t id) const;

    // 只读访问顶层 Shape 列表（供上层模块合成 / 调试；不触发脏区域）
    const std::vector<Shape>& shapes() const { return shapes_; }
    size_t shapeCount() const { return shapes_.size(); }

    // 清空全部几何体（删除几何体前的显式释放，整体标脏）
    void clear();

    // 便捷：修改 Shape（自动 markDirty 修改前 bounds + 修改后 bounds）
    // 用法：canvas.updateShape(id, [](Shape& s) { ...修改 s.data... });
    // 支持顶层 Shape + Group 内的嵌套 Shape
    template <typename F>
    void updateShape(uint32_t id, F&& func) {
        // 一次遍历同时拿到目标 Shape 与它的祖先链：
        // 祖先链用于 (a) 增量算绝对偏移 (b) 修改后清空整条链的包围盒缓存
        std::vector<Shape*>& chain = locateScratch_;
        Shape* s = locateShape(id, chain);
        if (!s) return;
        Vec2i oldOffset = chainOffset(chain);
        Rect oldAbsolute = s->absoluteBounds(oldOffset);

        const bool wasGroup = (s->type() == Shape::Type::Group);
        const size_t oldChildCount =
            wasGroup ? std::get<GroupData>(s->data).children.size() : 0;

        func(*s);
        ++s->base.revision;   // 内容版本 +1（Blur 缓存等后端判断"底层是否变化"）

        // 几何 / 文本 / 结构都可能已变：清空自身与祖先的包围盒缓存
        for (Shape* node : chain) node->invalidateBoundsCache();

        // 逐几何体脏记录（含"整体结构是否变化"）
        noteShapeTouched(id);

        const bool isGroup = (s->type() == Shape::Type::Group);
        const size_t newChildCount =
            isGroup ? std::get<GroupData>(s->data).children.size() : 0;

        // 合成器只看"子节点增删 / 类型变化"；纯数据替换（id 归一化后重新分配）
        // 不应触发上层重建镜像结构。
        if (wasGroup != isGroup || (wasGroup && oldChildCount != newChildCount)) {
            structureDirty_ = true;
        }

        // 一次遍历：给新节点补 ID 并刷新 parentMap_（返回"是否有新节点"）。
        // 旧实现先算两次子树哈希再决定是否修复，Group 包装器每帧要遍历两遍；
        // 现在常规更新只遍历一次，且结构未变时完全不动映射表。
        if (isGroup) {
            const bool assignedIds = fixupChildIds(*s, id);
            if (assignedIds || wasGroup != isGroup) parentMapRebuildPending_ = true;
        } else if (wasGroup) {
            // Group → 叶子：旧后代映射延迟清理
            parentMapRebuildPending_ = true;
        }

        // Text 修改后必须重新测量，且要在计算新脏区之前完成
        if (s->type() == Shape::Type::Text) {
            auto& ts = std::get<TextShape>(s->data);
            ts.measured = false;
            ts.measuredBounds = Rect::null();
            s->invalidateBoundsCache();
        }
        measureShapeTexts(*s);

        // Group 的 scrollOffset 可能变了，偏移需要重新计算（祖先链已就绪，无需再走 parentMap）
        Vec2i newOffset = chainOffset(chain);
        Rect newAbsolute = s->absoluteBounds(newOffset);

        markDirty(oldAbsolute);
        markDirty(newAbsolute);
    }

    // ========================================================================
    // 形状语法糖（链式）
    // ========================================================================
    //
    //   canvas.rect(0, 0, 200, 80).fill(Color{40, 40, 48}).add();
    //   canvas.circle(cx, cy, 6.0f).fill(accent).add();
    //   canvas.text("Hello", x, y, 14).fill(Color::white()).add();
    //
    // 每个方法都返回 ShapeBuilder；调用 add() 时提交并返回 id。

    ShapeBuilder rect(int32_t x, int32_t y, int32_t w, int32_t h) {
        Shape shape;
        shape.data = RectShape{x, y, w, h};
        return ShapeBuilder(*this, std::move(shape));
    }
    ShapeBuilder rrect(int32_t x, int32_t y, int32_t w, int32_t h,
                       float radiusX, float radiusY = -1.0f) {
        Shape shape;
        shape.data = RRectShape{x, y, w, h, radiusX,
                                radiusY < 0.0f ? radiusX : radiusY};
        return ShapeBuilder(*this, std::move(shape));
    }
    ShapeBuilder circle(int32_t cx, int32_t cy, float r) {
        Shape shape;
        shape.data = CircleShape{cx, cy, r};
        return ShapeBuilder(*this, std::move(shape));
    }
    ShapeBuilder ellipse(int32_t cx, int32_t cy, float rx, float ry) {
        Shape shape;
        shape.data = EllipseShape{cx, cy, rx, ry};
        return ShapeBuilder(*this, std::move(shape));
    }
    ShapeBuilder line(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
        Shape shape;
        shape.data = LineShape{x1, y1, x2, y2};
        return ShapeBuilder(*this, std::move(shape));
    }
    ShapeBuilder ring(int32_t cx, int32_t cy, float rx, float ry, float thickness) {
        Shape shape;
        shape.data = RingShape{cx, cy, rx, ry, thickness};
        return ShapeBuilder(*this, std::move(shape));
    }
    ShapeBuilder text(std::string content, int32_t x, int32_t y, int32_t fontSize) {
        Shape shape;
        TextShape text;
        text.text = std::move(content);
        text.x = x;
        text.y = y;
        text.fontSize = fontSize;
        shape.data = std::move(text);
        return ShapeBuilder(*this, std::move(shape));
    }
    ShapeBuilder image(uint64_t handle, int32_t x, int32_t y, int32_t w, int32_t h) {
        Shape shape;
        shape.data = ImageShape{handle, x, y, w, h, 0, 0, 0, 0, false};
        return ShapeBuilder(*this, std::move(shape));
    }
    ShapeBuilder svg(std::string markup, int32_t x, int32_t y, int32_t w, int32_t h,
                     bool preserveAspectRatio = true) {
        Shape shape;
        SvgShape svg;
        svg.data = std::move(markup);
        svg.x = x;
        svg.y = y;
        svg.w = w;
        svg.h = h;
        svg.preserveAspectRatio = preserveAspectRatio;
        shape.data = std::move(svg);
        return ShapeBuilder(*this, std::move(shape));
    }
    // 从磁盘加载位图 / SVG（需要后端实现 loadImageFile / loadSvgFile）
    //   canvas.imageFile("assets/logo.png", 16, 16);            // 使用图片原始尺寸
    //   canvas.svgFile("assets/icon.svg", x, y, 24, 24).add();
    ShapeBuilder imageFile(const std::string& path, int32_t x, int32_t y,
                           int32_t w = 0, int32_t h = 0) {
        uint64_t handle = 0;
        if (renderer_) handle = renderer_->loadImageFile(path, nullptr);
        if (handle != 0 && renderer_ && (w <= 0 || h <= 0)) {
            int32_t naturalW = 0, naturalH = 0;
            if (renderer_->imageSize(handle, naturalW, naturalH)) {
                if (w <= 0) w = naturalW;
                if (h <= 0) h = naturalH;
            }
        }
        return image(handle, x, y, w > 0 ? w : 1, h > 0 ? h : 1);
    }
    ShapeBuilder svgFile(const std::string& path, int32_t x, int32_t y, int32_t w, int32_t h,
                         bool preserveAspectRatio = true) {
        std::string markup;
        if (renderer_) renderer_->loadSvgFile(path, markup, nullptr);
        return svg(std::move(markup), x, y, w, h, preserveAspectRatio);
    }
    // 背景模糊（Blur Behind）：把该几何体下方已绘制的像素做高斯模糊后覆盖回来。
    // 采样 / 卷积 / 缓存全部由后端实现，Canvas 只负责脏区与场景图管理。
    //   canvas.blur(0, 0, 320, 240, 16.0f, 8.0f).z(10).add();
    ShapeBuilder blur(int32_t x, int32_t y, int32_t w, int32_t h,
                      float radius = 16.0f, float cornerRadius = 0.0f) {
        Shape shape;
        shape.data = BlurShape{x, y, w, h, radius, cornerRadius};
        return ShapeBuilder(*this, std::move(shape));
    }
    ShapeBuilder group() {
        Shape shape;
        shape.data = GroupData{};
        return ShapeBuilder(*this, std::move(shape));
    }

    // 删除 Shape（删除前自动标脏）
    bool removeShape(uint32_t id);

    // --- 脏区域手动标记（updateShape 内部已自动处理；这里提供外部调用）---
    void markDirty(const Rect& rect);
    void markDirtyAll();
    void markShapeDirty(uint32_t id);
    void markShapeDirty(const Shape& shape);

    // ========================================================================
    // Text Metric Provider（可选）
    // ========================================================================
    //
    // 方式 1：通过 IRenderer 实现 measureText()（推荐，与后端生命周期绑定）
    //   Canvas 会自动调用 renderer_->measureText() 填充 TextShape.measuredBounds
    //
    // 方式 2：通过 setTextMetricProvider 设回调（独立于 IRenderer）
    //   用于后端不方便继承 IRenderer 或需要临时替换的场景
    //
    // 设置任一方式后，drawFrame 会在脏区域计算前调用它测量所有未测量的 TextShape

    using TextMetricProvider = std::function<Rect(const TextShape&)>;

    void setTextMetricProvider(TextMetricProvider provider) {
        textMetricProvider_ = std::move(provider);
        invalidateTextMetrics();
    }
    const TextMetricProvider& textMetricProvider() const     { return textMetricProvider_; }

    // 手动测量指定 TextShape
    void measureTextShape(TextShape& ts) const;

    // 使所有缓存的字形度量失效（切换字体 / 后端 / 度量回调后调用），并整屏标脏。
    void invalidateTextMetrics();

    // ========================================================================
    // Clip（场景图驱动，推荐方式）
    // ========================================================================
    //
    // Scroll Area 示例：
    //   Shape scrollArea;
    //   GroupData g;
    //   g.clipRect = Rect{100, 100, 400, 300};   // 视口
    //   g.scrollOffset = Vec2i{0, -150};         // 内容向上滚 150px
    //   g.children.push_back(childShape);
    //   scrollArea.data = g;
    //   canvas.addShape(scrollArea);

    // ========================================================================
    // Clip Stack（全局，DEPRECATED — 推荐用 GroupData.clipRect）
    // ========================================================================
    // 保留用于向后兼容 / 不需要场景图 clip 的简单场景
    // markDirty 仍然遵守 currentClip()，但 buildDirtyEntries 不使用 clipStack
    // （场景图 GroupData.clipRect 有更高优先级）

    [[deprecated("Use GroupData::clipRect instead")]]
    void pushClip(const Rect& rect);
    [[deprecated("Use GroupData::clipRect instead")]]
    void popClip();
    [[deprecated("Use GroupData::clipRect instead")]]
    void clearClip();
    const Rect& currentClip() const;    // markDirty 仍用这个裁剪

    // ========================================================================
    // 帧驱动
    // ========================================================================

    // 主帧：measureTexts → mergeDirty → tile → filter → render
    void drawFrame();

    // 是否有待绘制的脏区域（用于事件驱动的主循环）
    bool needsRedraw() const { return dirtyAll_ || !dirtyRects_.empty(); }

    // 脏区域合并策略：允许把"几乎相邻"的 Tile 矩形合成大矩形时，
    // 额外重绘像素占合并后总面积的最大比例（0 = 严格不重绘额外像素）。
    // 数值越大 → 矩形数量 / DrawCall / Clip 次数越少，但软件光栅化像素变多。
    void setDirtyMergeBudget(float maxOverdrawRatio) {
        dirtyMergeBudget_ = std::max(0.0f, std::min(1.0f, maxOverdrawRatio));
    }
    float dirtyMergeBudget() const { return dirtyMergeBudget_; }

    // 调试
    const std::vector<Rect>& debugDirtyRects() const { return dirtyRects_; }
    // 调试：上一帧实际提交给后端的重绘矩形（Tile 位图 → 极大矩形 → 预算内合并的结果）
    const std::vector<Rect>& debugRenderRegions() const { return tileMerged_; }

public:
    // ========================================================================
    // 脏状态回调（供上层组合器维护"脏子树"标记，避免每帧全树遍历）
    // ========================================================================
    using InvalidateCallback = std::function<void()>;
    void setInvalidateCallback(InvalidateCallback callback) {
        invalidateCallback_ = std::move(callback);
    }

private:
    // Shape 索引（顶层 shapes_ 内）
    Shape* findShape(uint32_t id);
    int32_t findShapeIndex(uint32_t id) const;

    // 递归查找（包括 Group 内的 children）
    Shape* findShapeRecursive(uint32_t id);
    const Shape* findShapeRecursive(uint32_t id) const;

    // 一次遍历定位 Shape，同时把祖先链（顶层 → 目标，含目标自身）写入 chain
    Shape* locateShape(uint32_t id, std::vector<Shape*>& chain);
    // 祖先链累积的 scrollOffset（chain 最后一项是目标自身，不计入）
    static Vec2i chainOffset(const std::vector<Shape*>& chain);

    // 递归清空整棵场景图的包围盒缓存（度量源变化等全局失效时使用）
    static void invalidateBoundsRecursive(Shape& shape);

    // 给 Shape 及其 Group children 递归分配 ID + 建 parentMap_ 映射
    uint32_t assignIdsRecursive(Shape& shape, uint32_t parentId);

    // 从 id 向上累积 scrollOffset 得到屏幕绝对坐标偏移
    Vec2i getAbsoluteOffset(uint32_t id) const;

    // 按当前场景图整体重建 childId → parentId 映射（延迟清理旧后代映射）
    void rebuildParentMap();

    // 递归清除 Text 的度量缓存
    static void resetTextMetrics(Shape& shape);

    // Text 批量测量（drawFrame 前调用）
    void measureTexts();

    // 递归测量一个 Shape（Text / Group children）；返回是否有文本刚完成测量
    bool measureShapeTexts(Shape& shape);

    // 修改 Group 数据后，递归补分配新节点的 ID 并登记 parentMap_；
    // 返回是否分配过新 ID（= 子树结构真的变了）
    bool fixupChildIds(Shape& group, uint32_t parentId);

    // 登记一个几何体为脏（自动置 isDirty），并置位结构脏（如有必要）
    void noteShapeTouched(uint32_t id);
    // 让所有"已登记"时间戳失效（几何体增删导致下标变化后调用）
    void bumpDirtyStamp();
    // 顶层 Shape 下标（nested id 会向上找到所属顶层节点）；找不到返回 -1
    int32_t topLevelIndexOf(uint32_t id) const;
    // 顶层 shapes_ 删除后，刷新 index >= from 的 id → 下标映射
    void reindexTopLevel(size_t from);

    // === 脏区域算法 ===
    // Tile 位图 → 贪心极大矩形提取 → 预算内合并
    // 使用成员 scratch（tileBits_ / tileUsed_ / tileMerged_），热帧零分配。
    const std::vector<Rect>& buildTiles(const std::vector<Rect>& dirtyRects,
                                        int32_t tileSize, const Rect& canvasBounds);
    // 把"几乎相邻"的矩形合并成大矩形（额外像素比例 ≤ dirtyMergeBudget_）
    void mergeDirtyRects(std::vector<Rect>& rects) const;

    // 场景图展平后的叶子项（区域无关：bounds / clip 都是画布绝对坐标）
    struct LeafEntry {
        const Shape* shape = nullptr;
        Vec2i        offset;              // 子节点局部坐标 → 绝对坐标的累积偏移
        Rect         clip;                // 各级 Group clipRect 的绝对交集（无裁剪 = 无限大）
        Rect         bounds;              // 绝对包围盒
        float        opacity = 1.0f;      // 各级 Group 与 Shape 自身的不透明度乘积
    };

    // 递归展平场景图（一次遍历收集全部叶子，后续按脏区域过滤）
    // cullRect：本帧脏区域并集；整棵子树完全在脏区之外时直接跳过（大场景小改动时 O(脏区)）
    // zOrdered：遍历顺序是否已经按 z 非降序（是则无需再排序）
    static void collectLeaves(const Shape& shape,
                              const Vec2i& parentOffset,
                              const Rect& parentClip,
                              const Rect& cullRect,
                              float parentOpacity,
                              std::vector<LeafEntry>& out,
                              bool& zOrdered,
                              int32_t& lastZ);

    // 展平 + 按脏区域过滤 → 生成 DirtyShapeRef
    void buildDirtyEntries(const std::vector<Rect>& regions, std::vector<DirtyEntry>& out);

    // === 数据成员 ===
    int32_t   width_  = 0;
    int32_t   height_ = 0;
    Color     backgroundColor_{};
    IRenderer* renderer_ = nullptr;

    std::vector<Shape> shapes_;          // 场景图（owned，顶层节点）
    uint32_t           nextShapeId_ = 1;

    // 顶层 id → shapes_ 下标（加速 findShape / 合成时的定位）
    std::unordered_map<uint32_t, int32_t> topLevelIndex_;

    // 父子关系映射：childId → parentId
    // 顶层节点 parentId = 0（无效）
    std::unordered_map<uint32_t, uint32_t> parentMap_;
    bool parentMapRebuildPending_ = false;   // 结构变化后延迟到下一帧整体重建

    std::vector<Rect>  dirtyRects_;      // 本帧待合并的脏矩形
    bool               dirtyAll_   = true;
    bool               dirty_      = true;   // 跨帧内容脏标记（isDirty / consumeDirty）
    bool               structureDirty_ = true;  // 几何体增删（合成器需重建结构）
    bool               allShapesDirty_ = true;  // 整块内容失效
    std::vector<uint32_t> dirtyShapeIds_;       // 逐几何体脏记录（跨帧保留）
    std::vector<uint32_t> dirtyShapeStamps_;    // 顶层 index → 最近一次登记轮次（O(1) 去重）
    uint32_t              dirtyStamp_ = 1;

    // === 脏区域 / 展平 热路径 scratch（跨帧复用，避免每帧堆分配）===
    std::vector<Shape*>    locateScratch_;      // locateShape 的祖先链
    std::vector<LeafEntry> leafScratch_;        // 本帧展平结果
    std::vector<uint32_t>  leafOrder_;          // z 乱序时的排序下标
    std::vector<uint64_t>  tileBits_;           // Tile 位图
    std::vector<uint64_t>  tileUsed_;           // Tile 已覆盖标记（贪心矩形提取用）
    std::vector<Rect>      tileMerged_;         // 最终脏区域
    std::vector<uint8_t>   dirtyVisitScratch_;  // forEachDirtyShape 去重
    std::vector<DirtyEntry> entryScratch_;      // DirtyEntry 复用（跨帧保留 shapes 容量）
    float                  dirtyMergeBudget_ = 0.25f;  // 合并允许的额外像素比例

    // 脏状态回调：任何标脏都会通知上层（View 用它维护 O(1) 的脏子树标记）
    InvalidateCallback    invalidateCallback_;

    // Text Metric（可选，回调方式）
    TextMetricProvider textMetricProvider_;
    // 度量失效后必须在下一次 drawFrame 重新测量（否则跳过全场景遍历）
    bool               textMeasurePending_ = false;

    // 全局 Clip Stack（deprecated，保留 markDirty 用）
    std::vector<Rect>  clipStack_;
    static const Rect   kInfiniteRect;

    uint64_t           frameCounter_ = 0;
};

} // namespace modern_ui
