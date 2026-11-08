// ============================================================================
// Win32LayeredWindow.cpp
// ============================================================================

#include "Win32LayeredWindow.h"

#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

namespace modern_ui {

namespace {

constexpr wchar_t kWindowClassName[] = L"ModernUI.Win32LayeredWindow";

void ensureDpiAwareness() {
    static std::once_flag once;
    std::call_once(once, [] {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (!user32) return;

        // GetProcAddress 返回 FARPROC，转成具体函数指针是 Win32 的常规做法；
        // GCC 的 -Wcast-function-type 会对此告警，这里局部抑制。
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        using SetDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
        auto setContext = reinterpret_cast<SetDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
            if (setContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) return;
        }

        using SetDpiAwareFn = BOOL(WINAPI*)();
        auto setAware = reinterpret_cast<SetDpiAwareFn>(
            GetProcAddress(user32, "SetProcessDPIAware"));
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
        if (setAware) setAware();
    });
}

bool ensureWindowClass() {
    static bool registered = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &Win32LayeredWindow::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kWindowClassName;
        if (RegisterClassExW(&wc)) return true;
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    return registered;
}

std::string formatWin32Error(const char* what, DWORD code) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s (Win32 error %lu)", what,
                  static_cast<unsigned long>(code));
    return std::string(buffer);
}

uint32_t currentModifiers() {
    uint32_t mods = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000)   mods |= ModifierShift;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= ModifierControl;
    if (GetKeyState(VK_MENU) & 0x8000)    mods |= ModifierAlt;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000))
        mods |= ModifierSuper;
    return mods;
}

uint8_t buttonFromMessage(UINT msg) {
    switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            return MouseButtonLeft;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            return MouseButtonRight;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            return MouseButtonMiddle;
        default:
            return MouseButtonNone;
    }
}

} // namespace

void enableDpiAwareness() {
    ensureDpiAwareness();
}

// ============================================================================
// 构造 / 析构
// ============================================================================

Win32LayeredWindow::Win32LayeredWindow(EventQueue& events)
    : events_(events) {}

Win32LayeredWindow::~Win32LayeredWindow() {
    destroy();
}

// ============================================================================
// 生命周期
// ============================================================================

bool Win32LayeredWindow::create(const Options& options, std::string* error) {
    if (hwnd_) destroy();

    quit_ = false;
    resizable_ = options.resizable;
    draggable_ = options.draggable;
    clickThrough_ = options.clickThrough;
    pushMouseEvents_ = options.pushMouseEvents;

    const int32_t w = std::max<int32_t>(1, options.width);
    const int32_t h = std::max<int32_t>(1, options.height);

    ensureDpiAwareness();
    if (!ensureWindowClass()) {
        if (error) *error = formatWin32Error("RegisterClassExW failed", GetLastError());
        return false;
    }

    int32_t x = options.x;
    int32_t y = options.y;
    if (x == INT32_MIN || y == INT32_MIN) {
        RECT work{};
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
            x = work.left + ((work.right - work.left) - w) / 2;
            y = work.top + ((work.bottom - work.top) - h) / 2;
        } else {
            x = 100;
            y = 100;
        }
    }

    DWORD exStyle = WS_EX_LAYERED;
    if (options.topmost) exStyle |= WS_EX_TOPMOST;
    if (options.clickThrough) exStyle |= WS_EX_TRANSPARENT;
    if (options.noActivate) exStyle |= WS_EX_NOACTIVATE;
    if (options.toolWindow) exStyle |= WS_EX_TOOLWINDOW;

    hwnd_ = CreateWindowExW(
        exStyle,
        kWindowClassName,
        options.title.c_str(),
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);

    if (!hwnd_) {
        if (error) *error = formatWin32Error("CreateWindowExW failed", GetLastError());
        return false;
    }

    if (!createSurface(hwnd_, w, h)) {
        if (error) *error = "Failed to create layered window surface (DIB section).";
        destroy();
        return false;
    }

    // 触控：注册后触摸不再被系统合成为鼠标消息，而是走 WM_TOUCH。
    // 忽略失败（没有触控硬件的机器上是正常的）。
    RegisterTouchWindow(hwnd_, 0);

    if (options.visible) {
        show();
    } else {
        present();
    }
    return true;
}

