// ============================================================================
// GlobalMouseHook.cpp
// ============================================================================

#include "GlobalMouseHook.h"

#include "Event.h"

#include <algorithm>
#include <cstddef>

namespace modern_ui {

namespace {

// 低层 Hook 在同一线程内回调，进程内只允许一个实例。
std::atomic<GlobalMouseHook*> g_hookInstance{nullptr};

uint32_t currentModifiers() {
    uint32_t mods = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000)   mods |= ModifierShift;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= ModifierControl;
    if (GetKeyState(VK_MENU) & 0x8000)    mods |= ModifierAlt;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000))
        mods |= ModifierSuper;
    return mods;
}

} // namespace

// ============================================================================
// 生命周期
// ============================================================================

GlobalMouseHook::~GlobalMouseHook() {
    stop();
}

bool GlobalMouseHook::start() {
    if (running_.load(std::memory_order_acquire)) return true;
    if (thread_.joinable()) {
        // 上一次启动失败：回收线程
        thread_.join();
    }

    stopRequested_.store(false, std::memory_order_relaxed);

    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!readyEvent_) return false;

    thread_ = std::thread(&GlobalMouseHook::threadMain, this);

    // 安装 Hook 本身是很快的；2 秒仅用于异常环境兜底。
    WaitForSingleObject(readyEvent_, 2000);

    CloseHandle(readyEvent_);
    readyEvent_ = nullptr;

    if (!running_.load(std::memory_order_acquire)) {
        if (thread_.joinable()) thread_.join();
        return false;
    }
    return true;
}

void GlobalMouseHook::stop() {
    if (!thread_.joinable()) {
        running_.store(false, std::memory_order_release);
        return;
    }

    stopRequested_.store(true, std::memory_order_release);

    // 线程已经创建过消息队列（threadMain 中 PeekMessage 保证）。
    const DWORD tid = threadId_.load(std::memory_order_acquire);
    if (tid != 0) PostThreadMessageW(tid, WM_QUIT, 0, 0);

    thread_.join();
    running_.store(false, std::memory_order_release);
}

// ============================================================================
// Hook 线程
// ============================================================================

