// ============================================================================
// FluentMedia.cpp — ImageView / PersonPicture / AnimatedImage
// ============================================================================

#include "FluentMedia.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace modern_ui {
namespace fluent {
namespace {

Rect fitImageRect(int32_t frameWidth, int32_t frameHeight,
                  int32_t imageWidth, int32_t imageHeight,
                  bool cover) {
    if (frameWidth <= 0 || frameHeight <= 0 || imageWidth <= 0 || imageHeight <= 0) {
        return Rect{0, 0, std::max(0, frameWidth), std::max(0, frameHeight)};
    }

    const float scaleX = static_cast<float>(frameWidth) / static_cast<float>(imageWidth);
    const float scaleY = static_cast<float>(frameHeight) / static_cast<float>(imageHeight);
    const float scale = cover ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY);

    const int32_t width = std::max(1, static_cast<int32_t>(std::lround(imageWidth * scale)));
    const int32_t height = std::max(1, static_cast<int32_t>(std::lround(imageHeight * scale)));
    return Rect{(frameWidth - width) / 2, (frameHeight - height) / 2, width, height};
}

std::string firstUtf8Char(const std::string& text) {
    if (text.empty()) return {};
    const unsigned char first = static_cast<unsigned char>(text.front());
    size_t length = 1;
    if ((first & 0xE0u) == 0xC0u) length = 2;
    else if ((first & 0xF0u) == 0xE0u) length = 3;
    else if ((first & 0xF8u) == 0xF0u) length = 4;
    return text.substr(0, std::min(length, text.size()));
}

} // namespace

// ===========================================================================
// ImageView
// ===========================================================================
ImageView::ImageView() {
    Shape placeholder = makeRRectShape(Rect{0, 0, 1, 1}, 8.0f,
                                       withOpacity(theme().textPrimary, 0.08f),
                                       theme().stroke, 1);
    placeholderId_ = canvas().addShape(std::move(placeholder));

    Shape icon = makePathShape(
        {
            {Vec2f{0, 0}, Vec2f{1, 1}},
            {Vec2f{0, 0}, Vec2f{1, 1}}
        },
        theme().textTertiary, 2);
    placeholderIconId_ = canvas().addShape(std::move(icon));

    Shape image;
    ImageShape bitmap;
    bitmap.handle = 0;
    bitmap.w = 0;
    bitmap.h = 0;
    image.base.visible = false;
    image.data = bitmap;
    imageId_ = canvas().addShape(std::move(image));
}

ImageView::ImageView(uint64_t imageHandle, int32_t imageWidth, int32_t imageHeight) : ImageView() {
    setImage(imageHandle, imageWidth, imageHeight);
}