void Win32LayeredWindow::destroy() {
    if (hwnd_) {
        HWND hwnd = hwnd_;
        UnregisterTouchWindow(hwnd);
        DestroyWindow(hwnd);
        hwnd_ = nullptr;
    }
    destroySurface();
    quit_ = true;
}

void Win32LayeredWindow::show() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, SW_SHOW);
    present();
}

void Win32LayeredWindow::hide() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, SW_HIDE);
}

void Win32LayeredWindow::setTitle(const std::wstring& title) {
    if (hwnd_) SetWindowTextW(hwnd_, title.c_str());
}

bool Win32LayeredWindow::setClientSize(int32_t width, int32_t height) {
    if (!hwnd_ || width <= 0 || height <= 0) return false;
    return SetWindowPos(hwnd_, nullptr, 0, 0, width, height,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
}

bool Win32LayeredWindow::setClickThrough(bool clickThrough) {
    clickThrough_ = clickThrough;
    if (!hwnd_) return true;

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (clickThrough) {
        exStyle |= WS_EX_TRANSPARENT;
    } else {
        exStyle &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    }
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
    // 样式变化后让系统重新评估命中测试 / 重绘区域。
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                 SWP_FRAMECHANGED);
    return true;
}

bool Win32LayeredWindow::registerHotkey(int id, uint32_t modifiers, uint32_t virtualKey) {
    if (!hwnd_) return false;

    UINT mods = MOD_NOREPEAT;
    if (modifiers & ModifierShift)   mods |= MOD_SHIFT;
    if (modifiers & ModifierControl) mods |= MOD_CONTROL;
    if (modifiers & ModifierAlt)     mods |= MOD_ALT;
    if (modifiers & ModifierSuper)   mods |= MOD_WIN;

    return RegisterHotKey(hwnd_, id, mods, virtualKey) != FALSE;
}

bool Win32LayeredWindow::unregisterHotkey(int id) {
    if (!hwnd_) return false;
    return UnregisterHotKey(hwnd_, id) != FALSE;
}

Vec2i Win32LayeredWindow::clientOrigin() const {
    if (!hwnd_) return Vec2i{0, 0};
    RECT rect{};
    if (!GetWindowRect(hwnd_, &rect)) return Vec2i{0, 0};
    return Vec2i{rect.left, rect.top};
}

// ============================================================================
// Surface
// ============================================================================

bool Win32LayeredWindow::createSurface(HWND hwnd, int32_t width, int32_t height) {
    if (!hwnd || width <= 0 || height <= 0) return false;
    if (bits_ && width == width_ && height == height_) return true;

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;    // top-down：与 BLImage 行序一致
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        DeleteDC(dc);
        return false;
    }

    HGDIOBJ selected = SelectObject(dc, bitmap);

    // 拖动调整大小时保留旧内容，避免新 DIB 尚未绘制就把窗口刷成空白。
    if (memDC_ && bitmap_) {
        const int32_t copyW = std::min(width, width_);
        const int32_t copyH = std::min(height, height_);
        if (copyW > 0 && copyH > 0) {
            BitBlt(dc, 0, 0, copyW, copyH, memDC_, 0, 0, SRCCOPY);
        }
    }

    // 释放旧 surface（先把旧 DC 恢复到原始选中对象）
    if (memDC_) {
        if (oldBitmap_) SelectObject(memDC_, oldBitmap_);
        DeleteDC(memDC_);
    }
    if (bitmap_) DeleteObject(bitmap_);

    memDC_ = dc;
    bitmap_ = bitmap;
    oldBitmap_ = selected;
    bits_ = bits;
    width_ = width;
    height_ = height;
    stride_ = width * 4;
    ++surfaceSerial_;

    // 新 DIB 未绘制区域为全透明；提交一次同步尺寸/位置
    present();
    return true;
}

