# ============================================================================
# src/event — 与渲染后端解耦的事件系统（header-only）
# ============================================================================

add_library(modernui_event INTERFACE)

add_library(modernui::event ALIAS modernui_event)

target_include_directories(modernui_event
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(modernui_event INTERFACE cxx_std_17)
