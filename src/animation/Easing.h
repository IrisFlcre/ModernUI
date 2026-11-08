// ============================================================================
// Easing.h — 缓动曲线（Fluent Motion 常用集合 + 三次贝塞尔）
// ============================================================================
//
// 约定：
//   - 输入 t / 输出均在 [0, 1]；超出范围会被钳制。
//   - Easing 枚举覆盖 Robert Penner 经典曲线与 Fluent 常用的 Back / Elastic。
//   - CubicBezier 用于精确复刻 Windows 11 的 Motion 曲线：
//       Standard      (0.20, 0.00, 0.00, 1.00)? 常用 (0.8, 0, 0.2, 1)
//       Decelerate    (0.10, 0.90, 0.20, 1.00)
//       Accelerate    (0.70, 0.00, 1.00, 0.50)
//     曲线以 x 轴为时间求解，再用 y 输出。
//
// ============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace modern_ui {
namespace anim {

enum class Easing : uint8_t {
    Linear = 0,
    InSine, OutSine, InOutSine,
    InQuad, OutQuad, InOutQuad,
    InCubic, OutCubic, InOutCubic,
    InQuart, OutQuart, InOutQuart,
    InQuint, OutQuint, InOutQuint,
    InExpo, OutExpo, InOutExpo,
    InCirc, OutCirc, InOutCirc,
    InBack, OutBack, InOutBack,
    InElastic, OutElastic, InOutElastic,
    InBounce, OutBounce, InOutBounce,
};

namespace detail {

inline float clamp01(float t) {
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

inline float bounceOut(float t) {
    constexpr float n1 = 7.5625f;
    constexpr float d1 = 2.75f;
    if (t < 1.0f / d1) return n1 * t * t;
    if (t < 2.0f / d1) {
        t -= 1.5f / d1;
        return n1 * t * t + 0.75f;
    }
    if (t < 2.5f / d1) {
        t -= 2.25f / d1;
        return n1 * t * t + 0.9375f;
    }
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
}

} // namespace detail

// 缓动求值：t ∈ [0,1]
inline float ease(Easing easing, float t) {
    t = detail::clamp01(t);
    constexpr float kPi = 3.14159265358979323846f;

    switch (easing) {
    case Easing::Linear: return t;

    case Easing::InSine:    return 1.0f - std::cos(t * kPi * 0.5f);
    case Easing::OutSine:   return std::sin(t * kPi * 0.5f);
    case Easing::InOutSine: return -(std::cos(kPi * t) - 1.0f) * 0.5f;

    case Easing::InQuad:    return t * t;
    case Easing::OutQuad:   return 1.0f - (1.0f - t) * (1.0f - t);
    case Easing::InOutQuad:
        return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;

    case Easing::InCubic:   return t * t * t;
    case Easing::OutCubic:  return 1.0f - std::pow(1.0f - t, 3.0f);
    case Easing::InOutCubic:
        return t < 0.5f ? 4.0f * t * t * t
                        : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;

    case Easing::InQuart:   return t * t * t * t;
    case Easing::OutQuart:  return 1.0f - std::pow(1.0f - t, 4.0f);
    case Easing::InOutQuart:
        return t < 0.5f ? 8.0f * t * t * t * t
                        : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) * 0.5f;

    case Easing::InQuint:   return t * t * t * t * t;
    case Easing::OutQuint:  return 1.0f - std::pow(1.0f - t, 5.0f);
    case Easing::InOutQuint:
        return t < 0.5f ? 16.0f * t * t * t * t * t
                        : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) * 0.5f;

    case Easing::InExpo:
        return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
    case Easing::OutExpo:
        return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
    case Easing::InOutExpo:
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return t < 0.5f
            ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f
            : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f;

    case Easing::InCirc:
        return 1.0f - std::sqrt(1.0f - t * t);
    case Easing::OutCirc:
        return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f));
    case Easing::InOutCirc:
        return t < 0.5f
            ? (1.0f - std::sqrt(1.0f - std::pow(2.0f * t, 2.0f))) * 0.5f
            : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) * 0.5f;

    case Easing::InBack: {
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        return c3 * t * t * t - c1 * t * t;
    }
    case Easing::OutBack: {
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
    }
    case Easing::InOutBack: {
        constexpr float c1 = 1.70158f;
        constexpr float c2 = c1 * 1.525f;
        return t < 0.5f
            ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f
            : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) * 0.5f;
    }

    case Easing::InElastic: {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        constexpr float c4 = (2.0f * kPi) / 3.0f;
        return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
    }
    case Easing::OutElastic: {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        constexpr float c4 = (2.0f * kPi) / 3.0f;
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
    }
    case Easing::InOutElastic: {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        constexpr float c5 = (2.0f * kPi) / 4.5f;
        return t < 0.5f
            ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) * 0.5f
            : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) * 0.5f + 1.0f;
    }

    case Easing::InBounce:  return 1.0f - detail::bounceOut(1.0f - t);
    case Easing::OutBounce: return detail::bounceOut(t);
    case Easing::InOutBounce:
        return t < 0.5f
            ? (1.0f - detail::bounceOut(1.0f - 2.0f * t)) * 0.5f
            : (1.0f + detail::bounceOut(2.0f * t - 1.0f)) * 0.5f;
    }
    return t;
}

// ---------------------------------------------------------------------------
// CubicBezier — 按 x 求 y（牛顿迭代 + 二分兜底）
// ---------------------------------------------------------------------------
struct CubicBezier {
    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;

    constexpr CubicBezier() = default;
    constexpr CubicBezier(float a, float b, float c, float d)
        : x1(a), y1(b), x2(c), y2(d) {}

    static constexpr CubicBezier standard()   { return {0.20f, 0.00f, 0.00f, 1.00f}; }
    static constexpr CubicBezier decelerate() { return {0.10f, 0.90f, 0.20f, 1.00f}; }
    static constexpr CubicBezier accelerate() { return {0.70f, 0.00f, 1.00f, 0.50f}; }
    static constexpr CubicBezier linear()     { return {0.00f, 0.00f, 1.00f, 1.00f}; }

    float at(float x) const {
        x = detail::clamp01(x);
        if (x <= 0.0f) return 0.0f;
        if (x >= 1.0f) return 1.0f;

        // 先按 x 求解参数 s，再计算 y(s)
        float s = x;
        for (int i = 0; i < 6; ++i) {
            const float xEstimate = sampleX(s) - x;
            if (std::fabs(xEstimate) < 1.0e-5f) break;
            const float slope = sampleDX(s);
            if (std::fabs(slope) < 1.0e-6f) break;
            s -= xEstimate / slope;
            if (s < 0.0f || s > 1.0f) {
                s = detail::clamp01(s);
                break;
            }
        }

        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 20 && std::fabs(sampleX(s) - x) > 1.0e-5f; ++i) {
            if (sampleX(s) < x) lo = s; else hi = s;
            s = (lo + hi) * 0.5f;
        }
        return sampleY(s);
    }

private:
    float sampleX(float s) const {
        const float inv = 1.0f - s;
        return 3.0f * inv * inv * s * x1 + 3.0f * inv * s * s * x2 + s * s * s;
    }
    float sampleY(float s) const {
        const float inv = 1.0f - s;
        return 3.0f * inv * inv * s * y1 + 3.0f * inv * s * s * y2 + s * s * s;
    }
    float sampleDX(float s) const {
        const float inv = 1.0f - s;
        return 3.0f * inv * inv * x1 +
               6.0f * inv * s * (x2 - x1) +
               3.0f * s * s * (1.0f - x2);
    }
};

} // namespace anim
} // namespace modern_ui