void GlobalMouseHook::threadMain() {
    threadId_.store(GetCurrentThreadId(), std::memory_order_release);

    // 强制创建线程消息队列，确保 stop() 的 PostThreadMessage 有效。
    MSG probe{};
    PeekMessageW(&probe, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    GlobalMouseHook* expected = nullptr;
    if (!g_hookInstance.compare_exchange_strong(expected, this,
                                                std::memory_order_acq_rel)) {
        running_.store(false, std::memory_order_release);
        if (readyEvent_) SetEvent(readyEvent_);
        return;
    }

    hook_ = SetWindowsHookExW(WH_MOUSE_LL, &GlobalMouseHook::hookProc,
                              GetModuleHandleW(nullptr), 0);

    if (!hook_) {
        g_hookInstance.store(nullptr, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        if (readyEvent_) SetEvent(readyEvent_);
        return;
    }

    running_.store(true, std::memory_order_release);
    if (readyEvent_) SetEvent(readyEvent_);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(hook_);
    hook_ = nullptr;
    g_hookInstance.store(nullptr, std::memory_order_release);
    running_.store(false, std::memory_order_release);
}

// ============================================================================
// 消费策略
// ============================================================================

void GlobalMouseHook::setLeftButtonPolicy(bool suppressAll, const Rect& suppressRect) {
    suppressRectX_.store(suppressRect.x, std::memory_order_relaxed);
    suppressRectY_.store(suppressRect.y, std::memory_order_relaxed);
    suppressRectW_.store(std::max(0, suppressRect.w), std::memory_order_relaxed);
    suppressRectH_.store(std::max(0, suppressRect.h), std::memory_order_relaxed);
    // release：保证 Hook 线程看到上面的矩形后再看到 suppressAll。
    suppressAllLeft_.store(suppressAll, std::memory_order_release);
}

bool GlobalMouseHook::pointInSuppressRect(int32_t x, int32_t y) const {
    const int32_t rx = suppressRectX_.load(std::memory_order_relaxed);
    const int32_t ry = suppressRectY_.load(std::memory_order_relaxed);
    const int32_t rw = suppressRectW_.load(std::memory_order_relaxed);
    const int32_t rh = suppressRectH_.load(std::memory_order_relaxed);
    if (rw <= 0 || rh <= 0) return false;
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void GlobalMouseHook::setWheelSuppressRect(const Rect& suppressRect) {
    wheelRectX_.store(suppressRect.x, std::memory_order_relaxed);
    wheelRectY_.store(suppressRect.y, std::memory_order_relaxed);
    wheelRectW_.store(std::max(0, suppressRect.w), std::memory_order_relaxed);
    wheelRectH_.store(std::max(0, suppressRect.h), std::memory_order_release);
}

bool GlobalMouseHook::pointInWheelRect(int32_t x, int32_t y) const {
    const int32_t rx = wheelRectX_.load(std::memory_order_relaxed);
    const int32_t ry = wheelRectY_.load(std::memory_order_relaxed);
    const int32_t rw = wheelRectW_.load(std::memory_order_relaxed);
    const int32_t rh = wheelRectH_.load(std::memory_order_acquire);
    if (rw <= 0 || rh <= 0) return false;
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void GlobalMouseHook::setCardSuppressRects(const Rect* rects, int32_t count) {
    const int32_t clamped =
        (rects == nullptr) ? 0 : std::max(0, std::min(count, kMaxCardRects));
    for (int32_t i = 0; i < clamped; ++i) {
        cardRectX_[i].store(rects[i].x, std::memory_order_relaxed);
        cardRectY_[i].store(rects[i].y, std::memory_order_relaxed);
        cardRectW_[i].store(std::max(0, rects[i].w), std::memory_order_relaxed);
        cardRectH_[i].store(std::max(0, rects[i].h), std::memory_order_relaxed);
    }
    // release：保证 Hook 线程看到矩形数据后再看到新的数量。
    cardRectCount_.store(clamped, std::memory_order_release);
}

bool GlobalMouseHook::pointInCardRect(int32_t x, int32_t y) const {
    const int32_t count = cardRectCount_.load(std::memory_order_acquire);
    for (int32_t i = 0; i < count && i < kMaxCardRects; ++i) {
        const int32_t rx = cardRectX_[i].load(std::memory_order_relaxed);
        const int32_t ry = cardRectY_[i].load(std::memory_order_relaxed);
        const int32_t rw = cardRectW_[i].load(std::memory_order_relaxed);
        const int32_t rh = cardRectH_[i].load(std::memory_order_relaxed);
        if (rw <= 0 || rh <= 0) continue;
        if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) return true;
    }
    return false;
}

// ============================================================================
// Hook 回调（只做原子读 + 入队，禁止阻塞）
// ============================================================================

LRESULT CALLBACK GlobalMouseHook::hookProc(int code, WPARAM wParam, LPARAM lParam) {
    GlobalMouseHook* self = g_hookInstance.load(std::memory_order_acquire);
    if (code < 0 || !self) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    const MSLLHOOKSTRUCT* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
    if (!info) return CallNextHookEx(nullptr, code, wParam, lParam);

    HookMouseEvent event;
    event.msg = static_cast<uint32_t>(wParam);
    event.x = info->pt.x;
    event.y = info->pt.y;
    event.timestampMs = GetTickCount64();
    event.modifiers = currentModifiers();
    event.injected = (info->flags & LLMHF_INJECTED) != 0;

    bool swallow = false;

    switch (wParam) {
        case WM_MOUSEMOVE:
            // 移动事件永不吞：这是保证系统指针与下层应用不被阻塞的关键。
            event.leftDown = self->leftDown_;
            break;

        case WM_LBUTTONDOWN:
            self->leftDown_ = true;
            swallow = self->suppressAllLeft_.load(std::memory_order_acquire) ||
                      self->pointInSuppressRect(info->pt.x, info->pt.y) ||
                      self->pointInCardRect(info->pt.x, info->pt.y);
            if (swallow) self->swallowLeftDown_ = true;
            break;

        case WM_LBUTTONUP:
            self->leftDown_ = false;
            // 只吞掉与已吞掉的按下配对的抬起，避免产生"孤立抬起"。
            swallow = self->swallowLeftDown_;
            self->swallowLeftDown_ = false;
            break;

        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            break;

        case WM_MOUSEWHEEL:
            event.wheelDelta = static_cast<int32_t>(
                static_cast<int16_t>(HIWORD(info->mouseData)) / WHEEL_DELTA);
            // 批注层卡片内部滚动：仅在鼠标精确位于某张卡片内时吞掉滚轮，
            // 避免卡片之间的空隙也拦截下层应用的滚动。
            swallow = self->pointInWheelRect(info->pt.x, info->pt.y) ||
                      self->pointInCardRect(info->pt.x, info->pt.y);
            break;

        default:
            return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    self->pushFromHook(event);
    self->lastEventTick_.store(event.timestampMs, std::memory_order_relaxed);

    if (swallow) return 1;
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// ============================================================================
// 无锁 SPSC 环形队列
// ============================================================================

bool GlobalMouseHook::pushFromHook(const HookMouseEvent& event) {
    const uint32_t head = head_.load(std::memory_order_relaxed);
    const uint32_t next = (head + 1) % static_cast<uint32_t>(kQueueCapacity);
    if (next == tail_.load(std::memory_order_acquire)) {
        // 队列满：丢弃新事件而不是阻塞 Hook。
        return false;
    }
    queue_[head] = event;
    head_.store(next, std::memory_order_release);
    return true;
}

size_t GlobalMouseHook::poll(HookMouseEvent* out, size_t capacity) {
    if (!out || capacity == 0) return 0;

    uint32_t tail = tail_.load(std::memory_order_relaxed);
    const uint32_t head = head_.load(std::memory_order_acquire);
    const uint32_t cap = static_cast<uint32_t>(kQueueCapacity);

    size_t count = 0;
    while (tail != head && count < capacity) {
        out[count++] = queue_[tail];
        tail = (tail + 1) % cap;
    }
    tail_.store(tail, std::memory_order_release);
    return count;
}

} // namespace modern_ui
