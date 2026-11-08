// ============================================================================
// InkEngine.h — 批注笔迹引擎
// ============================================================================
//
// 职责：
//   1. 原始点 → EMA 时间平滑（可调时间常数）；
//   2. 平滑点 → Catmull-Rom / 三次 Bezier 采样；
//   3. 速度 → 模拟压力 → 变宽笔迹轮廓（含起笔/收笔 taper 笔锋）；
//   4. 完成笔迹光栅化进 PRGB32 Bitmap（每页一张，由 Blend2DRenderer 注册）；
//   5. 像素橡皮（DST_OUT 直接擦除 Bitmap）；
//   6. 撤销 / 重做（按笔迹包围盒保存 Bitmap 前后快照，天然保留擦除结果）；
//   7. 多页批注（每页独立 Bitmap + 历史）。
//
// 坐标：全部为覆盖层客户区像素坐标（浮点）。
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Canvas.h"

namespace modern_ui {
class Blend2DRenderer;
}

namespace annotation {

struct InkStyle {
    modern_ui::Color color{255, 255, 255, 255};
    float width = 6.0f;        // 基础笔宽（像素）
    float alpha = 1.0f;        // 额外不透明度（荧光笔在引擎内再乘 0.34）
    bool  highlighter = false; // 荧光笔：更宽、半透明
};

struct InkParameters {
    float smoothingTau = 0.020f; // EMA 时间常数（秒）；0 = 关闭
    float penTip = 0.65f;        // 速度对笔宽的影响强度 0..1
    float taper = 0.30f;         // 起笔/收笔锥化比例 0..1
    float speedScale = 1400.0f;  // 笔宽降到最细对应的书写速度 px/s
};

class InkEngine {
public:
    void initialize(modern_ui::Blend2DRenderer* renderer, int32_t width, int32_t height);
    bool ready() const { return renderer_ != nullptr && width_ > 0 && height_ > 0; }

    int32_t width() const { return width_; }
    int32_t height() const { return height_; }
    modern_ui::Blend2DRenderer* renderer() const { return renderer_; }

    void setParameters(const InkParameters& parameters) { parameters_ = parameters; }
    const InkParameters& parameters() const { return parameters_; }

    void setStyle(const InkStyle& style) { style_ = style; }
    const InkStyle& style() const { return style_; }

    // -------------------- 多页 --------------------
    int32_t pageCount() const { return static_cast<int32_t>(pages_.size()); }
    int32_t pageIndex() const { return pageIndex_; }
    bool addPage();
    bool setPage(int32_t index);
    void clearPage();
    // 当前页 Bitmap 句柄（必要时创建）
    uint64_t currentImageHandle();
    // 当前页已存在的 Bitmap 句柄；不创建，未分配时返回 0。
    uint64_t existingImageHandle() const;
    // 扩充页数到 count（不切换当前页）；返回是否成功。
    bool ensurePageCount(int32_t count);

    // -------------------- 笔迹 --------------------
    bool strokeActive() const { return strokeActive_; }
    void beginStroke(const modern_ui::Vec2f& point, double timeSeconds);
    void addPoint(const modern_ui::Vec2f& point, double timeSeconds);
    // 提交笔迹并返回需要重绘的区域；无有效笔迹返回 false。
    bool endStroke(modern_ui::Rect* dirtyRect);
    void cancelStroke();

    bool liveDirty() const { return liveDirty_; }
    void consumeLiveDirty() { liveDirty_ = false; }
    bool hasLiveStroke() const { return strokeActive_ && !points_.empty(); }
    std::size_t livePointCount() const { return points_.size(); }
    // 实时笔迹的矢量 Shape（荧光笔等整条轮廓）
    modern_ui::Shape buildLiveShape() const;
    // 钢笔实时笔迹分块：只重建最新块，避免每帧处理整条长笔迹。
    bool buildLiveOutline(std::size_t begin, std::size_t end,
                          bool applyStartTaper, bool applyEndTaper,
                          std::vector<modern_ui::Vec2f>& outline,
                          modern_ui::Rect* bounds) const;
    modern_ui::Color liveFillColor() const { return fillColor(); }

    // -------------------- 像素橡皮 --------------------
    bool erasing() const { return erasing_; }
    void beginErase(const modern_ui::Vec2f& point, float radius,
                    modern_ui::Rect* dirtyRect = nullptr);
    void eraseTo(const modern_ui::Vec2f& point, float radius, modern_ui::Rect* dirtyRect);
    void endErase();

    // -------------------- 撤销 --------------------
    bool canUndo() const;
    bool undo(modern_ui::Rect* dirtyRect);
    // 擦除会改变 Bitmap 且不可撤销：清空当前页历史，之后的笔迹重新开始记录。
    void invalidateHistory();

    // 当前笔宽（不含压力变化），用于光标环
    float currentBaseWidth() const;

private:
    struct InkPoint {
        modern_ui::Vec2f pos;
        float pressure = 1.0f;
        float time = 0.0f;
    };

    struct PixelSnapshot {
        modern_ui::Rect rect;
        std::vector<uint8_t> bytes;
    };

    struct HistoryEntry {
        PixelSnapshot before;
        PixelSnapshot after;
    };

    struct Page {
        uint64_t imageHandle = 0;
        std::size_t historyPos = 0;
        std::vector<HistoryEntry> history;
        int32_t width = 0;
        int32_t height = 0;
    };

    bool ensurePageImage(Page& page);
    bool ensureCurrentImage();

    bool buildOutline(std::vector<modern_ui::Vec2f>& outline,
                      modern_ui::Rect* bounds) const;
    bool buildOutlineRange(std::size_t begin, std::size_t end,
                           bool applyStartTaper, bool applyEndTaper,
                           std::vector<modern_ui::Vec2f>& outline,
                           modern_ui::Rect* bounds) const;
    void rasterizeOutline(const std::vector<modern_ui::Vec2f>& outline, bool erase);
    modern_ui::Color fillColor() const;

    bool captureRegion(Page& page, const modern_ui::Rect& rect, PixelSnapshot* snapshot);
    bool restoreRegion(Page& page, const PixelSnapshot& snapshot);
    void pushHistory(Page& page, const modern_ui::Rect& bounds,
                     const PixelSnapshot& before, const PixelSnapshot& after);

    modern_ui::Blend2DRenderer* renderer_ = nullptr;
    int32_t width_ = 0;
    int32_t height_ = 0;

    InkParameters parameters_{};
    InkStyle style_{};

    std::vector<Page> pages_;
    int32_t pageIndex_ = 0;

    // 当前笔迹
    bool strokeActive_ = false;
    bool liveDirty_ = false;
    std::vector<InkPoint> points_;
    modern_ui::Vec2f rawPoint_{};
    modern_ui::Vec2f smoothedPoint_{};
    float speed_ = 0.0f;
    float pressure_ = 1.0f;
    double lastTime_ = 0.0;

    // 橡皮
    bool erasing_ = false;
    modern_ui::Vec2f lastErasePoint_{};
};

} // namespace annotation
