// ============================================================================
// Snapshot.h — 拍照结果输出（剪贴板 + PNG）
// ============================================================================

#pragma once

#include <cstdint>
#include <string>

namespace camerapro {

struct SnapshotResult {
    bool ok = false;
    bool copiedToClipboard = false;
    bool savedToFile = false;
    std::wstring savedPath;
    std::string error;
};

// bgra 为 top-down BGRA（A=255），行 stride = width * 4。
SnapshotResult saveBgraToClipboardAndPng(int32_t width, int32_t height,
                                         const void* bgra);

// UTF-8 文本写入剪贴板（CF_UNICODETEXT）。
bool copyTextToClipboard(const std::string& utf8Text);

} // namespace camerapro
