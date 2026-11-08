# ============================================================================
# examples/ui_demo — 控件层 Demo（Flexbox 排版 + 子 Canvas 合成 + 事件）
#
# 目录结构：
#   main.cpp            控件树搭建、事件循环、动画驱动
#   controls/*.h/.cpp   简单控件（演示"构造函数建几何体 / onEvent 改几何体"）
#                      Panel / Label / Button / Toggle / Slider / ProgressBar / BarChart
# ============================================================================

add_executable(modernui_ui_demo
    main.cpp
    controls/Panel.cpp
    controls/Label.cpp
    controls/Button.cpp
    controls/Toggle.cpp
    controls/Slider.cpp
    controls/ProgressBar.cpp
    controls/BarChart.cpp
)

target_include_directories(modernui_ui_demo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(modernui_ui_demo
    PRIVATE
        modernui::app
        modernui::ui
        modernui::backend_blend2d
        modernui::canvas
        modernui::event
)

target_compile_features(modernui_ui_demo PRIVATE cxx_std_17)
