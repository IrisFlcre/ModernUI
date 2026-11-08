// ============================================================================
// InkEngine.cpp
// ============================================================================

#include "InkEngine.h"

#include "Blend2DRenderer.h"

#include <blend2d/blend2d.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace annotation {

namespace {

constexpr int32_t kMaxPages = 64;
constexpr size_t  kMaxHistoryEntries = 14;
constexpr size_t  kMaxHistoryBytes = 96u * 1024u * 1024u;
constexpr float   kPi = 3.14159265358979323846f;

float clampf(float value, float lo, float hi) {
    return std::max(lo, std::min(hi, value));
}

float length(const modern_ui::Vec2f& v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

modern_ui::Vec2f normalize(const modern_ui::Vec2f& v) {
    const float len = length(v);
    if (len < 1.0e-6f) return modern_ui::Vec2f{1.0f, 0.0f};
    return modern_ui::Vec2f{v.x / len, v.y / len};
}

modern_ui::Vec2f cubicAt(const modern_ui::Vec2f& p0,
                         const modern_ui::Vec2f& c1,
                         const modern_ui::Vec2f& c2,
                         const modern_ui::Vec2f& p1,
                         float t) {
    const float u = 1.0f - t;
    const float w0 = u * u * u;
    const float w1 = 3.0f * u * u * t;
    const float w2 = 3.0f * u * t * t;
    const float w3 = t * t * t;
    return modern_ui::Vec2f{
        p0.x * w0 + c1.x * w1 + c2.x * w2 + p1.x * w3,
        p0.y * w0 + c1.y * w1 + c2.y * w2 + p1.y * w3,
    };
}

modern_ui::Rect intRect(float minX, float minY, float maxX, float maxY) {
    const int32_t x = static_cast<int32_t>(std::floor(minX - 2.0f));
    const int32_t y = static_cast<int32_t>(std::floor(minY - 2.0f));
    const int32_t r = static_cast<int32_t>(std::ceil(maxX + 2.0f));
    const int32_t b = static_cast<int32_t>(std::ceil(maxY + 2.0f));
    return modern_ui::Rect{x, y, std::max(0, r - x), std::max(0, b - y)};
}

modern_ui::Rect unite(const modern_ui::Rect& a, const modern_ui::Rect& b) {
    return a.unite(b);
}

} // namespace

// ============================================================================
// 初始化 / 页面
// ============================================================================

void InkEngine::initialize(modern_ui::Blend2DRenderer* renderer,
                           int32_t width, int32_t height) {
    renderer_ = renderer;
    width_ = std::max(1, width);
    height_ = std::max(1, height);

    strokeActive_ = false;
    erasing_ = false;
    points_.clear();

    pages_.clear();
    pageIndex_ = 0;
    pages_.push_back(Page{});
    ensureCurrentImage();
}

bool InkEngine::ensurePageImage(Page& page) {
    if (page.imageHandle != 0 && page.width == width_ && page.height == height_) {
        return true;
    }
    if (!renderer_ || width_ <= 0 || height_ <= 0) return false;

    page.imageHandle = renderer_->createBitmapImage(
        width_, height_, modern_ui::Color{0, 0, 0, 0});
    page.width = width_;
    page.height = height_;
    return page.imageHandle != 0;
}

bool InkEngine::ensureCurrentImage() {
    if (pages_.empty()) {
        pages_.push_back(Page{});
        pageIndex_ = 0;
    }
    pageIndex_ = std::max(0, std::min(pageIndex_,
                                      static_cast<int32_t>(pages_.size()) - 1));
    return ensurePageImage(pages_[static_cast<size_t>(pageIndex_)]);
}

uint64_t InkEngine::currentImageHandle() {
    if (!ensureCurrentImage()) return 0;
    return pages_[static_cast<size_t>(pageIndex_)].imageHandle;
}

uint64_t InkEngine::existingImageHandle() const {
    if (pages_.empty()) return 0;
    const int32_t index = std::max(0, std::min(
        pageIndex_, static_cast<int32_t>(pages_.size()) - 1));
    return pages_[static_cast<size_t>(index)].imageHandle;
}

bool InkEngine::ensurePageCount(int32_t count) {
    const int32_t target = std::max(1, std::min(count, kMaxPages));
    if (static_cast<int32_t>(pages_.size()) >= target) return true;

    cancelStroke();
    endErase();
    pages_.resize(static_cast<size_t>(target));
    return true;
}

bool InkEngine::addPage() {
    if (static_cast<int32_t>(pages_.size()) >= kMaxPages) return false;
    cancelStroke();
    endErase();

    // 位图在首次落笔时按需分配，翻页浏览不占用内存。
    pages_.push_back(Page{});
    pageIndex_ = static_cast<int32_t>(pages_.size()) - 1;
    return true;
}

bool InkEngine::setPage(int32_t index) {
    if (index < 0 || index >= static_cast<int32_t>(pages_.size())) return false;
    if (index == pageIndex_) return true;

    cancelStroke();
    endErase();
    pageIndex_ = index;
    return true;
}

void InkEngine::clearPage() {
    cancelStroke();
    endErase();

    Page& page = pages_[static_cast<size_t>(pageIndex_)];
    if (!ensurePageImage(page)) return;

    if (auto* image = renderer_ ? renderer_->image(page.imageHandle) : nullptr) {
        BLContext context(*image);
        if (context.is_valid()) {
            context.set_comp_op(BL_COMP_OP_SRC_COPY);
            context.fill_all(BLRgba32(0, 0, 0, 0));
            context.flush(BL_CONTEXT_FLUSH_SYNC);
        }
    }

    page.history.clear();
    page.historyPos = 0;
}

// ============================================================================
// 笔迹输入
// ============================================================================

void InkEngine::beginStroke(const modern_ui::Vec2f& point, double timeSeconds) {
    if (!ready()) return;

    cancelStroke();
    endErase();

    strokeActive_ = true;
    liveDirty_ = true;
    points_.clear();

    rawPoint_ = point;
    smoothedPoint_ = point;
    speed_ = 0.0f;
    pressure_ = 1.0f;
    lastTime_ = timeSeconds;

    InkPoint first;
    first.pos = point;
    first.pressure = 0.92f;
    first.time = static_cast<float>(timeSeconds);
    points_.push_back(first);
}

void InkEngine::addPoint(const modern_ui::Vec2f& point, double timeSeconds) {
    if (!strokeActive_) return;

    const float dt = clampf(static_cast<float>(timeSeconds - lastTime_), 0.0008f, 0.050f);
    lastTime_ = std::max(lastTime_, timeSeconds);
    rawPoint_ = point;

    const float tau = std::max(0.0f, parameters_.smoothingTau);
    const float alpha = (tau <= 0.0001f)
        ? 1.0f
        : (1.0f - std::exp(-dt / tau));
    smoothedPoint_.x += (point.x - smoothedPoint_.x) * alpha;
    smoothedPoint_.y += (point.y - smoothedPoint_.y) * alpha;

    const modern_ui::Vec2f delta{smoothedPoint_.x - points_.back().pos.x,
                                 smoothedPoint_.y - points_.back().pos.y};
    const float distance = length(delta);
    const float instantSpeed = distance / dt;

    const float speedAlpha = 1.0f - std::exp(-dt / 0.040f);
    speed_ += (instantSpeed - speed_) * speedAlpha;

    const float speedRatio = clampf(speed_ / std::max(1.0f, parameters_.speedScale),
                                    0.0f, 1.0f);
    const float targetPressure = 1.0f - parameters_.penTip * speedRatio;
    const float pressureAlpha = 1.0f - std::exp(-dt / 0.022f);
    pressure_ += (clampf(targetPressure, 0.10f, 1.0f) - pressure_) * pressureAlpha;

    if (distance < 0.55f) {
        // 点位几乎没动，只更新末端压力，让笔宽仍然能响应速度变化。
        points_.back().pressure = clampf(pressure_, 0.10f, 1.0f);
        liveDirty_ = true;
        return;
    }

    InkPoint next;
    next.pos = smoothedPoint_;
    next.pressure = clampf(pressure_, 0.10f, 1.0f);
    next.time = static_cast<float>(timeSeconds);
    points_.push_back(next);
    liveDirty_ = true;
}

modern_ui::Color InkEngine::fillColor() const {
    modern_ui::Color color = style_.color;
    float alpha = style_.alpha * (static_cast<float>(color.a) / 255.0f);
    if (style_.highlighter) alpha *= 0.34f;
    alpha = clampf(alpha, 0.0f, 1.0f);
    color.a = static_cast<uint8_t>(std::lround(alpha * 255.0f));
    return color;
}

float InkEngine::currentBaseWidth() const {
    const float base = style_.width * (style_.highlighter ? 2.4f : 1.0f);
    return clampf(base, 0.8f, 240.0f);
}

bool InkEngine::buildOutline(std::vector<modern_ui::Vec2f>& outline,
                             modern_ui::Rect* bounds) const {
    if (points_.empty()) {
        outline.clear();
        if (bounds) *bounds = modern_ui::Rect::null();
        return false;
    }
    return buildOutlineRange(0, points_.size() - 1, true, true, outline, bounds);
}

bool InkEngine::buildLiveOutline(std::size_t begin, std::size_t end,
                                 bool applyStartTaper, bool applyEndTaper,
                                 std::vector<modern_ui::Vec2f>& outline,
                                 modern_ui::Rect* bounds) const {
    return buildOutlineRange(begin, end, applyStartTaper, applyEndTaper,
                             outline, bounds);
}

bool InkEngine::buildOutlineRange(std::size_t begin, std::size_t end,
                                  bool applyStartTaper, bool applyEndTaper,
                                  std::vector<modern_ui::Vec2f>& outline,
                                  modern_ui::Rect* bounds) const {
    outline.clear();
    if (bounds) *bounds = modern_ui::Rect::null();
    if (points_.empty() || begin > end || end >= points_.size()) return false;

    struct Sample {
        modern_ui::Vec2f pos;
        float pressure = 1.0f;
        float arc = 0.0f;
        float width = 1.0f;
    };

    const float baseWidth = currentBaseWidth();

    // 单点 = 圆点
    if (begin == end) {
        const float radius = std::max(0.8f, baseWidth * 0.5f);
        const modern_ui::Vec2f center = points_[begin].pos;
        constexpr int32_t segments = 16;
        float minX = center.x - radius;
        float minY = center.y - radius;
        float maxX = center.x + radius;
        float maxY = center.y + radius;
        for (int32_t i = 0; i < segments; ++i) {
            const float angle = 2.0f * kPi * static_cast<float>(i) /
                                static_cast<float>(segments);
            outline.push_back(modern_ui::Vec2f{
                center.x + std::cos(angle) * radius,
                center.y + std::sin(angle) * radius});
        }
        if (bounds) *bounds = intRect(minX, minY, maxX, maxY);
        return !outline.empty();
    }

    std::vector<Sample> samples;
    samples.reserve((end - begin + 1) * 3);

    Sample first;
    first.pos = points_[begin].pos;
    first.pressure = points_[begin].pressure;
    first.arc = 0.0f;
    samples.push_back(first);

    const float tension = 0.85f;
    for (size_t i = begin; i < end; ++i) {
        const modern_ui::Vec2f p0 = points_[i].pos;
        const modern_ui::Vec2f p1 = points_[i + 1].pos;
        const modern_ui::Vec2f pPrev = points_[i == begin ? begin : i - 1].pos;
        const modern_ui::Vec2f pNext = points_[std::min(end, i + 2)].pos;

        const modern_ui::Vec2f c1 = modern_ui::Vec2f{
            p0.x + (p1.x - pPrev.x) * (tension / 6.0f),
            p0.y + (p1.y - pPrev.y) * (tension / 6.0f)};
        const modern_ui::Vec2f c2 = modern_ui::Vec2f{
            p1.x - (pNext.x - p0.x) * (tension / 6.0f),
            p1.y - (pNext.y - p0.y) * (tension / 6.0f)};

        const float chord = length(modern_ui::Vec2f{p1.x - p0.x, p1.y - p0.y});
        const int32_t steps = std::max(1, std::min(28,
            static_cast<int32_t>(std::ceil(chord / 2.0f))));

        for (int32_t s = 1; s <= steps; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(steps);
            const modern_ui::Vec2f pos = cubicAt(p0, c1, c2, p1, t);
            const float stepDistance = length(
                modern_ui::Vec2f{pos.x - samples.back().pos.x,
                                 pos.y - samples.back().pos.y});

            if (stepDistance < 0.04f && s != steps) continue;

            Sample sample;
            sample.pos = pos;
            sample.pressure = clampf(
                points_[i].pressure * (1.0f - t) + points_[i + 1].pressure * t,
                0.08f, 1.0f);
            sample.arc = samples.back().arc + stepDistance;
            samples.push_back(sample);
        }
    }

    const float totalLength = samples.empty() ? 0.0f : samples.back().arc;
    if (samples.size() < 2 || totalLength < 0.8f) {
        // 轨迹过短：退化为圆点
        const float radius = std::max(0.8f, baseWidth * 0.5f);
        const modern_ui::Vec2f center = points_[end].pos;
        constexpr int32_t segments = 16;
        for (int32_t i = 0; i < segments; ++i) {
            const float angle = 2.0f * kPi * static_cast<float>(i) /
                                static_cast<float>(segments);
            outline.push_back(modern_ui::Vec2f{
                center.x + std::cos(angle) * radius,
                center.y + std::sin(angle) * radius});
        }
        if (bounds) {
            *bounds = intRect(center.x - radius, center.y - radius,
                              center.x + radius, center.y + radius);
        }
        return !outline.empty();
    }

    // 起笔 / 收笔 taper 长度（分块预览只对首块起笔、整条提交时两端都处理）
    float taperLength = 0.0f;
    if ((applyStartTaper || applyEndTaper) && parameters_.taper > 0.01f) {
        taperLength = std::min(totalLength * 0.45f,
                               baseWidth * (1.5f + 8.0f * parameters_.taper));
    }

    for (Sample& sample : samples) {
        float width = 0.0f;
        if (style_.highlighter) {
            width = baseWidth * (0.72f + 0.28f * sample.pressure);
        } else {
            width = baseWidth * (0.30f +
                                 0.70f * std::pow(sample.pressure, 0.85f));
        }

        if (taperLength > 0.01f) {
            float t = 1.0f;
            if (applyStartTaper) {
                t = std::min(t, sample.arc / taperLength);
            }
            if (applyEndTaper) {
                t = std::min(t, (totalLength - sample.arc) / taperLength);
            }
            t = clampf(t, 0.0f, 1.0f);
            float envelope = 0.18f + 0.82f * std::pow(t, 0.70f);
            if (style_.highlighter) envelope = 0.55f + 0.45f * envelope;
            width *= envelope;
        }

        sample.width = clampf(width, 0.7f, 240.0f);
    }

    // 逐采样点法线 → 左右轮廓
    std::vector<modern_ui::Vec2f> left(samples.size());
    std::vector<modern_ui::Vec2f> right(samples.size());
    std::vector<modern_ui::Vec2f> tangents(samples.size());

    for (size_t i = 0; i < samples.size(); ++i) {
        const size_t prev = (i == 0) ? 0 : i - 1;
        const size_t next = std::min(samples.size() - 1, i + 1);
        tangents[i] = normalize(modern_ui::Vec2f{
            samples[next].pos.x - samples[prev].pos.x,
            samples[next].pos.y - samples[prev].pos.y});

        const modern_ui::Vec2f normal{-tangents[i].y, tangents[i].x};
        const float halfWidth = samples[i].width * 0.5f;
        left[i] = modern_ui::Vec2f{samples[i].pos.x + normal.x * halfWidth,
                                   samples[i].pos.y + normal.y * halfWidth};
        right[i] = modern_ui::Vec2f{samples[i].pos.x - normal.x * halfWidth,
                                    samples[i].pos.y - normal.y * halfWidth};
    }

    float minX = left[0].x;
    float maxX = left[0].x;
    float minY = left[0].y;
    float maxY = left[0].y;

    outline.push_back(left[0]);
    for (size_t i = 1; i < left.size(); ++i) {
        outline.push_back(left[i]);
    }

    // 终点圆帽：left → 前向 → right
    {
        const size_t last = samples.size() - 1;
        const modern_ui::Vec2f normal{-tangents[last].y, tangents[last].x};
        const float radius = samples[last].width * 0.5f;
        constexpr int32_t segments = 8;
        for (int32_t s = 1; s < segments; ++s) {
            const float theta = kPi * static_cast<float>(s) /
                                static_cast<float>(segments);
            const modern_ui::Vec2f dir = modern_ui::Vec2f{
                normal.x * std::cos(theta) + tangents[last].x * std::sin(theta),
                normal.y * std::cos(theta) + tangents[last].y * std::sin(theta)};
            outline.push_back(modern_ui::Vec2f{
                samples[last].pos.x + dir.x * radius,
                samples[last].pos.y + dir.y * radius});
        }
    }

    outline.push_back(right[right.size() - 1]);
    for (size_t i = right.size() - 1; i-- > 0;) {
        outline.push_back(right[i]);
    }

    // 起点圆帽：right → 后向 → left
    {
        const float radius = samples[0].width * 0.5f;
        const modern_ui::Vec2f normal{-tangents[0].y, tangents[0].x};
        constexpr int32_t segments = 8;
        for (int32_t s = 1; s < segments; ++s) {
            const float theta = kPi * static_cast<float>(s) /
                                static_cast<float>(segments);
            const modern_ui::Vec2f dir = modern_ui::Vec2f{
                -normal.x * std::cos(theta) - tangents[0].x * std::sin(theta),
                -normal.y * std::cos(theta) - tangents[0].y * std::sin(theta)};
            outline.push_back(modern_ui::Vec2f{
                samples[0].pos.x + dir.x * radius,
                samples[0].pos.y + dir.y * radius});
        }
    }

    for (const modern_ui::Vec2f& point : outline) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }

