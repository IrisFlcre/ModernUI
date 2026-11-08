# ============================================================================
# ModernUI — 根 CMakeLists.txt
#
# 构建环境：MSYS2 UCRT64 (x86_64-w64-mingw32) + CMake >= 3.16
#
# 用法（MSYS2 UCRT64 shell）：
#   cmake -S . -B build -G "MinGW Makefiles"
#   cmake --build build -j
#
# 或使用 Ninja（更快）：
#   cmake -S . -B build -G Ninja
#   cmake --build build -j
# ============================================================================

cmake_minimum_required(VERSION 3.16)
project(ModernUI
    VERSION 0.1.0
    DESCRIPTION "Modern UI framework — Canvas / Scene graph / Dirty-region management"
    LANGUAGES CXX
)

# 示例 / Demo 是否参与构建。独立项目（如 Annotation）可设为 OFF，
# 只构建框架库，避免和宿主应用互相影响。
option(MODERNUI_BUILD_EXAMPLES "Build ModernUI example programs" ON)

# --- C++17 强制 ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)       # 用 -std=c++17 而非 -std=gnu++17

# --- 默认 Release ---
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- MSYS2 / MinGW 特有设置 ---
if(MINGW)
    # 静态链接 libgcc / libstdc++ / winpthread，避免运行时 DLL 依赖
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")

    # 启用异常 + RTTI（默认应开启，但显式写出避免意外）
    add_compile_options(-fexceptions -frtti)
endif()

# --- 全局警告等级 ---
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Release 优化（MinGW Makefiles 是单配置生成器，CMAKE_BUILD_TYPE 已在上面设为 Release）
    # x86-64-v2 作为发布基线（SSE4.2/POPCNT），避免 -march=native 在较老
    # CPU 上产生非法指令；需要本机极限性能时可手动改回 native。
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=x86-64-v2)
    endif()
elseif(MSVC)
    add_compile_options(/W4 /permissive-)
endif()

# --- Blend2D（vendored 源码，静态库）---
set(BLEND2D_STATIC   ON  CACHE BOOL "Build Blend2D as a static library" FORCE)
set(BLEND2D_NO_INSTALL ON CACHE BOOL "Disable Blend2D install rules" FORCE)
set(BLEND2D_TEST     OFF CACHE BOOL "Build Blend2D tests" FORCE)
set(BLEND2D_DEMOS    OFF CACHE BOOL "Build Blend2D demos" FORCE)
add_subdirectory(3rd/blend2d)

# 第三方 Blend2D 头文件按系统头处理：避免 -Wall/-Wextra/-Wpedantic 报告库内部告警
if(TARGET blend2d)
    get_target_property(BLEND2D_PUBLIC_INCLUDES blend2d INTERFACE_INCLUDE_DIRECTORIES)
    if(BLEND2D_PUBLIC_INCLUDES)
        set_target_properties(blend2d PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
        target_include_directories(blend2d SYSTEM INTERFACE ${BLEND2D_PUBLIC_INCLUDES})
    endif()
endif()

# --- Canvas 子模块 ---
add_subdirectory(src/canvas)

# --- 事件系统（与渲染后端解耦）---
add_subdirectory(src/event)

# --- 全局输入源（WH_MOUSE_LL 专用线程 / 无锁队列）---
add_subdirectory(src/input)

# --- 2D 动画 / Sprite 动画 ---
add_subdirectory(src/animation)

# --- 样式层（StyleSheet / CSS / .ui 解析）---
add_subdirectory(src/style)

# --- 控件层（接口 / Flexbox 排版 / 脏控件合成）---
add_subdirectory(src/ui)

# --- Windows 11 Fluent Design 标准控件库 ---
add_subdirectory(src/std_widgets)

# --- Blend2D / Win32 分层窗口后端 ---
add_subdirectory(src/backend_blend2d)

# --- 应用门面（窗口 + 后端 + View + 主循环）---
add_subdirectory(src/app)

# --- Demo ---
if(MODERNUI_BUILD_EXAMPLES)
    add_subdirectory(examples/blend2d_demo)
    add_subdirectory(examples/ui_demo)
    add_subdirectory(examples/fluent_demo)
    add_subdirectory(examples/style_demo)
endif()
