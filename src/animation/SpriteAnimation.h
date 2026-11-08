// ============================================================================
// SpriteAnimation.h — Sprite 2D 帧动画系统
// ============================================================================
//
// 组成：
//   SpriteSheet    — 纹理图集描述（网格 / 边距 / 间距 / 帧尺寸 / 默认帧时长）
//   SpriteClip     — 动画剪辑（帧序列 / fps / 循环 / 乒乓 / 逐帧时长）
//   SpriteAnimator — 播放器（播放 / 暂停 / 速度 / 帧回调 / 完成回调）
//
// 渲染接口：
//   ImageShape 新增 srcX/srcY/srcW/srcH 源矩形；SpriteAnimator::applyTo()
//   把当前帧写入 ImageShape，由后端从图集中裁切并显示。
//
// 用法：
//   SpriteSheet sheet = SpriteSheet::grid(handle, 48, 48, 8, 1, 0, 0, 100);
//   SpriteAnimator animator;
//   animator.setSheet(sheet);
//   animator.addClip(SpriteClip::allFrames(sheet, 12.0f, true, "idle"));
//   animator.play("idle");
//   // 每帧： if (animator.tick(dt)) updateImageShape();
//
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Canvas.h"

namespace modern_ui {
namespace anim {

struct SpriteFrame {
    Rect    source;             // 图集内的像素矩形
    int32_t durationMs = 0;     // 0 = 使用剪辑 fps / 图集默认值
};

// ---------------------------------------------------------------------------
// SpriteSheet
// ---------------------------------------------------------------------------
struct SpriteSheet {
    uint64_t imageHandle = 0;
    int32_t  frameWidth = 0;
    int32_t  frameHeight = 0;
    int32_t  columns = 1;
    int32_t  rows = 1;
    int32_t  margin = 0;        // 图集四周边距
    int32_t  spacing = 0;       // 帧与帧之间的间距
    int32_t  frameTimeMs = 100; // 默认单帧时长（当剪辑未给 fps 时使用）

    static SpriteSheet grid(uint64_t handle,
                            int32_t frameWidth,
                            int32_t frameHeight,
                            int32_t columns,
                            int32_t rows,
                            int32_t margin = 0,
                            int32_t spacing = 0,
                            int32_t frameTimeMs = 100);

    int32_t frameCount() const { return columns > 0 && rows > 0 ? columns * rows : 0; }
    bool valid() const { return imageHandle != 0 && frameWidth > 0 && frameHeight > 0; }

    // index 从 0 开始，行优先
    Rect frameRect(int32_t index) const;

    // 图集纹理总尺寸（用于布局 / 校验）
    int32_t atlasWidth() const {
        if (columns <= 0 || frameWidth <= 0) return 0;
        return margin * 2 + columns * frameWidth + (columns - 1) * spacing;
    }
    int32_t atlasHeight() const {
        if (rows <= 0 || frameHeight <= 0) return 0;
        return margin * 2 + rows * frameHeight + (rows - 1) * spacing;
    }
};

// ---------------------------------------------------------------------------
// SpriteClip
// ---------------------------------------------------------------------------
struct SpriteClip {
    std::string name;
    std::vector<int32_t> frames;               // SpriteSheet 帧号，空 = 全部帧
    std::vector<int32_t> frameDurationsMs;     // 可选，按 frames 下标覆盖时长
    float fps = 12.0f;                         // <= 0 时使用图集 frameTimeMs
    bool loop = true;
    bool pingPong = false;

    static SpriteClip allFrames(const SpriteSheet& sheet,
                                float fps = 12.0f,
                                bool loop = true,
                                std::string name = "default");

    static SpriteClip range(const SpriteSheet& sheet,
                            int32_t first,
                            int32_t last,
                            float fps = 12.0f,
                            bool loop = true,
                            std::string name = "range");
};

// ---------------------------------------------------------------------------
// SpriteAnimator
// ---------------------------------------------------------------------------
class SpriteAnimator {
public:
    SpriteAnimator() = default;

    // ---- 数据 ----
    void setSheet(const SpriteSheet& sheet);
    const SpriteSheet& sheet() const { return sheet_; }
    bool hasSheet() const { return sheet_.valid(); }

    void addClip(SpriteClip clip);
    void clearClips();
    size_t clipCount() const { return clips_.size(); }
    const SpriteClip* clip(const std::string& name) const;
    const SpriteClip* activeClip() const;

    // ---- 播放控制 ----
    bool play(const std::string& name, bool restart = true);
    bool playClip(SpriteClip clip, bool restart = true);
    bool playDefault(bool restart = true);

    void pause();
    void resume();
    void stop();

    bool playing() const { return playing_; }
    bool finished() const { return finished_; }
    float speed() const { return speed_; }
    void setSpeed(float speed) { speed_ = speed < 0.0f ? 0.0f : speed; }
    void setLoop(bool loop);
    void setPingPong(bool pingPong);

    // ---- 当前帧 ----
    int32_t frameCursor() const { return cursor_; }       // 剪辑内下标
    int32_t currentFrame() const;                         // 图集帧号
    Rect sourceRect() const;
    uint64_t imageHandle() const { return sheet_.imageHandle; }

    // ---- 驱动 ----
    // 返回本帧序号是否变化（便于只更新脏几何体）
    bool tick(float dtSeconds);

    // 把当前帧写入 ImageShape（自动带图集源矩形）
    void applyTo(ImageShape& image, int32_t x, int32_t y, int32_t w, int32_t h,
                 bool flipY = false) const;

    // ---- 回调 ----
    std::function<void(int32_t)> onFrameChanged;   // 参数 = 图集帧号
    std::function<void()> onCompleted;

private:
    const SpriteClip* lookup(const std::string& name) const;
    float frameSeconds(int32_t cursor) const;
    void emitFrame();

    SpriteSheet sheet_{};
    std::vector<SpriteClip> clips_{};

    int32_t activeClip_ = -1;
    int32_t cursor_ = 0;
    int8_t  direction_ = 1;      // 1 正向 / -1 反向（pingPong）
    float   frameElapsed_ = 0.0f;
    float   speed_ = 1.0f;

    bool playing_ = false;
    bool finished_ = true;
    bool completedEmitted_ = false;
};

} // namespace anim
} // namespace modern_ui
