// ============================================================================
// Blend2DRenderer.cpp
// ============================================================================

#include "Blend2DRenderer.h"

#include "Win32LayeredWindow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

// Blur 卷积的 SIMD 路径：x86（SSE2）。其余平台自动退化为标量实现。
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
  #define MODERNUI_BLUR_SSE2 1
  #include <emmintrin.h>
#else
  #define MODERNUI_BLUR_SSE2 0
#endif

// nanosvg 为 vendored 单头库，集中在此翻译单元实现；
// 局部抑制其第三方告警，避免污染本项目的 -Wall -Wextra -Wpedantic 输出。
#if defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wunused-function"
  #pragma GCC diagnostic ignored "-Wunused-parameter"
  #pragma GCC diagnostic ignored "-Wsign-compare"
  #pragma GCC diagnostic ignored "-Wpedantic"
  #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
  #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#if defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif

namespace modern_ui {

namespace {

// ============================================================================
// SVG 缓存内部数据结构
// ============================================================================

enum class SvgPaintType : uint8_t { None = 0, Solid, Gradient };

struct SvgPaint {
    SvgPaintType type = SvgPaintType::None;
    BLRgba32     color;
    BLGradient   gradient;
};

struct SvgCachedShape {
    BLPath              path;
    SvgPaint            fill;
    SvgPaint            stroke;
    BLFillRule          fillRule = BL_FILL_RULE_NON_ZERO;
    double              strokeWidth = 0.0;
    double              miterLimit = 4.0;
    double              dashOffset = 0.0;
    std::vector<double> dashArray;
    uint8_t             strokeCap = 0;
    uint8_t             strokeJoin = 0;
};

} // namespace

// 解析结果与布局无关：只保存 intrinsic 尺寸、路径与画刷。
struct Blend2DRenderer::SvgDocument {
    bool        valid = false;
    float       width = 0.0f;
    float       height = 0.0f;
    uint64_t    lastUse = 0;
    std::vector<SvgCachedShape> shapes;
};

namespace {

// ============================================================================
// 通用转换
// ============================================================================

BLRgba32 toBL(const Color& color) {
    return BLRgba32(color.r, color.g, color.b, color.a);
}

std::string blError(const char* what, BLResult result) {
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer), "%s (Blend2D error %u)", what,
                  static_cast<unsigned>(result));
    return std::string(buffer);
}

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                        static_cast<int>(text.size()),
                        result.data(), length, nullptr, nullptr);
    return result;
}

double clampRadius(double radius, double width, double height) {
    return std::max(0.0, std::min(radius, std::min(width, height) * 0.5));
}

BLExtendMode toBLExtend(SpreadMethod spread) {
    switch (spread) {
    case SpreadMethod::Reflect: return BL_EXTEND_MODE_REFLECT;
    case SpreadMethod::Repeat:  return BL_EXTEND_MODE_REPEAT;
    case SpreadMethod::Pad:
    default:                    return BL_EXTEND_MODE_PAD;
    }
}

std::vector<BLGradientStop> toBLStops(const std::vector<GradientStop>& stops) {
    std::vector<BLGradientStop> result;
    result.reserve(stops.size());
    for (const GradientStop& stop : stops) {
        const double offset = std::min(1.0, std::max(0.0, static_cast<double>(stop.offset)));
        result.emplace_back(offset, toBL(stop.color));
    }
    return result;
}

enum class BrushKind { None, Solid, Gradient };

// 把 Canvas Paint 解析成 Blend2D 可用的画刷。
// 退化渐变（零长度 / 零半径）退化为最后一个 stop 的纯色，避免无效 BLGradient。
BrushKind prepareBrush(const Paint& paint, BLRgba32& solid, BLGradient& gradient) {
    if (paint.isNone() || paint.isTransparent()) return BrushKind::None;

    if (const Color* color = paint.solidColor()) {
        solid = toBL(*color);
        return BrushKind::Solid;
    }

    if (const LinearGradient* linear = paint.linearGradient()) {
        const std::vector<BLGradientStop> stops = toBLStops(linear->stops);
        if (stops.empty()) return BrushKind::None;

        const BLRgba32 fallback = toBL(linear->stops.back().color);
        const bool degenerate = linear->from.x == linear->to.x &&
                                linear->from.y == linear->to.y;
        if (degenerate) {
            solid = fallback;
            return BrushKind::Solid;
        }

        const BLResult result = gradient.create(
            BLLinearGradientValues(linear->from.x, linear->from.y,
                                   linear->to.x, linear->to.y),
            toBLExtend(linear->spread), stops.data(), stops.size());
        if (result == BL_SUCCESS) return BrushKind::Gradient;

        solid = fallback;
        return BrushKind::Solid;
    }

    if (const RadialGradient* radial = paint.radialGradient()) {
        const std::vector<BLGradientStop> stops = toBLStops(radial->stops);
        if (stops.empty()) return BrushKind::None;

        const BLRgba32 fallback = toBL(radial->stops.back().color);
        if (!(radial->radius > 0.0f)) {
            solid = fallback;
            return BrushKind::Solid;
        }

        const BLResult result = gradient.create(
            BLRadialGradientValues(radial->focal.x, radial->focal.y,
                                   radial->center.x, radial->center.y,
                                   0.0, static_cast<double>(radial->radius)),
            toBLExtend(radial->spread), stops.data(), stops.size());
        if (result == BL_SUCCESS) return BrushKind::Gradient;

        solid = fallback;
        return BrushKind::Solid;
    }

    return BrushKind::None;
}

// ============================================================================
// SVG 转换
// ============================================================================

uint64_t hashSvg(const std::string& data) {
    // FNV-1a 64：内容变 → 缓存键变
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : data) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

// nanosvg 的颜色是 R | G<<8 | B<<16 | A<<24
BLRgba32 svgColorToBL(uint32_t color, float opacity) {
    const uint32_t r = color & 0xFFu;
    const uint32_t g = (color >> 8) & 0xFFu;
    const uint32_t b = (color >> 16) & 0xFFu;
    const float alpha = std::max(0.0f, std::min(1.0f, opacity));
    const uint32_t a = static_cast<uint32_t>(
        std::lround(static_cast<double>((color >> 24) & 0xFFu) * alpha));
    return BLRgba32(r, g, b, a);
}

// nanosvg 的 gradient->xform 是 user → gradient（构造阶段做过一次求逆）。
// Blend2D 的 gradient transform 语义是 gradient → user，因此这里再求逆一次。
BLMatrix2D invertSvgMatrix(const float m[6]) {
    const double det = static_cast<double>(m[0]) * m[3] -
                       static_cast<double>(m[2]) * m[1];
    if (det > -1e-9 && det < 1e-9) {
        return BLMatrix2D(1.0, 0.0, 0.0, 1.0, 0.0, 0.0);
    }
    const double inv = 1.0 / det;
    return BLMatrix2D(
        m[3] * inv, -m[1] * inv,
        -m[2] * inv, m[0] * inv,
        (m[2] * m[5] - m[3] * m[4]) * inv,
        (m[1] * m[4] - m[0] * m[5]) * inv);
}

BLExtendMode svgExtend(char spread) {
    switch (spread) {
    case NSVG_SPREAD_REFLECT: return BL_EXTEND_MODE_REFLECT;
    case NSVG_SPREAD_REPEAT:  return BL_EXTEND_MODE_REPEAT;
    default:                  return BL_EXTEND_MODE_PAD;
    }
}

BLFillRule svgFillRule(char rule) {
    return rule == NSVG_FILLRULE_EVENODD ? BL_FILL_RULE_EVEN_ODD
                                         : BL_FILL_RULE_NON_ZERO;
}

BLStrokeCap svgCap(char cap) {
    switch (cap) {
    case NSVG_CAP_ROUND:  return BL_STROKE_CAP_ROUND;
    case NSVG_CAP_SQUARE: return BL_STROKE_CAP_SQUARE;
    default:              return BL_STROKE_CAP_BUTT;
    }
}

BLStrokeJoin svgJoin(char join) {
    switch (join) {
    case NSVG_JOIN_ROUND: return BL_STROKE_JOIN_ROUND;
    case NSVG_JOIN_BEVEL: return BL_STROKE_JOIN_BEVEL;
    default:              return BL_STROKE_JOIN_MITER_CLIP;
    }
}

// 一条 NSVGshape 的所有子路径合并为一个 BLPath（多 subpath，共享 fill rule）
BLPath buildSvgPath(const NSVGshape* shape) {
    BLPath path;
    for (const NSVGpath* p = shape->paths; p != nullptr; p = p->next) {
        // 至少需要一个 (x, y) 起点；否则下面的 pts[1] 会越界。
        if (!p->pts || p->npts < 2) continue;

        const float* pts = p->pts;
        path.move_to(pts[0], pts[1]);
        for (int i = 1; i + 2 < p->npts; i += 3) {
            path.cubic_to(pts[i * 2],         pts[i * 2 + 1],
                          pts[(i + 1) * 2],   pts[(i + 1) * 2 + 1],
                          pts[(i + 2) * 2],   pts[(i + 2) * 2 + 1]);
        }
        if (p->closed) path.close();
    }
    return path;
}

bool buildSvgGradient(const NSVGpaint& paint, float opacity, BLGradient& out) {
    if (paint.type != NSVG_PAINT_LINEAR_GRADIENT &&
        paint.type != NSVG_PAINT_RADIAL_GRADIENT) {
        return false;
    }
    const NSVGgradient* gradient = paint.gradient;
    if (!gradient || gradient->nstops <= 0) return false;

    std::vector<BLGradientStop> stops;
    stops.reserve(static_cast<size_t>(gradient->nstops));
    for (int i = 0; i < gradient->nstops; ++i) {
        const NSVGgradientStop& stop = gradient->stops[i];
        const double offset = std::min(1.0, std::max(0.0, static_cast<double>(stop.offset)));
        stops.emplace_back(offset, svgColorToBL(stop.color, opacity));
    }

    // 规范渐变空间：线性 (0,0)→(1,0)，径向 单位圆 + focal
    const BLMatrix2D transform = invertSvgMatrix(gradient->xform);
    BLResult result;
    if (paint.type == NSVG_PAINT_LINEAR_GRADIENT) {
        result = out.create(BLLinearGradientValues(0.0, 0.0, 1.0, 0.0),
                            svgExtend(gradient->spread),
                            stops.data(), stops.size(), transform);
    } else {
        result = out.create(BLRadialGradientValues(gradient->fx, gradient->fy,
                                                   0.0, 0.0, 0.0, 1.0),
                            svgExtend(gradient->spread),
                            stops.data(), stops.size(), transform);
    }
    return result == BL_SUCCESS;
}

SvgPaint buildSvgPaint(const NSVGpaint& paint, float opacity) {
    SvgPaint out;

    if (paint.type == NSVG_PAINT_COLOR) {
        out.type = SvgPaintType::Solid;
        out.color = svgColorToBL(paint.color, opacity);
    } else if (paint.type == NSVG_PAINT_LINEAR_GRADIENT ||
               paint.type == NSVG_PAINT_RADIAL_GRADIENT) {
        if (buildSvgGradient(paint, opacity, out.gradient)) {
            out.type = SvgPaintType::Gradient;
        } else if (paint.gradient && paint.gradient->nstops > 0) {
            // 退化渐变 → 退化为最后一个 stop 的纯色
            const NSVGgradientStop& last =
                paint.gradient->stops[paint.gradient->nstops - 1];
            out.type = SvgPaintType::Solid;
            out.color = svgColorToBL(last.color, opacity);
        }
    }

    if (out.type == SvgPaintType::Solid && out.color.a() == 0) {
        out.type = SvgPaintType::None;
    }
    return out;
}

