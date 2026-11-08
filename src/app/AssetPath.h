// ============================================================================
// AssetPath.h — 资源目录定位
// ----------------------------------------------------------------------------
// 安装后的程序把 icons / models 放在 exe 同目录；开发期仍使用源码目录中的
// 资源。这里统一做“exe 同目录优先，源码目录回退”的解析。
// ============================================================================

#pragma once

#include <string>

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace modern_ui {

// 当前可执行文件所在目录（不含末尾分隔符）
inline std::string executableDirectory() {
    char buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};

    std::string path(buffer, length);
    const std::string::size_type slash = path.find_last_of("\\/");
    if (slash == std::string::npos) return {};
    return path.substr(0, slash);
}

// 优先返回 exe 同目录下的 name 子目录（安装布局），不存在时回退到
// developmentDirectory（开发期源码布局）。
inline std::string resolveAssetDirectory(const std::string& name,
                                         const std::string& developmentDirectory) {
    if (!name.empty()) {
        const std::string installed = executableDirectory() + "\\" + name;
        const DWORD attributes = GetFileAttributesA(installed.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            return installed;
        }
    }
    return developmentDirectory;
}

} // namespace modern_ui
