# ============================================================================
# examples/fluent_demo — Windows 11 Fluent Design 控件库 + Sprite 动画 Demo
# ============================================================================

add_executable(modernui_fluent_demo
    main.cpp
)

target_include_directories(modernui_fluent_demo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(modernui_fluent_demo
    PRIVATE
        modernui::app
        modernui::std_widgets
        modernui::backend_blend2d
        modernui::animation
        modernui::ui
        modernui::canvas
        modernui::event
)

target_compile_features(modernui_fluent_demo PRIVATE cxx_std_17)
