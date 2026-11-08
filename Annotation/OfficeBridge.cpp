// ============================================================================
// OfficeBridge.cpp
// ============================================================================

#include "OfficeBridge.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <objbase.h>
#include <oleauto.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace annotation {

namespace {

constexpr uint32_t kPollIntervalMs = 350;

// ----------------------------------------------------------------------------
// COM 智能指针（MinGW 无 ATL，手工管理引用）
// ----------------------------------------------------------------------------
template <typename T>
class ComRef {
public:
    ComRef() = default;
    ComRef(T* pointer) : pointer_(pointer) {}
    ~ComRef() { reset(); }

    ComRef(const ComRef&) = delete;
    ComRef& operator=(const ComRef&) = delete;

    T* get() const { return pointer_; }
    explicit operator bool() const { return pointer_ != nullptr; }

    void reset(T* pointer = nullptr) {
        if (pointer_) pointer_->Release();
        pointer_ = pointer;
    }

private:
    T* pointer_ = nullptr;
};

bool getDispId(IDispatch* object, const wchar_t* name, DISPID* id) {
    if (!object || !name || !id) return false;
    LPOLESTR names[1] = {const_cast<LPOLESTR>(name)};
    return SUCCEEDED(object->GetIDsOfNames(IID_NULL, names, 1,
                                           LOCALE_USER_DEFAULT, id));
}

bool invoke(IDispatch* object, DISPID id, WORD flags, VARIANT* argument,
            VARIANT* result) {
    if (!object) return false;
    DISPPARAMS params{};
    if (argument) {
        params.rgvarg = argument;
        params.cArgs = 1;
    }
    return SUCCEEDED(object->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags,
                                    &params, result, nullptr, nullptr));
}

// 读取无参属性；返回 DISPATCH 对象的所有权（调用方负责 Release）。
IDispatch* getDispatch(IDispatch* object, const wchar_t* name) {
    DISPID id = DISPID_UNKNOWN;
    if (!getDispId(object, name, &id)) return nullptr;

    VARIANT value;
    VariantInit(&value);
    if (!invoke(object, id, DISPATCH_PROPERTYGET, nullptr, &value)) return nullptr;
    if (value.vt != VT_DISPATCH || !value.pdispVal) {
        VariantClear(&value);
        return nullptr;
    }

    IDispatch* result = value.pdispVal;
    value.pdispVal = nullptr;
    value.vt = VT_EMPTY;
    return result;
}

// 集合 Item(index)：优先 Item 名称，失败时退回默认成员。
IDispatch* getItem(IDispatch* collection, int32_t index) {
    if (!collection) return nullptr;

    VARIANT argument;
    VariantInit(&argument);
    argument.vt = VT_I4;
    argument.lVal = index;

    DISPID id = DISPID_VALUE;
    if (!getDispId(collection, L"Item", &id)) id = DISPID_VALUE;

    VARIANT value;
    VariantInit(&value);
    if (invoke(collection, id, DISPATCH_PROPERTYGET | DISPATCH_METHOD,
               &argument, &value)) {
        if (value.vt == VT_DISPATCH && value.pdispVal) {
            IDispatch* result = value.pdispVal;
            value.pdispVal = nullptr;
            value.vt = VT_EMPTY;
            return result;
        }
        VariantClear(&value);
    }
    return nullptr;
}

int32_t getInt(IDispatch* object, const wchar_t* name) {
    DISPID id = DISPID_UNKNOWN;
    if (!getDispId(object, name, &id)) return 0;

    VARIANT value;
    VariantInit(&value);
    if (!invoke(object, id, DISPATCH_PROPERTYGET, nullptr, &value)) return 0;

    VARIANT converted;
    VariantInit(&converted);
    int32_t result = 0;
    if (SUCCEEDED(VariantChangeType(&converted, &value, 0, VT_I4))) {
        result = converted.lVal;
    }
    VariantClear(&converted);
    VariantClear(&value);
    return result;
}

std::wstring baseNameLower(const wchar_t* path) {
    std::wstring name(path ? path : L"");
    const size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    for (wchar_t& ch : name) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + 32);
    }
    return name;
}

