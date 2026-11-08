// ============================================================================
// App.cpp — 应用门面实现
// ============================================================================

#include "App.h"

#include "GlobalMouseHook.h"

#include <algorithm>
#include <cstdio>

namespace modern_ui {

App::App(std::wstring title, int32_t width, int32_t height)
    : window_(events_),
      renderer_(&window_),
      view_(new View(width, height)),
      title_(std::move(title)),
      width_(width),
      height_(height) {}

App::~App() {
    if (mouseHook_) mouseHook_->stop();
    window_.destroy();
}

// ============================================================================
// 链式配置
// ============================================================================

App& App::resizable(bool value) { resizable_ = value; return *this; }
App& App::topmost(bool value) { topmost_ = value; return *this; }
App& App::draggable(bool value) { draggable_ = value; return *this; }

App& App::position(int32_t x, int32_t y) {
    x_ = x;
    y_ = y;
    return *this;
}

App& App::background(Color color) {
    background_ = color;
    return *this;
}

App& App::escapeQuits(bool value) { escapeQuits_ = value; return *this; }

App& App::virtualScreenFullscreen(bool value) {
    virtualScreenFullscreen_ = value;
    return *this;
}

App& App::clickThrough(bool value) {
    clickThrough_ = value;
    return *this;
}

App& App::noActivate(bool value) {
    noActivate_ = value;
    return *this;
}

App& App::toolWindow(bool value) {
    toolWindow_ = value;
    return *this;
}

App& App::mouseInputMode(MouseInputMode mode) {
    mouseInputMode_ = mode;
    return *this;
}

App& App::registerHotkey(int id, uint32_t modifiers, uint32_t virtualKey) {
    hotkeys_.push_back(HotkeySpec{id, modifiers, virtualKey});
    if (running_ && window_.valid()) {
        window_.registerHotkey(id, modifiers, virtualKey);
    }
    return *this;
}

App& App::font(const std::string& path) {
    fontPath_ = path;
    return *this;
}

App& App::styleSheetFile(const std::string& path) {
    styleFile_ = path;
    return *this;
}

App& App::styleSheetText(const std::string& text) {
    styleText_ = text;
    return *this;
}

App& App::content(ContentBuilder builder) {
    content_ = std::move(builder);
    return *this;
}

App& App::viewFactory(ViewFactory factory) {
    viewFactory_ = std::move(factory);
    return *this;
}

App& App::onEvent(EventHook hook) {
    eventHook_ = std::move(hook);
    return *this;
}

App& App::onTick(TickHook hook) {
    tickHook_ = std::move(hook);
    return *this;
}

App& App::frameInterval(uint32_t milliseconds) {
    frameIntervalMs_ = milliseconds;
    return *this;
}

// ============================================================================
// 运行
// ============================================================================

int App::run() {
    if (running_) return 0;

    // 必须在读取虚拟屏幕尺寸 / 启动 Hook 之前启用 DPI 感知。
    enableDpiAwareness();

    // --- 输入源：先启动全局 Hook（不依赖窗口），失败则回退到窗口消息 ---
    bool hookActive = false;
    if (mouseInputMode_ == MouseInputMode::Hook) {
        mouseHook_.reset(new GlobalMouseHook());
        hookActive = mouseHook_->start();
        if (!hookActive) {
            std::fprintf(stderr,
                         "[ModernUI] global mouse hook failed; falling back to window messages\n");
            mouseHook_.reset();
        }
    }

    std::string error;
    Win32LayeredWindow::Options options;
    options.title = title_;
    options.width = width_;
    options.height = height_;
    options.x = x_;
    options.y = y_;

    if (virtualScreenFullscreen_) {
        const int32_t vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int32_t vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int32_t vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int32_t vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (vw > 0 && vh > 0) {
            options.x = vx;
            options.y = vy;
            options.width = vw;
            options.height = vh;
        }
    }

    options.resizable = resizable_;
    options.topmost = topmost_;
    options.draggable = draggable_;
    options.visible = true;
    // 没有 Hook 时不能点击穿透，否则窗口会完全收不到鼠标输入。
    options.clickThrough = clickThrough_ && hookActive;
    options.noActivate = noActivate_;
    options.toolWindow = toolWindow_;
    options.pushMouseEvents = !hookActive;

    if (!window_.create(options, &error)) {
        std::fprintf(stderr, "[ModernUI] window: %s\n", error.c_str());
        if (mouseHook_) mouseHook_->stop();
        return 1;
    }

    clientOrigin_ = window_.clientOrigin();

    for (const HotkeySpec& hotkey : hotkeys_) {
        if (!window_.registerHotkey(hotkey.id, hotkey.modifiers, hotkey.virtualKey)) {
            std::fprintf(stderr, "[ModernUI] hotkey %d registration failed\n", hotkey.id);
        }
    }

    // 视图尺寸与窗口客户区对齐；自定义工厂可以换成派生 View
    if (viewFactory_) {
        view_ = viewFactory_(window_.width(), window_.height());
    }
    if (!view_) view_.reset(new View(window_.width(), window_.height()));
    view_->resize(window_.width(), window_.height());
    view_->canvas().setBackgroundColor(background_);

    // 渲染后端 + 文本度量（控件子 Canvas 不接触后端，由 View 统一下发）
    const bool fontOk = fontPath_.empty()
        ? renderer_.loadDefaultFont(&error)
        : renderer_.loadFontFile(fontPath_, 0, &error);
    if (!fontOk) {
        std::fprintf(stderr, "[ModernUI] font warning: %s\n", error.c_str());
    }
    view_->setRenderer(&renderer_);
    view_->setTextMetricProvider([this](const TextShape& text) {
        return renderer_.measureText(text).value_or(Rect::null());
    });

    if (!styleFile_.empty()) {
        if (!view_->loadStyleSheetFile(styleFile_, &error)) {
            std::fprintf(stderr, "[ModernUI] stylesheet: %s\n", error.c_str());
        }
    }
    if (!styleText_.empty()) {
        if (!view_->loadStyleSheetText(styleText_, &error)) {
            std::fprintf(stderr, "[ModernUI] stylesheet: %s\n", error.c_str());
        }
    }

    if (content_) content_(*view_);
    view_->layoutNow();

    running_ = true;
    quitRequested_ = false;

    LARGE_INTEGER frequency{}, previous{}, nowCounter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);