    if (bounds) *bounds = intRect(minX, minY, maxX, maxY);
    return outline.size() >= 3;
}

modern_ui::Shape InkEngine::buildLiveShape() const {
    modern_ui::Shape shape;
    shape.base.visible = false;
    shape.base.z = -9000;

    if (!hasLiveStroke()) return shape;

    std::vector<modern_ui::Vec2f> outline;
    modern_ui::Rect bounds;
    if (!buildOutline(outline, &bounds)) return shape;

    modern_ui::PathShape path;
    path.steps.reserve(outline.size() + 1);
    for (size_t i = 0; i < outline.size(); ++i) {
        modern_ui::PathShape::Step step;
        step.cmd = (i == 0) ? modern_ui::PathShape::Cmd::MoveTo
                            : modern_ui::PathShape::Cmd::LineTo;
        step.pts[0] = outline[i];
        path.steps.push_back(step);
    }
    modern_ui::PathShape::Step close;
    close.cmd = modern_ui::PathShape::Cmd::Close;
    path.steps.push_back(close);

    shape.data = std::move(path);
    shape.base.fill = modern_ui::Paint(fillColor());
    shape.base.visible = true;
    return shape;
}

void InkEngine::rasterizeOutline(const std::vector<modern_ui::Vec2f>& outline,
                                 bool erase) {
    if (outline.size() < 3) return;
    if (!ensureCurrentImage()) return;

    Page& page = pages_[static_cast<size_t>(pageIndex_)];
    BLImage* image = renderer_ ? renderer_->image(page.imageHandle) : nullptr;
    if (!image) return;

    BLPath path;
    path.move_to(outline[0].x, outline[0].y);
    for (size_t i = 1; i < outline.size(); ++i) {
        path.line_to(outline[i].x, outline[i].y);
    }
    path.close();

    BLContext context(*image);
    if (!context.is_valid()) return;

    if (erase) {
        // 直接以 SRC_COPY 写入全透明像素：DST_OUT 在当前 Blend2D 版本会把
        // 目标写成不透明白色，导致橡皮擦出白色轨迹而不是露出下层。
        context.set_comp_op(BL_COMP_OP_SRC_COPY);
        context.fill_path(path, BLRgba32(0, 0, 0, 0));
    } else {
        context.set_comp_op(BL_COMP_OP_SRC_OVER);
        const modern_ui::Color color = fillColor();
        context.fill_path(path, BLRgba32(color.r, color.g, color.b, color.a));
    }
    context.flush(BL_CONTEXT_FLUSH_SYNC);
}

