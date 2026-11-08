// ============================================================================
// Blend2DRenderer.h — Canvas 的 Blend2D 软件渲染后端
// ============================================================================
//
// 渲染目标：Win32LayeredWindow 的 32bpp PRGB32 DIB（逐像素 alpha）。
// 每帧流程：
//   beginFrame  → 绑定/重建 BLImage（窗口 resize 后）
//   renderTile  → 清 Tile 背景 + 按 DirtyShapeRef 逐 Shape 绘制
//   endFrame    → flush + UpdateLayeredWindow
//
// 资源：
//   - 字体：BLFontFace + 按字号缓存的 BLFont（默认加载系统字体）
//   - 图片：文件/内存解码 → handle 注册表（ImageShape.handle）
//   - SVG：nanosvg 解析 → BLPath + BLGradient 矢量缓存（按内容哈希）
//   - 渐变：Canvas Paint → BLGradient（线性 / 径向 / spread）
//   - Blur（BlurShape）：从当前表面回读底图 → 分离高斯卷积（SIMD）→
//     按"目标区域 + 底层内容版本"缓存；底层内容变化时只重建一次
//
// 文本：
//   - 字形缓存：文本 shaping 结果 + A8 栅格蒙版（整行 / 单字形）双层缓存
//   - 度量与绘制都只做一次 shaping / 光栅化；之后每帧仅一次 fill_mask
//   - 蒙版与颜色无关，纯色 / 渐变共用；超长文本降级为逐字形蒙版
//   - LRU + 字节预算，避免长跑进程内存膨胀
// ============================================================================

#pragma once

#include <blend2d/blend2d.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "Canvas.h"

namespace modern_ui {

class Win32LayeredWindow;

class Blend2DRenderer final : public IRenderer {
public:
    explicit Blend2DRenderer(Win32LayeredWindow* window = nullptr);
    ~Blend2DRenderer() override;

    Blend2DRenderer(const Blend2DRenderer&) = delete;
    Blend2DRenderer& operator=(const Blend2DRenderer&) = delete;

    // --- 窗口 / Surface ---
    void attachWindow(Win32LayeredWindow* window) { window_ = window; }
    Win32LayeredWindow* window() const { return window_; }

    // --- 字体 ---
    bool loadFontFile(const std::string& path, uint32_t faceIndex = 0, std::string* error = nullptr);
    bool loadDefaultFont(std::string* error = nullptr);
    bool hasFont() const { return fontReady_; }

    // --- 图片资源注册表 ---
    uint64_t loadImageFromFile(const std::string& path, std::string* error = nullptr);
    uint64_t loadImageFromMemory(const void* data, size_t size, std::string* error = nullptr);
    // 直接从 PRGB32（BGRA / 预乘 alpha / top-down）像素创建纹理；stride=0 表示 width*4。
    uint64_t createImageFromPixels(int32_t width, int32_t height, const void* pixels,
                                   int32_t stride = 0, std::string* error = nullptr);

    // --- 可写位图（笔迹 Bitmap 缓存）---
    // 创建一张 PRGB32 BLImage 并注册为后端纹理；返回句柄。
    // 调用方可通过 image(handle) 拿到 BLImage*，用 BLContext 直接增量绘制，
    // 随后只需对 Canvas 做 markDirty(区域) —— 纹理内容与绘制共用同一块内存。
    uint64_t createBitmapImage(int32_t width, int32_t height,
                               Color clear = Color{0, 0, 0, 0});
    BLImage* image(uint64_t handle);
    const BLImage* image(uint64_t handle) const;

    bool imageSize(uint64_t handle, int32_t& width, int32_t& height) const override;
    bool releaseImage(uint64_t handle);
    void clearImages();

    // --- IRenderer 可选资源接口（Canvas::imageFile / svgFile 的底层实现）---
    // 同路径重复加载命中缓存，只解码一次。
    uint64_t loadImageFile(const std::string& path, std::string* error = nullptr) override;
    // 读取 SVG 文件原文（解析结果交给 svgCache_ 按内容哈希缓存）
    bool loadSvgFile(const std::string& path, std::string& out,
                     std::string* error = nullptr) override;

    // --- SVG 缓存 ---
    // 解析结果按 SVG 文本内容哈希缓存；布局尺寸变化不会触发重新解析。
    void clearSvgCache();
    size_t svgCacheSize() const { return svgCache_.size(); }

