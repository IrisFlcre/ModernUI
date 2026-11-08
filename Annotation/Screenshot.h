// ============================================================================
// Screenshot.h — 屏幕区域捕获 + 批注 Bitmap 合成 + 剪贴板 / PNG
// ============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class BLImage;

namespace annotation {

struct CaptureResult {
    bool ok = false;
    bool copiedToClipboard = false;
    bool savedToFile = false;
    std::wstring savedPath;
    std::string error;
};

// 屏幕区域原样捕获（不含批注层），像素为 packed RGB、行优先。
struct CapturedPixels {
    bool ok = false;
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> rgb;
    std::string error;
};

CapturedPixels captureScreenRegionRgb(
    int32_t screenX, int32_t screenY, int32_t width, int32_t height);

// UTF-8 文本写入剪贴板（CF_UNICODETEXT）。
bool copyTextToClipboard(const std::string& utf8Text);

// screenX/screenY/width/height 为屏幕物理像素坐标。
// inkImage 为覆盖层笔迹 Bitmap（PRGB32 / 预乘 alpha），
// inkOffsetX/inkOffsetY = 屏幕左上角在笔迹 Bitmap 中的偏移。
CaptureResult captureRegionToClipboardAndPng(
    int32_t screenX, int32_t screenY, int32_t width, int32_t height,
    int32_t inkOffsetX, int32_t inkOffsetY,
    const BLImage* inkImage);

} // namespace annotation
