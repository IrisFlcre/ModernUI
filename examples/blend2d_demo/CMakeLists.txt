# ============================================================================
# examples/blend2d_demo — 分层窗口 + Canvas + Blend2D 验证程序
# ============================================================================

add_executable(modernui_blend2d_demo
    main.cpp
)

target_link_libraries(modernui_blend2d_demo
    PRIVATE
        modernui::backend_blend2d
        modernui::canvas
        modernui::event
)

target_compile_features(modernui_blend2d_demo PRIVATE cxx_std_17)
