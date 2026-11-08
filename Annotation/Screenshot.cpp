// ============================================================================
// Screenshot.cpp
// ============================================================================

#include "Screenshot.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <blend2d/blend2d.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <vector>

namespace annotation {

namespace {

ULONG_PTR gdiplusToken = 0;
bool gdiplusReady = false;

void ensureGdiplus() {
    if (gdiplusReady) return;
    Gdiplus::GdiplusStartupInput input;
    gdiplusReady =
        Gdiplus::GdiplusStartup(&gdiplusToken, &input, nullptr) == Gdiplus::Ok;
}

int encoderClsid(const WCHAR* mimeType, CLSID* clsid) {
    UINT count = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (size == 0) return -1;

    std::vector<BYTE> buffer(size);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, size, codecs) != Gdiplus::Ok) return -1;

    for (UINT i = 0; i < count; ++i) {
        if (std::wcscmp(codecs[i].MimeType, mimeType) == 0) {
            *clsid = codecs[i].Clsid;
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::wstring timestampName() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t name[64];
    std::swprintf(name, sizeof(name) / sizeof(name[0]),
                  L"Annotation_%04d%02d%02d_%02d%02d%02d.png",
                  time.wYear, time.wMonth, time.wDay,
                  time.wHour, time.wMinute, time.wSecond);
    return std::wstring(name);
}

std::wstring picturesDirectory() {
    wchar_t profile[MAX_PATH] = L"";
    if (!GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH)) {
        return L".";
    }
    std::filesystem::path directory =
        std::filesystem::path(profile) / L"Pictures" / L"ModernUI Annotation";
    return directory.wstring();
}

bool copyToClipboard(const uint8_t* pixels, const BITMAPINFOHEADER& header,
                     int32_t width, int32_t height) {
    const size_t pixelBytes = static_cast<size_t>(width) * height * 4u;
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE,
                                 sizeof(BITMAPINFOHEADER) + pixelBytes);
    if (!handle) return false;

    auto* memory = static_cast<uint8_t*>(GlobalLock(handle));
    if (!memory) {
        GlobalFree(handle);
        return false;
    }

    BITMAPINFOHEADER dibHeader = header;
    dibHeader.biHeight = height;           // 剪贴板 DIB 使用 bottom-up
    dibHeader.biSizeImage = static_cast<DWORD>(pixelBytes);
    std::memcpy(memory, &dibHeader, sizeof(dibHeader));

    uint8_t* destination = memory + sizeof(BITMAPINFOHEADER);
    for (int32_t y = 0; y < height; ++y) {
        std::memcpy(destination +
                        static_cast<size_t>(height - 1 - y) * width * 4u,
                    pixels + static_cast<size_t>(y) * width * 4u,
                    static_cast<size_t>(width) * 4u);
    }
    GlobalUnlock(handle);

    bool copied = false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            if (SetClipboardData(CF_DIB, handle) != nullptr) {
                copied = true;
            }
            CloseClipboard();
            if (copied) break;
        }
        Sleep(10);
    }

    if (!copied) GlobalFree(handle);
    return copied;
}

} // namespace

CapturedPixels captureScreenRegionRgb(
    int32_t screenX, int32_t screenY, int32_t width, int32_t height) {
    CapturedPixels result;
    width = std::max(1, width);
    height = std::max(1, height);

    HDC screen = GetDC(nullptr);
    if (!screen) {
        result.error = "GetDC failed";
        return result;
    }

    HDC memory = CreateCompatibleDC(screen);
    if (!memory) {
        ReleaseDC(nullptr, screen);
        result.error = "CreateCompatibleDC failed";
        return result;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;   // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits,
                                   nullptr, 0);
    if (!dib || !bits) {
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        result.error = "CreateDIBSection failed";
        return result;
    }

    HGDIOBJ previous = SelectObject(memory, dib);
    const DWORD rop = SRCCOPY | CAPTUREBLT;
    if (!BitBlt(memory, 0, 0, width, height, screen, screenX, screenY, rop)) {
        BitBlt(memory, 0, 0, width, height, screen, screenX, screenY, SRCCOPY);
    }

    const uint8_t* pixels = static_cast<const uint8_t*>(bits);
    result.rgb.resize(static_cast<size_t>(width) * height * 3u);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        // DIB 为 BGRA → packed RGB
        result.rgb[i * 3u + 0u] = pixels[i * 4u + 2u];
        result.rgb[i * 3u + 1u] = pixels[i * 4u + 1u];
        result.rgb[i * 3u + 2u] = pixels[i * 4u + 0u];
    }
    result.width = width;
    result.height = height;
    result.ok = true;

    SelectObject(memory, previous);
    DeleteObject(dib);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return result;
}

