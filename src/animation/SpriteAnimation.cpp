// ============================================================================
// SpriteAnimation.cpp — Sprite 图集 / 剪辑 / 播放器实现
// ============================================================================

#include "SpriteAnimation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace modern_ui {
namespace anim {

// ===========================================================================
// SpriteSheet
// ===========================================================================

SpriteSheet SpriteSheet::grid(uint64_t handle,
                              int32_t frameWidthIn,
                              int32_t frameHeightIn,
                              int32_t columnsIn,
                              int32_t rowsIn,
                              int32_t marginIn,
                              int32_t spacingIn,
                              int32_t frameTimeMsIn) {
    SpriteSheet sheet;
    sheet.imageHandle = handle;
    sheet.frameWidth = frameWidthIn;
    sheet.frameHeight = frameHeightIn;
    sheet.columns = std::max(0, columnsIn);
    sheet.rows = std::max(0, rowsIn);
    sheet.margin = std::max(0, marginIn);
    sheet.spacing = std::max(0, spacingIn);
    sheet.frameTimeMs = std::max(1, frameTimeMsIn);
    return sheet;
}

Rect SpriteSheet::frameRect(int32_t index) const {
    const int32_t count = frameCount();
    if (index < 0 || index >= count || frameWidth <= 0 || frameHeight <= 0) {
        return Rect::null();
    }

    const int32_t column = index % columns;
    const int32_t row = index / columns;
    return Rect{
        margin + column * (frameWidth + spacing),
        margin + row * (frameHeight + spacing),
        frameWidth,
        frameHeight
    };
}

// ===========================================================================
// SpriteClip
// ===========================================================================

SpriteClip SpriteClip::allFrames(const SpriteSheet& sheet,
                                 float fpsIn,
                                 bool loopIn,
                                 std::string nameIn) {
    SpriteClip result;
    result.name = std::move(nameIn);
    result.fps = fpsIn;
    result.loop = loopIn;
    const int32_t count = sheet.frameCount();
    result.frames.reserve(static_cast<size_t>(std::max(0, count)));
    for (int32_t index = 0; index < count; ++index) result.frames.push_back(index);
    return result;
}

SpriteClip SpriteClip::range(const SpriteSheet& sheet,
                             int32_t first,
                             int32_t last,
                             float fpsIn,
                             bool loopIn,
                             std::string nameIn) {
    SpriteClip result;
    result.name = std::move(nameIn);
    result.fps = fpsIn;
    result.loop = loopIn;

    const int32_t count = sheet.frameCount();
    if (count <= 0) return result;

    first = std::max(0, std::min(first, count - 1));
    last = std::max(0, std::min(last, count - 1));
    if (last < first) std::swap(first, last);

    result.frames.reserve(static_cast<size_t>(last - first + 1));
    for (int32_t index = first; index <= last; ++index) result.frames.push_back(index);
    return result;
}

// ===========================================================================
// SpriteAnimator
// ===========================================================================

void SpriteAnimator::setSheet(const SpriteSheet& sheetIn) {
    sheet_ = sheetIn;
    clips_.clear();
    activeClip_ = -1;
    cursor_ = 0;
    direction_ = 1;
    frameElapsed_ = 0.0f;
    playing_ = false;
    finished_ = true;
    completedEmitted_ = false;
}

void SpriteAnimator::addClip(SpriteClip clip) {
    if (clip.name.empty()) clip.name = "clip" + std::to_string(clips_.size());
    for (SpriteClip& existing : clips_) {
        if (existing.name == clip.name) {
            existing = std::move(clip);
            return;
        }
    }
    clips_.push_back(std::move(clip));
}

void SpriteAnimator::clearClips() {
    clips_.clear();
    activeClip_ = -1;
    playing_ = false;
    finished_ = true;
}

const SpriteClip* SpriteAnimator::clip(const std::string& name) const {
    return lookup(name);
}

const SpriteClip* SpriteAnimator::activeClip() const {
    if (activeClip_ < 0 || activeClip_ >= static_cast<int32_t>(clips_.size())) return nullptr;
    return &clips_[static_cast<size_t>(activeClip_)];
}

const SpriteClip* SpriteAnimator::lookup(const std::string& name) const {
    for (const SpriteClip& item : clips_) {
        if (item.name == name) return &item;
    }
    return nullptr;
}

bool SpriteAnimator::playClip(SpriteClip clipIn, bool restart) {
    if (!sheet_.valid()) return false;

    if (clipIn.frames.empty()) {
        clipIn = SpriteClip::allFrames(sheet_, clipIn.fps, clipIn.loop, clipIn.name);
    }
    if (clipIn.frames.empty()) return false;

    // 同一剪辑且不要求重播：保持当前进度继续
    const SpriteClip* active = activeClip();
    if (!restart && active && active->name == clipIn.name && playing_) return true;

    if (clipIn.name.empty()) clipIn.name = "clip" + std::to_string(clips_.size());

    int32_t index = -1;
    for (size_t i = 0; i < clips_.size(); ++i) {
        if (clips_[i].name == clipIn.name) {
            index = static_cast<int32_t>(i);
            break;
        }
    }
    if (index >= 0) {
        clips_[static_cast<size_t>(index)] = std::move(clipIn);
    } else {
        clips_.push_back(std::move(clipIn));
        index = static_cast<int32_t>(clips_.size() - 1);
    }

    activeClip_ = index;
    cursor_ = 0;
    direction_ = 1;
    frameElapsed_ = 0.0f;
    playing_ = true;
    finished_ = false;
    completedEmitted_ = false;
    emitFrame();
    return true;
}

bool SpriteAnimator::play(const std::string& name, bool restart) {
    const SpriteClip* found = lookup(name);
    if (!found) return false;
    return playClip(*found, restart);
}

bool SpriteAnimator::playDefault(bool restart) {
    if (clips_.empty()) {
        if (!sheet_.valid()) return false;
        return playClip(SpriteClip::allFrames(sheet_), restart);
    }
    return playClip(clips_.front(), restart);
}

void SpriteAnimator::pause() {
    playing_ = false;
}

void SpriteAnimator::resume() {
    if (!activeClip() || finished_) return;
    playing_ = true;
}

void SpriteAnimator::stop() {
    playing_ = false;
    finished_ = true;
    cursor_ = 0;
    direction_ = 1;
    frameElapsed_ = 0.0f;
    emitFrame();
}

void SpriteAnimator::setLoop(bool loop) {
    if (activeClip_ < 0 || activeClip_ >= static_cast<int32_t>(clips_.size())) return;
    clips_[static_cast<size_t>(activeClip_)].loop = loop;
}

void SpriteAnimator::setPingPong(bool pingPong) {
    if (activeClip_ < 0 || activeClip_ >= static_cast<int32_t>(clips_.size())) return;
    clips_[static_cast<size_t>(activeClip_)].pingPong = pingPong;
}

int32_t SpriteAnimator::currentFrame() const {
    const SpriteClip* active = activeClip();
    if (!active || active->frames.empty()) return -1;
    if (cursor_ < 0 || cursor_ >= static_cast<int32_t>(active->frames.size())) return -1;
    return active->frames[static_cast<size_t>(cursor_)];
}

Rect SpriteAnimator::sourceRect() const {
    const int32_t frame = currentFrame();
    if (frame < 0) return Rect::null();
    return sheet_.frameRect(frame);
}

float SpriteAnimator::frameSeconds(int32_t cursor) const {
    const SpriteClip* active = activeClip();
    if (!active) return 1.0f;

    if (cursor >= 0 && cursor < static_cast<int32_t>(active->frameDurationsMs.size())) {
        const int32_t ms = active->frameDurationsMs[static_cast<size_t>(cursor)];
        if (ms > 0) return static_cast<float>(ms) * 0.001f;
    }
    if (active->fps > 0.0f) return 1.0f / active->fps;
    return static_cast<float>(std::max(1, sheet_.frameTimeMs)) * 0.001f;
}

bool SpriteAnimator::tick(float dtSeconds) {
    if (!playing_ || !activeClip() || dtSeconds <= 0.0f || speed_ <= 0.0f) return false;

    const SpriteClip* active = activeClip();
    if (!active || active->frames.empty()) {
        playing_ = false;
        finished_ = true;
        return false;
    }

    const int32_t last = static_cast<int32_t>(active->frames.size()) - 1;
    const int32_t previousFrame = currentFrame();

    frameElapsed_ += dtSeconds * speed_;

    bool ended = false;
    int32_t guard = 0;
    while (frameElapsed_ >= frameSeconds(cursor_) && guard++ < 10000) {
        frameElapsed_ -= frameSeconds(cursor_);

        if (last <= 0) {
            // 单帧剪辑
            if (active->loop) {
                cursor_ = 0;
                continue;
            }
            ended = true;
            break;
        }

        if (active->pingPong) {
            if (direction_ > 0) {
                if (cursor_ >= last) {
                    if (active->loop) {
                        direction_ = -1;
                        cursor_ = std::max(0, last - 1);
                    } else {
                        ended = true;
                        break;
                    }
                } else {
                    ++cursor_;
                }
            } else {
                if (cursor_ <= 0) {
                    if (active->loop) {
                        direction_ = 1;
                        cursor_ = std::min(last, 1);
                    } else {
                        ended = true;
                        break;
                    }
                } else {
                    --cursor_;
                }
            }
        } else if (active->loop) {
            cursor_ = (cursor_ + 1) % (last + 1);
        } else if (cursor_ < last) {
            ++cursor_;
        } else {
            ended = true;
            break;
        }
    }

    if (ended) {
        frameElapsed_ = 0.0f;
        playing_ = false;
        finished_ = true;
        if (!completedEmitted_) {
            completedEmitted_ = true;
            emitFrame();
            if (onCompleted) onCompleted();
        }
        return currentFrame() != previousFrame;
    }

    const int32_t current = currentFrame();
    if (current != previousFrame) emitFrame();
    return current != previousFrame;
}

void SpriteAnimator::emitFrame() {
    if (onFrameChanged) {
        const int32_t frame = currentFrame();
        if (frame >= 0) onFrameChanged(frame);
    }
}

void SpriteAnimator::applyTo(ImageShape& image, int32_t x, int32_t y,
                             int32_t w, int32_t h, bool flipYIn) const {
    image.handle = sheet_.imageHandle;
    image.x = x;
    image.y = y;
    image.w = w;
    image.h = h;
    image.flipY = flipYIn;

    const Rect source = sourceRect();
    if (source.isNull()) {
        image.srcX = 0;
        image.srcY = 0;
        image.srcW = 0;
        image.srcH = 0;
    } else {
        image.srcX = source.x;
        image.srcY = source.y;
        image.srcW = source.w;
        image.srcH = source.h;
    }
}

} // namespace anim
} // namespace modern_ui
