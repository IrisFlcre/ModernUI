# ============================================================================
# src/std_widgets — Windows 11 Fluent Design 标准控件库（静态库）
#
#   FluentTheme       — 主题令牌 / 明暗模式 / 颜色工具
#   FluentControl     — 控件基类（状态动画 / 焦点 / 主题同步）
#   FluentTypography  — TextBlock / Divider
#   FluentButtons     — Button / ToggleButton / RepeatButton / Hyperlink /
#                       DropDownButton / SplitButton
#   FluentSelection   — CheckBox / RadioButton / ToggleSwitch / SegmentedControl /
#                       Slider / RangeSlider / ComboBox / RatingControl
#   FluentInput       — TextBox / PasswordBox / NumberBox
#   FluentProgress    — ProgressBar / ProgressRing / RadialGauge / Skeleton /
#                       InfoBadge / InfoBar
#   FluentCollections — StackPanel / Card / AcrylicPanel / Chip / Expander /
#                       ScrollViewer / ListView / GridView / NavigationView /
#                       TabView / BreadcrumbBar
#   FluentFeedback    — TeachingTip / ToolTip / ContentDialog / MenuFlyout /
#                       MenuBar / CommandBar
#   FluentMedia       — ImageView / PersonPicture / AnimatedImage(Sprite)
#   FluentAdvanced    — TreeView / CalendarView / ColorPicker / PipsPager / Sparkline
#   Markdown          — MarkdownView（可复用 Markdown 文档渲染控件）
#
# 统一入口：Fluent.h
# ============================================================================

add_library(modernui_std_widgets STATIC
    FluentTheme.cpp
    FluentControl.cpp
    FluentTypography.cpp
    FluentButtons.cpp
    FluentSelection.cpp
    FluentInput.cpp
    FluentProgress.cpp
    FluentCollections.cpp
    FluentFeedback.cpp
    FluentMedia.cpp
    FluentAdvanced.cpp
    Markdown.cpp
    FluentUi.cpp
)

add_library(modernui::std_widgets ALIAS modernui_std_widgets)

target_include_directories(modernui_std_widgets
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(modernui_std_widgets
    PUBLIC
        modernui::ui
        modernui::animation
)

target_compile_features(modernui_std_widgets PUBLIC cxx_std_17)
