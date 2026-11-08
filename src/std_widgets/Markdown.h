// ============================================================================
// Markdown.h — 可复用 Markdown 渲染控件（std_widgets）
// ============================================================================
//
// 独立的纯 Canvas 控件：解析 Markdown → 排版成文本行 → 写入控件子 Canvas。
// 不依赖任何具体应用，可直接被其它项目 emplaceChild<MarkdownView>() 使用。
//
// 支持：
//   - 标题（# / ## … / Setext）、段落、自动换行（中英文 / 标点断行）
//   - 无序 / 有序 / 嵌套 / 任务列表
//   - 引用块、分割线、围栏代码块、表格（含对齐与表头）
//   - 行内样式：粗体 / 斜体 / 删除线 / 代码片段 / 链接 / 图片占位
//   - 滚轮 / PageUp / PageDown 滚动 + 右侧滚动条
//
// 用法：
//   auto* md = view.emplaceChild<modern_ui::fluent::MarkdownView>();
//   md->grow(1);
//   md->setMarkdown("# 标题\n\n正文 **加粗** …");
//
// ============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Canvas.h"
#include "Widget.h"

namespace modern_ui {
namespace fluent {

// 控件配色 / 字号 / 间距。默认适配深色背景。
struct MarkdownTheme {
    Color text{228, 234, 244, 255};
    Color textMuted{158, 168, 186, 255};
    Color accent{96, 205, 255, 255};
    Color link{96, 190, 255, 255};
    Color background{0, 0, 0, 0};
    Color codeBackground{255, 255, 255, 18};
    Color codeBorder{255, 255, 255, 34};
    Color tableBorder{255, 255, 255, 30};
    Color tableHeader{255, 255, 255, 20};
    Color divider{255, 255, 255, 28};
    Color quoteBar{96, 205, 255, 150};
    Color scrollbarTrack{255, 255, 255, 20};
    Color scrollbarThumb{148, 158, 178, 160};

    int32_t baseFontSize = 14;
    int32_t codeFontSize = 13;
    int32_t padding = 14;
    int32_t lineHeight = 21;
};

// 内部排版数据结构（公开仅为让实现与测试复用；普通调用方无需使用）。
namespace markdown_detail {

struct InlineRun {
    std::string text;
    bool bold = false;
    bool italic = false;
    bool code = false;
    bool strike = false;
    bool link = false;
};

struct Segment {
    std::string text;
    bool bold = false;
    bool italic = false;
    bool code = false;
    bool strike = false;
    bool link = false;
    int32_t fontSize = 14;
    int32_t x = 0;
    int32_t width = 0;
};

struct TextLine {
    std::vector<Segment> segments;
    int32_t height = 0;
    int32_t width = 0;
};

struct TableCell {
    std::vector<InlineRun> runs;
    std::vector<TextLine> lines;
};

struct Block {
    enum class Kind : uint8_t {
        Paragraph = 0,
        Heading,
        ListItem,
        Quote,
        Code,
        Table,
        Divider,
        Empty
    };

    Kind kind = Kind::Paragraph;
    int32_t level = 0;          // 标题级别 / 列表嵌套层级
    bool ordered = false;
    int32_t number = 1;
    bool hasTask = false;
    bool checked = false;
    std::vector<InlineRun> runs;
    std::vector<std::string> codeLines;
    std::vector<std::vector<TableCell>> tableRows;
    std::vector<int32_t> tableAlign;   // 0 左 / 1 中 / 2 右
    std::vector<int32_t> cellWidths;   // 表格列宽（含单元格内边距）
    std::vector<int32_t> rowHeights;
    int32_t tableWidth = 0;
    std::vector<TextLine> lines;
    int32_t gapBefore = 0;
    int32_t height = 0;
    int32_t y = 0;
};

} // namespace markdown_detail

class MarkdownView : public Widget {
public:
    MarkdownView();
    ~MarkdownView() override;

    // 设置 Markdown 源文本；修改后自动重新解析 / 排版。
    void setMarkdown(std::string markdown);
    const std::string& markdown() const { return source_; }

    // 去掉标记后的纯文本（可直接用于复制 / 搜索）。
    std::string plainText() const;

    void setTheme(const MarkdownTheme& theme);
    const MarkdownTheme& markdownTheme() const { return theme_; }

    // 控件所有几何体使用的 z 基准值：内容 = layer+2，滚动条 = layer+3。
    // 宿主可用它把 Markdown 内容插到自己的背景 / 标题栏之间。
    void setLayer(int32_t layer);
    int32_t layer() const { return layer_; }

    void scrollBy(int32_t delta);
    void scrollTo(int32_t offset);
    int32_t scrollOffset() const { return scroll_; }
    int32_t maxScroll() const;
    int32_t contentHeight() const { return contentHeight_; }

    // ---- Widget ----
    Vec2i onMeasure(const Vec2i& available) const override;
    void onLayout(const Rect& frame) override;
    bool onEvent(const Event& ev) override;
    bool acceptsFocus() const override { return true; }

private:
    using InlineRun = markdown_detail::InlineRun;
    using Segment = markdown_detail::Segment;
    using TextLine = markdown_detail::TextLine;
    using TableCell = markdown_detail::TableCell;
    using Block = markdown_detail::Block;

    void parseMarkdown();
    void layoutBlocks();
    void rebuildVisuals();
    void applyScroll();

    std::vector<TextLine> layoutText(const std::vector<InlineRun>& runs,
                                     int32_t fontSize, int32_t lineHeight,
                                     int32_t maxWidth, bool trimLines) const;
    void layoutTable(Block& block, int32_t contentWidth) const;
    int32_t headingFontSize(int32_t level) const;
    int32_t headingLineHeight(int32_t level) const;
    int32_t gapBeforeFor(const Block& block, const Block* previous) const;

    std::string plain_;
    std::string source_;
    std::vector<Block> blocks_;

    MarkdownTheme theme_;
    int32_t layer_ = 0;
    int32_t contentHeight_ = 0;
    int32_t scroll_ = 0;
    int32_t layoutWidth_ = -1;
    int32_t layoutHeight_ = -1;
    int32_t layoutPadding_ = -1;
    bool parsed_ = false;

    uint32_t backgroundId_ = 0;
    uint32_t contentGroupId_ = 0;
    uint32_t scrollTrackId_ = 0;
    uint32_t scrollThumbId_ = 0;
};

} // namespace fluent
} // namespace modern_ui
