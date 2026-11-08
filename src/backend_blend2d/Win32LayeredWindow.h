// ============================================================================
// Win32LayeredWindow.h — Win32 原生分层窗口（Per-Pixel Alpha）
// ============================================================================
//
// 职责：
//   - 创建 WS_EX_LAYERED + WS_POPUP 无边框窗口
//   - 维护 32bpp top-down DIB Surface，交给 Blend2D 直接绘制
//   - 通过 UpdateLayeredWindow(Indirect) + AC_SRC_ALPHA 实现逐像素透明
//   - 消息循环、resize、DPI、鼠标/键盘事件（事件经 EventQueue 输出）
//
// 事件系统独立在 src/event，本窗口只负责"产生事件"。
// 渲染后端通过 pixels()/stride()/surfaceSerial() 绑定同一块 DIB 内存。
//
// 注意：
//   - 像素格式固定为 PRGB32（BGRA，预乘 alpha），与 ULW_ALPHA 匹配。
//   - 窗口重建 Surface 后 surfaceSerial() 会自增，后端需重新绑定 BLImage。
//   - 应用必须在收到 Resize 事件时同步 Canvas 尺寸。
// ============================================================================

#pragma once

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <cstdint>
#include <string>

#include "Canvas.h"
#include "Event.h"

namespace modern_ui {

// 尽早调用（run 之前）：启用 Per-Monitor V2 DPI 感知。
// 全屏悬浮层依赖它保证虚拟屏幕 / Hook 坐标都是物理像素。
void enableDpiAwareness();

class Win32LayeredWindow {
public:
    struct Options {
        std::wstring title = L"ModernUI";
        int32_t width = 900;
        int32_t height = 600;
        int32_t x = INT32_MIN;      // INT32_MIN = 屏幕工作区居中
        int32_t y = INT32_MIN;
        bool resizable = true;
        bool topmost = false;
        bool draggable = true;      // 顶部 40px 作为拖动区域
        bool visible = true;

        // --- 全屏批注 / 悬浮层选项 ---
        // clickThrough = true：窗口对鼠标完全透明（WS_EX_LAYERED |
        // WS_EX_TRANSPARENT），点击会落到下层应用；鼠标事件需由外部
        // GlobalMouseHook 提供（或动态关闭 clickThrough 后再用窗口消息）。
        bool clickThrough = false;
        // 不抢焦点（WS_EX_NOACTIVATE）：悬浮层显示时不会把前台应用切走。
        bool noActivate = false;
        // 工具窗口（WS_EX_TOOLWINDOW）：不出现在任务栏 / Alt+Tab。
        bool toolWindow = false;
        // 是否把窗口收到的鼠标消息推进 EventQueue。使用 Hook 输入时应设为 false，
        // 避免同一物理事件被窗口与 Hook 各派发一次。
        bool pushMouseEvents = true;
    };

    explicit Win32LayeredWindow(EventQueue& events);
    ~Win32LayeredWindow();

    Win32LayeredWindow(const Win32LayeredWindow&) = delete;
    Win32LayeredWindow& operator=(const Win32LayeredWindow&) = delete;

    // --- 生命周期 ---
    bool create(const Options& options, std::string* error = nullptr);
    void destroy();
    bool valid() const { return hwnd_ != nullptr; }
    HWND hwnd() const { return hwnd_; }

    void show();
    void hide();
    void setTitle(const std::wstring& title);
    void setResizable(bool resizable) { resizable_ = resizable; }
    void setDraggable(bool draggable) { draggable_ = draggable; }

    // --- 点击穿透（动态切换）---
    // true  = WS_EX_TRANSPARENT（鼠标完全穿透到下层窗口）
    // false = 恢复接收鼠标消息
    bool setClickThrough(bool clickThrough);
    bool clickThrough() const { return clickThrough_; }

    // --- 全局热键（RegisterHotKey）---
    // 快捷键会以 EventType::Hotkey（hotkeyId = id）推进事件队列。
    // modifiers 使用 EventModifier，内部自动加 MOD_NOREPEAT。
    bool registerHotkey(int id, uint32_t modifiers, uint32_t virtualKey);
    bool unregisterHotkey(int id);

    // 客户区 (0,0) 对应的屏幕坐标（无边框分层窗口 = 窗口左上角）
    Vec2i clientOrigin() const;

    // 修改客户区尺寸（无边框窗口的 client == window）
    bool setClientSize(int32_t width, int32_t height);

    // --- Surface（PRGB32 / BGRA，top-down DIB）---
    int32_t width() const { return width_; }
    int32_t height() const { return height_; }
    int32_t stride() const { return stride_; }
    uint8_t* pixels() const { return static_cast<uint8_t*>(bits_); }
    uint64_t surfaceSerial() const { return surfaceSerial_; }

    // --- 呈现（默认全量提交；传入 dirtyRect 时只提交该区域）---
    // dirtyRect 为 null 表示整窗提交（跨系统版本最可靠）；
    // 非 null 时通过 ULW prcDirty 只更新该区域，未变化像素沿用上次提交内容。
    void present(const Rect* dirtyRect = nullptr);

    // --- 消息 / 事件 ---
    EventQueue& events() { return events_; }

    // 非阻塞分发窗口消息；返回 false 表示消息循环已退出
    bool pumpMessages();

    // 最多等待 timeoutMs（或直到有消息/用户输入）
    bool waitMessages(uint32_t timeoutMs);

    void requestQuit();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool createSurface(HWND hwnd, int32_t width, int32_t height);
    void destroySurface();
    void pushEvent(Event ev);

    EventQueue& events_;

    HWND    hwnd_ = nullptr;
    HDC     memDC_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    void*   bits_ = nullptr;

    int32_t  width_ = 0;
    int32_t  height_ = 0;
    int32_t  stride_ = 0;
    uint64_t surfaceSerial_ = 0;

    bool resizable_ = true;
    bool draggable_ = true;
    bool clickThrough_ = false;
    bool pushMouseEvents_ = true;
    bool trackingLeave_ = false;
    bool quit_ = false;
};

} // namespace modern_ui
