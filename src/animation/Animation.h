// ============================================================================
// Animation.h — 通用补间 / 关键帧 / 弹簧动画
// ============================================================================
//
// 内容：
//   - lerpValue()：float / Color / Vec2f / Vec2i 的插值（可扩展自定义类型）
//   - Tween<T>：单个值的补间动画（延迟 / 循环 / 乒乓 / 缓动 / 回调）
//   - KeyframeTrack：多关键帧时间轴（每段可独立缓动）
//   - SpringValue：基于弹簧-阻尼物理的自然运动（适合拖拽回弹 / 磁吸）
//   - approachExponential / moveTowards：UI 状态过渡的常用小工具
//
// 时间单位统一为秒，dt 由 View::tick() 下发。
//
// ============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

#include "Canvas.h"
#include "Easing.h"

namespace modern_ui {
namespace anim {

// ---------------------------------------------------------------------------
// 插值（先定义具体类型重载，再提供通用模板）
// ---------------------------------------------------------------------------
inline float lerpValue(float a, float b, float t) {
    return a + (b - a) * t;
}

inline Vec2f lerpValue(const Vec2f& a, const Vec2f& b, float t) {
    return Vec2f{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

inline Vec2i lerpValue(const Vec2i& a, const Vec2i& b, float t) {
    return Vec2i{
        static_cast<int32_t>(std::lround(static_cast<float>(a.x) + (static_cast<float>(b.x - a.x) * t))),
        static_cast<int32_t>(std::lround(static_cast<float>(a.y) + (static_cast<float>(b.y - a.y) * t)))
    };
}

inline Color lerpValue(const Color& a, const Color& b, float t) {
    const auto mix = [t](uint8_t x, uint8_t y) -> uint8_t {
        const float v = static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t;
        return static_cast<uint8_t>(std::lround(std::max(0.0f, std::min(255.0f, v))));
    };
    return Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

template <typename T>
inline T lerpValue(const T& a, const T& b, float t) {
    return a + (b - a) * t;
}

// ---------------------------------------------------------------------------
// UI 过渡小工具
// ---------------------------------------------------------------------------
inline float approachExponential(float current, float target, float rate, float dt) {
    const float alpha = 1.0f - std::exp(-std::max(0.0f, rate) * std::max(0.0f, dt));
    return current + (target - current) * alpha;
}

inline float moveTowards(float current, float target, float maxDelta) {
    const float delta = target - current;
    if (std::fabs(delta) <= maxDelta) return target;
    return current + (delta > 0.0f ? maxDelta : -maxDelta);
}

// ===========================================================================
// Tween<T>
// ===========================================================================
//
// 用法：
//   Tween<float> tween(0.0f, 1.0f, 0.28f);
//   tween.easing = Easing::OutCubic;
//   tween.onUpdate = [](float v) { ... };
//   tween.play();
//   // 每帧： tween.tick(dt);
//
// loops：完整播放段数；0 = 无限循环。
// pingPong：每段结束后反向播放（往复），总段数按 loops × 2 计。
// ===========================================================================
template <typename T>
class Tween {
public:
    Tween() = default;

    Tween(T from, T to, float durationSeconds)
        : from_(std::move(from)), to_(std::move(to)), value_(from_), duration_(durationSeconds) {}

    // ---- 配置 -------------------------------------------------------------
    void setRange(T from, T to) { from_ = std::move(from); to_ = std::move(to); }
    void setDuration(float seconds) { duration_ = std::max(0.0f, seconds); }
    void setDelay(float seconds) { delay_ = std::max(0.0f, seconds); }
    void setEasing(Easing value) { easing_ = value; }
    void setLoop(bool loop, int loopCount = 0) {
        loops_ = loop ? loopCount : 1;
        if (!loop) pingPong_ = false;
    }
    void setPingPong(bool value) { pingPong_ = value; }
    void setSpeed(float speed) { speed_ = std::max(0.0f, speed); }

    float duration() const { return duration_; }
    float delay() const { return delay_; }
    Easing easing() const { return easing_; }

    T from() const { return from_; }
    T to() const { return to_; }

    // ---- 状态 -------------------------------------------------------------
    bool playing() const { return playing_; }
    bool finished() const { return finished_; }
    bool reverse() const { return reverse_; }
    T value() const { return value_; }
    float progress() const {
        if (duration_ <= 0.0f) return 1.0f;
        return detail::clamp01(legElapsed_ / duration_);
    }

    // ---- 控制 -------------------------------------------------------------
    void play() {
        playing_ = true;
        finished_ = false;
        legIndex_ = 0;
        legElapsed_ = 0.0f;
        delayLeft_ = delay_;
        reverse_ = false;
        value_ = from_;
        if (delayLeft_ <= 0.0f) emitUpdate();
    }

    void restart() { play(); }

    void pause() { playing_ = false; }

    void resume() {
        if (finished_) { play(); return; }
        playing_ = true;
    }

    void stop() {
        playing_ = false;
        legElapsed_ = 0.0f;
        delayLeft_ = 0.0f;
        value_ = from_;
        emitUpdate();
    }

    // 跳到归一化进度（不改变播放状态）
    void seek(float normalized) {
        const float t = detail::clamp01(normalized);
        legElapsed_ = duration_ * t;
        const T& a = reverse_ ? to_ : from_;
        const T& b = reverse_ ? from_ : to_;
        value_ = lerpValue(a, b, ease(easing_, t));
        emitUpdate();
    }

    // ---- 驱动 -------------------------------------------------------------
    void tick(float dtSeconds) {
        if (!playing_) return;

        float dt = std::max(0.0f, dtSeconds) * speed_;
        if (dt <= 0.0f) return;

        if (delayLeft_ > 0.0f) {
            delayLeft_ -= dt;
            if (delayLeft_ > 0.0f) return;
            dt = -delayLeft_;
            delayLeft_ = 0.0f;
        }

        if (duration_ <= 0.0f) {
            value_ = reverse_ ? from_ : to_;
            finish();
            return;
        }

        legElapsed_ += dt;

        while (legElapsed_ >= duration_) {
            legElapsed_ -= duration_;

            // 完成一段
            ++legIndex_;
            const int totalLegs = (loops_ == 0)
                ? std::numeric_limits<int>::max()
                : loops_ * (pingPong_ ? 2 : 1);

            if (legIndex_ >= totalLegs) {
                legElapsed_ = 0.0f;
                value_ = reverse_ ? from_ : to_;
                finish();
                return;
            }

            if (pingPong_) {
                reverse_ = !reverse_;
            } else {
                reverse_ = false;
            }
        }

        const float t = detail::clamp01(legElapsed_ / duration_);
        const T& a = reverse_ ? to_ : from_;
        const T& b = reverse_ ? from_ : to_;
        value_ = lerpValue(a, b, ease(easing_, t));
        emitUpdate();
    }

    // ---- 回调 -------------------------------------------------------------
    std::function<void(const T&)> onUpdate;
    std::function<void()> onComplete;

private:
    void emitUpdate() {
        if (onUpdate) onUpdate(value_);
    }

    void finish() {
        if (finished_) return;
        finished_ = true;
        playing_ = false;
        emitUpdate();
        if (onComplete) onComplete();
    }

    T from_{};
    T to_{};
    T value_{};

    float duration_ = 0.3f;
    float delay_ = 0.0f;
    float speed_ = 1.0f;
    float delayLeft_ = 0.0f;
    float legElapsed_ = 0.0f;

    Easing easing_ = Easing::OutCubic;
    int loops_ = 1;
    int legIndex_ = 0;

    bool pingPong_ = false;
    bool reverse_ = false;
    bool playing_ = false;
    bool finished_ = true;
};

using FloatTween = Tween<float>;
using ColorTween = Tween<Color>;
using PointTween = Tween<Vec2f>;

// ===========================================================================
// KeyframeTrack — 数值关键帧时间轴
// ===========================================================================
struct Keyframe {
    float time = 0.0f;          // 秒
    float value = 0.0f;
    Easing easing = Easing::Linear;   // 从上一帧到本帧的缓动
};

class KeyframeTrack {
public:
    std::vector<Keyframe> keys;
    bool loop = false;
    float speed = 1.0f;

    std::function<void(float)> onUpdate;
    std::function<void()> onComplete;

    void play() {
        if (keys.empty()) return;
        playing_ = true;
        finished_ = false;
        time_ = 0.0f;
        sample(0.0f);
    }

    void pause() { playing_ = false; }
    void stop() {
        playing_ = false;
        time_ = 0.0f;
        sample(0.0f);
    }

    bool playing() const { return playing_; }
    bool finished() const { return finished_; }
    float time() const { return time_; }
    float duration() const { return keys.empty() ? 0.0f : keys.back().time; }

    float sample(float t) const {
        if (keys.empty()) return 0.0f;
        if (t <= keys.front().time) return keys.front().value;
        if (t >= keys.back().time) return keys.back().value;

        size_t index = 1;
        for (; index < keys.size(); ++index) {
            if (keys[index].time >= t) break;
        }
        if (index >= keys.size()) return keys.back().value;

        const Keyframe& a = keys[index - 1];
        const Keyframe& b = keys[index];
        const float span = b.time - a.time;
        const float local = span <= 0.0f ? 1.0f : (t - a.time) / span;
        return a.value + (b.value - a.value) * ease(b.easing, local);
    }

    void tick(float dtSeconds) {
        if (!playing_ || keys.empty()) return;

        const float total = duration();
        time_ += std::max(0.0f, dtSeconds) * speed;

        if (total <= 0.0f) {
            time_ = 0.0f;
            emit(sample(0.0f));
            finish();
            return;
        }

        if (time_ >= total) {
            if (loop) {
                time_ = std::fmod(time_, total);
            } else {
                time_ = total;
                emit(sample(total));
                finish();
                return;
            }
        }
        emit(sample(time_));
    }

private:
    void emit(float value) {
        if (onUpdate) onUpdate(value);
    }
    void finish() {
        if (finished_) return;
        finished_ = true;
        playing_ = false;
        if (onComplete) onComplete();
    }

    float time_ = 0.0f;
    bool playing_ = false;
    bool finished_ = true;
};

// ===========================================================================
// SpringValue — 弹簧阻尼动画（半隐式欧拉 + 固定子步）
// ===========================================================================
class SpringValue {
public:
    float value = 0.0f;
    float target = 0.0f;
    float velocity = 0.0f;

    float stiffness = 220.0f;   // 刚度
    float damping = 26.0f;      // 阻尼
    float mass = 1.0f;

    void snapTo(float v) {
        value = v;
        target = v;
        velocity = 0.0f;
    }

    void setTarget(float v) { target = v; }

    // 返回本帧是否发生可见变化
    bool tick(float dtSeconds) {
        constexpr float kStep = 1.0f / 240.0f;
        const float total = std::max(0.0f, dtSeconds);
        float remaining = total;

        while (remaining > 1.0e-6f) {
            const float step = std::min(kStep, remaining);
            remaining -= step;

            const float springForce = (target - value) * stiffness;
            const float damperForce = velocity * damping;
            const float acceleration = (springForce - damperForce) / std::max(0.01f, mass);
            velocity += acceleration * step;
            value += velocity * step;
        }

        if (settled()) {
            value = target;
            velocity = 0.0f;
            return false;
        }
        return total > 0.0f;
    }

    bool settled(float epsilon = 0.01f) const {
        return std::fabs(target - value) < epsilon &&
               std::fabs(velocity) < epsilon * 10.0f;
    }
};

} // namespace anim
} // namespace modern_ui
