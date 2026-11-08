// ============================================================================
// OcrEngine.cpp
// ============================================================================

#include "OcrEngine.h"

#include "rapidocrlite/rapidocrlite.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include <utility>

namespace camerapro {

namespace {

constexpr int32_t kOcrLongSideMin = 960;
constexpr int32_t kOcrLongSideMax = 1600;
constexpr float   kOcrMaxUpscale = 2.5f;

bool fileExists(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return file.good();
}

std::string joinPath(const std::string& directory, const std::string& file) {
    if (directory.empty()) return file;
    const char last = directory.back();
    if (last == '/' || last == '\\') return directory + file;
    return directory + "/" + file;
}

// 去掉首尾 ASCII 空白，便于过滤纯空白 / 单字符噪声结果。
std::string trimAscii(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (begin < end && isSpace(text[begin])) ++begin;
    while (end > begin && isSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

std::size_t utf8CodePointCount(const std::string& text) {
    std::size_t count = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t step = 1;
        if (lead >= 0xF0) step = 4;
        else if (lead >= 0xE0) step = 3;
        else if (lead >= 0xC0) step = 2;
        index += step;
        ++count;
    }
    return count;
}

// 双线性缩放（packed RGB）。
std::vector<uint8_t> resizeRgb(const std::vector<uint8_t>& source,
                               int32_t sourceWidth, int32_t sourceHeight,
                               int32_t targetWidth, int32_t targetHeight) {
    std::vector<uint8_t> target(
        static_cast<size_t>(targetWidth) * targetHeight * 3u, 0);
    if (sourceWidth <= 0 || sourceHeight <= 0 ||
        targetWidth <= 0 || targetHeight <= 0) {
        return target;
    }

    const float scaleX = static_cast<float>(sourceWidth) /
                         static_cast<float>(targetWidth);
    const float scaleY = static_cast<float>(sourceHeight) /
                         static_cast<float>(targetHeight);

    for (int32_t y = 0; y < targetHeight; ++y) {
        const float sourceY = std::max(
            0.0f, std::min(static_cast<float>(sourceHeight - 1),
                           (static_cast<float>(y) + 0.5f) * scaleY - 0.5f));
        const int32_t y0 = static_cast<int32_t>(sourceY);
        const int32_t y1 = std::min(sourceHeight - 1, y0 + 1);
        const float fy = sourceY - static_cast<float>(y0);

        for (int32_t x = 0; x < targetWidth; ++x) {
            const float sourceX = std::max(
                0.0f, std::min(static_cast<float>(sourceWidth - 1),
                               (static_cast<float>(x) + 0.5f) * scaleX - 0.5f));
            const int32_t x0 = static_cast<int32_t>(sourceX);
            const int32_t x1 = std::min(sourceWidth - 1, x0 + 1);
            const float fx = sourceX - static_cast<float>(x0);

            const size_t row0 = static_cast<size_t>(y0) * sourceWidth * 3u;
            const size_t row1 = static_cast<size_t>(y1) * sourceWidth * 3u;
            const size_t out = (static_cast<size_t>(y) * targetWidth +
                                static_cast<size_t>(x)) * 3u;

            for (int32_t channel = 0; channel < 3; ++channel) {
                const float p00 = static_cast<float>(source[row0 +
                    static_cast<size_t>(x0) * 3u + static_cast<size_t>(channel)]);
                const float p10 = static_cast<float>(source[row0 +
                    static_cast<size_t>(x1) * 3u + static_cast<size_t>(channel)]);
                const float p01 = static_cast<float>(source[row1 +
                    static_cast<size_t>(x0) * 3u + static_cast<size_t>(channel)]);
                const float p11 = static_cast<float>(source[row1 +
                    static_cast<size_t>(x1) * 3u + static_cast<size_t>(channel)]);

                const float top = p00 + (p10 - p00) * fx;
                const float bottom = p01 + (p11 - p01) * fx;
                const float value = top + (bottom - top) * fy;
                target[out + static_cast<size_t>(channel)] =
                    static_cast<uint8_t>(std::lround(std::max(0.0f,
                        std::min(255.0f, value))));
            }
        }
    }
    return target;
}

struct PreparedImage {
    std::vector<uint8_t> rgb;
    int32_t width = 0;
    int32_t height = 0;
    float scale = 1.0f;   // 处理图 → 原始截图
};

PreparedImage prepareImage(const std::vector<uint8_t>& source,
                           int32_t width, int32_t height) {
    PreparedImage prepared;
    prepared.width = width;
    prepared.height = height;
    prepared.rgb = source;

    const int32_t longSide = std::max(width, height);
    if (longSide <= 0) return prepared;

    float scale = 1.0f;
    if (longSide > kOcrLongSideMax) {
        scale = static_cast<float>(kOcrLongSideMax) / static_cast<float>(longSide);
    } else if (longSide < kOcrLongSideMin) {
        scale = std::min(kOcrMaxUpscale,
                         static_cast<float>(kOcrLongSideMin) /
                             static_cast<float>(longSide));
    }
    if (scale <= 1.001f && scale >= 0.999f) return prepared;

    prepared.width = std::max(1, static_cast<int32_t>(
        std::lround(static_cast<float>(width) * scale)));
    prepared.height = std::max(1, static_cast<int32_t>(
        std::lround(static_cast<float>(height) * scale)));
    prepared.scale = static_cast<float>(prepared.width) /
                     static_cast<float>(width);
    prepared.rgb = resizeRgb(source, width, height,
                             prepared.width, prepared.height);
    return prepared;
}

} // namespace

// ============================================================================
// 工作线程
// ============================================================================

struct OcrEngine::Impl {
    struct Job {
        int id = 0;
        int32_t offsetX = 0;
        int32_t offsetY = 0;
        int32_t width = 0;
        int32_t height = 0;
        std::vector<uint8_t> rgb;
    };