bool InkEngine::endStroke(modern_ui::Rect* dirtyRect) {
    if (!strokeActive_) return false;

    std::vector<modern_ui::Vec2f> outline;
    modern_ui::Rect bounds;
    const bool valid = buildOutline(outline, &bounds);

    strokeActive_ = false;
    liveDirty_ = true;

    if (!valid || bounds.isNull()) {
        points_.clear();
        return false;
    }

    if (!ensureCurrentImage()) {
        points_.clear();
        return false;
    }

    Page& page = pages_[static_cast<size_t>(pageIndex_)];
    const modern_ui::Rect canvasBounds{0, 0, width_, height_};
    const modern_ui::Rect clipped = bounds.intersect(canvasBounds);
    if (clipped.isNull()) {
        points_.clear();
        return false;
    }

    PixelSnapshot before;
    PixelSnapshot after;
    const bool capturedBefore = captureRegion(page, clipped, &before);

    rasterizeOutline(outline, false);

    const bool capturedAfter = captureRegion(page, clipped, &after);

    if (capturedBefore && capturedAfter) {
        pushHistory(page, clipped, before, after);
    }

    points_.clear();
    if (dirtyRect) *dirtyRect = clipped;
    return true;
}

void InkEngine::cancelStroke() {
    if (!strokeActive_ && points_.empty()) return;
    strokeActive_ = false;
    points_.clear();
    liveDirty_ = true;
}

