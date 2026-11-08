# ============================================================================
# examples/style_demo — CSS + UI 样式系统 Demo
#
#   assets/demo.css  — 选择器 / 状态伪类 / 变量 / {entry} 引用
#   assets/demo.ui   — XML 风格布局 + 命名回调
#
# 构建后把 assets 复制到可执行文件旁；也支持从仓库根目录直接运行。
# ============================================================================

add_executable(modernui_style_demo
    main.cpp
)

target_include_directories(modernui_style_demo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(modernui_style_demo
    PRIVATE
        modernui::std_widgets
        modernui::backend_blend2d
        modernui::ui
        modernui::style
        modernui::canvas
        modernui::event
)

target_compile_features(modernui_style_demo PRIVATE cxx_std_17)

add_custom_command(TARGET modernui_style_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:modernui_style_demo>/assets
)