void Win32LayeredWindow::destroySurface() {
    if (memDC_) {
        if (oldBitmap_) SelectObject(memDC_, oldBitmap_);
        oldBitmap_ = nullptr;
        DeleteDC(memDC_);
        memDC_ = nullptr;
    }
    if (bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    bits_ = nullptr;
    width_ = 0;
    height_ = 0;
    stride_ = 0;
}

void Win32LayeredWindow::present(const Rect* dirtyRect) {
    if (!hwnd_ || !memDC_ || !bits_ || width_ <= 0 || height_ <= 0) return;

    HDC screen = GetDC(nullptr);
    if (!screen) return;

    POINT srcPoint{0, 0};
    SIZE  size{width_, height_};

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;   // 逐像素 alpha
    blend.AlphaFormat = AC_SRC_ALPHA;

    RECT dirty{};
    bool hasDirty = false;
    if (dirtyRect && dirtyRect->w > 0 && dirtyRect->h > 0) {
        dirty.left = dirtyRect->x;
        dirty.top = dirtyRect->y;
        dirty.right = dirtyRect->right();
        dirty.bottom = dirtyRect->bottom();
        // 夹到窗口范围内
        dirty.left = std::max<LONG>(0, dirty.left);
        dirty.top = std::max<LONG>(0, dirty.top);
        dirty.right = std::min<LONG>(width_, dirty.right);
        dirty.bottom = std::min<LONG>(height_, dirty.bottom);
        hasDirty = dirty.right > dirty.left && dirty.bottom > dirty.top;
    }

    UPDATELAYEREDWINDOWINFO info{};
    info.cbSize = sizeof(info);
    info.hdcDst = screen;
    info.pptDst = nullptr;             // 位置不变
    info.psize = &size;
    info.hdcSrc = memDC_;
    info.pptSrc = &srcPoint;
    info.crKey = 0;
    info.pblend = &blend;
    info.dwFlags = ULW_ALPHA;
    info.prcDirty = hasDirty ? &dirty : nullptr;

    UpdateLayeredWindowIndirect(hwnd_, &info);
    ReleaseDC(nullptr, screen);
}

// ============================================================================
// 消息循环
// ============================================================================

bool Win32LayeredWindow::pumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            quit_ = true;
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return !quit_;
}

bool Win32LayeredWindow::waitMessages(uint32_t timeoutMs) {
    if (quit_) return false;
    MsgWaitForMultipleObjectsEx(0, nullptr, timeoutMs, QS_ALLINPUT, 0);
    return pumpMessages();
}

void Win32LayeredWindow::requestQuit() {
    quit_ = true;
    PostQuitMessage(0);
}

void Win32LayeredWindow::pushEvent(Event ev) {
    if (ev.timestampMs == 0) ev.timestampMs = GetTickCount64();
    events_.push(ev);
}

// ============================================================================
// WndProc
// ============================================================================