// ============================================================================
// 像素橡皮
// ============================================================================

void InkEngine::beginErase(const modern_ui::Vec2f& point, float radius,
                           modern_ui::Rect* dirtyRect) {
    if (!ensureCurrentImage()) return;

    cancelStroke();
    // 擦除会直接改写 Bitmap 且按需求不可撤销：清空历史，避免撤销把擦掉的像素又画回来。
    invalidateHistory();

    erasing_ = true;
    lastErasePoint_ = point;

    eraseTo(point, radius, dirtyRect);
}

void InkEngine::eraseTo(const modern_ui::Vec2f& point, float radius,
                        modern_ui::Rect* dirtyRect) {
    if (!erasing_) return;
    if (!ensureCurrentImage()) return;

    Page& page = pages_[static_cast<size_t>(pageIndex_)];
    BLImage* image = renderer_ ? renderer_->image(page.imageHandle) : nullptr;
    if (!image) return;

    radius = clampf(radius, 1.0f, 240.0f);
    const float step = std::max(1.0f, radius * 0.32f);
    const float distance = length(modern_ui::Vec2f{
        point.x - lastErasePoint_.x, point.y - lastErasePoint_.y});
    const int32_t count = std::max(1, static_cast<int32_t>(
        std::ceil(distance / step)));

    BLContext context(*image);
    if (!context.is_valid()) return;
    // SRC_COPY + 全透明 = 把圆形区域覆盖为透明（露出下层桌面）。
    context.set_comp_op(BL_COMP_OP_SRC_COPY);

    modern_ui::Rect dirty;
    for (int32_t i = 1; i <= count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count);
        const float x = lastErasePoint_.x + (point.x - lastErasePoint_.x) * t;
        const float y = lastErasePoint_.y + (point.y - lastErasePoint_.y) * t;
        context.fill_circle(x, y, radius, BLRgba32(0, 0, 0, 0));

        const modern_ui::Rect stamp{
            static_cast<int32_t>(std::floor(x - radius - 1.0f)),
            static_cast<int32_t>(std::floor(y - radius - 1.0f)),
            static_cast<int32_t>(std::ceil(radius * 2.0f + 2.0f)),
            static_cast<int32_t>(std::ceil(radius * 2.0f + 2.0f))};
        dirty = dirty.isNull() ? stamp : unite(dirty, stamp);
    }
    context.flush(BL_CONTEXT_FLUSH_SYNC);

    lastErasePoint_ = point;

    if (dirtyRect) {
        *dirtyRect = dirty.intersect(modern_ui::Rect{0, 0, width_, height_});
    }
}