void ImageView::setImage(uint64_t imageHandle, int32_t imageWidth, int32_t imageHeight) {
    imageHandle_ = imageHandle;
    imageWidth_ = std::max(0, imageWidth);
    imageHeight_ = std::max(0, imageHeight);
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

bool ImageView::loadImageFile(IRenderer& renderer, const std::string& path,
                              std::string* error) {
    const uint64_t handle = renderer.loadImageFile(path, error);
    if (handle == 0) return false;

    int32_t width = 0, height = 0;
    if (!renderer.imageSize(handle, width, height)) return false;
    setImage(handle, width, height);
    return true;
}

void ImageView::clearImage() {
    imageHandle_ = 0;
    imageWidth_ = 0;
    imageHeight_ = 0;
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void ImageView::setStretch(Stretch stretch) {
    stretch_ = stretch;
    if (laidOut()) updateVisuals();
}

Vec2i ImageView::onMeasure(const Vec2i& available) const {
    (void)available;
    if (imageWidth_ > 0 && imageHeight_ > 0) {
        return Vec2i{imageWidth_, imageHeight_};
    }
    return Vec2i{160, 120};
}

void ImageView::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (width <= 0 || height <= 0) return;

    const bool hasImage = imageHandle_ != 0;

    canvas().updateShape(placeholderId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, width, height, 8.0f, 8.0f};
        shape.base.fill = withOpacity(theme().textPrimary, 0.08f);
        shape.base.stroke = theme().stroke;
        shape.base.strokeWidth = 1;
        shape.base.visible = !hasImage;
    });

    canvas().updateShape(placeholderIconId_, [&](Shape& shape) {
        const int32_t cx = width / 2;
        const int32_t cy = height / 2;
        shape = makePathShape(
            {
                {Vec2f{static_cast<float>(cx - 22), static_cast<float>(cy + 12)},
                 Vec2f{static_cast<float>(cx - 8), static_cast<float>(cy - 6)},
                 Vec2f{static_cast<float>(cx + 2), static_cast<float>(cy + 6)},
                 Vec2f{static_cast<float>(cx + 10), static_cast<float>(cy - 2)},
                 Vec2f{static_cast<float>(cx + 22), static_cast<float>(cy + 12)}},
                {Vec2f{static_cast<float>(cx - 6), static_cast<float>(cy + 12)},
                 Vec2f{static_cast<float>(cx + 5), static_cast<float>(cy + 12)}}
            },
            theme().textTertiary, 2);
        shape.base.visible = !hasImage;
    });

    Rect destination{0, 0, width, height};
    if (stretch_ == Stretch::Uniform) {
        destination = fitImageRect(width, height, imageWidth_, imageHeight_, false);
    } else if (stretch_ == Stretch::UniformToFill) {
        destination = fitImageRect(width, height, imageWidth_, imageHeight_, true);
    }

    canvas().updateShape(imageId_, [&](Shape& shape) {
        ImageShape bitmap;
        bitmap.handle = imageHandle_;
        bitmap.x = destination.x;
        bitmap.y = destination.y;
        bitmap.w = destination.w;
        bitmap.h = destination.h;
        shape.data = bitmap;
        shape.base.visible = hasImage;
    });
}

// ===========================================================================
// PersonPicture
// ===========================================================================
PersonPicture::PersonPicture(std::string displayName, int32_t size)
    : displayName_(std::move(displayName)), preferredSize_(std::max(16, size)) {
    Shape circle = makeCircleShape(0, 0, 1.0f, theme().accent);
    circleId_ = canvas().addShape(std::move(circle));

    Shape initials = makeTextShape("?", 16, theme().textOnAccent);
    initialsId_ = canvas().addShape(std::move(initials));

    Shape status = makeCircleShape(0, 0, 1.0f, theme().success);
    status.base.visible = false;
    statusId_ = canvas().addShape(std::move(status));
}

void PersonPicture::setDisplayName(std::string displayName) {
    if (displayName == displayName_) return;
    displayName_ = std::move(displayName);
    if (laidOut()) updateVisuals();
}

void PersonPicture::setStatus(Status status) {
    status_ = status;
    if (laidOut()) updateVisuals();
}

std::string PersonPicture::initials() const {
    std::string result;
    bool newWord = true;
    for (size_t i = 0; i < displayName_.size() && result.size() < 8; ++i) {
        const unsigned char ch = static_cast<unsigned char>(displayName_[i]);
        if (ch == ' ' || ch == '\t') {
            newWord = true;
            continue;
        }
        if (newWord) {
            if (ch < 0x80u) {
                result.push_back(static_cast<char>(ch));
            } else {
                result += firstUtf8Char(displayName_.substr(i));
            }
            newWord = false;
            if (result.size() >= 2) break;
        }
    }
    if (result.empty()) result = "?";
    return result;
}

Vec2i PersonPicture::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{preferredSize_, preferredSize_};
}

