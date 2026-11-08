// ============================================================================
// Snapshot.cpp
// ============================================================================

#include "Snapshot.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <vector>

namespace camerapro {

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
                  L"CameraPro_%04d%02d%02d_%02d%02d%02d.png",
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
        std::filesystem::path(profile) / L"Pictures" / L"CameraPro";
    return directory.wstring();
}

bool copyToClipboard(const uint8_t* pixels, int32_t width, int32_t height) {
    const size_t pixelBytes = static_cast<size_t>(width) * height * 4u;
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE,
                                 sizeof(BITMAPINFOHEADER) + pixelBytes);
    if (!handle) return false;

    auto* memory = static_cast<uint8_t*>(GlobalLock(handle));
    if (!memory) {
        GlobalFree(handle);
        return false;
    }

    BITMAPINFOHEADER header{};
    header.biSize = sizeof(BITMAPINFOHEADER);
    header.biWidth = width;
    header.biHeight = height;              // bottom-up
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;
    header.biSizeImage = static_cast<DWORD>(pixelBytes);
    std::memcpy(memory, &header, sizeof(header));

    uint8_t* destination = memory + sizeof(BITMAPINFOHEADER);
    for (int32_t y = 0; y < height; ++y) {
        std::memcpy(destination + static_cast<size_t>(height - 1 - y) *
                                   static_cast<size_t>(width) * 4u,
                    pixels + static_cast<size_t>(y) * width * 4u,
                    static_cast<size_t>(width) * 4u);
    }
    GlobalUnlock(handle);

    bool copied = false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            if (SetClipboardData(CF_DIB, handle) != nullptr) copied = true;
            CloseClipboard();
            if (copied) break;
        }
        Sleep(10);
    }

    if (!copied) GlobalFree(handle);
    return copied;
}

} // namespace

SnapshotResult saveBgraToClipboardAndPng(int32_t width, int32_t height,
                                         const void* bgra) {
    SnapshotResult result;
    width = std::max(1, width);
    height = std::max(1, height);
    if (!bgra) {
        result.error = "empty snapshot";
        return result;
    }

    const auto* pixels = static_cast<const uint8_t*>(bgra);
    result.copiedToClipboard = copyToClipboard(pixels, width, height);

    ensureGdiplus();
    if (gdiplusReady) {
        CLSID pngClsid{};
        if (encoderClsid(L"image/png", &pngClsid) >= 0) {
            Gdiplus::Bitmap bitmap(width, height, width * 4,
                                   PixelFormat32bppARGB,
                                   const_cast<BYTE*>(pixels));
            const std::wstring directory = picturesDirectory();
            std::filesystem::create_directories(directory);
            const std::wstring path = directory + L"\\" + timestampName();

            if (bitmap.Save(path.c_str(), &pngClsid, nullptr) == Gdiplus::Ok) {
                result.savedToFile = true;
                result.savedPath = path;
            }
        }
    }

    result.ok = result.copiedToClipboard || result.savedToFile;
    if (!result.ok) result.error = "clipboard and PNG save both failed";
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

} // namespace camerapro