    std::string modelDirectory;
    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable condition;

    std::unique_ptr<Job> pending;
    std::vector<OcrResult> completed;
    int nextRequestId = 1;
    bool running = false;
    bool busy = false;
    bool quit = false;

    // 仅工作线程访问
    std::unique_ptr<rpocr::OCR> ocr;

    rpocr::OCR& ensureOcr() {
        if (ocr) return *ocr;

        const std::string det = joinPath(modelDirectory, "PP-OCRv6_det_small.onnx");
        const std::string rec = joinPath(modelDirectory, "PP-OCRv6_rec_small.onnx");
        const std::string dict = joinPath(modelDirectory, "ppocrv6_dict.txt");
        if (!fileExists(det) || !fileExists(rec) || !fileExists(dict)) {
            throw std::runtime_error(
                "未找到 PP-OCRv6 模型，请检查 " + modelDirectory);
        }

        rpocr::Options options;
        options.backend = rpocr::Backend::cpu_only;
        options.det_limit_side_len = kOcrLongSideMax;
        options.det_limit_type = 0;       // 长边超限时缩小
        options.det_use_dilation = 1;     // 与 RapidOCR 3.x 默认一致
        options.rec_batch_size = 4;
        options.use_cls = false;

        const unsigned hardware = std::thread::hardware_concurrency();
        options.rec_parallelism = static_cast<int>(
            std::max(2u, std::min(8u, hardware == 0 ? 4u : hardware)));

        ocr = std::make_unique<rpocr::OCR>(det, rec, dict, options);
        return *ocr;
    }

    void run() {
        while (true) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this] { return quit || pending != nullptr; });
                if (quit && !pending) return;
                job = std::move(*pending);
                pending.reset();
                busy = true;
            }

            OcrResult result;
            result.requestId = job.id;

            try {
                const PreparedImage prepared =
                    prepareImage(job.rgb, job.width, job.height);
                if (prepared.rgb.empty()) {
                    throw std::runtime_error("截图区域为空");
                }

                rpocr::Image image;
                image.width = prepared.width;
                image.height = prepared.height;
                image.rgb = prepared.rgb;

                const auto lines = ensureOcr().Recognize(image);
                const float inverseScale = 1.0f / prepared.scale;

                result.lines.reserve(lines.size());
                for (const rpocr::Result& line : lines) {
                    // 单字符结果多为图标 / 线条噪声，直接舍去。
                    const std::string visible = trimAscii(line.text);
                    if (utf8CodePointCount(visible) < 2) continue;

                    OcrLine converted;
                    converted.text = line.text;
                    converted.confidence = line.confidence;
                    for (int32_t corner = 0; corner < 4; ++corner) {
                        converted.quad[static_cast<size_t>(corner)] =
                            modern_ui::Vec2f{
                                static_cast<float>(line.box[static_cast<size_t>(corner)][0]) *
                                    inverseScale + static_cast<float>(job.offsetX),
                                static_cast<float>(line.box[static_cast<size_t>(corner)][1]) *
                                    inverseScale + static_cast<float>(job.offsetY)};
                    }
                    result.lines.push_back(std::move(converted));
                }
                result.ok = true;
            } catch (const std::exception& error) {
                result.ok = false;
                result.error = error.what();
            }

            {
                std::lock_guard<std::mutex> lock(mutex);
                busy = false;
                completed.push_back(std::move(result));
            }
        }
    }
};

// ============================================================================
// 公共接口
// ============================================================================

OcrEngine::OcrEngine() : impl_(std::make_unique<Impl>()) {}

OcrEngine::~OcrEngine() {
    shutdown();
}

void OcrEngine::initialize(const std::string& modelDirectory) {
    if (!impl_ || impl_->running) return;

    impl_->modelDirectory = modelDirectory;
    impl_->quit = false;
    impl_->running = true;
    impl_->worker = std::thread([impl = impl_.get()] { impl->run(); });
}

bool OcrEngine::initialized() const {
    return impl_ && impl_->running;
}

int OcrEngine::recognize(int32_t offsetX, int32_t offsetY,
                         int32_t width, int32_t height,
                         std::vector<uint8_t> rgb) {
    if (!impl_ || !impl_->running) return 0;

    Impl::Job job;
    int requestId = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        job.id = impl_->nextRequestId++;
        requestId = job.id;
        job.offsetX = offsetX;
        job.offsetY = offsetY;
        job.width = width;
        job.height = height;
        job.rgb = std::move(rgb);
        impl_->pending = std::make_unique<Impl::Job>(std::move(job));
    }
    impl_->condition.notify_one();
    return requestId;
}

bool OcrEngine::poll(OcrResult* result) {
    if (!impl_ || !result) return false;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->completed.empty()) return false;
    *result = std::move(impl_->completed.front());
    impl_->completed.erase(impl_->completed.begin());
    return true;
}

bool OcrEngine::busy() const {
    if (!impl_) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->busy || impl_->pending != nullptr;
}

void OcrEngine::shutdown() {
    if (!impl_ || !impl_->running) return;

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->quit = true;
        impl_->pending.reset();
    }
    impl_->condition.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->running = false;
}

} // namespace camerapro