void InkEngine::endErase() {
    erasing_ = false;
}

// ============================================================================
// 撤销 / 重做
// ============================================================================

bool InkEngine::canUndo() const {
    if (pages_.empty()) return false;
    const Page& page = pages_[static_cast<size_t>(pageIndex_)];
    return page.historyPos > 0 && page.historyPos <= page.history.size();
}

bool InkEngine::undo(modern_ui::Rect* dirtyRect) {
    if (!canUndo()) return false;
    Page& page = pages_[static_cast<size_t>(pageIndex_)];
    if (!ensurePageImage(page)) return false;

    const HistoryEntry& entry = page.history[page.historyPos - 1];
    if (!restoreRegion(page, entry.before)) return false;

    page.historyPos -= 1;
    if (dirtyRect) *dirtyRect = entry.before.rect;
    return true;
}

void InkEngine::invalidateHistory() {
    if (pages_.empty()) return;
    Page& page = pages_[static_cast<size_t>(pageIndex_)];
    page.history.clear();
    page.historyPos = 0;
}

// ============================================================================
// Bitmap 快照 / 恢复
// ============================================================================

bool InkEngine::captureRegion(Page& page, const modern_ui::Rect& rect,
                              PixelSnapshot* snapshot) {
    if (!snapshot || !renderer_) return false;
    BLImage* image = renderer_->image(page.imageHandle);
    if (!image) return false;

    BLImageData data{};
    if (image->get_data(&data) != BL_SUCCESS || !data.pixel_data) return false;

    const modern_ui::Rect clipped =
        rect.intersect(modern_ui::Rect{0, 0, width_, height_});
    if (clipped.isNull()) return false;

    const size_t rowBytes = static_cast<size_t>(clipped.w) * 4u;
    snapshot->rect = clipped;
    snapshot->bytes.resize(rowBytes * static_cast<size_t>(clipped.h));

    const uint8_t* source = static_cast<const uint8_t*>(data.pixel_data);
    for (int32_t y = 0; y < clipped.h; ++y) {
        std::memcpy(snapshot->bytes.data() + rowBytes * static_cast<size_t>(y),
                    source + static_cast<ptrdiff_t>(clipped.y + y) * data.stride +
                        static_cast<ptrdiff_t>(clipped.x) * 4,
                    rowBytes);
    }
    return true;
}

