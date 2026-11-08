// ============================================================================
// FrameProcessor.h — 摄像头画面后处理（CPU / 原地处理 top-down BGRA）
// ============================================================================
//
// 处理链：自动增强测光 → 亮度 / 对比度 LUT → 饱和度 → 锐化 → 水平镜像。
// 参数使用 0..1 的滑块量，内部换算成实际图像学参数。
// ============================================================================

#pragma once

#include <cstdint>
#include <vector>

namespace camerapro {

struct PostParams {
    float brightness = 0.5f;   // 0..1 → -100..+100
    float contrast = 0.5f;     // 0..1 → ×0.5..×1.8
    float saturation = 0.5f;   // 0..1 → ×0..×2
    float sharpness = 0.0f;    // 0..1 → 0..1.5 反锐化
    bool autoEnhance = false;  // 自动测光补偿
    bool mirror = false;       // 水平镜像
};

class FrameProcessor {
public:
    void setParams(const PostParams& params);
    const PostParams& params() const { return params_; }

    // 原地处理；bgra 为 width * height * 4，A 通道输出恒为 255。
    void process(uint8_t* bgra, int32_t width, int32_t height);

private:
    void rebuildLut(float autoOffset);

    PostParams params_{};
    bool lutDirty_ = true;
    uint8_t lut_[256]{};
    float autoOffset_ = 0.0f;
    std::vector<uint8_t> scratch_;
};

} // namespace camerapro
