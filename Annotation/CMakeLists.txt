# ============================================================================
# Annotation — 独立项目（全屏批注 + RapidOCRLite 文字识别）
#
# 与 ModernUI 框架解耦：本目录是独立的 CMake 工程，直接引用上级目录的
# 框架源码，但不参与 ModernUI 根工程的构建。
#
# 用法（MSYS2 UCRT64 shell）：
#   cmake -S . -B build -G Ninja
#   cmake --build build -j
# ============================================================================

cmake_minimum_required(VERSION 3.20)
project(ModernUIAnnotation
    VERSION 0.1.0
    DESCRIPTION "ModernUI full-screen annotation with RapidOCRLite"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS
        "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
    add_compile_options(-fexceptions -frtti)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3)
    endif()
elseif(MSVC)
    add_compile_options(/W4 /permissive-)
endif()

# --- ModernUI 框架（只构建库，不构建其示例）---
set(MODERNUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.." modernui)

# --- RapidOCRLite（静态库 / CPU 后端）---
set(PPOCR_BUILD_SHARED  OFF CACHE BOOL "" FORCE)
set(PPOCR_BUILD_STATIC  ON  CACHE BOOL "" FORCE)
set(PPOCR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PPOCR_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(PPOCR_ENABLE_VULKAN OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../3rd/RapidOCRLite"
    rapidocrlite
)

# --- 批注程序 ---
add_executable(modernui_annotation
    main.cpp
    AnnotationView.cpp
    InkEngine.cpp
    Toolbar.cpp
    Screenshot.cpp
    OcrEngine.cpp
    AiClient.cpp
    AiCards.cpp
    OfficeBridge.cpp
)

target_include_directories(modernui_annotation
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(modernui_annotation
    PRIVATE
        modernui::app
        modernui::backend_blend2d
        modernui::ui
        modernui::canvas
        modernui::event
        modernui::input
        RapidOCRLite::static
        gdiplus
        winhttp
        ole32
        oleaut32
        uuid
)

target_compile_definitions(modernui_annotation
    PRIVATE
        MODERNUI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
        ANNOTATION_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models"
)

target_compile_features(modernui_annotation PRIVATE cxx_std_20)
