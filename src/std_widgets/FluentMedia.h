// ============================================================================
// FluentMedia.h — 媒体类控件
// ============================================================================
//
//   ImageView   — 位图显示（Stretch / Uniform / UniformToFill）
//   PersonPicture — 首字母头像 / 在线状态点
//   AnimatedImage — Sprite 图集动画（驱动 SpriteAnimator，按帧更新 ImageShape）
//
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "FluentControl.h"
#include "SpriteAnimation.h"

namespace modern_ui {
namespace fluent {

class ImageView : public FluentControl {
public:
    enum class Stretch : uint8_t { Uniform = 0, UniformToFill, Fill };

    ImageView();
    explicit ImageView(uint64_t imageHandle, int32_t imageWidth, int32_t imageHeight);

    void setImage(uint64_t imageHandle, int32_t imageWidth, int32_t imageHeight);
    // 从磁盘加载位图（需要后端实现 IRenderer::loadImageFile）。
    // 同一路径重复调用命中后端缓存，不会重复解码。
    bool loadImageFile(IRenderer& renderer, const std::string& path,
                       std::string* error = nullptr);
    void clearImage();

    void setStretch(Stretch stretch);
    Stretch stretch() const { return stretch_; }

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool acceptsFocus() const override { return false; }

private:
    uint64_t imageHandle_ = 0;
    int32_t imageWidth_ = 0;
    int32_t imageHeight_ = 0;
    Stretch stretch_ = Stretch::Uniform;

    uint32_t placeholderId_ = 0;
    uint32_t placeholderIconId_ = 0;
    uint32_t imageId_ = 0;
};

class PersonPicture : public FluentControl {
public:
    enum class Status : uint8_t { None = 0, Online, Away, Offline };

    explicit PersonPicture(std::string displayName = {}, int32_t size = 48);

    void setDisplayName(std::string displayName);
    const std::string& displayName() const { return displayName_; }

    void setStatus(Status status);
    Status status() const { return status_; }

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    bool acceptsFocus() const override { return false; }

private:
    std::string initials() const;

    std::string displayName_;
    int32_t preferredSize_ = 48;
    Status status_ = Status::None;

    uint32_t circleId_ = 0;
    uint32_t initialsId_ = 0;
    uint32_t statusId_ = 0;
};

class AnimatedImage : public FluentControl {
public:
    AnimatedImage();

    void setSheet(const anim::SpriteSheet& sheet);
    const anim::SpriteSheet& sheet() const { return animator_.sheet(); }

    void addClip(anim::SpriteClip clip);
    bool play(const std::string& clipName = {}, bool restart = true);
    void pause();
    void resume();
    void stop();

    bool playing() const { return animator_.playing(); }
    bool finished() const { return animator_.finished(); }
    int32_t currentFrame() const { return animator_.currentFrame(); }

    void setSpeed(float speed) { animator_.setSpeed(speed); }
    void setScale(float scale);
    float scale() const { return scale_; }

    void setFlipY(bool flipY) { flipY_ = flipY; if (laidOut()) updateVisuals(); }

    Vec2i onMeasure(const Vec2i& available) const override;
    void updateVisuals() override;
    void onTick(float dtSeconds) override;
    bool acceptsFocus() const override { return false; }

private:
    anim::SpriteAnimator animator_;
    float scale_ = 1.0f;
    bool flipY_ = false;
    uint32_t imageId_ = 0;
};

} // namespace fluent
} // namespace modern_ui
