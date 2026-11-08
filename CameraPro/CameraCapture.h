// ============================================================================
// CameraCapture.h — Media Foundation 摄像头采集（独立工作线程）
// ============================================================================
//
// 设计：
//   - UI 线程只调用 start/stop/takeFrame；采集、格式协商、像素拷贝全部在
//     专用线程完成，摄像头掉线不会阻塞主线程。
//   - 输出统一为 top-down BGRA（A=255），可直接包装成 Blend2D PRGB32 纹理。
//   - 只保留最新一帧，消费端来不及取时旧帧被丢弃（永远显示实时画面）。
// ============================================================================

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace camerapro {

struct VideoFrame {
    int32_t width = 0;
    int32_t height = 0;
    uint64_t sequence = 0;
    std::vector<uint8_t> bgra;
};

class CameraCapture {
public:
    CameraCapture();
    ~CameraCapture();

    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;

    // 停止旧设备并异步启动指定序号的摄像头（0 = 默认设备）。
    void start(int32_t deviceIndex = 0);
    void stop();

    // 最近一次枚举到的设备列表 / 当前设备序号。
    std::vector<std::string> deviceNames() const;
    int32_t deviceIndex() const;

    // 采集 / 呈现帧率上限（默认 10fps，可运行时调整）。
    // 摄像头仍按原生帧率读取，但只按目标节奏做色彩转换和发布。
    void setTargetFps(int32_t fps);
    int32_t targetFps() const;

    // 取出最新帧（移动语义；没有新帧时返回 false）。
    bool takeFrame(VideoFrame* frame);

    bool running() const;
    std::string error() const;

    int32_t frameWidth() const;
    int32_t frameHeight() const;

private:
    void workerMain(int32_t deviceIndex);

    mutable std::mutex mutex_;
    std::vector<std::string> deviceNames_;
    std::string error_;
    VideoFrame pending_;
    bool hasPending_ = false;
    bool running_ = false;
    bool stopRequested_ = false;
    int32_t deviceIndex_ = 0;
    int32_t frameWidth_ = 0;
    int32_t frameHeight_ = 0;
    int32_t targetFps_ = 10;
    std::thread worker_;
};

} // namespace camerapro