// ============================================================================
// Canvas 几何体 → BLPath
// ============================================================================

constexpr double kPiD = 3.14159265358979323846;

double degToRad(double degrees) {
    return degrees * (kPiD / 180.0);
}

// 星形 / 正多边形：rotation = 0 时第一个顶点朝上（-90°）
BLPath buildStarPath(const StarShape& star) {
    const int32_t points = std::max<int32_t>(3, star.points);
    const double outer = std::fabs(static_cast<double>(star.outerRadius));
    const double inner = std::min(std::fabs(static_cast<double>(star.innerRadius)), outer);
    const double start = degToRad(static_cast<double>(star.rotation) - 90.0);
    const double step = kPiD / points;

    BLPath path;
    for (int32_t i = 0; i < points * 2; ++i) {
        const double radius = (i % 2 == 0) ? outer : inner;
        const double angle = start + step * i;
        const double x = static_cast<double>(star.cx) + radius * std::cos(angle);
        const double y = static_cast<double>(star.cy) + radius * std::sin(angle);
        if (i == 0) path.move_to(x, y);
        else        path.line_to(x, y);
    }
    path.close();
    return path;
}

BLPath buildPolygonPath(const PolygonShape& polygon) {
    const int32_t sides = std::max<int32_t>(3, polygon.sides);
    const double radius = std::fabs(static_cast<double>(polygon.radius));
    const double start = degToRad(static_cast<double>(polygon.rotation) - 90.0);
    const double step = 2.0 * kPiD / sides;

    BLPath path;
    for (int32_t i = 0; i < sides; ++i) {
        const double angle = start + step * i;
        const double x = static_cast<double>(polygon.cx) + radius * std::cos(angle);
        const double y = static_cast<double>(polygon.cy) + radius * std::sin(angle);
        if (i == 0) path.move_to(x, y);
        else        path.line_to(x, y);
    }
    path.close();
    return path;
}

// 扇形：圆心 → 圆弧起点 → 圆弧 → 圆心闭合
BLPath buildPiePath(const PieShape& pie) {
    BLPath path;
    path.move_to(pie.cx, pie.cy);
    path.arc_to(pie.cx, pie.cy,
                std::fabs(static_cast<double>(pie.rx)),
                std::fabs(static_cast<double>(pie.ry)),
                degToRad(pie.startAngle), degToRad(pie.sweepAngle), false);
    path.close();
    return path;
}

} // namespace

// ============================================================================
// 字形缓存（shaping 结果 + A8 栅格蒙版）
// ============================================================================
//
// 文本绘制最贵的部分是“UTF-8 解码 → 字形映射 / shaping → 轮廓解码 → 光栅化”，
// 而 UI 文本大多逐帧重复、位置不变。这里按 (字体代次, 字号, 文本) 缓存：
//   1) RunEntry：shaping 结果（BLGlyphBuffer，含 glyph id / placement / 度量）；
//   2) 整行 A8 蒙版：按整数基线光栅化一次，之后纯色 / 渐变都只做一次 fill_mask；
//   3) 单字形 A8 蒙版：整行蒙版超限时的降级路径（含 1/4 像素 X 相位）；
//   4) 两级 LRU + 字节预算，控制长跑进程的内存。
//
// 前提：TextShape 只使用整数坐标，且绘制上下文只有平移变换，因此蒙版像素相位
// 稳定，满足 Blend2D fill_mask 的“像素对齐 + translate-only”要求。

namespace {

uint64_t fnv1a(const void* data, size_t size, uint64_t hash = 1469598103934665603ull) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= static_cast<uint64_t>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

// splitmix64 终混：把 (文本, 字号, 字体代次) 压成低碰撞的 64 位缓存键
uint64_t mixHash(uint64_t value) {
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value;
}

uint64_t makeRunKey(const std::string& text, int32_t pixelSize, uint32_t face) {
    const uint64_t content = fnv1a(text.data(), text.size());
    return mixHash(content ^
                   (static_cast<uint64_t>(static_cast<uint32_t>(pixelSize)) << 32) ^ face);
}

uint64_t makeGlyphKey(uint32_t face, int32_t pixelSize, uint32_t glyphId, uint32_t fraction) {
    const uint64_t base = mixHash(static_cast<uint64_t>(face) ^
                                  (static_cast<uint64_t>(static_cast<uint32_t>(pixelSize)) << 20));
    return mixHash(base ^ (static_cast<uint64_t>(glyphId) << 2 | (fraction & 3u)));
}

struct MeasureMetrics {
    int32_t x = 0, y = 0, w = 1, h = 1;
};

// 与旧版 measureText 完全一致：bounding_box 没有高度时回退到字体 ascent / descent。
bool computeMetrics(const BLFont& font, BLGlyphBuffer& buffer, MeasureMetrics& out) {
    if (font.shape(buffer) != BL_SUCCESS) return false;

    BLTextMetrics metrics;
    if (font.get_text_metrics(buffer, metrics) != BL_SUCCESS) return false;

    double x0 = metrics.bounding_box.x0;
    double y0 = metrics.bounding_box.y0;
    double x1 = metrics.bounding_box.x1;
    double y1 = metrics.bounding_box.y1;

    if (!(x1 > x0) || !(y1 > y0)) {
        const BLFontMetrics& fontMetrics = font.metrics();
        x0 = 0.0;
        y0 = -fontMetrics.ascent;
        x1 = std::max(1.0, metrics.advance.x);
        y1 = fontMetrics.descent;
    }

    out.x = static_cast<int32_t>(std::floor(x0)) - 1;
    out.y = static_cast<int32_t>(std::floor(y0)) - 1;
    out.w = std::max(1, static_cast<int32_t>(std::ceil(x1 - x0)) + 2);
    out.h = std::max(1, static_cast<int32_t>(std::ceil(y1 - y0)) + 2);
    return true;
}

// fill_mask 只在“纯平移 + 像素对齐”下生效；不满足时回退到 fill_glyph_run。
bool maskPathUsable(const BLContext& ctx) {
    const BLMatrix2D& m = ctx.user_transform();
    constexpr double kEps = 1e-9;
    return std::fabs(m.m00 - 1.0) < kEps && std::fabs(m.m11 - 1.0) < kEps &&
           std::fabs(m.m01) < kEps && std::fabs(m.m10) < kEps &&
           std::fabs(m.m20 - std::round(m.m20)) < kEps &&
           std::fabs(m.m21 - std::round(m.m21)) < kEps;
}

} // namespace

struct Blend2DRenderer::GlyphCache {
    struct RunEntry {
        std::string      text;
        int32_t          pixelSize = 0;
        uint32_t         face = 0;
        uint64_t         key = 0;
        uint64_t         lastUse = 0;

        BLGlyphBuffer    glyphs;                  // shaping 结果（glyph id + placement）
        MeasureMetrics   measured;                // 相对基线原点的文本脏区

        bool             rasterTried = false;     // 已尝试光栅化（失败也不再重试）
        bool             rasterReady = false;
        BLImage          mask;                    // A8 整行蒙版（颜色无关）
        int32_t          maskX = 0, maskY = 0;    // 蒙版左上角相对基线原点
        int32_t          maskW = 0, maskH = 0;
        size_t           maskBytes = 0;
    };

    struct GlyphEntry {
        uint64_t  key = 0;
        uint64_t  lastUse = 0;
        uint32_t  face = 0;
        uint32_t  pixelSize = 0;
        uint32_t  glyphId = 0;
        uint32_t  fraction = 0;                   // 1/4 像素 X 相位（0..3）

        BLImage   mask;                           // A8 单字形蒙版
        int32_t   offsetX = 0, offsetY = 0;       // 蒙版左上角相对字形笔位
        int32_t   width = 0, height = 0;
        size_t    bytes = 0;
    };

    // 内存预算：整行蒙版占大头，单字形蒙版单独记账。
    static constexpr size_t  kRunBytesBudget    = 16u * 1024u * 1024u;
    static constexpr size_t  kGlyphBytesBudget  = 4u * 1024u * 1024u;
    static constexpr size_t  kMaxRunEntries     = 4096;
    static constexpr size_t  kMaxGlyphEntries   = 8192;
    static constexpr int32_t kMaxRunMaskDim     = 4096;
    static constexpr int32_t kMaxGlyphMaskDim   = 1024;
    static constexpr int64_t kMaxRunMaskPixels  = 1ll << 21;   // 2M 像素 = 2MB
    static constexpr int64_t kMaxGlyphMaskPixels = 1ll << 18;  // 256K 像素

    std::unordered_map<uint64_t, std::unique_ptr<RunEntry>>   runs_;
    std::unordered_map<uint64_t, std::unique_ptr<GlyphEntry>> glyphs_;
    size_t   runBytes_ = 0;
    size_t   glyphBytes_ = 0;
    uint64_t tick_ = 0;
    uint64_t runHits_ = 0;
    uint64_t runMisses_ = 0;
    uint64_t rasterizations_ = 0;

    RunEntry* acquireRun(const std::string& text, int32_t pixelSize, uint32_t face,
                         const BLFont& font);
    bool ensureRunMask(RunEntry& entry, const BLFont& font);

    // 整行蒙版超限时的降级：逐字形蒙版填充。返回 false 表示应回退 fill_glyph_run。
    bool drawGlyphFallback(BLContext& ctx, RunEntry& entry, const BLFont& font,
                           int32_t originX, int32_t originY, BrushKind kind,
                           const BLRgba32& solid, const BLGradient& gradient);

    GlyphEntry* acquireGlyph(const BLFont& font, int32_t pixelSize, uint32_t face,
                             BLGlyphId glyphId, uint32_t fraction);

    void trimRuns(const RunEntry* protect);
    void trimGlyphs(const GlyphEntry* protect);
    void clear();
};

Blend2DRenderer::GlyphCache::RunEntry* Blend2DRenderer::GlyphCache::acquireRun(
    const std::string& text, int32_t pixelSize, uint32_t face, const BLFont& font) {
    const uint64_t key = makeRunKey(text, pixelSize, face);

    auto it = runs_.find(key);
    if (it != runs_.end()) {
        RunEntry* entry = it->second.get();
        if (entry->text == text && entry->pixelSize == pixelSize && entry->face == face) {
            entry->lastUse = ++tick_;
            ++runHits_;
            return entry;
        }
        // 哈希碰撞：本轮不走缓存，避免把别的文本当成命中
        return nullptr;
    }

    auto entry = std::make_unique<RunEntry>();
    entry->text = text;
    entry->pixelSize = pixelSize;
    entry->face = face;
    entry->key = key;
    entry->lastUse = ++tick_;

    if (entry->glyphs.set_utf8_text(text.c_str(), text.size()) != BL_SUCCESS) return nullptr;
    if (!computeMetrics(font, entry->glyphs, entry->measured)) return nullptr;

    ++runMisses_;
    RunEntry* raw = entry.get();
    runs_.emplace(key, std::move(entry));
    trimRuns(raw);
    return raw;
}

