# ============================================================================
# src/animation — 2D 动画系统（静态库）
#
#   Easing.h           — 缓动曲线 + 三次贝塞尔
#   Animation.h        — Tween / KeyframeTrack / SpringValue
#   SpriteAnimation    — Sprite 图集 / 剪辑 / 播放器
# ============================================================================

add_library(modernui_animation STATIC
    SpriteAnimation.cpp
)

add_library(modernui::animation ALIAS modernui_animation)

target_include_directories(modernui_animation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(modernui_animation
    PUBLIC
        modernui::canvas
)

target_compile_features(modernui_animation PUBLIC cxx_std_17)
