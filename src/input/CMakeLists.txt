# ============================================================================
# src/input — 全局输入源（Low-Level Hook）
# ============================================================================

add_library(modernui_input STATIC
    GlobalMouseHook.cpp
)

add_library(modernui::input ALIAS modernui_input)

target_include_directories(modernui_input
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(modernui_input
    PUBLIC
        modernui::event
        modernui::canvas
        user32
)

target_compile_definitions(modernui_input
    PUBLIC
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_compile_features(modernui_input PUBLIC cxx_std_17)
