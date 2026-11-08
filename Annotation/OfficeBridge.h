// ============================================================================
// OfficeBridge.h — WPS / Microsoft Office 放映感知
// ============================================================================
//
// 通过 COM 自动化连接正在运行的 PowerPoint 或 WPS 演示，读取放映页码：
//   Application.SlideShowWindows.Item(1).View.Slide.SlideIndex
//   Application.ActivePresentation.Slides.Count
// 轮询在后台线程执行（MTA），避免 Office 忙碌时阻塞批注 UI。
//
// 前台放映窗口识别：
//   - PowerPoint：窗口类名 screenClass
//   - WPS 演示：窗口类名 Widget（部分版本 SlideShowClass），标题含“幻灯片”，
//     并结合全屏 / WS_POPUP 样式做辅助校验。
//
// 未运行放映时不创建 Office 实例（GetActiveObject 只连接已存在实例）。
// ============================================================================

#pragma once

#include <cstdint>
#include <memory>

namespace annotation {

class OfficeBridge {
public:
    enum class App : uint8_t {
        None = 0,
        PowerPoint,
        Wps,
    };

    struct Snapshot {
        App app = App::None;
        bool presenting = false;
        int32_t page = 0;    // 当前放映页码（1 基）；0 = 未知
        int32_t total = 0;   // 演示文稿总页数；0 = 未知
    };

    OfficeBridge();
    ~OfficeBridge();

    OfficeBridge(const OfficeBridge&) = delete;
    OfficeBridge& operator=(const OfficeBridge&) = delete;

    // 启动后台 COM 轮询（幂等）。
    void initialize();

    // 读取最近一次轮询结果（线程安全）。
    Snapshot snapshot() const;

    // 前台是否为正在放映的 PowerPoint / WPS，可安全转发翻页键。
    bool canControlPresentation() const;

    // 向当前前台放映发送 PageDown / PageUp；返回是否已发送。
    bool sendPageKey(bool next) const;

private:
    struct Shared;

    static void poll(std::shared_ptr<Shared> shared);
    static App foregroundApp();
    static bool isPowerPointShowWindow(void* hwnd);
    static bool isWpsShowWindow(void* hwnd);
    static bool isFullscreenWindow(void* hwnd);

    std::shared_ptr<Shared> shared_;
};

} // namespace annotation