bool InkEngine::restoreRegion(Page& page, const PixelSnapshot& snapshot) {
    if (!renderer_) return false;
    BLImage* image = renderer_->image(page.imageHandle);
    if (!image) return false;

    BLImageData data{};
    if (image->get_data(&data) != BL_SUCCESS || !data.pixel_data) return false;

    const size_t rowBytes = static_cast<size_t>(snapshot.rect.w) * 4u;
    if (snapshot.bytes.size() < rowBytes * static_cast<size_t>(snapshot.rect.h)) {
        return false;
    }

    uint8_t* destination = static_cast<uint8_t*>(data.pixel_data);
    for (int32_t y = 0; y < snapshot.rect.h; ++y) {
        std::memcpy(destination +
                        static_cast<ptrdiff_t>(snapshot.rect.y + y) * data.stride +
                        static_cast<ptrdiff_t>(snapshot.rect.x) * 4,
                    snapshot.bytes.data() + rowBytes * static_cast<size_t>(y),
                    rowBytes);
    }
    return true;
}

void InkEngine::pushHistory(Page& page, const modern_ui::Rect& bounds,
                            const PixelSnapshot& before,
                            const PixelSnapshot& after) {
    (void)bounds;

    // 丢弃重做分支
    if (page.historyPos < page.history.size()) {
        page.history.resize(page.historyPos);
    }

    HistoryEntry entry;
    entry.before = before;
    entry.after = after;
    page.history.push_back(std::move(entry));

    auto bytesOf = [](const HistoryEntry& item) {
        return item.before.bytes.size() + item.after.bytes.size();
    };

    size_t totalBytes = 0;
    for (const HistoryEntry& item : page.history) totalBytes += bytesOf(item);

    while (page.history.size() > kMaxHistoryEntries ||
           (totalBytes > kMaxHistoryBytes && page.history.size() > 1)) {
        totalBytes -= bytesOf(page.history.front());
        page.history.erase(page.history.begin());
    }

    page.historyPos = page.history.size();
}

} // namespace annotation
