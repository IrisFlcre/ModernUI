# ============================================================================
# src/ui — 控件层（接口 / 排版 / 合成，静态库）
#
# 只包含框架能力，不含任何具体控件：
#   Widget.h/.cpp      — 控件接口（子 Canvas / 生命周期 / 事件 / 布局 / 样式表）
#   Flexbox.h/.cpp     — Flexbox 排版算法（标准子集）
#   View.h/.cpp        — 视图根节点（排版驱动 / 脏控件合成 / 事件 / CSS 加载）
#   StyleEngine.h/.cpp — .css 选择器匹配与样式级联
#   UiLoader.h/.cpp    — .ui 节点 → 控件树（布局属性 + 命名回调）
#   Layout.h           — 布局值类型（Dimension / Edges / FlexStyle）
# ============================================================================

add_library(modernui_ui STATIC
    Widget.cpp
    Flexbox.cpp
    View.cpp
    StyleEngine.cpp
    UiLoader.cpp
)

add_library(modernui::ui ALIAS modernui_ui)

target_include_directories(modernui_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(modernui_ui
    PUBLIC
        modernui::canvas
        modernui::event
        modernui::style
)

target_compile_features(modernui_ui PUBLIC cxx_std_17)