    // --- 字形缓存 ---
    // 每个 (字体, 字号, 文本) 只 shaping / 光栅化一次，之后按 A8 蒙版填充；
    // 蒙版与颜色无关，色值动画 / 渐变文字也会命中同一份蒙版。
    struct GlyphCacheStats {
        size_t   textEntries   = 0;   // 缓存的 shaping 结果数量
        size_t   maskEntries   = 0;   // 缓存的 A8 蒙版数量（整行 + 单字形）
        size_t   maskBytes     = 0;   // A8 蒙版内存估算（字节）
        uint64_t textHits      = 0;   // shaping 缓存命中次数
        uint64_t textMisses    = 0;   // shaping 缓存未命中次数
        uint64_t rasterizations = 0;  // 实际执行的轮廓光栅化次数
    };
    void clearGlyphCache();
    GlyphCacheStats glyphCacheStats() const;

    // --- Blur 缓存 ---
    // 每个 Blur 几何体缓存一份"目标区域的高斯模糊结果"：
    //   - 命中条件：目标区域 / σ / 圆角一致，且底层内容签名（背景色 +
    //     该区域内 z 更低的几何体 id / revision / 包围盒）未变化；
    //   - 未命中时回读表面像素并重新卷积（SIMD 分离高斯）。
    struct BlurCacheStats {
        size_t   entries = 0;   // 缓存的模糊结果数量
        uint64_t hits    = 0;   // 命中次数
        uint64_t misses  = 0;   // 重建次数
    };
    void clearBlurCache();
    BlurCacheStats blurCacheStats() const;

    // --- IRenderer ---
    void beginFrame(const CanvasInfo& info) override;
    void endFrame() override;
    void renderTile(const CanvasInfo& info, const DirtyEntry& entry) override;
    std::optional<Rect> measureText(const TextShape& text) override;

private:
    // SVG 解析缓存（结构定义在 .cpp，避免把 nanosvg / BLPath 细节暴露到公共头）
    struct SvgDocument;
    static constexpr size_t kSvgCacheCapacity = 128;

    // 字形缓存（结构定义在 .cpp；内部持有 BLGlyphBuffer / BLImage）
    struct GlyphCache;
    GlyphCache& glyphCache();

    // Blur 缓存（结构定义在 .cpp；内部持有 BLImage、卷积 scratch 与统计）
    struct BlurCache;
    BlurCache& blurCache();

    bool syncSurface();
    const BLFont* fontForSize(int32_t pixelSize);
    void drawShape(const Shape& shape);
    void drawText(const TextShape& text, const Paint& paint);
    void drawSvg(const SvgShape& svg);
    // Blur：clip 为表面绝对坐标下的实际绘制区域；offset 为 Shape 局部 → 绝对偏移
    void drawBlur(const Shape& shape, const Vec2i& offset, const Rect& clip,
                  float opacity, uint64_t signature);

    // 把 Canvas Paint 转成 Blend2D 的 solid / gradient 画刷
    bool fillPath(const Paint& paint, const BLPath& path);
    bool strokePath(const Paint& paint, const BLPath& path, int32_t strokeWidth);

    // SVG 解析 + LRU 缓存
    static SvgDocument parseSvg(const std::string& markup);
    const SvgDocument* svgDocument(const SvgShape& svg);

    Win32LayeredWindow* window_ = nullptr;

    BLImage   surface_;
    BLContext ctx_;
    uint64_t  surfaceSerial_ = ~uint64_t(0);
    int32_t   surfaceWidth_ = 0;
    int32_t   surfaceHeight_ = 0;

    bool       frameActive_ = false;
    bool       frameDirty_ = false;
    Rect       frameDirtyRect_{};      // 本帧实际重绘区域（用于增量提交窗口）

    BLFontFace fontFace_;
    bool       fontReady_ = false;
    uint32_t   fontGeneration_ = 0;   // 字体每次重载 +1，用于让字形缓存失效
    std::unordered_map<int32_t, BLFont> fonts_;

    std::unordered_map<uint64_t, BLImage> images_;
    uint64_t nextImageHandle_ = 1;
    std::unordered_map<std::string, uint64_t> imagePathCache_;   // 路径 → 已解码句柄
    std::unordered_map<std::string, std::string> svgPathCache_;  // 路径 → SVG 原文

    std::unordered_map<uint64_t, std::unique_ptr<SvgDocument>> svgCache_;
    uint64_t svgCacheTick_ = 0;

    std::unique_ptr<GlyphCache> glyphCache_;

    std::unique_ptr<BlurCache> blurCache_;
};

} // namespace modern_ui
