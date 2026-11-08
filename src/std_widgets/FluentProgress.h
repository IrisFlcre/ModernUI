// ============================================================================
// FluentProgress.h — 进度 / 状态控件
// ============================================================================
//
//   ProgressBar — 确定进度 + 不确定动画（两根循环滑动的强调色胶囊）
//   ProgressRing — 确定进度环 + 不确定旋转弧
//   RadialGauge — 环形仪表盘（渐变弧 + 端点光点 + 数值文本）
//   Skeleton    — 骨架屏占位（流光亮带扫过）
//   InfoBadge   — 数字 / 圆点徽标
//   InfoBar     — 信息条（Informational / Success / Warning / Error）
//
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "FluentControl.h"

namespace modern_ui {
namespace fluent {

class ProgressBar : public FluentControl {
public:
    explicit ProgressBar(float progress = 0.0f, bool indeterminate = false);

    void setProgress(float progress);
    float progress() const { return progress_; }

    void setIndeterminate(bool indeterminate);
    bool indeterminate() const { return indeterminate_; }

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;
    bool acceptsFocus() const override { return false; }

private:
    float progress_ = 0.0f;
    bool indeterminate_ = false;
    float phase_ = 0.0f;
    uint32_t trackId_ = 0;
    uint32_t primaryId_ = 0;
    uint32_t secondaryId_ = 0;
};

class ProgressRing : public FluentControl {
public:
    explicit ProgressRing(float progress = 0.0f, bool indeterminate = false);

    void setProgress(float progress);
    float progress() const { return progress_; }

    void setIndeterminate(bool indeterminate);
    bool indeterminate() const { return indeterminate_; }

    void setStrokeWidth(int32_t width) { strokeWidth_ = std::max(1, width); }

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;
    bool acceptsFocus() const override { return false; }

private:
    float progress_ = 0.0f;
    bool indeterminate_ = false;
    float phase_ = 0.0f;
    int32_t strokeWidth_ = 3;
    uint32_t trackId_ = 0;
    uint32_t arcId_ = 0;
    uint32_t arcSecondaryId_ = 0;
};

// ===========================================================================
class RadialGauge : public FluentControl {
public:
    explicit RadialGauge(float value = 0.0f, std::string label = {});

    void setValue(float value);
    float value() const { return value_; }

    void setLabel(std::string label);
    const std::string& label() const { return label_; }

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;
    bool acceptsFocus() const override { return false; }

private:
    float value_ = 0.0f;
    float display_ = 0.0f;
    std::string label_;
    int32_t fontSize_ = 22;
    mutable Rect valueMeasured_{};
    mutable Rect labelMeasured_{};

    uint32_t trackId_ = 0;
    uint32_t arcId_ = 0;
    uint32_t glowId_ = 0;
    uint32_t capId_ = 0;
    uint32_t valueId_ = 0;
    uint32_t labelId_ = 0;
};

// ===========================================================================
class Skeleton : public FluentControl {
public:
    explicit Skeleton(float radius = 4.0f, bool circle = false);

    void setRadius(float radius) {
        radius_ = std::max(0.0f, radius);
        if (laidOut()) updateVisuals();
    }
    float radius() const { return radius_; }

    void setCircle(bool circle) {
        circle_ = circle;
        invalidateLayout();
        if (laidOut()) updateVisuals();
    }
    bool circle() const { return circle_; }

    void setShimmer(bool shimmer) {
        shimmer_ = shimmer;
        if (laidOut()) updateVisuals();
    }
    bool shimmer() const { return shimmer_; }

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;
    bool acceptsFocus() const override { return false; }

private:
    float radius_ = 4.0f;
    bool circle_ = false;
    bool shimmer_ = true;
    float phase_ = 0.0f;
    uint32_t blockId_ = 0;
};

// ===========================================================================
class InfoBadge : public FluentControl {
public:
    enum class Kind : uint8_t { Attention = 0, Informational, Success, Critical };

    explicit InfoBadge(int32_t value = -1, Kind kind = Kind::Attention);

    void setValue(int32_t value) {
        if (value_ == value) return;
        value_ = value;
        invalidateLayout();
        if (laidOut()) updateVisuals();
    }
    int32_t value() const { return value_; }

    void setKind(Kind kind) { kind_ = kind; if (laidOut()) updateVisuals(); }
    Kind kind() const { return kind_; }

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool acceptsFocus() const override { return false; }

private:
    int32_t value_ = -1;   // < 0 = 圆点
    Kind kind_ = Kind::Attention;
    mutable Rect measured_{};
    uint32_t backgroundId_ = 0;
    uint32_t textId_ = 0;
};

class InfoBar : public FluentControl {
public:
    enum class Severity : uint8_t { Informational = 0, Success, Warning, Error };

    explicit InfoBar(std::string title = {}, std::string message = {},
                     Severity severity = Severity::Informational);

    void setTitle(std::string title);
    const std::string& title() const { return title_; }

    void setMessage(std::string message);
    const std::string& message() const { return message_; }

    void setSeverity(Severity severity);
    Severity severity() const { return severity_; }

    void setClosable(bool closable) { closable_ = closable; if (laidOut()) updateVisuals(); }
    bool closable() const { return closable_; }

    bool isOpen() const { return open_; }
    void show() { open_ = true; invalidateLayout(); }
    void close();

    std::function<void()> onClose;

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool onEvent(const Event& ev) override;
    void onStateChanged() override;
    bool acceptsFocus() const override { return false; }

private:
    Color severityColor() const;
    Color glyphColor(const Color& background) const;

    std::string title_;
    std::string message_;
    Severity severity_ = Severity::Informational;
    bool closable_ = true;
    bool open_ = true;
    bool closeHover_ = false;

    mutable Rect titleMeasured_{};
    mutable Rect messageMeasured_{};

    uint32_t backgroundId_ = 0;
    uint32_t iconBackgroundId_ = 0;
    uint32_t iconGlyphId_ = 0;
    uint32_t titleId_ = 0;
    uint32_t messageId_ = 0;
    uint32_t closeBackgroundId_ = 0;
    uint32_t closeGlyphId_ = 0;
};

} // namespace fluent
} // namespace modern_ui
