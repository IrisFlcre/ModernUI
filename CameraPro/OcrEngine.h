// ============================================================================
// OcrEngine.h — RapidOCRLite 异步文字识别封装
// ============================================================================
//
// UI 线程提交截图（RGB / BGRA 像素），工作线程懒加载 PP-OCR 模型并识别，
// 识别结果通过 poll() 回到 UI 线程。坐标已换算回批注覆盖层客户区坐标。
// ============================================================================

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Canvas.h"

namespace camerapro {

struct OcrLine {
    std::string text;
    float confidence = 0.0f;
    // TL / TR / BR / BL，单位为覆盖层客户区像素坐标
    std::array<modern_ui::Vec2f, 4> quad{};
};

struct OcrResult {
    int requestId = 0;
    bool ok = false;
    std::string error;
    std::vector<OcrLine> lines;
};

class OcrEngine {
public:
    OcrEngine();
    ~OcrEngine();

    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    // modelDirectory 内需包含 PP-OCRv6_det_small.onnx / PP-OCRv6_rec_small.onnx /
    // ppocrv6_dict.txt。模型在首次识别时由工作线程加载。
    void initialize(const std::string& modelDirectory);

    // 提交识别任务（packed RGB，行优先）。返回请求 id；同一时间只保留最新任务。
    int recognize(int32_t offsetX, int32_t offsetY, int32_t width, int32_t height,
                  std::vector<uint8_t> rgb);

    // UI 线程轮询；有结果返回 true。
    bool poll(OcrResult* result);

    bool busy() const;
    bool initialized() const;

    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace camerapro
