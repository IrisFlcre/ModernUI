// ============================================================================
// Event.h — 与渲染后端解耦的输入 / 窗口事件系统
// ============================================================================
//
// 设计目标：
//   - 事件类型与窗口/渲染后端解耦，任何 Win32 后端都可以复用。
//   - 窗口线程把事件 push 进 EventQueue，应用主循环 poll 出来处理。
//   - 只定义跨后端通用的字段（鼠标/键盘/尺寸/DPI），后端特有信息不进事件。
//
// 用法：
//   EventQueue events;
//   // 后端窗口内部：events.push(Event{...});
//   Event ev;
//   while (events.poll(ev)) { ... }
//
// ============================================================================

#pragma once

#include <cstdint>
#include <deque>
#include <mutex>

namespace modern_ui {

enum class EventType : uint8_t {
    None = 0,
    Quit,          // 消息循环退出
    Close,         // 用户请求关闭窗口（WM_CLOSE）
    Resize,        // 客户区尺寸变化（width/height 生效）
    Move,          // 窗口位置变化（x/y 为屏幕坐标）
    MouseMove,     // x/y 为客户区像素坐标
    MouseLeave,    // 鼠标离开客户区
    MouseDown,
    MouseUp,
    MouseWheel,    // wheelDelta 以"格"为单位（+1 = 向上滚一格）
    KeyDown,       // key = Win32 virtual-key code
    KeyUp,
    Char,          // codepoint = Unicode 码点
    FocusIn,
    FocusOut,
    DpiChanged,    // dpiScale = newDpi / 96.0
    Hotkey,        // hotkeyId 生效（RegisterHotKey / 全局热键）
};

// 事件来源：窗口消息 / 全局低级 Hook / 程序合成。
// Hook 事件的 x/y 已由宿主换算成客户区坐标，screenX/screenY 保留屏幕绝对坐标。
enum class InputSource : uint8_t {
    Window = 0,
    Hook,
    Synthetic,
};

// 指针设备类型；Hook 路径目前只能识别鼠标，Window 路径可携带笔/触摸。
enum class PointerType : uint8_t {
    Mouse = 0,
    Pen,
    Touch,
};

enum MouseButton : uint8_t {
    MouseButtonNone = 0,
    MouseButtonLeft = 1,
    MouseButtonRight = 2,
    MouseButtonMiddle = 3,
};

enum EventModifier : uint32_t {
    ModifierShift   = 1u << 0,
    ModifierControl = 1u << 1,
    ModifierAlt     = 1u << 2,
    ModifierSuper   = 1u << 3,
};

struct Event {
    EventType type = EventType::None;
    uint64_t  timestampMs = 0;

    // 鼠标 / 窗口坐标（客户区）
    int32_t x = 0;
    int32_t y = 0;

    // 屏幕绝对坐标（Hook 事件必有；窗口事件在宿主换算后填入）
    int32_t screenX = 0;
    int32_t screenY = 0;

    // 事件来源 / 指针类型 / 笔压（0..1，鼠标默认 1.0）
    InputSource source = InputSource::Window;
    PointerType pointerType = PointerType::Mouse;
    float       pressure = 1.0f;
    // 触点 / 指针 ID（多指触控时区分不同手指；鼠标恒为 0）
    int32_t     pointerId = 0;
    bool        injected = false;      // 是否由 mouse_event / SendInput 注入
    // 触控专用：主触点 / 手掌 / 笔
    bool        primaryTouch = false;
    bool        palmTouch = false;

    // 全局热键（EventType::Hotkey）
    int32_t hotkeyId = 0;

    // Resize
    int32_t width = 0;
    int32_t height = 0;

    // 鼠标
    uint8_t button = MouseButtonNone;   // MouseButton
    float   wheelDelta = 0.0f;         // 格数

    // 键盘 / 字符
    int32_t  key = 0;                  // Win32 VK code
    uint32_t codepoint = 0;
    uint32_t modifiers = 0;            // EventModifier 位组合

    // DPI
    float dpiScale = 1.0f;

    bool isMouseEvent() const {
        return type == EventType::MouseMove || type == EventType::MouseLeave ||
               type == EventType::MouseDown || type == EventType::MouseUp ||
               type == EventType::MouseWheel;
    }
};

// ============================================================================
// EventQueue — 轻量线程安全队列
// ============================================================================

class EventQueue {
public:
    void push(const Event& ev) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(ev);
    }

    // 取出一个事件；队列为空返回 false
    bool poll(Event& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        out = queue_.front();
        queue_.pop_front();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::deque<Event> queue_;
};

} // namespace modern_ui