    while (!quitRequested_) {
        if (!window_.pumpMessages()) break;

        Event ev;
        while (events_.poll(ev)) {
            dispatchEvent(ev, quitRequested_);
            if (quitRequested_) break;
        }
        if (quitRequested_) break;

        pollHookEvents();
        if (quitRequested_) break;

        QueryPerformanceCounter(&nowCounter);
        float dt = static_cast<float>(
            static_cast<double>(nowCounter.QuadPart - previous.QuadPart) /
            static_cast<double>(frequency.QuadPart));
        previous = nowCounter;
        dt = std::min(dt, 0.05f);

        if (tickHook_) tickHook_(*view_, dt);
        const bool changed = view_->tick(dt);
        if (view_->needsDraw() || changed) view_->drawFrame();

        if (!window_.waitMessages(frameIntervalMs_)) break;
    }

    for (const HotkeySpec& hotkey : hotkeys_) window_.unregisterHotkey(hotkey.id);
    running_ = false;
    if (mouseHook_) {
        mouseHook_->stop();
        mouseHook_.reset();
    }
    window_.destroy();
    return 0;
}

// ============================================================================
// 事件派发
// ============================================================================

void App::dispatchEvent(const Event& ev, bool& quitRequested) {
    bool handled = false;

    switch (ev.type) {
    case EventType::Close:
    case EventType::Quit:
        quitRequested = true;
        handled = true;
        break;

    case EventType::KeyDown:
        if (escapeQuits_ && ev.key == VK_ESCAPE) {
            quitRequested = true;
            handled = true;
        }
        break;

    case EventType::Resize:
        view_->resize(std::max(1, ev.width), std::max(1, ev.height));
        handled = true;
        break;

    default:
        break;
    }

    if (quitRequested) return;

    if (!handled && eventHook_ && eventHook_(ev)) handled = true;
    if (!handled) view_->handleEvent(ev);
}

// 把 Hook 队列里的屏幕坐标事件换算成客户区坐标并派发。
bool App::pollHookEvents() {
    if (!mouseHook_) return true;

    HookMouseEvent batch[256];
    clientOrigin_ = window_.valid() ? window_.clientOrigin() : Vec2i{0, 0};

    size_t count = 0;
    do {
        count = mouseHook_->poll(batch, sizeof(batch) / sizeof(batch[0]));
        for (size_t i = 0; i < count; ++i) {
            const HookMouseEvent& source = batch[i];

            Event ev;
            ev.source = InputSource::Hook;
            ev.timestampMs = source.timestampMs;
            ev.modifiers = source.modifiers;
            ev.injected = source.injected;
            ev.screenX = source.x;
            ev.screenY = source.y;
            ev.x = source.x - clientOrigin_.x;
            ev.y = source.y - clientOrigin_.y;

            switch (source.msg) {
            case WM_MOUSEMOVE:
                ev.type = EventType::MouseMove;
                break;
            case WM_LBUTTONDOWN:
                ev.type = EventType::MouseDown;
                ev.button = MouseButtonLeft;
                break;
            case WM_LBUTTONUP:
                ev.type = EventType::MouseUp;
                ev.button = MouseButtonLeft;
                break;
            case WM_RBUTTONDOWN:
                ev.type = EventType::MouseDown;
                ev.button = MouseButtonRight;
                break;
            case WM_RBUTTONUP:
                ev.type = EventType::MouseUp;
                ev.button = MouseButtonRight;
                break;
            case WM_MBUTTONDOWN:
                ev.type = EventType::MouseDown;
                ev.button = MouseButtonMiddle;
                break;
            case WM_MBUTTONUP:
                ev.type = EventType::MouseUp;
                ev.button = MouseButtonMiddle;
                break;
            case WM_MOUSEWHEEL:
                ev.type = EventType::MouseWheel;
                ev.wheelDelta = static_cast<float>(source.wheelDelta);
                break;
            default:
                continue;
            }

            dispatchEvent(ev, quitRequested_);
            if (quitRequested_) return false;
        }
    } while (count == sizeof(batch) / sizeof(batch[0]));

    return true;
}

} // namespace modern_ui
