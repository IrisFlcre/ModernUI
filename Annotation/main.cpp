// ============================================================================
// ModernUI — 全屏批注程序 (Annotation)
// ----------------------------------------------------------------------------
// 输入：WH_MOUSE_LL 专用 Hook 线程 + 无锁队列（鼠标移动永不阻塞）
//      - 批注模式始终捕获左键；指针模式仅吞工具栏区域，其余点击透传
// 笔迹：EMA 平滑 → 三次 Bezier → 速度模拟笔锋 → 变宽轮廓
//      → 完成笔迹光栅化进每页独立 Bitmap
// 界面：右侧浮动选择栏（icons/*.svg 染色图标 + 颜色/笔宽 + 笔迹参数）
// ============================================================================

#include "AnnotationView.h"
#include "App.h"
#include "AssetPath.h"
#include "Event.h"
#include "GlobalMouseHook.h"
#include "View.h"
#include "Win32LayeredWindow.h"

#ifndef MODERNUI_SOURCE_DIR
  #define MODERNUI_SOURCE_DIR "."
#endif

#include <memory>
#include <string>

using namespace modern_ui;

namespace {

bool fileExists(const char* path) {
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES;
}

std::string chooseFont() {
    // 微软雅黑：覆盖中英文标签；不存在时退回黑体 / 系统默认字体
    if (fileExists("C:/Windows/Fonts/msyh.ttc")) return "C:/Windows/Fonts/msyh.ttc";
    if (fileExists("C:/Windows/Fonts/simhei.ttf")) return "C:/Windows/Fonts/simhei.ttf";
    if (fileExists("C:/Windows/Fonts/segoeui.ttf")) return "C:/Windows/Fonts/segoeui.ttf";
    return {};
}

} // namespace

int main() {
    App app(L"ModernUI 全屏批注", 1280, 800);

    annotation::AnnotationView* view = nullptr;

    app.viewFactory([&](int32_t width, int32_t height) {
           auto instance = std::make_unique<annotation::AnnotationView>(width, height);
           view = instance.get();
           return instance;
       })
        .virtualScreenFullscreen(true)
        .topmost(true)
        .resizable(false)
        .draggable(false)
        .escapeQuits(false)
        .background(Color{0, 0, 0, 0})
        .clickThrough(true)
        .noActivate(true)
        .toolWindow(true)
        .mouseInputMode(MouseInputMode::Hook)
        .frameInterval(4)
        .onEvent([&](const Event& event) {
            return view && view->handleAppEvent(event);
        })
        .content([&](View&) {
            if (!view) return;
            const std::string iconDirectory = resolveAssetDirectory(
                "icons", std::string(MODERNUI_SOURCE_DIR) + "/icons");
            view->initialize(app.renderer(), app.window(), app.mouseHook(),
                             iconDirectory);
            view->onQuitRequested = [&app] { app.quit(); };
        });

    const std::string font = chooseFont();
    if (!font.empty()) app.font(font);

    // 全局热键：F9 显示/隐藏批注层；F10 指针透传模式
    app.registerHotkey(annotation::HotkeyToggleVisible, 0, VK_F9);
    app.registerHotkey(annotation::HotkeyPointerMode, 0, VK_F10);
    app.registerHotkey(annotation::HotkeyUndo, ModifierControl, 'Z');
    app.registerHotkey(annotation::HotkeyCopyOcr,
                       ModifierControl | ModifierShift, 'O');
    app.registerHotkey(annotation::HotkeyPrevPage, ModifierControl, VK_PRIOR);
    app.registerHotkey(annotation::HotkeyNextPage, ModifierControl, VK_NEXT);
    app.registerHotkey(annotation::HotkeyQuit,
                       ModifierControl | ModifierShift, 'Q');

    return app.run();
}
