// ============================================================================
// FrameProcessor.cpp
// ============================================================================

#include "FrameProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace camerapro {

namespace {

float clampf(float value, float lo, float hi) {
    return std::max(lo, std::min(hi, value));
}

inline uint8_t clampByte(int32_t value) {
    return static_cast<uint8_t>(std::max(0, std::min(255, value)));
}

} // namespace

void FrameProcessor::setParams(const PostParams& params) {
    const bool changed =
        std::fabs(params.brightness - params_.brightness) > 0.0001f ||
        std::fabs(params.contrast - params_.contrast) > 0.0001f ||
        std::fabs(params.saturation - params_.saturation) > 0.0001f ||
        std::fabs(params.sharpness - params_.sharpness) > 0.0001f ||
        params.autoEnhance != params_.autoEnhance ||
        params.mirror != params_.mirror;

    params_ = params;
    if (changed) lutDirty_ = true;
}

void FrameProcessor::rebuildLut(float autoOffset) {
    const float brightnessOffset =
        (params_.brightness - 0.5f) * 200.0f + autoOffset;
    const float contrastFactor = 0.5f + params_.contrast * 1.3f;

    for (int32_t i = 0; i < 256; ++i) {
        float value = static_cast<float>(i) + brightnessOffset;
        value = (value - 128.0f) * contrastFactor + 128.0f;
        lut_[i] = clampByte(static_cast<int32_t>(std::lround(value)));
    }
    lutDirty_ = false;
}

void FrameProcessor::process(uint8_t* bgra, int32_t width, int32_t height) {
    if (!bgra || width <= 0 || height <= 0) return;

    const size_t pixelCount = static_cast<size_t>(width) * height;

    // 自动测光：抽样 1/64 像素求平均亮度，缓慢跟随，避免画面忽明忽暗。
    if (params_.autoEnhance) {
        double sum = 0.0;
        size_t samples = 0;
        for (int32_t y = 0; y < height; y += 8) {
            const uint8_t* row = bgra + static_cast<size_t>(y) * width * 4u;
            for (int32_t x = 0; x < width; x += 8) {
                const uint8_t* pixel = row + static_cast<size_t>(x) * 4u;
                sum += 0.114 * pixel[0] + 0.587 * pixel[1] + 0.299 * pixel[2];
                ++samples;
            }
        }
        if (samples > 0) {
            const float mean = static_cast<float>(sum / static_cast<double>(samples));
            const float target = (mean - 132.0f) * 0.55f;
            const float desired = clampf(target, -70.0f, 70.0f);
            autoOffset_ += (desired - autoOffset_) * 0.12f;
        }
    } else if (autoOffset_ != 0.0f) {
        autoOffset_ *= 0.88f;
        if (std::fabs(autoOffset_) < 0.2f) autoOffset_ = 0.0f;
    }

    if (lutDirty_ || params_.autoEnhance) rebuildLut(autoOffset_);

    const float saturation = params_.saturation * 2.0f;
    const bool useSaturation = std::fabs(saturation - 1.0f) > 0.01f;

    if (useSaturation) {
        uint8_t* pixel = bgra;
        for (size_t i = 0; i < pixelCount; ++i, pixel += 4) {
            const float b = static_cast<float>(lut_[pixel[0]]);
            const float g = static_cast<float>(lut_[pixel[1]]);
            const float r = static_cast<float>(lut_[pixel[2]]);
            const float luma = 0.114f * b + 0.587f * g + 0.299f * r;

            pixel[0] = clampByte(static_cast<int32_t>(std::lround(
                luma + (b - luma) * saturation)));
            pixel[1] = clampByte(static_cast<int32_t>(std::lround(
                luma + (g - luma) * saturation)));
            pixel[2] = clampByte(static_cast<int32_t>(std::lround(
                luma + (r - luma) * saturation)));
            pixel[3] = 255;
        }
    } else {
        uint8_t* pixel = bgra;
        for (size_t i = 0; i < pixelCount; ++i, pixel += 4) {
            pixel[0] = lut_[pixel[0]];
            pixel[1] = lut_[pixel[1]];
            pixel[2] = lut_[pixel[2]];
            pixel[3] = 255;
        }
    }

    // 反锐化：4 邻域均值作为低频，原图 + amount × (原图 - 低频)。
    if (params_.sharpness > 0.02f) {
        const float amount = params_.sharpness * 1.5f;
        scratch_.resize(pixelCount * 4u);
        std::memcpy(scratch_.data(), bgra, pixelCount * 4u);

        const uint8_t* src = scratch_.data();
        for (int32_t y = 0; y < height; ++y) {
            const int32_t yUp = std::max(0, y - 1);
            const int32_t yDown = std::min(height - 1, y + 1);
            uint8_t* row = bgra + static_cast<size_t>(y) * width * 4u;
            const uint8_t* rowUp = src + static_cast<size_t>(yUp) * width * 4u;
            const uint8_t* rowDown = src + static_cast<size_t>(yDown) * width * 4u;
            const uint8_t* rowMid = src + static_cast<size_t>(y) * width * 4u;

            for (int32_t x = 0; x < width; ++x) {
                const int32_t xLeft = std::max(0, x - 1);
                const int32_t xRight = std::min(width - 1, x + 1);
                const size_t offset = static_cast<size_t>(x) * 4u;

                for (int32_t channel = 0; channel < 3; ++channel) {
                    const int32_t center = static_cast<int32_t>(
                        rowMid[offset + static_cast<size_t>(channel)]);
                    const int32_t blur =
                        (static_cast<int32_t>(rowMid[static_cast<size_t>(xLeft) * 4u +
                                               static_cast<size_t>(channel)]) +
                         static_cast<int32_t>(rowMid[static_cast<size_t>(xRight) * 4u +
                                               static_cast<size_t>(channel)]) +
                         static_cast<int32_t>(rowUp[offset + static_cast<size_t>(channel)]) +
                         static_cast<int32_t>(rowDown[offset + static_cast<size_t>(channel)]) +
                         2) / 4;
                    const int32_t value = static_cast<int32_t>(std::lround(
                        center + (center - blur) * amount));
                    row[offset + static_cast<size_t>(channel)] = clampByte(value);
                }
                row[offset + 3u] = 255;
            }
        }
    }

    // 水平镜像（交换像素而不是字节，保持 BGRA 通道顺序）。
    if (params_.mirror) {
        for (int32_t y = 0; y < height; ++y) {
            uint8_t* row = bgra + static_cast<size_t>(y) * width * 4u;
            for (int32_t x = 0; x < width / 2; ++x) {
                uint8_t* left = row + static_cast<size_t>(x) * 4u;
                uint8_t* right = row + static_cast<size_t>(width - 1 - x) * 4u;
                for (int32_t channel = 0; channel < 4; ++channel) {
                    std::swap(left[channel], right[channel]);
                }
            }
            row[3] = 255;
            if (width > 1) row[static_cast<size_t>(width - 1) * 4u + 3u] = 255;
        }
    }
}

} // namespace camerapro
