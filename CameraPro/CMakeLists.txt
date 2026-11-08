# ============================================================================
# CameraPro — 教学摄像头展台（独立 CMake 工程）
#
# 用法（MSYS2 UCRT64 shell）：
#   cmake -S . -B build -G Ninja
#   cmake --build build -j
# ============================================================================

cmake_minimum_required(VERSION 3.20)
project(CameraPro
    VERSION 0.1.0
    DESCRIPTION "Teaching camera document stand with annotation / zoom / post-processing"
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

# --- RapidOCRLite（OCR 保留）---
set(PPOCR_BUILD_SHARED  OFF CACHE BOOL "" FORCE)
set(PPOCR_BUILD_STATIC  ON  CACHE BOOL "" FORCE)
set(PPOCR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PPOCR_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(PPOCR_ENABLE_VULKAN OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../3rd/RapidOCRLite"
    rapidocrlite
)

# --- 应用程序 ---
add_executable(CameraPro
    main.cpp
    CameraProView.cpp
    CameraToolbar.cpp
    CameraCapture.cpp
    FrameProcessor.cpp
    InkEngine.cpp
    OcrEngine.cpp
    Snapshot.cpp
)

target_include_directories(CameraPro
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(CameraPro
    PRIVATE
        modernui::app
        modernui::backend_blend2d
        modernui::ui
        modernui::canvas
        modernui::event
        modernui::input
        RapidOCRLite::static
        mfplat
        mfreadwrite
        mfcore
        mfuuid
        ole32
        gdiplus
)

target_compile_definitions(CameraPro
    PRIVATE
        CAMERAPRO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        CAMERAPRO_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models"
)

target_compile_features(CameraPro PRIVATE cxx_std_20)