bool Blend2DRenderer::GlyphCache::ensureRunMask(RunEntry& entry, const BLFont& font) {
    if (entry.rasterTried) return entry.rasterReady;
    entry.rasterTried = true;

    const BLGlyphRun& run = entry.glyphs.glyph_run();
    if (run.size == 0) {
        entry.rasterReady = true;   // 空文本：无需绘制
        return true;
    }

    // 一次轮廓解码：同时拿到精确墨水范围（含 placement / kerning）
    BLPath outline;
    if (font.get_glyph_run_outlines(run, outline) != BL_SUCCESS) return false;
    if (outline.is_empty()) {
        entry.rasterReady = true;   // 纯空格：没有墨迹
        return true;
    }

    BLBox box;
    if (outline.get_bounding_box(&box) != BL_SUCCESS) return false;
    const int32_t x0 = static_cast<int32_t>(std::floor(box.x0)) - 1;
    const int32_t y0 = static_cast<int32_t>(std::floor(box.y0)) - 1;
    const int32_t x1 = static_cast<int32_t>(std::ceil(box.x1)) + 1;
    const int32_t y1 = static_cast<int32_t>(std::ceil(box.y1)) + 1;
    const int32_t width = x1 - x0;
    const int32_t height = y1 - y0;

    if (width <= 0 || height <= 0 ||
        width > kMaxRunMaskDim || height > kMaxRunMaskDim ||
        static_cast<int64_t>(width) * height > kMaxRunMaskPixels) {
        return false;   // 超限：走逐字形蒙版
    }

    BLImage mask;
    if (mask.create(width, height, BL_FORMAT_A8) != BL_SUCCESS) return false;

    BLContext maskCtx(mask);
    if (!maskCtx.is_valid()) return false;

    maskCtx.clear_all();
    maskCtx.translate(-static_cast<double>(x0), -static_cast<double>(y0));
    maskCtx.set_fill_style(BLRgba32(255, 255, 255, 255));
    if (maskCtx.fill_glyph_run(BLPointI(0, 0), font, run) != BL_SUCCESS) return false;
    maskCtx.flush(BL_CONTEXT_FLUSH_SYNC);

    entry.mask = std::move(mask);
    entry.maskX = x0;
    entry.maskY = y0;
    entry.maskW = width;
    entry.maskH = height;
    entry.maskBytes = static_cast<size_t>(width) * static_cast<size_t>(height);
    runBytes_ += entry.maskBytes;
    ++rasterizations_;
    trimRuns(&entry);

    entry.rasterReady = true;
    return true;
}

Blend2DRenderer::GlyphCache::GlyphEntry* Blend2DRenderer::GlyphCache::acquireGlyph(
    const BLFont& font, int32_t pixelSize, uint32_t face, BLGlyphId glyphId,
    uint32_t fraction) {
    const uint64_t key = makeGlyphKey(face, pixelSize, glyphId, fraction);

    auto it = glyphs_.find(key);
    if (it != glyphs_.end()) {
        GlyphEntry* entry = it->second.get();
        if (entry->face == face && entry->pixelSize == static_cast<uint32_t>(pixelSize) &&
            entry->glyphId == glyphId && entry->fraction == fraction) {
            entry->lastUse = ++tick_;
            return entry;
        }
        return nullptr;   // 哈希碰撞
    }

    auto entry = std::make_unique<GlyphEntry>();
    entry->key = key;
    entry->face = face;
    entry->pixelSize = static_cast<uint32_t>(pixelSize);
    entry->glyphId = glyphId;
    entry->fraction = fraction;
    entry->lastUse = ++tick_;

    BLPath outline;
    if (font.get_glyph_outlines(
            glyphId,
            BLMatrix2D::make_translation(static_cast<double>(fraction) * 0.25, 0.0),
            outline) != BL_SUCCESS) {
        return nullptr;   // 无法解码轮廓：交给 fill_glyph_run 兜底
    }

    if (!outline.is_empty()) {
        BLBox box;
        if (outline.get_bounding_box(&box) != BL_SUCCESS) return nullptr;
        const int32_t x0 = static_cast<int32_t>(std::floor(box.x0)) - 1;
        const int32_t y0 = static_cast<int32_t>(std::floor(box.y0)) - 1;
        const int32_t x1 = static_cast<int32_t>(std::ceil(box.x1)) + 1;
        const int32_t y1 = static_cast<int32_t>(std::ceil(box.y1)) + 1;
        const int32_t width = x1 - x0;
        const int32_t height = y1 - y0;

        if (width <= 0 || height <= 0 ||
            width > kMaxGlyphMaskDim || height > kMaxGlyphMaskDim ||
            static_cast<int64_t>(width) * height > kMaxGlyphMaskPixels) {
            return nullptr;   // 过大：不缓存，交由上层回退
        }

        BLImage mask;
        if (mask.create(width, height, BL_FORMAT_A8) != BL_SUCCESS) return nullptr;

        BLContext maskCtx(mask);
        if (!maskCtx.is_valid()) return nullptr;

        maskCtx.clear_all();
        maskCtx.translate(-static_cast<double>(x0), -static_cast<double>(y0));
        maskCtx.set_fill_style(BLRgba32(255, 255, 255, 255));
        if (maskCtx.fill_path(outline) != BL_SUCCESS) return nullptr;
        maskCtx.flush(BL_CONTEXT_FLUSH_SYNC);

        entry->mask = std::move(mask);
        entry->offsetX = x0;
        entry->offsetY = y0;
        entry->width = width;
        entry->height = height;
        entry->bytes = static_cast<size_t>(width) * static_cast<size_t>(height);
        ++rasterizations_;
    }

    GlyphEntry* raw = entry.get();
    glyphBytes_ += entry->bytes;
    glyphs_.emplace(key, std::move(entry));
    trimGlyphs(raw);
    return raw;
}

bool Blend2DRenderer::GlyphCache::drawGlyphFallback(
    BLContext& ctx, RunEntry& entry, const BLFont& font,
    int32_t originX, int32_t originY, BrushKind kind,
    const BLRgba32& solid, const BLGradient& gradient) {
    const BLGlyphRun& run = entry.glyphs.glyph_run();
    if (run.size == 0) return true;
    if (run.placement_type != BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET || !run.placement_data) {
        return false;   // 非标准 placement：交给 fill_glyph_run
    }

    struct PendingGlyph {
        BLImage mask;
        int32_t x = 0, y = 0, w = 0, h = 0;
    };

    // 先收集全部蒙版再绘制：中途失败时可以安全回退而不出现半截文本。
    std::vector<PendingGlyph> pending;
    pending.reserve(run.size);

    double penX = 0.0;
    double penY = 0.0;
    BLGlyphRunIterator it(run);
    while (!it.at_end()) {
        const BLGlyphPlacement& placement = it.placement<BLGlyphPlacement>();
        const double glyphX = penX + placement.placement.x;
        const double glyphY = penY + placement.placement.y;

        const int32_t ix = static_cast<int32_t>(std::floor(glyphX));
        const int32_t iy = static_cast<int32_t>(std::floor(glyphY));
        const int32_t fraction = static_cast<int32_t>(std::lround((glyphX - ix) * 4.0));
        const uint32_t quantized =
            static_cast<uint32_t>(std::min(3, std::max(0, fraction)));

        GlyphEntry* glyph =
            acquireGlyph(font, entry.pixelSize, entry.face, it.glyph_id(), quantized);
        if (!glyph) return false;

        if (glyph->width > 0 && glyph->height > 0) {
            // 拷贝 BLImage：即使后续 LRU 淘汰该条目，本帧蒙版仍然有效
            pending.push_back(PendingGlyph{glyph->mask,
                                           ix + glyph->offsetX,
                                           iy + glyph->offsetY,
                                           glyph->width,
                                           glyph->height});
        }

        penX += placement.advance.x;
        penY += placement.advance.y;
        it.advance();
    }

    for (const PendingGlyph& glyph : pending) {
        const BLPointI origin(originX + glyph.x, originY + glyph.y);
        const BLRectI area(0, 0, glyph.w, glyph.h);
        if (kind == BrushKind::Solid) ctx.fill_mask(origin, glyph.mask, area, solid);
        else                          ctx.fill_mask(origin, glyph.mask, area, gradient);
    }
    return true;
}

void Blend2DRenderer::GlyphCache::trimRuns(const RunEntry* protect) {
    auto evictOldest = [&](bool masksOnly) {
        RunEntry* victim = nullptr;
        for (const auto& item : runs_) {
            RunEntry* candidate = item.second.get();
            if (candidate == protect) continue;
            if (masksOnly && candidate->maskBytes == 0) continue;
            if (!victim || candidate->lastUse < victim->lastUse) victim = candidate;
        }
        if (!victim) return false;
        runBytes_ -= victim->maskBytes;
        runs_.erase(victim->key);
        return true;
    };

    while (runBytes_ > kRunBytesBudget && evictOldest(true)) {}
    while (runs_.size() > kMaxRunEntries && evictOldest(false)) {}
}

void Blend2DRenderer::GlyphCache::trimGlyphs(const GlyphEntry* protect) {
    auto evictOldest = [&](bool masksOnly) {
        GlyphEntry* victim = nullptr;
        for (const auto& item : glyphs_) {
            GlyphEntry* candidate = item.second.get();
            if (candidate == protect) continue;
            if (masksOnly && candidate->bytes == 0) continue;
            if (!victim || candidate->lastUse < victim->lastUse) victim = candidate;
        }
        if (!victim) return false;
        glyphBytes_ -= victim->bytes;
        glyphs_.erase(victim->key);
        return true;
    };

    while (glyphBytes_ > kGlyphBytesBudget && evictOldest(true)) {}
    while (glyphs_.size() > kMaxGlyphEntries && evictOldest(false)) {}
}

void Blend2DRenderer::GlyphCache::clear() {
    runs_.clear();
    glyphs_.clear();
    runBytes_ = 0;
    glyphBytes_ = 0;
    tick_ = 0;
    runHits_ = 0;
    runMisses_ = 0;
    rasterizations_ = 0;
}

// ============================================================================
// 构造 / 析构
// ============================================================================

Blend2DRenderer::Blend2DRenderer(Win32LayeredWindow* window)
    : window_(window) {}

// 析构定义放在 BlurCache 完整定义之后（unique_ptr<BlurCache> 需要完整类型）。

// ============================================================================
// 字形缓存入口
// ============================================================================

Blend2DRenderer::GlyphCache& Blend2DRenderer::glyphCache() {
    if (!glyphCache_) glyphCache_ = std::make_unique<GlyphCache>();
    return *glyphCache_;
}

void Blend2DRenderer::clearGlyphCache() {
    if (glyphCache_) glyphCache_->clear();
}

Blend2DRenderer::GlyphCacheStats Blend2DRenderer::glyphCacheStats() const {
    GlyphCacheStats stats;
    if (!glyphCache_) return stats;

    for (const auto& item : glyphCache_->runs_) {
        ++stats.textEntries;
        if (item.second->maskBytes > 0) {
            ++stats.maskEntries;
            stats.maskBytes += item.second->maskBytes;
        }
    }
    for (const auto& item : glyphCache_->glyphs_) {
        if (item.second->bytes > 0) {
            ++stats.maskEntries;
            stats.maskBytes += item.second->bytes;
        }
    }

    stats.textHits = glyphCache_->runHits_;
    stats.textMisses = glyphCache_->runMisses_;
    stats.rasterizations = glyphCache_->rasterizations_;
    return stats;
}

