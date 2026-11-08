# ============================================================================
# src/canvas — Canvas 管理模块（静态库）
# ============================================================================

add_library(modernui_canvas STATIC
    Canvas.cpp
)

add_library(modernui::canvas ALIAS modernui_canvas)

target_include_directories(modernui_canvas
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(modernui_canvas PUBLIC cxx_std_17)