bool copyTextToClipboard(const std::string& utf8Text) {
    if (utf8Text.empty()) return false;

    const int length = MultiByteToWideChar(
        CP_UTF8, 0, utf8Text.data(), static_cast<int>(utf8Text.size()),
        nullptr, 0);
    if (length <= 0) return false;

    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Text.data(),
                        static_cast<int>(utf8Text.size()),
                        wide.data(), length);

    HGLOBAL handle = GlobalAlloc(
        GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
    if (!handle) return false;

    auto* memory = static_cast<wchar_t*>(GlobalLock(handle));
    if (!memory) {
        GlobalFree(handle);
        return false;
    }
    std::wmemcpy(memory, wide.c_str(), wide.size() + 1);
    GlobalUnlock(handle);

    bool copied = false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            if (SetClipboardData(CF_UNICODETEXT, handle) != nullptr) {
                copied = true;
            }
            CloseClipboard();
            if (copied) break;
        }
        Sleep(10);
    }

    if (!copied) GlobalFree(handle);
    return copied;
}

CaptureResult captureRegionToClipboardAndPng(
    int32_t screenX, int32_t screenY, int32_t width, int32_t height,
    int32_t inkOffsetX, int32_t inkOffsetY,
    const BLImage* inkImage) {
    CaptureResult result;
    width = std::max(1, width);
    height = std::max(1, height);

    HDC screen = GetDC(nullptr);
    if (!screen) {
        result.error = "GetDC failed";
        return result;
    }

    HDC memory = CreateCompatibleDC(screen);
    if (!memory) {
        ReleaseDC(nullptr, screen);
        result.error = "CreateCompatibleDC failed";
        return result;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;   // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits,
                                   nullptr, 0);
    if (!dib || !bits) {
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        result.error = "CreateDIBSection failed";
        return result;
    }

    HGDIOBJ previous = SelectObject(memory, dib);
    const DWORD rop = SRCCOPY | CAPTUREBLT;
    if (!BitBlt(memory, 0, 0, width, height, screen, screenX, screenY, rop)) {
        BitBlt(memory, 0, 0, width, height, screen, screenX, screenY, SRCCOPY);
    }

    uint8_t* pixels = static_cast<uint8_t*>(bits);
    for (int32_t i = 0; i < width * height; ++i) {
        pixels[static_cast<size_t>(i) * 4u + 3u] = 255;
    }

    // 把当前页笔迹 Bitmap 合成到截图上（预乘 alpha 源 + 不透明目标）
    if (inkImage) {
        BLImageData inkData{};
        if (inkImage->get_data(&inkData) == BL_SUCCESS && inkData.pixel_data) {
            const uint8_t* ink =
                static_cast<const uint8_t*>(inkData.pixel_data);
            for (int32_t y = 0; y < height; ++y) {
                const int32_t iy = inkOffsetY + y;
                if (iy < 0 || iy >= static_cast<int32_t>(inkData.size.h)) continue;

                uint8_t* dstRow = pixels + static_cast<size_t>(y) * width * 4u;
                const uint8_t* srcRow = ink +
                    static_cast<ptrdiff_t>(iy) * inkData.stride;

                for (int32_t x = 0; x < width; ++x) {
                    const int32_t ix = inkOffsetX + x;
                    if (ix < 0 || ix >= static_cast<int32_t>(inkData.size.w)) continue;

                    const uint8_t* src = srcRow + static_cast<size_t>(ix) * 4u;
                    const uint8_t alpha = src[3];
                    if (alpha == 0) continue;

                    uint8_t* dst = dstRow + static_cast<size_t>(x) * 4u;
                    const int32_t inverse = 255 - static_cast<int32_t>(alpha);
                    for (int32_t channel = 0; channel < 3; ++channel) {
                        const int32_t value =
                            static_cast<int32_t>(src[channel]) +
                            (static_cast<int32_t>(dst[channel]) * inverse + 127) / 255;
                        dst[channel] = static_cast<uint8_t>(std::min(255, value));
                    }
                    dst[3] = 255;
                }
            }
        }
    }

    BITMAPINFOHEADER header = info.bmiHeader;

    result.copiedToClipboard = copyToClipboard(pixels, header, width, height);

    ensureGdiplus();
    if (gdiplusReady) {
        CLSID pngClsid{};
        if (encoderClsid(L"image/png", &pngClsid) >= 0) {
            Gdiplus::Bitmap bitmap(width, height, width * 4,
                                   PixelFormat32bppARGB,
                                   static_cast<BYTE*>(bits));
            const std::wstring directory = picturesDirectory();
            std::filesystem::create_directories(directory);
            const std::wstring path = directory + L"\\" + timestampName();

            if (bitmap.Save(path.c_str(), &pngClsid, nullptr) == Gdiplus::Ok) {
                result.savedToFile = true;
                result.savedPath = path;
            }
        }
    }

    SelectObject(memory, previous);
    DeleteObject(dib);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    result.ok = result.copiedToClipboard || result.savedToFile;
    if (!result.ok) {
        result.error = "clipboard and PNG save both failed";
    }
    return result;
}

} // namespace annotation
