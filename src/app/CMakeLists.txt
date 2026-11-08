# ============================================================================
# src/app — 应用门面（窗口 + Blend2D 后端 + View + 主循环）
# ============================================================================

add_library(modernui_app STATIC
    App.cpp
)

add_library(modernui::app ALIAS modernui_app)

target_include_directories(modernui_app
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(modernui_app
    PUBLIC
        modernui::ui
        modernui::style
        modernui::backend_blend2d
        modernui::input
)

target_compile_features(modernui_app PUBLIC cxx_std_17)
