// ============================================================================
// CameraCapture.cpp
// ============================================================================

#include "CameraCapture.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace camerapro {

namespace {

uint64_t monotonicMicros() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value;
    }();

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    if (frequency.QuadPart <= 0) return 0;
    return static_cast<uint64_t>(
        static_cast<double>(counter.QuadPart) * 1000000.0 /
        static_cast<double>(frequency.QuadPart));
}

std::string hresultText(HRESULT hr, const char* what) {
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s (0x%08X)", what,
                  static_cast<unsigned>(hr));
    return buffer;
}

enum class PixelFormat {
    Rgb32 = 0,
    Nv12,
    Yuy2,
};

// 把源行指针 / pitch 统一成 top-down BGRA，并强制 A=255。
bool convertFrame(const uint8_t* scan0, int32_t pitch,
                  int32_t width, int32_t height,
                  PixelFormat format, std::vector<uint8_t>* out) {
    if (!scan0 || width <= 0 || height <= 0 || !out) return false;

    out->resize(static_cast<size_t>(width) * height * 4u);
    uint8_t* dst = out->data();

    if (format == PixelFormat::Rgb32) {
        for (int32_t y = 0; y < height; ++y) {
            const uint8_t* srcRow =
                scan0 + static_cast<ptrdiff_t>(y) * pitch;
            uint8_t* dstRow = dst + static_cast<size_t>(y) * width * 4u;
            std::memcpy(dstRow, srcRow, static_cast<size_t>(width) * 4u);
            for (int32_t x = 0; x < width; ++x) {
                dstRow[static_cast<size_t>(x) * 4u + 3u] = 255;
            }
        }
        return true;
    }

    if (format == PixelFormat::Nv12) {
        const uint8_t* yPlane = scan0;
        const uint8_t* uvPlane =
            scan0 + static_cast<size_t>(pitch) * static_cast<size_t>(height);
        for (int32_t y = 0; y < height; ++y) {
            const uint8_t* yRow = yPlane + static_cast<ptrdiff_t>(y) * pitch;
            const uint8_t* uvRow =
                uvPlane + static_cast<ptrdiff_t>(y / 2) * pitch;
            uint8_t* dstRow = dst + static_cast<size_t>(y) * width * 4u;
            for (int32_t x = 0; x < width; ++x) {
                const int32_t yy = static_cast<int32_t>(yRow[x]) - 16;
                const int32_t u = static_cast<int32_t>(uvRow[(x / 2) * 2]) - 128;
                const int32_t v = static_cast<int32_t>(uvRow[(x / 2) * 2 + 1]) - 128;

                int32_t r = (298 * yy + 409 * v + 128) >> 8;
                int32_t g = (298 * yy - 100 * u - 208 * v + 128) >> 8;
                int32_t b = (298 * yy + 516 * u + 128) >> 8;
                r = std::max(0, std::min(255, r));
                g = std::max(0, std::min(255, g));
                b = std::max(0, std::min(255, b));

                uint8_t* pixel = dstRow + static_cast<size_t>(x) * 4u;
                pixel[0] = static_cast<uint8_t>(b);
                pixel[1] = static_cast<uint8_t>(g);
                pixel[2] = static_cast<uint8_t>(r);
                pixel[3] = 255;
            }
        }
        return true;
    }

    // YUY2: Y0 U Y1 V
    for (int32_t y = 0; y < height; ++y) {
        const uint8_t* srcRow = scan0 + static_cast<ptrdiff_t>(y) * pitch;
        uint8_t* dstRow = dst + static_cast<size_t>(y) * width * 4u;
        for (int32_t x = 0; x < width; x += 2) {
            const int32_t offset = (x / 2) * 4;
            const int32_t y0 = static_cast<int32_t>(srcRow[offset]) - 16;
            const int32_t u = static_cast<int32_t>(srcRow[offset + 1]) - 128;
            const int32_t y1 = static_cast<int32_t>(srcRow[offset + 2]) - 16;
            const int32_t v = static_cast<int32_t>(srcRow[offset + 3]) - 128;

            auto store = [&](int32_t index, int32_t luma) {
                int32_t r = (298 * luma + 409 * v + 128) >> 8;
                int32_t g = (298 * luma - 100 * u - 208 * v + 128) >> 8;
                int32_t b = (298 * luma + 516 * u + 128) >> 8;
                r = std::max(0, std::min(255, r));
                g = std::max(0, std::min(255, g));
                b = std::max(0, std::min(255, b));
                uint8_t* pixel = dstRow + static_cast<size_t>(x + index) * 4u;
                pixel[0] = static_cast<uint8_t>(b);
                pixel[1] = static_cast<uint8_t>(g);
                pixel[2] = static_cast<uint8_t>(r);
                pixel[3] = 255;
            };
            store(0, y0);
            if (x + 1 < width) store(1, y1);
        }
    }
    return true;
}