// ----------------------------------------------------------------------------
// 查询单个 Office 应用的放映状态
// ----------------------------------------------------------------------------
bool queryApplication(const wchar_t* progId, OfficeBridge::App app,
                      OfficeBridge::Snapshot* out) {
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(progId, &clsid))) return false;

    IUnknown* unknown = nullptr;
    if (FAILED(GetActiveObject(clsid, nullptr, &unknown)) || !unknown) {
        return false;
    }
    ComRef<IUnknown> unknownRef(unknown);

    IDispatch* dispatch = nullptr;
    if (FAILED(unknown->QueryInterface(IID_IDispatch,
                                       reinterpret_cast<void**>(&dispatch))) ||
        !dispatch) {
        return false;
    }
    ComRef<IDispatch> application(dispatch);

    // 放映窗口集合：Count > 0 表示正在放映。
    ComRef<IDispatch> windows = getDispatch(application.get(), L"SlideShowWindows");
    ComRef<IDispatch> window;
    bool presenting = false;

    if (windows) {
        const int32_t windowCount = getInt(windows.get(), L"Count");
        presenting = windowCount > 0;
        if (presenting) window.reset(getItem(windows.get(), 1));
    } else {
        // 兼容不暴露集合的实现：退回 ActivePresentation.SlideShowWindow。
        ComRef<IDispatch> presentation =
            getDispatch(application.get(), L"ActivePresentation");
        if (presentation) {
            window.reset(getDispatch(presentation.get(), L"SlideShowWindow"));
            presenting = static_cast<bool>(window);
        }
    }

    if (!presenting) return false;

    int32_t page = 0;
    {
        ComRef<IDispatch> view;
        if (window) view.reset(getDispatch(window.get(), L"View"));
        if (view) {
            ComRef<IDispatch> slide = getDispatch(view.get(), L"Slide");
            if (slide) page = getInt(slide.get(), L"SlideIndex");
            // 备选：PowerPoint 的放映位置（自定义放映下语义不同，仅作兜底）。
            if (page <= 0) page = getInt(view.get(), L"CurrentShowPosition");
        }
    }

    int32_t total = 0;
    {
        ComRef<IDispatch> presentation =
            getDispatch(application.get(), L"ActivePresentation");
        if (presentation) {
            ComRef<IDispatch> slides = getDispatch(presentation.get(), L"Slides");
            if (slides) total = getInt(slides.get(), L"Count");
        }
    }

    out->app = app;
    out->presenting = true;
    out->page = page;
    out->total = total;
    return true;
}

} // namespace

// ============================================================================
// 共享状态 / 后台轮询
// ============================================================================

struct OfficeBridge::Shared {
    std::atomic<bool> stop{false};
    mutable std::mutex mutex;
    Snapshot snapshot;
};

void OfficeBridge::poll(std::shared_ptr<Shared> shared) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comReady = SUCCEEDED(hr);

    while (!shared->stop.load(std::memory_order_relaxed)) {
        Snapshot next;
        if (comReady) {
            // PowerPoint 优先；未放映时再尝试 WPS 演示。
            queryApplication(L"PowerPoint.Application", App::PowerPoint, &next);
            if (!next.presenting) {
                queryApplication(L"KWPP.Application", App::Wps, &next);
            }
        }

        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->snapshot = next;
        }

        // 分段休眠，退出时可及时响应。
        for (uint32_t slept = 0;
             slept < kPollIntervalMs && !shared->stop.load(std::memory_order_relaxed);
             slept += 20) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    if (comReady) CoUninitialize();
}

// ============================================================================
// 对外接口
// ============================================================================

OfficeBridge::OfficeBridge() = default;

OfficeBridge::~OfficeBridge() {
    if (shared_) shared_->stop.store(true, std::memory_order_relaxed);
}

void OfficeBridge::initialize() {
    if (shared_) return;
    shared_ = std::make_shared<Shared>();
    auto shared = shared_;
    std::thread([shared = std::move(shared)] { poll(shared); }).detach();
}

OfficeBridge::Snapshot OfficeBridge::snapshot() const {
    if (!shared_) return {};
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->snapshot;
}