// ============================================================================
// Blur 几何体 — 表面回读 + 分离高斯卷积（SIMD）+ 结果缓存
// ============================================================================
//
// 语义：BlurShape 把"该几何体之下（先绘制）已经落在表面上的像素"当作底图，
// 高斯模糊后覆盖回自身区域，等价于 CSS backdrop-filter: blur(σ)。
//
// 流程：
//   1. renderTile 先清 Tile 并重绘区域内所有低于 Blur 的几何体；
//   2. 命中缓存（目标区域 / σ / 圆角 / 底层内容签名 一致）时直接 blit；
//   3. 否则回读表面像素，B/G/R/A 四张 float 平面做分离高斯卷积
//      （水平 + 垂直，SSE2 一次 4 像素），写回 PRGB32 缓存图；
//   4. 圆角：按圆角矩形 SDF 计算逐像素覆盖率并乘到预乘 alpha 上，
//      保证圆角外的像素 alpha = 0，SRC_OVER 时保持底层原样。
//
// 缓存失效依据（"底下内容变化时再更新缓存"）：
//   - 背景色；
//   - 目标区域内 z 更低的几何体 id / revision / 包围盒 / 透明度；
//   - Blur 自身的目标区域 / σ / 圆角。
//   revision 由 Canvas 在 addShape / updateShape 时递增，因此颜色、文本等
//   任何内容修改都能被 O(1) 捕获，无需逐像素比对。
//
// 采样只覆盖当前 Tile 区域（不外扩）：区域外可能是上一帧的模糊结果，
// 重复采样会造成二次模糊；向内 clamp 采样更稳定。
// ============================================================================

namespace {

uint64_t mixHash(uint64_t h, uint64_t value) {
    h ^= value + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}

uint64_t hashRect(uint64_t h, const Rect& r) {
    h = mixHash(h, static_cast<uint64_t>(static_cast<uint32_t>(r.x)));
    h = mixHash(h, static_cast<uint64_t>(static_cast<uint32_t>(r.y)));
    h = mixHash(h, static_cast<uint64_t>(static_cast<uint32_t>(r.w)));
    h = mixHash(h, static_cast<uint64_t>(static_cast<uint32_t>(r.h)));
    return h;
}

// 底层内容签名：目标区域内 z 更低（先绘制）的几何体全部参与，
// 只要 id / revision / 包围盒 / 透明度有一个变化就视为"底图已变"。
uint64_t blurSignature(const CanvasInfo& info, const std::vector<DirtyShapeRef>& shapes,
                       size_t blurIndex, const Rect& target) {
    uint64_t h = 0xcbf29ce484222325ull;
    const uint64_t background =
        (static_cast<uint64_t>(info.backgroundColor.r) << 24) |
        (static_cast<uint64_t>(info.backgroundColor.g) << 16) |
        (static_cast<uint64_t>(info.backgroundColor.b) << 8) |
        static_cast<uint64_t>(info.backgroundColor.a);
    h = mixHash(h, background);
    h = hashRect(h, target);

    for (size_t i = 0; i < blurIndex && i < shapes.size(); ++i) {
        const DirtyShapeRef& below = shapes[i];
        if (!below.shape || !below.shape->base.visible) continue;
        const Rect bounds = below.shape->bounds().translated(below.accumulatedOffset);
        if (!bounds.intersects(target)) continue;
        h = mixHash(h, below.shape->base.id);
        h = mixHash(h, below.shape->base.revision);
        h = mixHash(h, static_cast<uint64_t>(static_cast<int64_t>(below.shape->base.z)));
        h = hashRect(h, bounds);
        h = mixHash(h, static_cast<uint64_t>(below.opacity * 1000.0f + 0.5f));
    }
    return h;
}

void buildGaussianKernel(float sigma, std::vector<float>& kernel, int32_t& radius) {
    radius = std::max(1, static_cast<int32_t>(std::ceil(sigma * 3.0f)));
    const size_t count = static_cast<size_t>(radius) * 2 + 1;
    kernel.assign(count, 0.0f);

    const float denominator = 2.0f * sigma * sigma;
    float sum = 0.0f;
    for (int32_t i = -radius; i <= radius; ++i) {
        const float weight = std::exp(-static_cast<float>(i * i) / denominator);
        kernel[static_cast<size_t>(i + radius)] = weight;
        sum += weight;
    }
    if (sum > 0.0f) {
        const float inverse = 1.0f / sum;
        for (float& weight : kernel) weight *= inverse;
    }
}

inline uint8_t floatToByte(float value) {
    return static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, value)) + 0.5f);
}

// 交错 PRGB32 → B/G/R/A 四张 float 平面（预乘 alpha 直接参与卷积）
void unpackToPlanes(const uint8_t* src, intptr_t stride, int32_t width, int32_t height,
                    std::array<std::vector<float>, 4>& planes) {
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t c = 0; c < 4; ++c) planes[c].resize(count);

    for (int32_t y = 0; y < height; ++y) {
        const uint8_t* row = src + static_cast<size_t>(y) * stride;
        const size_t offset = static_cast<size_t>(y) * static_cast<size_t>(width);
        float* planeB = planes[0].data() + offset;
        float* planeG = planes[1].data() + offset;
        float* planeR = planes[2].data() + offset;
        float* planeA = planes[3].data() + offset;
        for (int32_t x = 0; x < width; ++x) {
            const uint8_t* pixel = row + static_cast<size_t>(x) * 4;
            planeB[x] = static_cast<float>(pixel[0]);
            planeG[x] = static_cast<float>(pixel[1]);
            planeR[x] = static_cast<float>(pixel[2]);
            planeA[x] = static_cast<float>(pixel[3]);
        }
    }
}

// 水平一维高斯卷积（边缘 replicate；中段 SSE2 一次 4 像素）
void convolveHorizontal(const std::vector<float>& src, int32_t width, int32_t height,
                        const std::vector<float>& kernel, int32_t radius,
                        std::vector<float>& dst) {
    dst.resize(src.size());
    const int32_t taps = radius * 2 + 1;

    for (int32_t y = 0; y < height; ++y) {
        const float* in = src.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        float* out = dst.data() + static_cast<size_t>(y) * static_cast<size_t>(width);

        int32_t x = 0;
        const int32_t leftEdge = std::min(radius, width);
        for (; x < leftEdge; ++x) {
            float sum = 0.0f;
            for (int32_t t = 0; t < taps; ++t) {
                int32_t sx = x + t - radius;
                sx = std::max(0, std::min(sx, width - 1));
                sum += in[sx] * kernel[static_cast<size_t>(t)];
            }
            out[x] = sum;
        }

#if MODERNUI_BLUR_SSE2
        for (; x + 3 < width - radius; x += 4) {
            __m128 accum = _mm_setzero_ps();
            for (int32_t t = 0; t < taps; ++t) {
                const __m128 weight = _mm_set1_ps(kernel[static_cast<size_t>(t)]);
                accum = _mm_add_ps(accum, _mm_mul_ps(weight, _mm_loadu_ps(in + x + t - radius)));
            }
            _mm_storeu_ps(out + x, accum);
        }
#endif

        for (; x < width; ++x) {
            float sum = 0.0f;
            for (int32_t t = 0; t < taps; ++t) {
                int32_t sx = x + t - radius;
                sx = std::max(0, std::min(sx, width - 1));
                sum += in[sx] * kernel[static_cast<size_t>(t)];
            }
            out[x] = sum;
        }
    }
}

// 垂直一维高斯卷积 + 写回交错 PRGB32 的某个通道
void convolveVertical(const std::vector<float>& src, int32_t width, int32_t height,
                      const std::vector<float>& kernel, int32_t radius,
                      int32_t channel, uint8_t* dst, intptr_t dstStride) {
    const int32_t taps = radius * 2 + 1;

    for (int32_t y = 0; y < height; ++y) {
        uint8_t* outRow = dst + static_cast<size_t>(y) * dstStride;

        int32_t x = 0;
#if MODERNUI_BLUR_SSE2
        for (; x + 3 < width; x += 4) {
            __m128 accum = _mm_setzero_ps();
            for (int32_t t = 0; t < taps; ++t) {
                int32_t sy = std::max(0, std::min(y + t - radius, height - 1));
                const __m128 weight = _mm_set1_ps(kernel[static_cast<size_t>(t)]);
                accum = _mm_add_ps(
                    accum,
                    _mm_mul_ps(weight, _mm_loadu_ps(src.data() +
                        static_cast<size_t>(sy) * static_cast<size_t>(width) + x)));
            }
            float values[4];
            _mm_storeu_ps(values, accum);
            for (int32_t i = 0; i < 4; ++i) {
                outRow[static_cast<size_t>(x + i) * 4 + channel] = floatToByte(values[i]);
            }
        }
#endif

        for (; x < width; ++x) {
            float sum = 0.0f;
            for (int32_t t = 0; t < taps; ++t) {
                int32_t sy = std::max(0, std::min(y + t - radius, height - 1));
                sum += src[static_cast<size_t>(sy) * static_cast<size_t>(width) + x] *
                       kernel[static_cast<size_t>(t)];
            }
            outRow[static_cast<size_t>(x) * 4 + channel] = floatToByte(sum);
        }
    }
}

// 圆角覆盖率：像素中心到圆角矩形 SDF，0.5 - d 得到 1px 抗锯齿；
// 覆盖率乘到预乘 alpha 与 RGB 上（圆角外 alpha = 0）。
void applyRoundedMask(uint8_t* dst, intptr_t stride, int32_t width, int32_t height,
                      const Rect& target, const Rect& blurRect, float radius) {
    if (!(radius > 0.0f) || blurRect.w <= 0 || blurRect.h <= 0) return;

    const float halfWidth = static_cast<float>(blurRect.w) * 0.5f;
    const float halfHeight = static_cast<float>(blurRect.h) * 0.5f;
    const float r = std::min(radius, std::min(halfWidth, halfHeight));
    if (!(r > 0.0f)) return;

    const float centerX = static_cast<float>(blurRect.x) + halfWidth;
    const float centerY = static_cast<float>(blurRect.y) + halfHeight;
    const float innerHalfWidth = halfWidth - r;
    const float innerHalfHeight = halfHeight - r;

    for (int32_t y = 0; y < height; ++y) {
        uint8_t* row = dst + static_cast<size_t>(y) * stride;
        const float py = static_cast<float>(target.y + y) + 0.5f;
        const float dy = std::fabs(py - centerY) - innerHalfHeight;
        const float clampY = std::max(dy, 0.0f);

        for (int32_t x = 0; x < width; ++x) {
            const float px = static_cast<float>(target.x + x) + 0.5f;
            const float dx = std::fabs(px - centerX) - innerHalfWidth;
            const float clampX = std::max(dx, 0.0f);

            const float outside = std::sqrt(clampX * clampX + clampY * clampY);
            const float inside = std::min(std::max(dx, dy), 0.0f);
            const float coverage = std::max(0.0f, std::min(1.0f, 0.5f - (outside + inside - r)));
            if (coverage >= 1.0f) continue;

            uint8_t* pixel = row + static_cast<size_t>(x) * 4;
            for (size_t c = 0; c < 4; ++c) {
                pixel[c] = static_cast<uint8_t>(static_cast<float>(pixel[c]) * coverage + 0.5f);
            }
        }
    }
}

} // namespace

