// ============================================================================
// GlobalMouseHook.h — 全局低级鼠标 Hook（WH_MOUSE_LL）
// ============================================================================
//
// 设计目标：
//   - 专用线程安装 Hook 并跑消息循环，绝不占用主渲染线程；
//   - Hook 回调只做"读原子策略 + 写入无锁 SPSC 环形队列 + 立即返回"，
//     不做绘制、不加锁、不分配内存，避免 LowLevelHooksTimeout 静默卸载；
//   - 默认全部透传（CallNextHookEx），可按原子策略吞掉左键；
//   - 鼠标移动事件永远不吞，保证系统指针 / 下层应用不被阻塞。
//
// 与窗口消息输入的关系：
//   - 本类只负责"产生事件"，坐标是屏幕绝对坐标；
//   - App 负责换算成客户区坐标并派发给 View；
//   - 启用 Hook 时窗口应设置 WS_EX_TRANSPARENT（点击穿透），两者不重复派发。
// ============================================================================

#pragma once

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

#include "Canvas.h"

namespace modern_ui {

// 由 Hook 线程写入、主线程读取的轻量事件（屏幕绝对坐标）
struct HookMouseEvent {
    uint32_t msg = 0;          // WM_MOUSEMOVE / WM_LBUTTONDOWN / ...
    int32_t  x = 0;            // 屏幕坐标
    int32_t  y = 0;            // 屏幕坐标
    int32_t  wheelDelta = 0;   // 以"格"为单位
    uint32_t modifiers = 0;    // EventModifier 位组合
    uint64_t timestampMs = 0;
    bool     leftDown = false; // 移动事件时的左键状态
    bool     injected = false; // LLMHF_INJECTED
};

class GlobalMouseHook {
public:
    static constexpr size_t kQueueCapacity = 8192;

    GlobalMouseHook() = default;
    ~GlobalMouseHook();

    GlobalMouseHook(const GlobalMouseHook&) = delete;
    GlobalMouseHook& operator=(const GlobalMouseHook&) = delete;

    // 启动专用 Hook 线程；返回是否安装成功（内部有 2 秒超时）
    bool start();
    void stop();
    bool running() const { return running_.load(std::memory_order_acquire); }

    // 左键消费策略（可在主线程随时更新，Hook 回调只读原子值）：
    //   suppressAll = true  -> 吞掉所有左键 Down/Up（鼠标移动始终透传）
    //   suppressAll = false -> 仅当指针位于 suppressRect 内时吞掉左键
    // suppressRect 为屏幕绝对坐标。
    void setLeftButtonPolicy(bool suppressAll, const Rect& suppressRect);

    // 滚轮消费策略：指针位于 suppressRect（屏幕绝对坐标）内时吞掉滚轮，
    // 避免批注层内部滚动时把滚轮泄漏给下层应用。空矩形 = 不吞。
    void setWheelSuppressRect(const Rect& suppressRect);

    // 卡片区域（屏幕绝对坐标）：无论是否指针模式，落在这些矩形内的左键
    // 与滚轮都吞掉，保证批注层的卡片始终可以交互且滚动不泄漏。
    // 最多 kMaxCardRects 个矩形。
    static constexpr int32_t kMaxCardRects = 4;
    void setCardSuppressRects(const Rect* rects, int32_t count);

    // 主线程非阻塞读取；返回本次读出的事件数
    size_t poll(HookMouseEvent* out, size_t capacity);

    // 是否已安装但长时间没有事件？用于诊断 Hook 是否被系统静默移除。
    uint64_t lastEventTick() const {
        return lastEventTick_.load(std::memory_order_relaxed);
    }

private:
    static LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam);
    void threadMain();
    bool pushFromHook(const HookMouseEvent& event);
    bool pointInSuppressRect(int32_t x, int32_t y) const;
    bool pointInWheelRect(int32_t x, int32_t y) const;
    bool pointInCardRect(int32_t x, int32_t y) const;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<DWORD> threadId_{0};
    HANDLE readyEvent_ = nullptr;

    // Hook 线程私有状态
    HHOOK hook_ = nullptr;
    bool  leftDown_ = false;
    bool  swallowLeftDown_ = false;
    std::atomic<uint64_t> lastEventTick_{0};

    // Hook 回调 → 主线程 SPSC 队列
    std::array<HookMouseEvent, kQueueCapacity> queue_{};
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> tail_{0};

    // 消费策略（原子，Hook 回调可无锁读取）
    std::atomic<bool>    suppressAllLeft_{false};
    std::atomic<int32_t> suppressRectX_{0};
    std::atomic<int32_t> suppressRectY_{0};
    std::atomic<int32_t> suppressRectW_{0};
    std::atomic<int32_t> suppressRectH_{0};

    std::atomic<int32_t> wheelRectX_{0};
    std::atomic<int32_t> wheelRectY_{0};
    std::atomic<int32_t> wheelRectW_{0};
    std::atomic<int32_t> wheelRectH_{0};

    // 卡片矩形（最多 kMaxCardRects 个；count 的最后一次 store 带 release）
    std::atomic<int32_t> cardRectCount_{0};
    std::atomic<int32_t> cardRectX_[kMaxCardRects]{0, 0, 0, 0};
    std::atomic<int32_t> cardRectY_[kMaxCardRects]{0, 0, 0, 0};
    std::atomic<int32_t> cardRectW_[kMaxCardRects]{0, 0, 0, 0};
    std::atomic<int32_t> cardRectH_[kMaxCardRects]{0, 0, 0, 0};
};

} // namespace modern_ui
