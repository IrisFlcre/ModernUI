// ============================================================================
// CameraPro — 教学摄像头展台
// ----------------------------------------------------------------------------
// 全屏展台：Media Foundation 摄像头采集 + 笔迹批注 + 内容缩放平移 +
// 画面后处理（亮度 / 对比度 / 饱和度 / 锐化 / 镜像 / 自动增强 / 冻结）
// 右侧工具栏布局沿用原 Annotation 程序，OCR 文字识别保留。
// ============================================================================

#include "App.h"
#include "AssetPath.h"
#include "CameraProView.h"
#include "Event.h"
#include "View.h"
#include "Win32LayeredWindow.h"

#ifndef CAMERAPRO_SOURCE_DIR
  #define CAMERAPRO_SOURCE_DIR "."
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
    App app(L"CameraPro 教学摄像头展台", 1600, 900);

    camerapro::CameraProView* view = nullptr;

    app.viewFactory([&](int32_t width, int32_t height) {
           auto instance =
               std::make_unique<camerapro::CameraProView>(width, height);
           view = instance.get();
           return instance;
       })
        .virtualScreenFullscreen(true)
        .topmost(false)
        .resizable(false)
        .draggable(false)
        .escapeQuits(false)
        .background(Color{10, 12, 17, 255})
        .clickThrough(false)
        .noActivate(false)
        .toolWindow(false)
        .mouseInputMode(MouseInputMode::Window)
        .frameInterval(8)
        .onEvent([&](const Event& event) {
            return view && view->handleAppEvent(event);
        })
        .content([&](View&) {
            if (!view) return;
            const std::string iconDirectory = resolveAssetDirectory(
                "icons", std::string(CAMERAPRO_SOURCE_DIR) + "/icons");
            view->initialize(app.renderer(), app.window(), iconDirectory);
            view->onQuitRequested = [&app] { app.quit(); };
        });

    const std::string font = chooseFont();
    if (!font.empty()) app.font(font);

    // 全局热键
    app.registerHotkey(camerapro::HotkeyToggleChrome, 0, VK_F9);
    app.registerHotkey(camerapro::HotkeyFreeze, 0, VK_F10);
    app.registerHotkey(camerapro::HotkeyUndo, ModifierControl, 'Z');
    app.registerHotkey(camerapro::HotkeyCopyOcr,
                       ModifierControl | ModifierShift, 'O');
    app.registerHotkey(camerapro::HotkeyFit, ModifierControl, '0');
    app.registerHotkey(camerapro::HotkeySnapshot, ModifierControl, 'S');
    app.registerHotkey(camerapro::HotkeyQuit,
                       ModifierControl | ModifierShift, 'Q');
    app.registerHotkey(camerapro::HotkeyPrevPage, ModifierControl, VK_PRIOR);
    app.registerHotkey(camerapro::HotkeyNextPage, ModifierControl, VK_NEXT);
    app.registerHotkey(camerapro::HotkeyZoomIn, ModifierControl, VK_OEM_PLUS);
    app.registerHotkey(camerapro::HotkeyZoomOut, ModifierControl, VK_OEM_MINUS);

    return app.run();
}
