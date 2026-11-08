// ============================================================================
// AiCards.h — AI 教学卡片（XML 协议 + 场景图渲染 + 展开 / 滚动）
// ============================================================================
//
// 数据流：DeepSeek 返回 <cards> XML → parseAiCardsXml() → AiCardSpec 列表
//        → AiCardLayer 在根 Canvas 上创建卡片（默认只显示标题栏）。
//
// 卡片交互：
//   - 拖动卡片任意位置：移动位置（松开后保持为屏幕比例位置）
//   - 点击卡片：展开 / 收起（展开时正文可滚动）
//   - 滚轮：滚动正文；卡片区域内的滚轮不会泄漏给下层应用（配合 Hook）
//   - 标题右侧 ×：关闭该卡片
//   - 标题图标：AI 通过 icon 属性选择 icons/ 下的语义图标
// ============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Canvas.h"

namespace annotation {

struct AiCardBlock {
    enum class Kind : uint8_t { Heading, Paragraph, Bullet, Note };
    Kind kind = Kind::Paragraph;
    std::string text;
};

struct AiCardSpec {
    std::string title;
    std::string icon;   // book / explain / inspiration / question / tips / roadmap
    std::vector<AiCardBlock> blocks;

    float x = 0.62f;        // 卡片左上角：屏幕横向比例 [0, 1]
    float y = 0.18f;        // 卡片左上角：屏幕纵向比例 [0, 1]
    int32_t width = 0;      // 逻辑像素（0 = 默认宽度）
    int32_t height = 0;     // 逻辑像素（0 = 按内容自适应）
    modern_ui::Color accent{96, 205, 255, 255};
    modern_ui::Color background{18, 21, 30, 244};
    modern_ui::Color text{233, 238, 248, 255};
};

// 解析模型返回的 XML（容忍 Markdown 代码块 / 注释 / 未知标签）。
// 位置支持百分比与像素两种写法，内部统一换算成屏幕比例 / 像素。
std::vector<AiCardSpec> parseAiCardsXml(const std::string& xml,
                                        int32_t screenWidth,
                                        int32_t screenHeight);

// 发给模型的系统提示词（包含 XML 协议与图标列表）。
const char* aiCardSystemPrompt();

// ============================================================================
// 屏幕批注（AI 划线 / 高亮 / 圈选 / 箭头）
// ============================================================================

struct AiMarkSpec {
    enum class Kind : uint8_t { Highlight, Underline, Circle, Box, Arrow, Line };
    Kind kind = Kind::Highlight;
    std::vector<modern_ui::Rect> rects;   // Highlight / Underline / Circle / Box
    modern_ui::Vec2f from{};
    modern_ui::Vec2f to{};
    modern_ui::Color color{255, 214, 64, 235};
    int32_t strokeWidth = 0;             // 0 = 默认
};

// lines="3,4" / "5-7" 引用 OCR 行（1 基），ocrLines 为逐行像素包围盒。
std::vector<AiMarkSpec> parseAiMarksXml(const std::string& xml,
                                        int32_t screenWidth,
                                        int32_t screenHeight,
                                        const std::vector<modern_ui::Rect>& ocrLines);

class AiAnnotationLayer {
public:
    void initialize(modern_ui::Canvas* canvas);
    void setMarks(std::vector<AiMarkSpec> marks);
    void clear();
    bool hasMarks() const { return !marks_.empty(); }

    // 逐帧推进"手绘"出现动画；全部完成后为空操作。
    void tick(float dtSeconds);

    // 橡皮擦命中批注：删除被擦到的整条批注。返回 true 表示有批注被擦除。
    bool eraseAt(const modern_ui::Vec2f& point, float radius);

private:
    struct Mark {
        AiMarkSpec spec;
        std::vector<uint32_t> ids;
        float age = 0.0f;
        float delay = 0.0f;
        bool settled = false;
    };

    void buildMark(Mark& mark, std::size_t index);
    void applyProgress(Mark& mark, float progress);

    modern_ui::Canvas* canvas_ = nullptr;
    std::vector<Mark> marks_;
    float scale_ = 1.0f;
};

class AiCardLayer {
public:
    void initialize(modern_ui::Canvas* canvas, const std::string& iconDirectory);
    void setCards(std::vector<AiCardSpec> specs);
    // 用户缩放系数（1.0 = 100%）；已有卡片会按新尺寸重新排版。
    void setUserScale(float factor);
    float userScale() const { return userScale_; }
    void clear();
    bool hasCards() const { return !cards_.empty(); }

    bool hitTest(const modern_ui::Vec2i& point) const;
    // 卡片交互：按下 → 拖动 → 松开。
    // 拖动超过阈值时移动卡片；未拖动则按点击处理（展开 / 收起 / 关闭）。
    bool handlePress(const modern_ui::Vec2i& point);
    bool handleDrag(const modern_ui::Vec2i& point);
    bool handleRelease(const modern_ui::Vec2i& point);
    bool handleWheel(const modern_ui::Vec2i& point, float wheelDelta);
    void updateHover(const modern_ui::Vec2i& point);

    // 所有卡片当前可见区域的并集（客户区坐标）。
    modern_ui::Rect bounds() const;
    // 每张卡片当前可见区域（客户区坐标），用于 Hook 精确吞掉卡片上的左键。
    std::vector<modern_ui::Rect> frames() const;

private:
    struct LayoutBlock {
        AiCardBlock::Kind kind = AiCardBlock::Kind::Paragraph;
        std::vector<std::string> lines;
        int32_t fontSize = 14;
        int32_t lineHeight = 20;
        int32_t gapBefore = 0;
        int32_t indent = 0;
        int32_t boxPadding = 0;
        int32_t wrapWidth = 0;
        int32_t height = 0;
    };

    struct Card {
        AiCardSpec spec;
        std::vector<LayoutBlock> layout;
        int32_t width = 0;
        int32_t headerHeight = 0;
        int32_t height = 0;            // 当前显示高度
        int32_t expandedHeight = 0;
        int32_t contentHeight = 0;
        int32_t viewportHeight = 0;
        int32_t contentTop = 0;
        int32_t scroll = 0;
        bool expanded = false;
        bool hovered = false;
        modern_ui::Rect frame{};
        uint32_t groupId = 0;
        uint32_t contentGroupId = 0;
        uint32_t trackId = 0;
        uint32_t thumbId = 0;
    };

    void layoutCard(Card& card);
    void buildCard(Card& card, int32_t index);
    void destroyCard(Card& card);
    void updateScroll(Card& card);
    void removeCard(int32_t index);
    void refreshScale();
    modern_ui::Rect closeButtonRect(const Card& card) const;
    void commitCardPosition(Card& card);

    int32_t padding() const;
    int32_t scaleInt(float value) const;
    std::string iconMarkupFor(const std::string& icon,
                              const modern_ui::Color& accent) const;

    modern_ui::Canvas* canvas_ = nullptr;
    std::string iconDirectory_;
    std::vector<Card> cards_;
    float scale_ = 1.0f;
    float userScale_ = 1.0f;
    int32_t hoverIndex_ = -1;

    // 拖动状态
    int32_t dragIndex_ = -1;
    modern_ui::Vec2i dragStartPoint_{};
    modern_ui::Rect dragStartFrame_{};
    bool dragMoved_ = false;
    bool dragOnClose_ = false;
    bool dragBuilt_ = false;
    uint64_t lastDragBuildMs_ = 0;

    mutable std::vector<std::pair<std::string, std::string>> iconCache_;
};

} // namespace annotation
