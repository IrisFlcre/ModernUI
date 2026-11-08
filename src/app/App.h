// ============================================================================
// App.h — 应用门面（窗口 + 渲染后端 + View + 主循环的一体化封装）
// ============================================================================
//
// 把"创建窗口 / 绑定 Blend2D 后端 / 注入文本度量 / 处理消息 / 推进动画 /
// 按需重绘"这些样板代码收敛成一个链式 API：
//
//   int main() {
//       modern_ui::App app(L"ModernUI Demo", 1280, 820);
//       app.background(Color{16, 18, 24})
//           .content([](modern_ui::View& view) {
//               view.emplaceChild<Panel>()->padding(16).gap(8)
//                   ->emplaceChild<Label>("Hello ModernUI");
//           })
//           .run();
//   }
//
// 需要自定义 View 子类（例如在根 Canvas 里画背景）时用 viewFactory：
//   app.viewFactory([](int32_t w, int32_t h) {
//       return std::make_unique<DemoView>(w, h);
//   });
//
// 仍然可以直接访问底层对象：app.window() / app.renderer() / app.view()。
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Blend2DRenderer.h"
#include "Event.h"
#include "View.h"
#include "Win32LayeredWindow.h"

namespace modern_ui {

class GlobalMouseHook;

// 鼠标输入来源：
//   Window — 传统窗口消息（窗口必须可命中，适合普通窗口应用）
//   Hook   — 全局 WH_MOUSE_LL（窗口通常配合 clickThrough 使用，事件不阻塞）
enum class MouseInputMode : uint8_t {
    Window = 0,
    Hook,
};

class App {
public:
    // 构建 UI：在样式加载完成、渲染后端已绑定之后调用一次
    using ContentBuilder = std::function<void(View&)>;
    // 全局事件钩子：返回 true 表示已消费（不再派发给 View）
    using EventHook = std::function<bool(const Event&)>;
    // 每帧回调：在 View::tick 之前调用（推进自定义动画 / 定时逻辑）
    using TickHook = std::function<void(View&, float dtSeconds)>;
    // 自定义 View 工厂（默认创建普通 View）
    using ViewFactory = std::function<std::unique_ptr<View>(int32_t width, int32_t height)>;

    App(std::wstring title, int32_t width, int32_t height);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // ==================== 窗口配置（run 之前设置）====================
    App& resizable(bool value);
    App& topmost(bool value);
    App& draggable(bool value);
    App& position(int32_t x, int32_t y);
    App& background(Color color);
    App& escapeQuits(bool value);

    // --- 全屏悬浮层 / 批注类应用 ---
    // 铺满整个虚拟桌面（所有显示器，含负坐标），窗口尺寸与位置自动计算。
    App& virtualScreenFullscreen(bool value = true);
    // 窗口鼠标完全穿透（需配合 Hook 输入，否则收不到鼠标事件）。
    App& clickThrough(bool value);
    // 悬浮层不抢前台焦点（WS_EX_NOACTIVATE）。
    App& noActivate(bool value = true);
    // 不出现在任务栏 / Alt+Tab（WS_EX_TOOLWINDOW）。
    App& toolWindow(bool value = true);
    // 鼠标输入来源；Hook 模式会启动专用 Hook 线程并关闭窗口鼠标派发。
    App& mouseInputMode(MouseInputMode mode);
    // 注册全局热键；热键以 EventType::Hotkey（hotkeyId = id）进入事件流。
    App& registerHotkey(int id, uint32_t modifiers, uint32_t virtualKey);

    // ==================== 资源 ====================
    // 自定义字体文件；不设置时加载系统默认字体
    App& font(const std::string& path);
    // 全局 CSS（文件 / 文本），在 content 之前加载
    App& styleSheetFile(const std::string& path);
    App& styleSheetText(const std::string& text);

    // ==================== 行为 ====================
    App& content(ContentBuilder builder);
    App& viewFactory(ViewFactory factory);
    App& onEvent(EventHook hook);
    App& onTick(TickHook hook);
    // 无消息时的等待时间（毫秒，默认 8 ≈ 120 FPS 上限）
    App& frameInterval(uint32_t milliseconds);

    // ==================== 运行 ====================
    // 创建窗口 → 构建内容 → 主循环；返回进程退出码
    int run();
    // 请求退出主循环（可在回调中调用）
    void quit() { quitRequested_ = true; }

    // ==================== 底层对象 ====================
    View& view() { return *view_; }
    Blend2DRenderer& renderer() { return renderer_; }
    Win32LayeredWindow& window() { return window_; }
    EventQueue& events() { return events_; }
    // 全局 Hook（仅 Hook 模式且启动成功后非空）
    GlobalMouseHook* mouseHook() { return mouseHook_.get(); }
    // 窗口是否已创建（run() 之后为 true）
    bool running() const { return running_; }

private:
    struct HotkeySpec {
        int      id = 0;
        uint32_t modifiers = 0;
        uint32_t virtualKey = 0;
    };

    void dispatchEvent(const Event& ev, bool& quitRequested);
    bool pollHookEvents();

    EventQueue         events_;
    Win32LayeredWindow window_;
    Blend2DRenderer    renderer_;
    std::unique_ptr<View> view_;

    std::wstring title_;
    int32_t      width_ = 0;
    int32_t      height_ = 0;

    bool resizable_ = true;
    bool topmost_ = false;
    bool draggable_ = true;
    bool escapeQuits_ = true;
    int32_t x_ = INT32_MIN;
    int32_t y_ = INT32_MIN;

    Color background_ = Color{0, 0, 0, 255};
    std::string fontPath_;
    std::string styleFile_;
    std::string styleText_;

    ContentBuilder content_;
    ViewFactory    viewFactory_;
    EventHook      eventHook_;
    TickHook       tickHook_;
    uint32_t       frameIntervalMs_ = 8;

    bool virtualScreenFullscreen_ = false;
    bool clickThrough_ = false;
    bool noActivate_ = false;
    bool toolWindow_ = false;
    MouseInputMode mouseInputMode_ = MouseInputMode::Window;
    std::unique_ptr<GlobalMouseHook> mouseHook_;
    std::vector<HotkeySpec> hotkeys_;
    Vec2i clientOrigin_{0, 0};

    bool running_ = false;
    bool quitRequested_ = false;
};

} // namespace modern_ui
