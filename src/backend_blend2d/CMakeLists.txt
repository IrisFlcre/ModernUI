# ============================================================================
# src/backend_blend2d — Win32 分层窗口 + Blend2D 渲染后端
# ============================================================================

add_library(modernui_backend_blend2d STATIC
    Blend2DRenderer.cpp
    Win32LayeredWindow.cpp
)

add_library(modernui::backend_blend2d ALIAS modernui_backend_blend2d)

target_include_directories(modernui_backend_blend2d
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/3rd          # vendored 单头库（nanosvg.h）
)

target_link_libraries(modernui_backend_blend2d
    PUBLIC
        modernui::canvas
        modernui::event
        blend2d::blend2d
        user32
        gdi32
)

target_compile_definitions(modernui_backend_blend2d
    PUBLIC
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_compile_features(modernui_backend_blend2d PUBLIC cxx_std_17)