LRESULT CALLBACK Win32LayeredWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Win32LayeredWindow* self = reinterpret_cast<Win32LayeredWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Win32LayeredWindow*>(cs->lpCreateParams);
        if (self) {
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    }

    if (self) return self->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Win32LayeredWindow::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (!resizable_ && !draggable_) return hit;

        RECT wr{};
        GetWindowRect(hwnd, &wr);
        const int32_t cx = GET_X_LPARAM(lParam);
        const int32_t cy = GET_Y_LPARAM(lParam);
        const int32_t border = 6;

        const bool onLeft   = cx >= wr.left && cx < wr.left + border;
        const bool onRight  = cx < wr.right && cx >= wr.right - border;
        const bool onTop    = cy >= wr.top && cy < wr.top + border;
        const bool onBottom = cy < wr.bottom && cy >= wr.bottom - border;

        if (resizable_) {
            if (onTop && onLeft)     return HTTOPLEFT;
            if (onTop && onRight)    return HTTOPRIGHT;
            if (onBottom && onLeft)  return HTBOTTOMLEFT;
            if (onBottom && onRight) return HTBOTTOMRIGHT;
            if (onLeft)   return HTLEFT;
            if (onRight)  return HTRIGHT;
            if (onTop)    return HTTOP;
            if (onBottom) return HTBOTTOM;
        }

        if (draggable_ && cy >= wr.top && cy < wr.top + 40) return HTCAPTION;
        return hit;
    }

    case WM_SIZE: {
        const int32_t w = LOWORD(lParam);
        const int32_t h = HIWORD(lParam);
        if (w > 0 && h > 0 && (w != width_ || h != height_)) {
            if (createSurface(hwnd, w, h)) {
                Event ev;
                ev.type = EventType::Resize;
                ev.width = w;
                ev.height = h;
                pushEvent(ev);
            }
        }
        return 0;
    }

    case WM_MOVE: {
        Event ev;
        ev.type = EventType::Move;
        ev.x = GET_X_LPARAM(lParam);
        ev.y = GET_Y_LPARAM(lParam);
        pushEvent(ev);
        return 0;
    }

    case WM_CLOSE:
        // 由应用决定是否真正关闭（先发事件，不自动 DestroyWindow）
        {
            Event ev;
            ev.type = EventType::Close;
            pushEvent(ev);
        }
        return 0;

    case WM_DESTROY:
        hwnd_ = nullptr;
        quit_ = true;
        {
            Event ev;
            ev.type = EventType::Quit;
            pushEvent(ev);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    // 触控输入：转换为 MouseDown / Move / Up 事件，并携带 pointerId，
    // 供应用实现单指绘制、双指缩放平移等手势。
    case WM_TOUCH: {
        const UINT count = LOWORD(wParam);
        if (count == 0) break;

        std::vector<TOUCHINPUT> inputs(count);
        if (!GetTouchInputInfo(reinterpret_cast<HTOUCHINPUT>(lParam), count,
                               inputs.data(), sizeof(TOUCHINPUT))) {
            break;
        }

        for (const TOUCHINPUT& input : inputs) {
            // 系统未过滤的手掌接触直接丢弃，避免误画 / 误触按钮。
            if (input.dwFlags & TOUCHEVENTF_PALM) continue;

            const DWORD flags = input.dwFlags;
            if (!(flags & (TOUCHEVENTF_DOWN | TOUCHEVENTF_UP |
                           TOUCHEVENTF_MOVE))) {
                continue;
            }

            const LONG screenX = TOUCH_COORD_TO_PIXEL(input.x);
            const LONG screenY = TOUCH_COORD_TO_PIXEL(input.y);
            POINT pt{static_cast<LONG>(screenX), static_cast<LONG>(screenY)};
            ScreenToClient(hwnd, &pt);

            Event ev;
            ev.source = InputSource::Window;
            ev.pointerType = (flags & TOUCHEVENTF_PEN) ? PointerType::Pen
                                                       : PointerType::Touch;
            ev.pointerId = static_cast<int32_t>(input.dwID);
            ev.primaryTouch = (flags & TOUCHEVENTF_PRIMARY) != 0;
            ev.palmTouch = (flags & TOUCHEVENTF_PALM) != 0;
            ev.timestampMs = static_cast<uint64_t>(input.dwTime);
            ev.x = static_cast<int32_t>(pt.x);
            ev.y = static_cast<int32_t>(pt.y);
            ev.screenX = static_cast<int32_t>(screenX);
            ev.screenY = static_cast<int32_t>(screenY);
            ev.pressure = 1.0f;
            ev.modifiers = currentModifiers();

            if (flags & TOUCHEVENTF_DOWN) {
                ev.type = EventType::MouseDown;
                ev.button = MouseButtonLeft;
            } else if (flags & TOUCHEVENTF_UP) {
                ev.type = EventType::MouseUp;
                ev.button = MouseButtonLeft;
            } else {
                ev.type = EventType::MouseMove;
                ev.button = MouseButtonLeft;
            }

            pushEvent(ev);
        }

        CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lParam));
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!pushMouseEvents_) return 0;
        if (!trackingLeave_) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            if (TrackMouseEvent(&tme)) trackingLeave_ = true;
        }
        Event ev;
        ev.type = EventType::MouseMove;
        ev.x = GET_X_LPARAM(lParam);
        ev.y = GET_Y_LPARAM(lParam);
        POINT screenPt{ev.x, ev.y};
        ClientToScreen(hwnd, &screenPt);
        ev.screenX = screenPt.x;
        ev.screenY = screenPt.y;
        ev.modifiers = currentModifiers();
        pushEvent(ev);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (!pushMouseEvents_) return 0;
        trackingLeave_ = false;
        {
            Event ev;
            ev.type = EventType::MouseLeave;
            pushEvent(ev);
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        if (!pushMouseEvents_) return 0;
        SetCapture(hwnd);
        Event ev;
        ev.type = EventType::MouseDown;
        ev.x = GET_X_LPARAM(lParam);
        ev.y = GET_Y_LPARAM(lParam);
        POINT screenPt{ev.x, ev.y};
        ClientToScreen(hwnd, &screenPt);
        ev.screenX = screenPt.x;
        ev.screenY = screenPt.y;
        ev.button = buttonFromMessage(msg);
        ev.modifiers = currentModifiers();
        pushEvent(ev);
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        if (!pushMouseEvents_) return 0;
        if (GetCapture() == hwnd) ReleaseCapture();
        Event ev;
        ev.type = EventType::MouseUp;
        ev.x = GET_X_LPARAM(lParam);
        ev.y = GET_Y_LPARAM(lParam);
        POINT screenPt{ev.x, ev.y};
        ClientToScreen(hwnd, &screenPt);
        ev.screenX = screenPt.x;
        ev.screenY = screenPt.y;
        ev.button = buttonFromMessage(msg);
        ev.modifiers = currentModifiers();
        pushEvent(ev);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (!pushMouseEvents_) return 0;
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        Event ev;
        ev.type = EventType::MouseWheel;
        ev.x = pt.x;
        ev.y = pt.y;
        ev.screenX = GET_X_LPARAM(lParam);
        ev.screenY = GET_Y_LPARAM(lParam);
        ev.wheelDelta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
        ev.modifiers = currentModifiers();
        pushEvent(ev);
        return 0;
    }

    case WM_HOTKEY: {
        Event ev;
        ev.type = EventType::Hotkey;
        ev.hotkeyId = static_cast<int32_t>(wParam);
        ev.key = static_cast<int32_t>(HIWORD(lParam));
        pushEvent(ev);
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        Event ev;
        ev.type = EventType::KeyDown;
        ev.key = static_cast<int32_t>(wParam);
        ev.modifiers = currentModifiers();
        pushEvent(ev);
        return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP: {
        Event ev;
        ev.type = EventType::KeyUp;
        ev.key = static_cast<int32_t>(wParam);
        ev.modifiers = currentModifiers();
        pushEvent(ev);
        return 0;
    }

    case WM_CHAR: {
        Event ev;
        ev.type = EventType::Char;
        ev.codepoint = static_cast<uint32_t>(wParam);
        ev.modifiers = currentModifiers();
        pushEvent(ev);
        return 0;
    }

    case WM_SETFOCUS: {
        Event ev;
        ev.type = EventType::FocusIn;
        pushEvent(ev);
        return 0;
    }

    case WM_KILLFOCUS: {
        Event ev;
        ev.type = EventType::FocusOut;
        pushEvent(ev);
        return 0;
    }

    case WM_DPICHANGED: {
        auto suggested = reinterpret_cast<RECT*>(lParam);
        if (suggested) {
            SetWindowPos(hwnd, nullptr,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        Event ev;
        ev.type = EventType::DpiChanged;
        ev.dpiScale = static_cast<float>(LOWORD(wParam)) / 96.0f;
        pushEvent(ev);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        if (mmi) {
            mmi->ptMinTrackSize.x = 160;
            mmi->ptMinTrackSize.y = 120;
        }
        return 0;
    }

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace modern_ui
