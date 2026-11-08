# ============================================================================
# src/style — 样式层（StyleSheet / CSS 解析 / .ui 解析，静态库）
#
#   StyleSheet.h/.cpp — 每个 Widget 的样式表（基础 Entry + others + 引用值）
#   Css.h/.cpp       — .css 解析（选择器 / 状态伪类 / 变量 / 声明）
#   Ui.h/.cpp        — .ui 布局文件解析（XML 风格自定义标签）
#
# 本模块只依赖 Canvas 的值类型（Color），不感知 Widget / 渲染后端。
# ============================================================================

add_library(modernui_style STATIC
    StyleSheet.cpp
    Css.cpp
    Ui.cpp
)

add_library(modernui::style ALIAS modernui_style)

target_include_directories(modernui_style
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(modernui_style
    PUBLIC
        modernui::canvas
)

target_compile_features(modernui_style PUBLIC cxx_std_17)