// 每个 Blur 几何体一份缓存：图像 = 目标区域（target）的模糊结果。
struct Blend2DRenderer::BlurCache {
    struct Entry {
        Rect     target{};
        Rect     geometry{};       // Blur 几何体自身矩形（圆角遮罩取决于它）
        float    radius = 0.0f;
        float    cornerRadius = 0.0f;
        uint64_t signature = 0;
        uint64_t lastUse = 0;
        BLImage  image;
    };

    static constexpr size_t kCapacity = 24;

    std::unordered_map<uint32_t, Entry> entries;

    // 卷积 scratch（跨帧复用，避免每次重建都重新分配）
    std::array<std::vector<float>, 4> planes;
    std::array<std::vector<float>, 4> horizontal;
    std::vector<float> kernel;
    float   kernelSigma  = -1.0f;
    int32_t kernelRadius = 0;

    uint64_t tick   = 0;
    uint64_t hits   = 0;
    uint64_t misses = 0;

    void clear() {
        entries.clear();
        kernel.clear();
        kernelSigma = -1.0f;
        kernelRadius = 0;
        tick = 0;
        hits = 0;
        misses = 0;
    }

    const std::vector<float>& kernelFor(float sigma, int32_t& radiusOut) {
        if (std::fabs(sigma - kernelSigma) > 1.0e-4f) {
            buildGaussianKernel(sigma, kernel, kernelRadius);
            kernelSigma = sigma;
        }
        radiusOut = kernelRadius;
        return kernel;
    }

    // LRU 淘汰后返回可写槽位
    Entry& prepare(uint32_t id) {
        if (entries.size() >= kCapacity) {
            auto found = entries.find(id);
            if (found == entries.end()) {
                auto oldest = entries.begin();
                for (auto it = entries.begin(); it != entries.end(); ++it) {
                    if (it->second.lastUse < oldest->second.lastUse) oldest = it;
                }
                entries.erase(oldest);
            }
        }
        return entries[id];
    }
};

Blend2DRenderer::BlurCache& Blend2DRenderer::blurCache() {
    if (!blurCache_) blurCache_ = std::make_unique<BlurCache>();
    return *blurCache_;
}

void Blend2DRenderer::clearBlurCache() {
    if (blurCache_) blurCache_->clear();
}

Blend2DRenderer::BlurCacheStats Blend2DRenderer::blurCacheStats() const {
    BlurCacheStats stats;
    if (!blurCache_) return stats;
    stats.entries = blurCache_->entries.size();
    stats.hits = blurCache_->hits;
    stats.misses = blurCache_->misses;
    return stats;
}

Blend2DRenderer::~Blend2DRenderer() {
    // 先释放 SVG / 字形 / Blur 缓存 / 图片资源，再解除对 DIB 外部内存的引用
    blurCache_.reset();
    glyphCache_.reset();
    svgCache_.clear();
    ctx_ = BLContext();
    surface_ = BLImage();
}

void Blend2DRenderer::drawBlur(const Shape& shape, const Vec2i& offset, const Rect& clip,
                               float opacity, uint64_t signature) {
    const BlurShape& blur = std::get<BlurShape>(shape.data);
    const Rect blurRect{blur.x + offset.x, blur.y + offset.y, blur.w, blur.h};
    const Rect target = blurRect.intersect(clip);
    if (target.isNull() || !(blur.radius > 0.1f)) return;

    const float sigma = std::min(blur.radius, 96.0f);
    const float cornerRadius = std::max(0.0f, blur.cornerRadius);

    BlurCache& cache = blurCache();
    BlurCache::Entry* entry = nullptr;

    auto found = cache.entries.find(shape.base.id);
    if (found != cache.entries.end()) {
        BlurCache::Entry& cached = found->second;
        if (!cached.image.is_empty() && cached.target == target &&
            cached.geometry == blurRect &&
            std::fabs(cached.radius - sigma) < 1.0e-4f &&
            std::fabs(cached.cornerRadius - cornerRadius) < 1.0e-4f &&
            cached.signature == signature) {
            cached.lastUse = ++cache.tick;
            entry = &cached;
            ++cache.hits;
        }
    }

    if (!entry) {
        // 先把已排队的绘制落到 DIB，回读到的才是真正"画在下面"的像素
        ctx_.flush(BL_CONTEXT_FLUSH_SYNC);

        BLImageData source{};
        if (surface_.get_data(&source) != BL_SUCCESS || !source.pixel_data ||
            source.format != BL_FORMAT_PRGB32 || source.stride <= 0) {
            return;
        }
        if (target.x < 0 || target.y < 0 ||
            target.right() > source.size.w || target.bottom() > source.size.h) {
            return;
        }

        BLImage blurred(target.w, target.h, BL_FORMAT_PRGB32);
        BLImageData output{};
        if (blurred.make_mutable(&output) != BL_SUCCESS || !output.pixel_data) return;

        const uint8_t* sourcePixels = static_cast<const uint8_t*>(source.pixel_data) +
            static_cast<size_t>(target.y) * source.stride +
            static_cast<size_t>(target.x) * 4;
        uint8_t* outputPixels = static_cast<uint8_t*>(output.pixel_data);

        int32_t kernelRadius = 0;
        const std::vector<float>& kernel = cache.kernelFor(sigma, kernelRadius);

        unpackToPlanes(sourcePixels, source.stride, target.w, target.h, cache.planes);
        for (size_t channel = 0; channel < 4; ++channel) {
            convolveHorizontal(cache.planes[channel], target.w, target.h,
                               kernel, kernelRadius, cache.horizontal[channel]);
            convolveVertical(cache.horizontal[channel], target.w, target.h,
                             kernel, kernelRadius, static_cast<int32_t>(channel),
                             outputPixels, output.stride);
        }
        applyRoundedMask(outputPixels, output.stride, target.w, target.h,
                         target, blurRect, cornerRadius);

        BlurCache::Entry& fresh = cache.prepare(shape.base.id);
        fresh.target = target;
        fresh.geometry = blurRect;
        fresh.radius = sigma;
        fresh.cornerRadius = cornerRadius;
        fresh.signature = signature;
        fresh.lastUse = ++cache.tick;
        fresh.image = std::move(blurred);
        entry = &fresh;
        ++cache.misses;
    }

    // 表面绝对坐标 blit（renderTile 的 clip / translate 已在 save 外层，这里自管理）
    ctx_.save();
    ctx_.set_comp_op(BL_COMP_OP_SRC_OVER);
    if (opacity < 0.999f) ctx_.set_global_alpha(static_cast<double>(opacity));
    ctx_.blit_image(
        BLRect(static_cast<double>(target.x), static_cast<double>(target.y),
               static_cast<double>(target.w), static_cast<double>(target.h)),
        entry->image, BLRectI(0, 0, target.w, target.h));
    ctx_.restore();
}

// ============================================================================
// Surface 绑定
// ============================================================================

bool Blend2DRenderer::syncSurface() {
    if (!window_ || !window_->valid()) return false;

    const uint64_t serial = window_->surfaceSerial();
    if (serial == surfaceSerial_ && ctx_.is_valid()) return true;

    // 旧 BLImage 引用的 DIB 可能已被窗口重建，先彻底解绑
    ctx_ = BLContext();
    surface_ = BLImage();
    surfaceSerial_ = serial;
    surfaceWidth_ = 0;
    surfaceHeight_ = 0;

    uint8_t* pixels = window_->pixels();
    const int32_t width = window_->width();
    const int32_t height = window_->height();
    if (!pixels || width <= 0 || height <= 0) return false;

    BLImage image;
    BLResult result = image.create_from_data(
        width, height, BL_FORMAT_PRGB32,
        pixels, window_->stride(),
        BL_DATA_ACCESS_RW, nullptr, nullptr);
    if (result != BL_SUCCESS) return false;

    BLContextCreateInfo createInfo{};
    // 0 = 同步渲染：不占用线程池，小 Tile 增量绘制下 CPU 开销更低
    createInfo.thread_count = 0;

    BLContext context(image, createInfo);
    if (!context.is_valid()) return false;
    context.set_comp_op(BL_COMP_OP_SRC_OVER);

    surface_ = std::move(image);
    ctx_ = std::move(context);
    surfaceWidth_ = width;
    surfaceHeight_ = height;
    // 新表面 = 全新像素：旧模糊结果不能复用
    if (blurCache_) blurCache_->entries.clear();
    return true;
}

// ============================================================================
// 字体
// ============================================================================

bool Blend2DRenderer::loadFontFile(const std::string& path, uint32_t faceIndex,
                                   std::string* error) {
    if (path.empty()) {
        if (error) *error = "Empty font path.";
        return false;
    }

    BLFontFace face;
    BLResult result;
    if (faceIndex == 0) {
        result = face.create_from_file(path.c_str());
    } else {
        BLFontData data;
        result = data.create_from_file(path.c_str());
        if (result == BL_SUCCESS) {
            result = face.create_from_data(data, faceIndex);
        }
    }

    if (result != BL_SUCCESS || !face.is_valid()) {
        if (error) *error = blError("Failed to load font face", result);
        return false;
    }

    fontFace_ = std::move(face);
    fontReady_ = true;
    fonts_.clear();
    ++fontGeneration_;      // 让旧字体的 shaping / 蒙版全部失效
    clearGlyphCache();
    clearBlurCache();       // 字形光栅变化 → 底图（文字）像素也变了
    return true;
}