namespace {

bool equalsAsciiIgnoreCase(const wchar_t* text, const wchar_t* expected) {
    if (!text || !expected) return false;
    for (;; ++text, ++expected) {
        wchar_t left = *text;
        wchar_t right = *expected;
        if (left >= L'A' && left <= L'Z') left = static_cast<wchar_t>(left + 32);
        if (right >= L'A' && right <= L'Z') right = static_cast<wchar_t>(right + 32);
        if (left != right) return false;
        if (left == 0) return true;
    }
}

bool windowTitleMentionsSlideshow(HWND window) {
    wchar_t title[256]{};
    GetWindowTextW(window, title, 256);
    const std::wstring text(title);
    return text.find(L"幻灯片") != std::wstring::npos ||
           text.find(L"放映") != std::wstring::npos;
}

bool hasPopupPresentationStyle(HWND window) {
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    if ((style & WS_POPUP) == 0) return false;
    return (style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) == 0;
}

} // namespace

OfficeBridge::App OfficeBridge::foregroundApp() {
    HWND window = GetForegroundWindow();
    if (!window) return App::None;

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (!processId) return App::None;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                 processId);
    if (!process) return App::None;

    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != 0;
    CloseHandle(process);
    if (!ok) return App::None;

    const std::wstring name = baseNameLower(path);
    if (name == L"powerpnt.exe") return App::PowerPoint;
    if (name == L"wpp.exe") return App::Wps;
    return App::None;
}

bool OfficeBridge::isPowerPointShowWindow(void* handle) {
    HWND window = static_cast<HWND>(handle);
    if (!window) return false;
    wchar_t className[64]{};
    GetClassNameW(window, className, 64);
    return equalsAsciiIgnoreCase(className, L"screenClass");
}

bool OfficeBridge::isWpsShowWindow(void* handle) {
    HWND window = static_cast<HWND>(handle);
    if (!window) return false;

    // WPS 放映窗口：类名 Widget（部分版本为 SlideShowClass），
    // 标题通常包含“幻灯片”，且为全屏弹出窗口。
    wchar_t className[64]{};
    GetClassNameW(window, className, 64);
    const bool classMatch = equalsAsciiIgnoreCase(className, L"Widget") ||
                            equalsAsciiIgnoreCase(className, L"SlideShowClass");

    const bool titleMatch = windowTitleMentionsSlideshow(window);
    const bool fullscreen = isFullscreenWindow(window);
    const bool popupStyle = hasPopupPresentationStyle(window);

    if (classMatch && (titleMatch || fullscreen || popupStyle)) return true;
    // 类名未匹配时的兜底：标题明确为放映且窗口全屏。
    return titleMatch && fullscreen;
}

bool OfficeBridge::isFullscreenWindow(void* handle) {
    HWND window = static_cast<HWND>(handle);
    if (!window) return false;

    RECT bounds{};
    if (!GetWindowRect(window, &bounds)) return false;

    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return false;

    const RECT& target = info.rcMonitor;
    return bounds.left <= target.left && bounds.top <= target.top &&
           bounds.right >= target.right && bounds.bottom >= target.bottom;
}

bool OfficeBridge::canControlPresentation() const {
    HWND window = GetForegroundWindow();
    if (!window) return false;

    const App foreground = foregroundApp();
    if (foreground == App::None) return false;

    const Snapshot state = snapshot();
    if (foreground == App::PowerPoint) {
        // 放映 / 演讲者视图窗口类名为 screenClass。
        if (isPowerPointShowWindow(window)) return true;
        return state.presenting && state.app == App::PowerPoint;
    }

    // WPS 放映窗口：类名 Widget / SlideShowClass（标题含“幻灯片”、全屏）。
    if (isWpsShowWindow(window)) return true;
    // 类名随版本变化时的兜底：COM 已确认在放映，且前台窗口全屏。
    return state.presenting && state.app == App::Wps && isFullscreenWindow(window);
}

bool OfficeBridge::sendPageKey(bool next) const {
    if (!canControlPresentation()) return false;

    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = next ? VK_NEXT : VK_PRIOR;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = next ? VK_NEXT : VK_PRIOR;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

} // namespace annotation