void PersonPicture::updateVisuals() {
    if (!laidOut()) return;
    const int32_t size = std::max(1, std::min(frameWidth(), frameHeight()));
    const int32_t cx = frameWidth() / 2;
    const int32_t cy = frameHeight() / 2;
    const float radius = static_cast<float>(size) * 0.5f - 1.0f;

    canvas().updateShape(circleId_, [&](Shape& shape) {
        shape.data = CircleShape{cx, cy, radius};
        shape.base.fill = theme().accent;
        shape.base.stroke = theme().stroke;
        shape.base.strokeWidth = 1;
        shape.base.visible = true;
    });

    const std::string text = initials();
    const int32_t font = std::max(11, static_cast<int32_t>(static_cast<float>(size) * 0.34f));
    const Rect measured = measureString(text, font);
    canvas().updateShape(initialsId_, [&](Shape& shape) {
        setShapeText(shape, text, font, theme().textOnAccent,
                cx - measured.w / 2,
                cy - measured.h / 2 - measured.y);
    });

    if (status_ == Status::None) {
        canvas().updateShape(statusId_, [](Shape& shape) { shape.base.visible = false; });
    } else {
        Color color = theme().textTertiary;
        if (status_ == Status::Online) color = theme().success;
        else if (status_ == Status::Away) color = theme().caution;

        const int32_t dotRadius = std::max(3, static_cast<int32_t>(radius * 0.24f));
        const int32_t dotX = cx + static_cast<int32_t>(radius * 0.66f);
        const int32_t dotY = cy + static_cast<int32_t>(radius * 0.66f);
        canvas().updateShape(statusId_, [&](Shape& shape) {
            shape.data = CircleShape{dotX, dotY, static_cast<float>(dotRadius)};
            shape.base.fill = color;
            shape.base.stroke = theme().card;
            shape.base.strokeWidth = 2;
            shape.base.visible = true;
        });
    }
}

// ===========================================================================
// AnimatedImage
// ===========================================================================
AnimatedImage::AnimatedImage() {
    Shape image;
    ImageShape bitmap;
    bitmap.handle = 0;
    bitmap.w = 0;
    bitmap.h = 0;
    image.base.visible = false;
    image.data = bitmap;
    imageId_ = canvas().addShape(std::move(image));
}

void AnimatedImage::setSheet(const anim::SpriteSheet& sheet) {
    animator_.setSheet(sheet);
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void AnimatedImage::addClip(anim::SpriteClip clip) {
    animator_.addClip(std::move(clip));
}

bool AnimatedImage::play(const std::string& clipName, bool restart) {
    const bool ok = clipName.empty() ? animator_.playDefault(restart)
                                     : animator_.play(clipName, restart);
    if (ok && laidOut()) updateVisuals();
    return ok;
}

void AnimatedImage::pause() {
    animator_.pause();
}

void AnimatedImage::resume() {
    animator_.resume();
}

void AnimatedImage::stop() {
    animator_.stop();
    if (laidOut()) updateVisuals();
}

void AnimatedImage::setScale(float scale) {
    const float next = std::max(0.1f, scale);
    if (std::fabs(next - scale_) < 1.0e-6f) return;
    scale_ = next;
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

Vec2i AnimatedImage::onMeasure(const Vec2i& available) const {
    (void)available;
    const anim::SpriteSheet& spriteSheet = animator_.sheet();
    if (!spriteSheet.valid()) return Vec2i{96, 96};

    return Vec2i{
        std::max(1, static_cast<int32_t>(std::lround(spriteSheet.frameWidth * scale_))),
        std::max(1, static_cast<int32_t>(std::lround(spriteSheet.frameHeight * scale_)))
    };
}

void AnimatedImage::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (width <= 0 || height <= 0) return;

    const anim::SpriteSheet& spriteSheet = animator_.sheet();
    const int32_t frame = animator_.currentFrame();
    if (!spriteSheet.valid() || frame < 0) {
        canvas().updateShape(imageId_, [](Shape& shape) { shape.base.visible = false; });
        return;
    }

    const float scale = std::min(
        static_cast<float>(width) / static_cast<float>(spriteSheet.frameWidth),
        static_cast<float>(height) / static_cast<float>(spriteSheet.frameHeight));
    const int32_t dstWidth = std::max(1, static_cast<int32_t>(std::lround(spriteSheet.frameWidth * scale)));
    const int32_t dstHeight = std::max(1, static_cast<int32_t>(std::lround(spriteSheet.frameHeight * scale)));
    const Rect destination{(width - dstWidth) / 2, (height - dstHeight) / 2, dstWidth, dstHeight};

    canvas().updateShape(imageId_, [&](Shape& shape) {
        ImageShape bitmap;
        animator_.applyTo(bitmap, destination.x, destination.y,
                          destination.w, destination.h, flipY_);
        shape.data = bitmap;
        shape.base.visible = true;
    });
}

void AnimatedImage::onTick(float dtSeconds) {
    FluentControl::onTick(dtSeconds);
    if (!animator_.playing()) return;
    if (animator_.tick(dtSeconds)) {
        if (laidOut()) updateVisuals();
    }
}

} // namespace fluent
} // namespace modern_ui