bool Blend2DRenderer::loadDefaultFont(std::string* error) {
    if (fontReady_) return true;

    wchar_t windowsDir[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(windowsDir, MAX_PATH);
    if (length == 0) {
        if (error) *error = "GetWindowsDirectoryW failed.";
        return false;
    }

    std::wstring fontDir(windowsDir, length);
    if (!fontDir.empty() && fontDir.back() != L'\\') fontDir += L'\\';
    fontDir += L"Fonts\\";

    // 优先中英文字形齐全的字体，避免中文 UI 出现缺字方框。
    const wchar_t* candidates[] = {
        L"msyh.ttc",        // Microsoft YaHei（中英文）
        L"segoeui.ttf",     // Segoe UI
        L"arial.ttf",
        L"tahoma.ttf",
        L"verdana.ttf",
    };

    for (const wchar_t* name : candidates) {
        const std::string path = wideToUtf8(fontDir + name);
        std::string localError;
        if (loadFontFile(path, 0, &localError)) return true;
    }

    if (error) *error = "No usable system font found (msyh/segoeui/arial/tahoma/verdana).";
    return false;
}

const BLFont* Blend2DRenderer::fontForSize(int32_t pixelSize) {
    if (!fontReady_ || !fontFace_.is_valid()) return nullptr;

    const int32_t size = std::max<int32_t>(1, std::min<int32_t>(pixelSize, 512));
    auto it = fonts_.find(size);
    if (it != fonts_.end()) return &it->second;

    BLFont font;
    if (font.create_from_face(fontFace_, static_cast<float>(size)) != BL_SUCCESS) {
        return nullptr;
    }
    if (!font.is_valid()) return nullptr;

    auto inserted = fonts_.emplace(size, std::move(font));
    return &inserted.first->second;
}

// ============================================================================
// 图片注册表
// ============================================================================

uint64_t Blend2DRenderer::loadImageFromFile(const std::string& path, std::string* error) {
    if (path.empty()) {
        if (error) *error = "Empty image path.";
        return 0;
    }

    BLImage image;
    BLResult result = image.read_from_file(path.c_str());
    if (result != BL_SUCCESS) {
        if (error) *error = blError("Failed to decode image file", result);
        return 0;
    }

    const uint64_t handle = nextImageHandle_++;
    images_.emplace(handle, std::move(image));
    return handle;
}

uint64_t Blend2DRenderer::loadImageFromMemory(const void* data, size_t size,
                                              std::string* error) {
    if (!data || size == 0) {
        if (error) *error = "Empty image memory buffer.";
        return 0;
    }

    BLImage image;
    BLResult result = image.read_from_data(data, size);
    if (result != BL_SUCCESS) {
        if (error) *error = blError("Failed to decode image memory", result);
        return 0;
    }

    const uint64_t handle = nextImageHandle_++;
    images_.emplace(handle, std::move(image));
    return handle;
}

uint64_t Blend2DRenderer::createImageFromPixels(int32_t width, int32_t height,
                                                const void* pixels,
                                                int32_t stride,
                                                std::string* error) {
    if (width <= 0 || height <= 0 || !pixels) {
        if (error) *error = "Invalid pixel buffer.";
        return 0;
    }

    BLImage image(width, height, BL_FORMAT_PRGB32);
    BLImageData data;
    BLResult result = image.get_data(&data);
    if (result != BL_SUCCESS) {
        if (error) *error = blError("Failed to allocate image", result);
        return 0;
    }

    const int32_t sourceStride = stride > 0 ? stride : width * 4;
    const int32_t rowBytes = std::min(width * 4, static_cast<int32_t>(data.stride));
    if (rowBytes <= 0) return 0;

    const uint8_t* src = static_cast<const uint8_t*>(pixels);
    uint8_t* dst = static_cast<uint8_t*>(data.pixel_data);
    for (int32_t y = 0; y < height; ++y) {
        std::memcpy(dst + static_cast<ptrdiff_t>(y) * data.stride,
                    src + static_cast<ptrdiff_t>(y) * sourceStride,
                    static_cast<size_t>(rowBytes));
    }

    const uint64_t handle = nextImageHandle_++;
    images_.emplace(handle, std::move(image));
    return handle;
}

uint64_t Blend2DRenderer::createBitmapImage(int32_t width, int32_t height, Color clear) {
    if (width <= 0 || height <= 0) return 0;

    BLImage image;
    if (image.create(width, height, BL_FORMAT_PRGB32) != BL_SUCCESS) return 0;

    BLContext context(image);
    if (!context.is_valid()) return 0;
    context.set_comp_op(BL_COMP_OP_SRC_COPY);
    context.fill_all(BLRgba32(clear.r, clear.g, clear.b, clear.a));
    context.flush(BL_CONTEXT_FLUSH_SYNC);

    const uint64_t handle = nextImageHandle_++;
    images_.emplace(handle, std::move(image));
    return handle;
}

BLImage* Blend2DRenderer::image(uint64_t handle) {
    auto it = images_.find(handle);
    return it == images_.end() ? nullptr : &it->second;
}

const BLImage* Blend2DRenderer::image(uint64_t handle) const {
    auto it = images_.find(handle);
    return it == images_.end() ? nullptr : &it->second;
}

bool Blend2DRenderer::imageSize(uint64_t handle, int32_t& width, int32_t& height) const {
    auto it = images_.find(handle);
    if (it == images_.end()) return false;
    width = it->second.width();
    height = it->second.height();
    return true;
}

// ============================================================================
// 磁盘资源加载（图片 / SVG）
// ============================================================================

uint64_t Blend2DRenderer::loadImageFile(const std::string& path, std::string* error) {
    if (path.empty()) {
        if (error) *error = "Empty image path.";
        return 0;
    }

    // 同路径只解码一次；句柄被释放后失效的缓存条目自动作废
    auto cached = imagePathCache_.find(path);
    if (cached != imagePathCache_.end() && images_.find(cached->second) != images_.end()) {
        return cached->second;
    }

    const uint64_t handle = loadImageFromFile(path, error);
    if (handle != 0) imagePathCache_[path] = handle;
    return handle;
}

bool Blend2DRenderer::loadSvgFile(const std::string& path, std::string& out,
                                  std::string* error) {
    if (path.empty()) {
        if (error) *error = "Empty SVG path.";
        return false;
    }

    auto cached = svgPathCache_.find(path);
    if (cached != svgPathCache_.end()) {
        out = cached->second;
        return true;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "Cannot open SVG file: " + path;
        return false;
    }

    std::string markup((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    if (markup.empty()) {
        if (error) *error = "Empty SVG file: " + path;
        return false;
    }

    // 缓存原文即可：解析结果由 svgCache_ 按内容哈希统一管理，
    // 同一 SVG 用于多个不同尺寸的布局矩形时不会重复解析。
    if (svgPathCache_.size() >= 256) svgPathCache_.clear();
    svgPathCache_[path] = markup;

    out = std::move(markup);
    return true;
}

bool Blend2DRenderer::releaseImage(uint64_t handle) {
    if (images_.erase(handle) == 0) return false;
    // 路径缓存里指向该句柄的条目一并作废（下次加载会重新解码）
    for (auto it = imagePathCache_.begin(); it != imagePathCache_.end();) {
        it = (it->second == handle) ? imagePathCache_.erase(it) : std::next(it);
    }
    return true;
}

void Blend2DRenderer::clearImages() {
    images_.clear();
    imagePathCache_.clear();
}

// ============================================================================
// SVG 解析 + 缓存
// ============================================================================

Blend2DRenderer::SvgDocument Blend2DRenderer::parseSvg(const std::string& markup) {
    SvgDocument document;
    if (markup.empty()) return document;

    // nsvgParse 会原地修改输入字符串，必须传入可写的 null-terminated 缓冲区
    std::vector<char> buffer(markup.begin(), markup.end());
    buffer.push_back('\0');

    NSVGimage* image = nsvgParse(buffer.data(), "px", 96.0f);
    if (!image) return document;

    document.width = image->width;
    document.height = image->height;

    for (const NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next) {
        if (!(shape->flags & NSVG_FLAGS_VISIBLE)) continue;

        SvgCachedShape cached;
        cached.path = buildSvgPath(shape);
        if (cached.path.is_empty()) continue;

        cached.fillRule = svgFillRule(shape->fillRule);
        cached.fill = buildSvgPaint(shape->fill, shape->opacity);
        cached.stroke = buildSvgPaint(shape->stroke, shape->opacity);
        if (cached.fill.type == SvgPaintType::None &&
            cached.stroke.type == SvgPaintType::None) {
            continue;
        }

        cached.strokeWidth = shape->strokeWidth;
        cached.miterLimit = shape->miterLimit;
        cached.dashOffset = shape->strokeDashOffset;
        cached.strokeCap = static_cast<uint8_t>(svgCap(shape->strokeLineCap));
        cached.strokeJoin = static_cast<uint8_t>(svgJoin(shape->strokeLineJoin));
        cached.dashArray.reserve(static_cast<size_t>(shape->strokeDashCount));
        for (int i = 0; i < shape->strokeDashCount; ++i) {
            cached.dashArray.push_back(shape->strokeDashArray[i]);
        }

        document.shapes.push_back(std::move(cached));
    }

    nsvgDelete(image);
    document.valid = true;
    return document;
}

const Blend2DRenderer::SvgDocument* Blend2DRenderer::svgDocument(const SvgShape& svg) {
    const uint64_t key = hashSvg(svg.data);

    auto it = svgCache_.find(key);
    if (it != svgCache_.end()) {
        it->second->lastUse = ++svgCacheTick_;
        return it->second.get();
    }

    std::unique_ptr<SvgDocument> document(new SvgDocument(parseSvg(svg.data)));
    document->lastUse = ++svgCacheTick_;

    if (svgCache_.size() >= kSvgCacheCapacity) {
        auto oldest = svgCache_.begin();
        for (auto i = svgCache_.begin(); i != svgCache_.end(); ++i) {
            if (i->second->lastUse < oldest->second->lastUse) oldest = i;
        }
        svgCache_.erase(oldest);
    }

    auto inserted = svgCache_.emplace(key, std::move(document));
    return inserted.first->second.get();
}

void Blend2DRenderer::clearSvgCache() {
    svgCache_.clear();
}

// ============================================================================
// 帧驱动
// ============================================================================

void Blend2DRenderer::beginFrame(const CanvasInfo& info) {
    frameDirty_ = false;
    frameDirtyRect_ = info.totalDirty;
    frameActive_ = syncSurface();
}

void Blend2DRenderer::endFrame() {
    if (!frameActive_) return;

    if (frameDirty_) {
        ctx_.flush(BL_CONTEXT_FLUSH_SYNC);

        if (window_) {
            // 只提交本帧真正重绘的区域；未变化区域的 DIB 内容与上次提交一致。
            // 覆盖整窗时退回全量提交（部分系统对 prcDirty 的实现存在差异）。
            const Rect bounds{0, 0, window_->width(), window_->height()};
            Rect dirty = frameDirtyRect_.intersect(bounds);
            const bool full = dirty.isNull() ||
                              (dirty.x <= 0 && dirty.y <= 0 &&
                               dirty.w >= bounds.w && dirty.h >= bounds.h);
            window_->present(full ? nullptr : &dirty);
        }
    }
    frameActive_ = false;
}

void Blend2DRenderer::renderTile(const CanvasInfo& info, const DirtyEntry& entry) {
    if (!frameActive_ || !ctx_.is_valid()) return;

    Rect region = entry.region.intersect(Rect{0, 0, surfaceWidth_, surfaceHeight_});
    if (region.isNull()) return;

    const BLRectI regionRect(region.x, region.y, region.w, region.h);

    // 整屏重绘时裁剪区与 Surface 完全一致：跳过 clip（省一次裁剪区光栅化）
    const bool fullRegion = region.x == 0 && region.y == 0 &&
                            region.w == surfaceWidth_ && region.h == surfaceHeight_;

    ctx_.save();
    if (!fullRegion) ctx_.clip_to_rect(regionRect);

    // 用背景色覆盖脏区域（SRC_COPY 保证 alpha 也被正确重置）
    ctx_.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx_.fill_rect(regionRect, toBL(info.backgroundColor));
    ctx_.set_comp_op(BL_COMP_OP_SRC_OVER);

    // 相邻 Shape 通常共享同一 (clip, offset, opacity)（同一控件的多个几何体），
    // 复用一次 save / clip / translate / global-alpha，避免逐个 Shape 设置上下文状态。
    bool runActive = false;      // 当前是否有为某个 run 建立的嵌套 save
    bool havePrev = false;
    Rect prevClip;
    Vec2i prevOffset;
    float prevOpacity = 1.0f;

    for (size_t index = 0; index < entry.shapes.size(); ++index) {
        const DirtyShapeRef& ref = entry.shapes[index];
        if (!ref.shape || !ref.shape->base.visible) continue;

        // Canvas 侧已给出 Tile ∩ 各级 Group clip 的最终绝对裁剪区
        const Rect clip = ref.clipRect.intersect(region);
        if (clip.isNull()) continue;

        const float opacity = std::max(0.0f, std::min(1.0f, ref.opacity));
        if (opacity <= 0.001f) continue;

        // Blur 几何体：回读表面 → 高斯模糊 → 覆盖回自身区域。
        // 它需要"表面绝对坐标"与独立的上下文状态，因此先结束当前的 run，
        // 再走自己的 save / clip / blit 流程。
        if (ref.shape->type() == Shape::Type::Blur) {
            if (runActive) {
                ctx_.restore();
                runActive = false;
            }
            havePrev = false;
            const uint64_t signature = blurSignature(info, entry.shapes, index, clip);
            drawBlur(*ref.shape, ref.accumulatedOffset, clip, opacity, signature);
            continue;
        }

        const bool fullAlpha = opacity >= 0.999f;

        const bool sameRun = havePrev && clip == prevClip &&
                             ref.accumulatedOffset == prevOffset &&
                             std::fabs(opacity - prevOpacity) < 1.0e-3f;
        if (!sameRun) {
            if (runActive) {
                ctx_.restore();
                runActive = false;
            }

            const bool trivialClip = (clip == region);
            const bool noOffset = ref.accumulatedOffset.x == 0 &&
                                  ref.accumulatedOffset.y == 0;
            if (!trivialClip || !noOffset || !fullAlpha) {
                ctx_.save();
                runActive = true;
                if (!trivialClip) {
                    ctx_.clip_to_rect(BLRectI(clip.x, clip.y, clip.w, clip.h));
                }
                if (!noOffset) {
                    ctx_.translate(static_cast<double>(ref.accumulatedOffset.x),
                                   static_cast<double>(ref.accumulatedOffset.y));
                }
                if (!fullAlpha) {
                    ctx_.set_global_alpha(static_cast<double>(opacity));
                }
            }

            prevClip = clip;
            prevOffset = ref.accumulatedOffset;
            prevOpacity = opacity;
            havePrev = true;
        }

        // 旋转：围绕 Shape 未旋转包围盒中心。渲染开销只作用于显式设置了
        // rotation 的几何体；普通控件完全走无变换快路径。
        if (ref.shape->base.rotation != 0.0f) {
            const Rect local = ref.shape->unrotatedBounds();
            if (local.isNull()) continue;
            const double pivotX = static_cast<double>(local.x) + static_cast<double>(local.w) * 0.5;
            const double pivotY = static_cast<double>(local.y) + static_cast<double>(local.h) * 0.5;

            ctx_.save();
            ctx_.translate(pivotX, pivotY);
            ctx_.rotate(static_cast<double>(ref.shape->base.rotation) * 3.14159265358979323846 / 180.0);
            ctx_.translate(-pivotX, -pivotY);
            drawShape(*ref.shape);
            ctx_.restore();
        } else {
            drawShape(*ref.shape);
        }
    }

    if (runActive) ctx_.restore();
    ctx_.restore();

    frameDirty_ = true;
}

// ============================================================================
// Text 测量
// ============================================================================

std::optional<Rect> Blend2DRenderer::measureText(const TextShape& text) {
    const BLFont* font = fontForSize(text.fontSize);
    if (!font) return std::nullopt;

    // 命中 shaping 缓存时度量也是 O(1)：布局阶段的重复测量不再触发字形映射。
    if (!text.text.empty()) {
        const GlyphCache::RunEntry* entry =
            glyphCache().acquireRun(text.text, text.fontSize, fontGeneration_, *font);
        if (entry) {
            return Rect{text.x + entry->measured.x, text.y + entry->measured.y,
                        std::max(1, entry->measured.w), std::max(1, entry->measured.h)};
        }
    }

    // 空文本 / 缓存失败：保持原有直算路径
    BLGlyphBuffer glyphBuffer;
    if (glyphBuffer.set_utf8_text(text.text.c_str(), text.text.size()) != BL_SUCCESS) {
        return std::nullopt;
    }

    MeasureMetrics metrics;
    if (!computeMetrics(*font, glyphBuffer, metrics)) return std::nullopt;

    return Rect{text.x + metrics.x, text.y + metrics.y,
                std::max(1, metrics.w), std::max(1, metrics.h)};
}

// ============================================================================
// Paint → 路径填充 / 描边
// ============================================================================

bool Blend2DRenderer::fillPath(const Paint& paint, const BLPath& path) {
    BLRgba32 solid;
    BLGradient gradient;

    switch (prepareBrush(paint, solid, gradient)) {
    case BrushKind::None:
        return false;
    case BrushKind::Solid:
        ctx_.fill_path(path, solid);
        return true;
    case BrushKind::Gradient:
        ctx_.fill_path(path, gradient);
        return true;
    }
    return false;
}

bool Blend2DRenderer::strokePath(const Paint& paint, const BLPath& path,
                                 int32_t strokeWidth) {
    BLRgba32 solid;
    BLGradient gradient;

    const BrushKind kind = prepareBrush(paint, solid, gradient);
    if (kind == BrushKind::None) return false;

    ctx_.set_stroke_width(std::max(1.0, static_cast<double>(strokeWidth)));
    if (kind == BrushKind::Solid) ctx_.stroke_path(path, solid);
    else                          ctx_.stroke_path(path, gradient);
    return true;
}

// ============================================================================
// SVG 绘制（布局变换 + 缓存路径；渐变变换在缓存中，随 context 变换自动复合）
// ============================================================================

void Blend2DRenderer::drawSvg(const SvgShape& svg) {
    if (svg.w <= 0 || svg.h <= 0) return;

    const SvgDocument* document = svgDocument(svg);
    if (!document || !document->valid || document->shapes.empty()) return;

    const double imageWidth = document->width;
    const double imageHeight = document->height;
    if (!(imageWidth > 0.0) || !(imageHeight > 0.0)) return;

    double scaleX = static_cast<double>(svg.w) / imageWidth;
    double scaleY = static_cast<double>(svg.h) / imageHeight;
    double translateX = static_cast<double>(svg.x);
    double translateY = static_cast<double>(svg.y);

    if (svg.preserveAspectRatio) {
        const double uniform = std::min(scaleX, scaleY);
        scaleX = uniform;
        scaleY = uniform;
        translateX += (static_cast<double>(svg.w) - imageWidth * uniform) * 0.5;
        translateY += (static_cast<double>(svg.h) - imageHeight * uniform) * 0.5;
    }

    ctx_.save();
    ctx_.translate(translateX, translateY);
    ctx_.scale(scaleX, scaleY);

    for (const SvgCachedShape& shape : document->shapes) {
        if (shape.fill.type != SvgPaintType::None) {
            ctx_.set_fill_rule(shape.fillRule);
            if (shape.fill.type == SvgPaintType::Gradient) {
                ctx_.fill_path(shape.path, shape.fill.gradient);
            } else {
                ctx_.fill_path(shape.path, shape.fill.color);
            }
        }

        if (shape.stroke.type != SvgPaintType::None && shape.strokeWidth > 0.0) {
            BLStrokeOptions options;
            options.width = shape.strokeWidth;
            options.miter_limit = shape.miterLimit;
            options.dash_offset = shape.dashOffset;
            options.set_caps(static_cast<BLStrokeCap>(shape.strokeCap));
            options.join = shape.strokeJoin;
            if (!shape.dashArray.empty()) {
                options.dash_array.assign_data(shape.dashArray.data(),
                                               shape.dashArray.size());
            }
            ctx_.set_stroke_options(options);

            if (shape.stroke.type == SvgPaintType::Gradient) {
                ctx_.stroke_path(shape.path, shape.stroke.gradient);
            } else {
                ctx_.stroke_path(shape.path, shape.stroke.color);
            }
        }
    }

    ctx_.restore();
}

// ============================================================================
// Shape 绘制（坐标系：Shape 局部坐标；clip / offset 由 renderTile 设置）
// ============================================================================

void Blend2DRenderer::drawShape(const Shape& shape) {
    const ShapeBase& base = shape.base;

    switch (shape.type()) {
    case Shape::Type::Rect: {
        const auto& s = std::get<RectShape>(shape.data);
        if (s.w <= 0 || s.h <= 0) break;

        // 快路径：纯色填充（+ 纯色描边）直接画矩形，省掉 BLPath 构建与路径光栅化。
        // 面板 / 背景 / 分隔线等大量几何体都属于这一类。
        const Color* fillSolid = base.fill.solidColor();
        const Color* strokeSolid = base.stroke.isVisible() ? base.stroke.solidColor() : nullptr;
        if (fillSolid && fillSolid->a > 0 && (!base.stroke.isVisible() || strokeSolid)) {
            ctx_.fill_rect(BLRectI(s.x, s.y, s.w, s.h), toBL(*fillSolid));
            if (strokeSolid) {
                ctx_.set_stroke_width(std::max(1.0, static_cast<double>(base.strokeWidth)));
                ctx_.stroke_rect(BLRectI(s.x, s.y, s.w, s.h), toBL(*strokeSolid));
            }
            break;
        }

        BLPath path;
        path.add_rect(BLRect(s.x, s.y, s.w, s.h));
        fillPath(base.fill, path);
        if (base.stroke.isVisible()) strokePath(base.stroke, path, base.strokeWidth);
        break;
    }

    case Shape::Type::RRect: {
        const auto& s = std::get<RRectShape>(shape.data);
        if (s.w <= 0 || s.h <= 0) break;

        const double rx = clampRadius(s.radiusX, s.w, s.h);
        const double ry = clampRadius(s.radiusY, s.w, s.h);
        const BLRect rect(s.x, s.y, s.w, s.h);

        // 快路径：纯色填充（+ 纯色描边）直接画圆角矩形
        const Color* fillSolid = base.fill.solidColor();
        const Color* strokeSolid = base.stroke.isVisible() ? base.stroke.solidColor() : nullptr;
        if (fillSolid && fillSolid->a > 0 && (!base.stroke.isVisible() || strokeSolid)) {
            ctx_.fill_round_rect(rect, rx, ry, toBL(*fillSolid));
            if (strokeSolid) {
                ctx_.set_stroke_width(std::max(1.0, static_cast<double>(base.strokeWidth)));
                ctx_.stroke_round_rect(rect, rx, ry, toBL(*strokeSolid));
            }
            break;
        }

        BLPath path;
        path.add_round_rect(BLRoundRect(rect, rx, ry));
        fillPath(base.fill, path);
        if (base.stroke.isVisible()) strokePath(base.stroke, path, base.strokeWidth);
        break;
    }

    case Shape::Type::Circle: {
        const auto& s = std::get<CircleShape>(shape.data);
        const double radius = std::fabs(static_cast<double>(s.r));
        if (!(radius > 0.0)) break;

        const BLCircle circle(static_cast<double>(s.cx), static_cast<double>(s.cy), radius);
        const Color* fillSolid = base.fill.solidColor();
        const Color* strokeSolid = base.stroke.isVisible() ? base.stroke.solidColor() : nullptr;
        if (fillSolid && fillSolid->a > 0 && (!base.stroke.isVisible() || strokeSolid)) {
            ctx_.fill_circle(circle, toBL(*fillSolid));
            if (strokeSolid) {
                ctx_.set_stroke_width(std::max(1.0, static_cast<double>(base.strokeWidth)));
                ctx_.stroke_circle(circle, toBL(*strokeSolid));
            }
            break;
        }

        BLPath path;
        path.add_circle(circle);
        fillPath(base.fill, path);
        if (base.stroke.isVisible()) strokePath(base.stroke, path, base.strokeWidth);
        break;
    }

    case Shape::Type::Ellipse: {
        const auto& s = std::get<EllipseShape>(shape.data);
        const double rx = std::fabs(static_cast<double>(s.rx));
        const double ry = std::fabs(static_cast<double>(s.ry));
        if (!(rx > 0.0) || !(ry > 0.0)) break;

        const BLEllipse ellipse(static_cast<double>(s.cx), static_cast<double>(s.cy), rx, ry);
        const Color* fillSolid = base.fill.solidColor();
        const Color* strokeSolid = base.stroke.isVisible() ? base.stroke.solidColor() : nullptr;
        if (fillSolid && fillSolid->a > 0 && (!base.stroke.isVisible() || strokeSolid)) {
            ctx_.fill_ellipse(ellipse, toBL(*fillSolid));
            if (strokeSolid) {
                ctx_.set_stroke_width(std::max(1.0, static_cast<double>(base.strokeWidth)));
                ctx_.stroke_ellipse(ellipse, toBL(*strokeSolid));
            }
            break;
        }

        BLPath path;
        path.add_ellipse(ellipse);
        fillPath(base.fill, path);
        if (base.stroke.isVisible()) strokePath(base.stroke, path, base.strokeWidth);
        break;
    }

    case Shape::Type::Arc: {
        const auto& s = std::get<ArcShape>(shape.data);
        const double rx = std::fabs(static_cast<double>(s.rx));
        const double ry = std::fabs(static_cast<double>(s.ry));
        if (!(rx > 0.0) || !(ry > 0.0) || std::fabs(s.sweepAngle) < 1e-4f) break;

        // 圆弧是开放路径：stroke 为主，未设置 stroke 时用 fill 兜底。
        const Paint& paint = base.stroke.isVisible() ? base.stroke : base.fill;
        if (!paint.isVisible()) break;

        BLPath path;
        path.arc_to(static_cast<double>(s.cx), static_cast<double>(s.cy),
                    rx, ry, degToRad(s.startAngle), degToRad(s.sweepAngle), true);
        strokePath(paint, path, base.strokeWidth);
        break;
    }

    case Shape::Type::Pie: {
        const auto& s = std::get<PieShape>(shape.data);
        const double rx = std::fabs(static_cast<double>(s.rx));
        const double ry = std::fabs(static_cast<double>(s.ry));
        if (!(rx > 0.0) || !(ry > 0.0) || std::fabs(s.sweepAngle) < 1e-4f) break;

        BLPath path = buildPiePath(s);
        fillPath(base.fill, path);
        if (base.stroke.isVisible()) strokePath(base.stroke, path, base.strokeWidth);
        break;
    }

    case Shape::Type::Ring: {
        const auto& s = std::get<RingShape>(shape.data);
        const double rx = std::fabs(static_cast<double>(s.rx));
        const double ry = std::fabs(static_cast<double>(s.ry));
        if (!(rx > 0.0) || !(ry > 0.0)) break;

        BLPath path;
        path.add_ellipse(BLEllipse(static_cast<double>(s.cx),
                                   static_cast<double>(s.cy), rx, ry));

        const double thickness = std::max(0.0, static_cast<double>(s.thickness));
        const double innerRx = rx - thickness;
        const double innerRy = ry - thickness;
        if (innerRx > 0.0 && innerRy > 0.0) {
            path.add_ellipse(BLEllipse(static_cast<double>(s.cx),
                                       static_cast<double>(s.cy), innerRx, innerRy));
        }

        // 偶奇填充：外圈 + 内圈 → 环
        ctx_.set_fill_rule(BL_FILL_RULE_EVEN_ODD);
        fillPath(base.fill, path);
        ctx_.set_fill_rule(BL_FILL_RULE_NON_ZERO);

        if (base.stroke.isVisible()) strokePath(base.stroke, path, base.strokeWidth);
        break;
    }

    case Shape::Type::Star: {
        const auto& s = std::get<StarShape>(shape.data);
        if (!(std::fabs(s.outerRadius) > 0.0f)) break;
        BLPath path = buildStarPath(s);
        fillPath(base.fill, path);
        if (base.stroke.isVisible()) strokePath(base.stroke, path, base.strokeWidth);
        break;
    }

    case Shape::Type::Polygon: {
        const auto& s = std::get<PolygonShape>(shape.data);
        if (!(std::fabs(s.radius) > 0.0f)) break;
        BLPath path = buildPolygonPath(s);
        fillPath(base.fill, path);
        if (base.stroke.isVisible()) strokePath(base.stroke, path, base.strokeWidth);
        break;
    }

    case Shape::Type::Line: {
        const auto& s = std::get<LineShape>(shape.data);
        const Paint& paint = base.stroke.isVisible() ? base.stroke : base.fill;
        BLPath path;
        path.move_to(s.x1, s.y1);
        path.line_to(s.x2, s.y2);
        strokePath(paint, path, base.strokeWidth);
        break;
    }

    case Shape::Type::Polyline: {
        const auto& s = std::get<PolylineShape>(shape.data);
        if (s.points.empty()) break;

        BLPath path;
        path.move_to(s.points.front().x, s.points.front().y);
        for (size_t i = 1; i < s.points.size(); ++i) {
            path.line_to(s.points[i].x, s.points[i].y);
        }
        if (s.closed) path.close();

        if (s.closed) fillPath(base.fill, path);

        const Paint& strokePaint = base.stroke.isVisible() ? base.stroke : base.fill;
        strokePath(strokePaint, path, base.strokeWidth);
        break;
    }

    case Shape::Type::Path: {
        const auto& s = std::get<PathShape>(shape.data);
        if (s.steps.empty()) break;

        BLPath path;
        for (const auto& step : s.steps) {
            switch (step.cmd) {
            case PathShape::Cmd::MoveTo:
                path.move_to(step.pts[0].x, step.pts[0].y);
                break;
            case PathShape::Cmd::LineTo:
                path.line_to(step.pts[0].x, step.pts[0].y);
                break;
            case PathShape::Cmd::QuadTo:
                path.quad_to(step.pts[0].x, step.pts[0].y,
                             step.pts[1].x, step.pts[1].y);
                break;
            case PathShape::Cmd::CubicTo:
                path.cubic_to(step.pts[0].x, step.pts[0].y,
                              step.pts[1].x, step.pts[1].y,
                              step.pts[2].x, step.pts[2].y);
                break;
            case PathShape::Cmd::Close:
                path.close();
                break;
            }
        }

        fillPath(base.fill, path);
        if (base.stroke.isVisible()) strokePath(base.stroke, path, base.strokeWidth);
        break;
    }

    case Shape::Type::Text: {
        const auto& s = std::get<TextShape>(shape.data);
        const Paint& paint = base.fill.isVisible() ? base.fill : base.stroke;
        if (paint.isVisible()) drawText(s, paint);
        break;
    }

    case Shape::Type::Image: {
        const auto& s = std::get<ImageShape>(shape.data);
        if (s.handle == 0 || s.w <= 0 || s.h <= 0) break;
        auto it = images_.find(s.handle);
        if (it == images_.end()) break;

        // Sprite 图集：srcW/srcH > 0 时裁切源矩形；否则整张纹理
        const int32_t imageWidth = it->second.width();
        const int32_t imageHeight = it->second.height();
        int32_t srcX = s.srcW > 0 ? s.srcX : 0;
        int32_t srcY = s.srcH > 0 ? s.srcY : 0;
        int32_t srcW = s.srcW > 0 ? s.srcW : imageWidth;
        int32_t srcH = s.srcH > 0 ? s.srcH : imageHeight;

        srcX = std::max(0, std::min(srcX, imageWidth));
        srcY = std::max(0, std::min(srcY, imageHeight));
        srcW = std::max(0, std::min(srcW, imageWidth - srcX));
        srcH = std::max(0, std::min(srcH, imageHeight - srcY));
        if (srcW <= 0 || srcH <= 0) break;

        ctx_.save();
        if (s.flipY) {
            ctx_.translate(static_cast<double>(s.x),
                           static_cast<double>(s.y + s.h));
            ctx_.scale(1.0, -1.0);
        } else {
            ctx_.translate(static_cast<double>(s.x), static_cast<double>(s.y));
        }
        ctx_.blit_image(BLRect(0.0, 0.0, s.w, s.h), it->second,
                        BLRectI(srcX, srcY, srcW, srcH));
        ctx_.restore();
        break;
    }

    case Shape::Type::Svg:
        drawSvg(std::get<SvgShape>(shape.data));
        break;

    case Shape::Type::Blur:
        // Blur 需要"表面回读 + 缓存 + 绝对坐标"，由 renderTile 直接分派给 drawBlur
        break;

    case Shape::Type::Group:
        // Group 不会作为叶子出现在 DirtyEntry 中
        break;
    }
}

void Blend2DRenderer::drawText(const TextShape& text, const Paint& paint) {
    if (text.text.empty()) return;

    const BLFont* font = fontForSize(text.fontSize);
    if (!font) return;

    BLRgba32 solid;
    BLGradient gradient;
    const BrushKind kind = prepareBrush(paint, solid, gradient);
    if (kind == BrushKind::None) return;

    GlyphCache& cache = glyphCache();
    GlyphCache::RunEntry* entry =
        cache.acquireRun(text.text, text.fontSize, fontGeneration_, *font);

    if (entry) {
        // 快速路径：整行 A8 蒙版 + fill_mask —— 每帧只做一次 blit，与颜色无关
        if (maskPathUsable(ctx_) && cache.ensureRunMask(*entry, *font)) {
            if (entry->maskW > 0 && entry->maskH > 0) {
                const BLPointI origin(text.x + entry->maskX, text.y + entry->maskY);
                const BLRectI area(0, 0, entry->maskW, entry->maskH);
                if (kind == BrushKind::Solid) {
                    ctx_.fill_mask(origin, entry->mask, area, solid);
                } else {
                    ctx_.fill_mask(origin, entry->mask, area, gradient);
                }
            }
            return;
        }

        // 整行蒙版超限：逐字形蒙版（同样与颜色无关）
        if (maskPathUsable(ctx_) &&
            cache.drawGlyphFallback(ctx_, *entry, *font, text.x, text.y, kind,
                                    solid, gradient)) {
            return;
        }

        // 至少复用 shaping 结果，不再重复 UTF-8 解码 / 字形映射
        const BLPointI origin(text.x, text.y);
        const BLGlyphRun& run = entry->glyphs.glyph_run();
        if (kind == BrushKind::Solid) {
            ctx_.fill_glyph_run(origin, *font, run, solid);
        } else {
            ctx_.fill_glyph_run(origin, *font, run, gradient);
        }
        return;
    }

    // shaping 失败时的最终兜底：保持原有直绘行为
    const BLPoint origin(static_cast<double>(text.x), static_cast<double>(text.y));
    if (kind == BrushKind::Solid) {
        ctx_.fill_utf8_text(origin, *font, text.text.c_str(), text.text.size(), solid);
        return;
    }

    BLGlyphBuffer glyphBuffer;
    if (glyphBuffer.set_utf8_text(text.text.c_str(), text.text.size()) != BL_SUCCESS) return;
    if (font->shape(glyphBuffer) != BL_SUCCESS) return;
    ctx_.fill_glyph_run(origin, *font, glyphBuffer.glyph_run(), gradient);
}

} // namespace modern_ui