PixelFormat formatFromSubtype(REFGUID subtype, bool* supported) {
    *supported = true;
    if (subtype == MFVideoFormat_RGB32) return PixelFormat::Rgb32;
    if (subtype == MFVideoFormat_NV12) return PixelFormat::Nv12;
    if (subtype == MFVideoFormat_YUY2) return PixelFormat::Yuy2;
    *supported = false;
    return PixelFormat::Rgb32;
}

HRESULT tryRequestType(IMFSourceReader* reader, REFGUID subtype,
                       uint32_t width, uint32_t height) {
    ComPtr<IMFMediaType> type;
    HRESULT hr = MFCreateMediaType(&type);
    if (FAILED(hr)) return hr;

    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, subtype);
    if (width > 0 && height > 0) {
        MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width, height);
    }
    type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    return reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                       nullptr, type.Get());
}

} // namespace

// ============================================================================
// 生命周期
// ============================================================================

CameraCapture::CameraCapture() = default;

CameraCapture::~CameraCapture() {
    stop();
}

void CameraCapture::start(int32_t deviceIndex) {
    stop();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = false;
        error_.clear();
        running_ = true;
        hasPending_ = false;
        pending_ = VideoFrame{};
        deviceIndex_ = std::max(0, deviceIndex);
    }

    try {
        worker_ = std::thread([this, deviceIndex] {
            workerMain(std::max(0, deviceIndex));
        });
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        error_ = "无法创建摄像头采集线程";
    }
}

void CameraCapture::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
    }
    if (worker_.joinable()) worker_.join();

    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
}

std::vector<std::string> CameraCapture::deviceNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceNames_;
}

int32_t CameraCapture::deviceIndex() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceIndex_;
}

bool CameraCapture::takeFrame(VideoFrame* frame) {
    if (!frame) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasPending_) return false;
    *frame = std::move(pending_);
    pending_ = VideoFrame{};
    hasPending_ = false;
    return frame->width > 0 && frame->height > 0 && !frame->bgra.empty();
}

bool CameraCapture::running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

std::string CameraCapture::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

int32_t CameraCapture::frameWidth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return frameWidth_;
}

int32_t CameraCapture::frameHeight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return frameHeight_;
}

void CameraCapture::setTargetFps(int32_t fps) {
    std::lock_guard<std::mutex> lock(mutex_);
    targetFps_ = std::max(1, std::min(60, fps));
}

int32_t CameraCapture::targetFps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return targetFps_;
}

// ============================================================================
// 采集线程
// ============================================================================

void CameraCapture::workerMain(int32_t deviceIndex) {
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(coHr);
    const HRESULT mfHr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(mfHr)) {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        error_ = hresultText(mfHr, "Media Foundation 初始化失败");
        if (comInitialized) CoUninitialize();
        return;
    }

    auto fail = [this](const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        error_ = text;
    };

    ComPtr<IMFMediaSource> source;
    ComPtr<IMFSourceReader> reader;
    std::vector<ComPtr<IMFActivate>> devices;
    bool configured = false;

    do {
        ComPtr<IMFAttributes> attributes;
        HRESULT hr = MFCreateAttributes(&attributes, 2);
        if (FAILED(hr)) {
            fail(hresultText(hr, "创建设备枚举属性失败"));
            break;
        }
        attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

        IMFActivate** rawDevices = nullptr;
        UINT32 count = 0;
        hr = MFEnumDeviceSources(attributes.Get(), &rawDevices, &count);
        if (FAILED(hr)) {
            fail(hresultText(hr, "枚举摄像头失败"));
            break;
        }

        std::vector<std::string> names;
        devices.reserve(count);
        names.reserve(count);
        for (UINT32 i = 0; i < count; ++i) {
            devices.emplace_back();
            devices.back().Attach(rawDevices[i]);
            WCHAR* name = nullptr;
            if (SUCCEEDED(rawDevices[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, nullptr)) &&
                name) {
                char utf8[512] = "";
                WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8),
                                    nullptr, nullptr);
                names.emplace_back(utf8);
                CoTaskMemFree(name);
            } else {
                names.push_back("摄像头 " + std::to_string(i + 1));
            }
        }
        CoTaskMemFree(rawDevices);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            deviceNames_ = names;
        }

        if (devices.empty()) {
            fail("未检测到摄像头");
            break;
        }

        const int32_t index = std::min<int32_t>(
            deviceIndex, static_cast<int32_t>(devices.size()) - 1);

        hr = devices[static_cast<size_t>(index)]->ActivateObject(
            IID_PPV_ARGS(&source));
        if (FAILED(hr)) {
            fail(hresultText(hr, "打开摄像头失败"));
            break;
        }

        ComPtr<IMFAttributes> readerAttributes;
        hr = MFCreateAttributes(&readerAttributes, 3);
        if (FAILED(hr)) {
            fail(hresultText(hr, "创建读取器属性失败"));
            break;
        }
        readerAttributes->SetUINT32(
            MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        readerAttributes->SetUINT32(
            MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        readerAttributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);

        hr = MFCreateSourceReaderFromMediaSource(
            source.Get(), readerAttributes.Get(), &reader);
        if (FAILED(hr)) {
            fail(hresultText(hr, "创建摄像头读取器失败"));
            break;
        }

        // 优先请求 RGB32（方便直接上传 Blend2D 纹理），按常见分辨率逐级回退。
        const uint32_t sizes[][2] = {
            {1920, 1080}, {1280, 720}, {640, 480}, {0, 0},
        };
        for (const auto& size : sizes) {
            hr = tryRequestType(reader.Get(), MFVideoFormat_RGB32,
                                size[0], size[1]);
            if (SUCCEEDED(hr)) {
                configured = true;
                break;
            }
        }

        // RGB32 不可用时使用摄像头原生类型（NV12 / YUY2），本地做色彩转换。
        if (!configured) {
            for (UINT32 i = 0; i < 32; ++i) {
                ComPtr<IMFMediaType> nativeType;
                if (FAILED(reader->GetNativeMediaType(
                        MF_SOURCE_READER_FIRST_VIDEO_STREAM, i,
                        &nativeType))) {
                    break;
                }
                GUID subtype{};
                if (FAILED(nativeType->GetGUID(MF_MT_SUBTYPE, &subtype))) {
                    continue;
                }
                if (subtype != MFVideoFormat_NV12 &&
                    subtype != MFVideoFormat_YUY2) {
                    continue;
                }
                if (SUCCEEDED(reader->SetCurrentMediaType(
                        MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr,
                        nativeType.Get()))) {
                    configured = true;
                    break;
                }
            }
        }

        if (!configured) {
            fail("摄像头不支持 RGB32 / NV12 / YUY2 输出");
            break;
        }

        ComPtr<IMFMediaType> currentType;
        hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                         &currentType);
        if (FAILED(hr)) {
            fail(hresultText(hr, "读取摄像头格式失败"));
            break;
        }

        GUID subtype{};
        currentType->GetGUID(MF_MT_SUBTYPE, &subtype);
        bool supported = false;
        PixelFormat format = formatFromSubtype(subtype, &supported);
        if (!supported) {
            fail("摄像头像素格式不受支持");
            break;
        }

        uint32_t width = 0;
        uint32_t height = 0;
        MFGetAttributeSize(currentType.Get(), MF_MT_FRAME_SIZE, &width, &height);
        if (width == 0 || height == 0) {
            fail("摄像头未返回有效分辨率");
            break;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            frameWidth_ = static_cast<int32_t>(width);
            frameHeight_ = static_cast<int32_t>(height);
            deviceIndex_ = index;
        }

        VideoFrame frame;
        uint64_t sequence = 0;
        uint64_t lastPublishMicros = 0;

        while (true) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopRequested_) break;
            }

            DWORD streamIndex = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                    0, &streamIndex, &flags,
                                    &timestamp, &sample);

            if (FAILED(hr)) {
                fail(hresultText(hr, "读取摄像头画面失败"));
                break;
            }

            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                ComPtr<IMFMediaType> changedType;
                if (SUCCEEDED(reader->GetCurrentMediaType(
                        MF_SOURCE_READER_FIRST_VIDEO_STREAM, &changedType))) {
                    GUID changedSubtype{};
                    changedType->GetGUID(MF_MT_SUBTYPE, &changedSubtype);
                    bool stillSupported = false;
                    const PixelFormat nextFormat =
                        formatFromSubtype(changedSubtype, &stillSupported);
                    if (!stillSupported) {
                        fail("摄像头像素格式发生变化且不受支持");
                        break;
                    }
                    format = nextFormat;
                    MFGetAttributeSize(changedType.Get(), MF_MT_FRAME_SIZE,
                                       &width, &height);
                    if (width > 0 && height > 0) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        frameWidth_ = static_cast<int32_t>(width);
                        frameHeight_ = static_cast<int32_t>(height);
                    }
                }
            }

            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                fail("摄像头数据流已结束");
                break;
            }

            if (!sample) continue;

            // 帧率上限：读取仍在摄像头原生节奏进行，但只在目标间隔内
            // 做一次像素转换 / 发布，避免 30fps 画面持续占满 CPU 与重绘。
            int32_t targetFps = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                targetFps = targetFps_;
            }
            const uint64_t nowMicros = monotonicMicros();
            if (targetFps > 0) {
                const uint64_t intervalMicros =
                    1000000ull / static_cast<uint64_t>(targetFps);
                if (lastPublishMicros != 0 &&
                    nowMicros - lastPublishMicros < intervalMicros) {
                    continue;
                }
                lastPublishMicros = nowMicros;
            }

            ComPtr<IMFMediaBuffer> buffer;
            hr = sample->ConvertToContiguousBuffer(&buffer);
            if (FAILED(hr)) continue;

            bool converted = false;
            ComPtr<IMF2DBuffer> buffer2D;
            if (SUCCEEDED(buffer.As(&buffer2D))) {
                uint8_t* scan0 = nullptr;
                LONG pitch = 0;
                if (SUCCEEDED(buffer2D->Lock2D(&scan0, &pitch))) {
                    converted = convertFrame(scan0, static_cast<int32_t>(pitch),
                                             static_cast<int32_t>(width),
                                             static_cast<int32_t>(height),
                                             format, &frame.bgra);
                    buffer2D->Unlock2D();
                }
            }
            if (!converted) {
                uint8_t* data = nullptr;
                DWORD maxLength = 0;
                DWORD currentLength = 0;
                if (SUCCEEDED(buffer->Lock(&data, &maxLength, &currentLength))) {
                    const int32_t pitch = static_cast<int32_t>(width) *
                        (format == PixelFormat::Rgb32 ? 4
                         : format == PixelFormat::Yuy2 ? 2 : 1);
                    converted = convertFrame(data, pitch,
                                             static_cast<int32_t>(width),
                                             static_cast<int32_t>(height),
                                             format, &frame.bgra);
                    buffer->Unlock();
                }
            }
            if (!converted) continue;

            frame.width = static_cast<int32_t>(width);
            frame.height = static_cast<int32_t>(height);
            frame.sequence = ++sequence;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_ = std::move(frame);
                hasPending_ = true;
            }
            frame = VideoFrame{};
        }
    } while (false);

    reader.Reset();
    source.Reset();
    devices.clear();
    MFShutdown();
    if (comInitialized) CoUninitialize();

    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
}

} // namespace camerapro
