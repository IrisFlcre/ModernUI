// ============================================================================
// Markdown.cpp — Markdown 解析 / 排版 / 渲染
// ============================================================================

#include "Markdown.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

using modern_ui::CircleShape;
using modern_ui::Color;
using modern_ui::GroupData;
using modern_ui::LineShape;
using modern_ui::Paint;
using modern_ui::Rect;
using modern_ui::RRectShape;
using modern_ui::RectShape;
using modern_ui::Shape;
using modern_ui::TextShape;
using modern_ui::Vec2i;

namespace modern_ui {
namespace fluent {

namespace {

// Win32 虚拟键码（控件层不引入 windows.h）。
constexpr int32_t kKeyPageUp = 0x21;    // VK_PRIOR
constexpr int32_t kKeyPageDown = 0x22;  // VK_NEXT
constexpr int32_t kKeyEnd = 0x23;       // VK_END
constexpr int32_t kKeyHome = 0x24;      // VK_HOME
constexpr int32_t kKeyUp = 0x26;        // VK_UP
constexpr int32_t kKeyDown = 0x28;      // VK_DOWN

// ============================================================================
// 基础字符串 / UTF-8 工具
// ============================================================================

bool isAsciiSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string trimRight(const std::string& text) {
    std::size_t end = text.size();
    while (end > 0 && isAsciiSpace(text[end - 1])) --end;
    return text.substr(0, end);
}

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && isAsciiSpace(text[begin])) ++begin;
    while (end > begin && isAsciiSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

bool isAsciiAlnum(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z');
}

bool isAsciiDigit(char c) {
    return c >= '0' && c <= '9';
}

std::size_t utf8Step(const std::string& text, std::size_t index) {
    if (index >= text.size()) return index;
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    if (lead >= 0xF0) return std::min<std::size_t>(4, text.size() - index);
    if (lead >= 0xE0) return std::min<std::size_t>(3, text.size() - index);
    if (lead >= 0xC0) return std::min<std::size_t>(2, text.size() - index);
    return 1;
}

bool isUtf8SpaceAt(const std::string& text, std::size_t index) {
    if (index >= text.size()) return false;
    const unsigned char c = static_cast<unsigned char>(text[index]);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') return true;
    return text.compare(index, 3, "\xE3\x80\x80") == 0;   // 全角空格
}

std::vector<std::size_t> codepointOffsets(const std::string& text,
                                          std::size_t begin, std::size_t end) {
    std::vector<std::size_t> offsets;
    end = std::min(end, text.size());
    std::size_t index = begin;
    while (index < end) {
        offsets.push_back(index);
        index += utf8Step(text, index);
    }
    offsets.push_back(end);
    return offsets;
}

bool isBreakCodepoint(const std::string& text, std::size_t begin,
                      std::size_t end) {
    if (end <= begin) return false;
    const std::string codepoint = text.substr(begin, end - begin);
    static const char* const kBreaks[] = {
        " ", "\t", "-", "/", ",", ".", ";", ":", "!", "?", ")", "]", "}",
        "\xEF\xBC\x8C",  // ，
        "\xE3\x80\x82",  // 。
        "\xE3\x80\x81",  // 、
        "\xEF\xBC\x9B",  // ；
        "\xEF\xBC\x9A",  // ：
        "\xEF\xBC\x81",  // ！
        "\xEF\xBC\x9F",  // ？
        "\xEF\xBC\x89",  // ）
        "\xE3\x80\x91",  // 】
        "\xE3\x80\x8B",  // 》
    };
    for (const char* item : kBreaks) {
        if (codepoint == item) return true;
    }
    return false;
}

std::string smartJoin(const std::string& left, const std::string& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    std::string result = left;
    if (result.back() == '-' && isAsciiAlnum(right.front())) {
        result.pop_back();
        result += right;
        return result;
    }
    if (isAsciiAlnum(result.back()) && isAsciiAlnum(right.front())) {
        result += ' ';
    }
    result += right;
    return result;
}

std::string replaceAll(std::string text, const std::string& from,
                       const std::string& to) {
    if (from.empty()) return text;
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}

std::string decodeEntities(const std::string& text) {
    std::string result = text;
    result = replaceAll(std::move(result), "&nbsp;", " ");
    result = replaceAll(std::move(result), "&amp;", "&");
    result = replaceAll(std::move(result), "&lt;", "<");
    result = replaceAll(std::move(result), "&gt;", ">");
    result = replaceAll(std::move(result), "&quot;", "\"");
    result = replaceAll(std::move(result), "&#39;", "'");
    result = replaceAll(std::move(result), "&apos;", "'");
    result = replaceAll(std::move(result), "&mdash;", "—");
    result = replaceAll(std::move(result), "&ndash;", "–");
    result = replaceAll(std::move(result), "&hellip;", "…");
    return result;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t position = 0;
    while (position <= text.size()) {
        const std::size_t newline = text.find('\n', position);
        std::string line = text.substr(
            position,
            newline == std::string::npos ? std::string::npos
                                         : newline - position);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (newline == std::string::npos) break;
        position = newline + 1;
    }
    return lines;
}

bool hasVisibleText(const std::string& text) {
    for (char c : text) {
        if (c != ' ' && c != '\t') return true;
    }
    return false;
}

// ============================================================================
// 文本度量 / 折行
// ============================================================================

Rect measureTextRect(const Canvas& canvas, const std::string& text,
                     int32_t fontSize) {
    if (text.empty()) return Rect{0, 0, 0, 0};
    TextShape probe;
    probe.text = text;
    probe.fontSize = fontSize;
    canvas.measureTextShape(probe);
    if (probe.measured && !probe.measuredBounds.isNull()) {
        return probe.measuredBounds;
    }
    const int32_t advance = std::max(1, fontSize / 2);
    return Rect{0, -fontSize,
                std::max(1, static_cast<int32_t>(text.size()) * advance),
                fontSize + 4};
}

int32_t measureTextWidth(const Canvas& canvas, const std::string& text,
                         int32_t fontSize) {
    return measureTextRect(canvas, text, fontSize).w;
}

std::vector<std::pair<std::size_t, std::size_t>> wrapSpans(
    const Canvas& canvas, const std::string& text, int32_t fontSize,
    int32_t maxWidth, bool trimLines) {
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    if (text.empty() || maxWidth <= 0) return spans;

    std::size_t cursor = 0;
    while (cursor < text.size()) {
        if (trimLines) {
            while (cursor < text.size() && isUtf8SpaceAt(text, cursor)) {
                cursor += utf8Step(text, cursor);
            }
        }
        if (cursor >= text.size()) break;

        const std::string remainder = text.substr(cursor);
        if (measureTextWidth(canvas, remainder, fontSize) <= maxWidth) {
            std::size_t end = text.size();
            if (trimLines) {
                while (end > cursor && isUtf8SpaceAt(text, end - 1)) --end;
            }
            if (end > cursor) spans.push_back({cursor, end});
            break;
        }

        const std::vector<std::size_t> offsets =
            codepointOffsets(text, cursor, text.size());
        const std::size_t count = offsets.size() - 1;

        std::size_t low = 1;
        std::size_t high = count;
        std::size_t best = 1;
        while (low <= high) {
            const std::size_t middle = (low + high) / 2;
            const std::string candidate =
                text.substr(cursor, offsets[middle] - cursor);
            if (measureTextWidth(canvas, candidate, fontSize) <= maxWidth) {
                best = middle;
                low = middle + 1;
            } else {
                if (middle == 1) break;
                high = middle - 1;
            }
        }

        std::size_t cut = best;
        const std::size_t floorCut = std::max<std::size_t>(
            1, best - std::max<std::size_t>(1, best * 2 / 5));
        for (std::size_t k = best; k > floorCut; --k) {
            if (isBreakCodepoint(text, offsets[k - 1], offsets[k])) {
                cut = k;
                break;
            }
        }
        if (cut == 0) cut = 1;

        std::size_t lineEnd = offsets[cut];
        if (trimLines) {
            while (lineEnd > cursor && isUtf8SpaceAt(text, lineEnd - 1)) {
                --lineEnd;
            }
        }
        if (lineEnd <= cursor) {
            cut = 1;
            lineEnd = offsets[1];
        }
        spans.push_back({cursor, lineEnd});

        cursor = lineEnd;
        if (trimLines) {
            while (cursor < text.size() && isUtf8SpaceAt(text, cursor)) {
                cursor += utf8Step(text, cursor);
            }
        }
    }
    return spans;
}

// ============================================================================
// 行内 Markdown 解析
// ============================================================================

bool sameFlags(const markdown_detail::InlineRun& a,
               const markdown_detail::InlineRun& b) {
    return a.bold == b.bold && a.italic == b.italic && a.code == b.code &&
           a.strike == b.strike && a.link == b.link;
}

void appendRun(std::vector<markdown_detail::InlineRun>& out, std::string text,
               const markdown_detail::InlineRun& style) {
    if (text.empty()) return;
    if (!out.empty() && sameFlags(out.back(), style)) {
        out.back().text += text;
        return;
    }
    markdown_detail::InlineRun run = style;
    run.text = std::move(text);
    out.push_back(std::move(run));
}

bool isEscaped(const std::string& text, std::size_t position) {
    std::size_t backslashes = 0;
    std::size_t index = position;
    while (index > 0 && text[index - 1] == '\\') {
        ++backslashes;
        --index;
    }
    return (backslashes & 1u) != 0;
}

std::size_t findMarker(const std::string& text, std::size_t from,
                       std::size_t end, const std::string& marker) {
    std::size_t position = from;
    while (position < end &&
           (position = text.find(marker, position)) != std::string::npos) {
        if (position + marker.size() > end) return std::string::npos;
        if (!isEscaped(text, position) && position > from) return position;
        position += marker.size();
    }
    return std::string::npos;
}

std::size_t findItalicClose(const std::string& text, std::size_t from,
                            std::size_t end, char marker) {
    for (std::size_t position = from; position < end; ++position) {
        if (text[position] != marker) continue;
        if (isEscaped(text, position)) continue;
        if (position + 1 < end && text[position + 1] == marker) {
            ++position;
            continue;
        }
        if (position > 0 && isAsciiSpace(text[position - 1])) continue;
        if (marker == '_' && position + 1 < end &&
            isAsciiAlnum(text[position + 1])) {
            continue;
        }
        return position;
    }
    return std::string::npos;
}

void parseInlineRange(const std::string& text, std::size_t begin,
                      std::size_t end, markdown_detail::InlineRun style,
                      std::vector<markdown_detail::InlineRun>& out,
                      int32_t depth = 0);

void parseInlineRange(const std::string& text, std::size_t begin,
                      std::size_t end, markdown_detail::InlineRun style,
                      std::vector<markdown_detail::InlineRun>& out,
                      int32_t depth) {
    if (begin >= end || depth > 8) return;

    std::string buffer;
    auto flush = [&] {
        if (buffer.empty()) return;
        appendRun(out, decodeEntities(buffer), style);
        buffer.clear();
    };

    std::size_t i = begin;
    while (i < end) {
        const char c = text[i];

        if (c == '\\' && i + 1 < end) {
            buffer += text[i + 1];
            i += 2;
            continue;
        }

        if (c == '`') {
            const std::size_t close = text.find('`', i + 1);
            if (close != std::string::npos && close < end) {
                flush();
                markdown_detail::InlineRun code = style;
                code.code = true;
                appendRun(out, text.substr(i + 1, close - i - 1), code);
                i = close + 1;
                continue;
            }
        }

        if (c == '!' && i + 1 < end && text[i + 1] == '[') {
            const std::size_t close = text.find("](", i + 2);
            if (close != std::string::npos && close < end) {
                const std::size_t paren = text.find(')', close + 2);
                if (paren != std::string::npos && paren <= end) {
                    flush();
                    const std::string label =
                        trim(text.substr(i + 2, close - i - 2));
                    markdown_detail::InlineRun image = style;
                    image.link = true;
                    appendRun(out,
                              label.empty() ? "[图片]" : "[图片：" + label + "]",
                              image);
                    i = paren + 1;
                    continue;
                }
            }
        }

        if (c == '[') {
            const std::size_t close = text.find("](", i + 1);
            if (close != std::string::npos && close < end) {
                const std::size_t paren = text.find(')', close + 2);
                if (paren != std::string::npos && paren <= end) {
                    flush();
                    markdown_detail::InlineRun link = style;
                    link.link = true;
                    parseInlineRange(text, i + 1, close, link, out, depth + 1);
                    i = paren + 1;
                    continue;
                }
            }
        }

        if ((c == '*' || c == '_') && i + 1 < end && text[i + 1] == c) {
            const std::string marker = text.substr(i, 2);
            const std::size_t close = findMarker(text, i + 2, end, marker);
            if (close != std::string::npos) {
                flush();
                markdown_detail::InlineRun bold = style;
                bold.bold = true;
                parseInlineRange(text, i + 2, close, bold, out, depth + 1);
                i = close + 2;
                continue;
            }
        }

        if (c == '~' && i + 1 < end && text[i + 1] == '~') {
            const std::size_t close = findMarker(text, i + 2, end, "~~");
            if (close != std::string::npos) {
                flush();
                markdown_detail::InlineRun strike = style;
                strike.strike = true;
                parseInlineRange(text, i + 2, close, strike, out, depth + 1);
                i = close + 2;
                continue;
            }
        }

        if (c == '*' || c == '_') {
            if (i + 1 < end && !isUtf8SpaceAt(text, i + 1)) {
                if (c == '_' && i > begin && isAsciiAlnum(text[i - 1])) {
                    buffer += c;
                    ++i;
                    continue;
                }
                const std::size_t close = findItalicClose(text, i + 1, end, c);
                if (close != std::string::npos) {
                    flush();
                    markdown_detail::InlineRun italic = style;
                    italic.italic = true;
                    parseInlineRange(text, i + 1, close, italic, out, depth + 1);
                    i = close + 1;
                    continue;
                }
            }
        }

        buffer += c;
        ++i;
    }
    flush();
}

std::vector<markdown_detail::InlineRun> parseInline(const std::string& text) {
    std::vector<markdown_detail::InlineRun> runs;
    markdown_detail::InlineRun base;
    parseInlineRange(text, 0, text.size(), base, runs);
    return runs;
}

std::string inlinePlainText(const std::vector<markdown_detail::InlineRun>& runs) {
    std::string result;
    for (const auto& run : runs) result += run.text;
    return result;
}

// ============================================================================
// 块级语法识别
// ============================================================================

bool allSameChar(const std::string& text, char c, std::size_t minimum) {
    std::size_t count = 0;
    for (char value : text) {
        if (value == ' ') continue;
        if (value != c) return false;
        ++count;
    }
    return count >= minimum;
}

bool isFence(const std::string& trimmed) {
    if (trimmed.size() < 3) return false;
    const char c = trimmed[0];
    if (c != '`' && c != '~') return false;
    std::size_t count = 0;
    while (count < trimmed.size() && trimmed[count] == c) ++count;
    return count >= 3;
}

bool isHorizontalRule(const std::string& trimmed) {
    if (trimmed.empty()) return false;
    const char c = trimmed[0];
    if (c != '-' && c != '*' && c != '_') return false;
    return allSameChar(trimmed, c, 3);
}

int32_t setextLevel(const std::string& trimmed) {
    if (trimmed.empty()) return 0;
    const char c = trimmed[0];
    if (c != '=' && c != '-') return 0;
    if (!allSameChar(trimmed, c, 1)) return 0;
    return c == '=' ? 1 : 2;
}

bool parseAtxHeading(const std::string& trimmed, int32_t* level,
                     std::string* content) {
    std::size_t hashes = 0;
    while (hashes < trimmed.size() && trimmed[hashes] == '#') ++hashes;
    if (hashes == 0 || hashes > 6) return false;
    if (hashes >= trimmed.size() || trimmed[hashes] != ' ') return false;

    std::string value = trim(trimmed.substr(hashes + 1));

    std::size_t hashStart = value.size();
    while (hashStart > 0 && value[hashStart - 1] == '#') --hashStart;
    if (hashStart > 0 && hashStart < value.size() &&
        isAsciiSpace(value[hashStart - 1])) {
        value = trimRight(value.substr(0, hashStart));
    }

    *level = static_cast<int32_t>(hashes);
    *content = value;
    return true;
}

struct ListMarker {
    int32_t indent = 0;
    int32_t level = 0;
    bool ordered = false;
    int32_t number = 1;
    bool hasTask = false;
    bool checked = false;
    std::size_t contentStart = 0;
};

bool parseListMarker(const std::string& raw, ListMarker* marker) {
    int32_t indent = 0;
    std::size_t position = 0;
    while (position < raw.size()) {
        if (raw[position] == ' ') {
            ++indent;
            ++position;
        } else if (raw[position] == '\t') {
            indent += 4;
            ++position;
        } else {
            break;
        }
    }
    if (position >= raw.size()) return false;

    const char c = raw[position];
    if ((c == '-' || c == '*' || c == '+') && position + 1 < raw.size() &&
        raw[position + 1] == ' ') {
        marker->ordered = false;
        marker->contentStart = position + 2;
    } else {
        std::size_t digits = position;
        while (digits < raw.size() && isAsciiDigit(raw[digits])) ++digits;
        if (digits == position || digits - position > 3) return false;
        if (digits >= raw.size() || (raw[digits] != '.' && raw[digits] != ')')) {
            return false;
        }
        if (digits + 1 >= raw.size() || raw[digits + 1] != ' ') return false;
        marker->ordered = true;
        marker->number = std::atoi(raw.substr(position, digits - position).c_str());
        marker->contentStart = digits + 2;
    }

    marker->indent = indent;
    marker->level = std::min(5, indent / 2);

    if (!marker->ordered && marker->contentStart + 3 < raw.size() &&
        raw[marker->contentStart] == '[' &&
        raw[marker->contentStart + 2] == ']' &&
        raw[marker->contentStart + 3] == ' ') {
        const char state = raw[marker->contentStart + 1];
        if (state == ' ' || state == 'x' || state == 'X') {
            marker->hasTask = true;
            marker->checked = (state == 'x' || state == 'X');
            marker->contentStart += 4;
        }
    }
    return true;
}

std::vector<std::string> splitTableRow(const std::string& line) {
    std::string text = trim(line);
    std::vector<std::string> cells;
    std::size_t begin = 0;
    std::size_t end = text.size();
    if (!text.empty() && text.front() == '|') begin = 1;
    if (end > begin && text[end - 1] == '|') --end;

    std::string current;
    for (std::size_t i = begin; i < end; ++i) {
        const char c = text[i];
        if (c == '\\' && i + 1 < end && text[i + 1] == '|') {
            current += '|';
            ++i;
            continue;
        }
        if (c == '|') {
            cells.push_back(trim(current));
            current.clear();
            continue;
        }
        current += c;
    }
    cells.push_back(trim(current));
    return cells;
}

bool isSeparatorCell(const std::string& cell) {
    const std::string text = trim(cell);
    if (text.empty()) return false;
    std::size_t position = 0;
    if (position < text.size() && text[position] == ':') ++position;
    std::size_t dashes = 0;
    while (position < text.size() && text[position] == '-') {
        ++dashes;
        ++position;
    }
    if (dashes == 0) return false;
    if (position < text.size() && text[position] == ':') ++position;
    return position == text.size();
}

bool isTableSeparator(const std::string& trimmed) {
    if (trimmed.find('|') == std::string::npos) return false;
    const std::vector<std::string> cells = splitTableRow(trimmed);
    if (cells.empty()) return false;
    for (const std::string& cell : cells) {
        if (!isSeparatorCell(cell)) return false;
    }
    return true;
}

int32_t tableAlignment(const std::string& cell) {
    const std::string text = trim(cell);
    if (text.empty()) return 0;
    const bool left = text.front() == ':';
    const bool right = text.back() == ':';
    if (left && right) return 1;
    if (right) return 2;
    return 0;
}

Color mixColorLocal(const Color& a, const Color& b, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return Color{
        static_cast<uint8_t>(std::lround(a.r + (b.r - a.r) * t)),
        static_cast<uint8_t>(std::lround(a.g + (b.g - a.g) * t)),
        static_cast<uint8_t>(std::lround(a.b + (b.b - a.b) * t)),
        static_cast<uint8_t>(std::lround(a.a + (b.a - a.a) * t))};
}

Color withAlphaLocal(Color color, uint8_t alpha) {
    color.a = alpha;
    return color;
}

} // namespace

// ============================================================================
// MarkdownView — 构造 / 配置
// ============================================================================

MarkdownView::MarkdownView() {
    canvas().setBackgroundColor(Color{0, 0, 0, 0});
}

MarkdownView::~MarkdownView() = default;

void MarkdownView::setMarkdown(std::string markdown) {
    source_ = std::move(markdown);
    parseMarkdown();
    scroll_ = 0;
    layoutWidth_ = -1;
    if (frame().w > 0 && frame().h > 0) {
        layoutBlocks();
        rebuildVisuals();
    }
}

void MarkdownView::setTheme(const MarkdownTheme& theme) {
    theme_ = theme;
    layoutWidth_ = -1;
    if (parsed_ && frame().w > 0 && frame().h > 0) {
        layoutBlocks();
        rebuildVisuals();
    }
}

void MarkdownView::setLayer(int32_t layer) {
    if (layer_ == layer) return;
    layer_ = layer;
    rebuildVisuals();
}

std::string MarkdownView::plainText() const {
    return plain_;
}

// ============================================================================
// 解析
// ============================================================================

void MarkdownView::parseMarkdown() {
    blocks_.clear();
    plain_.clear();
    parsed_ = true;

    const std::vector<std::string> lines = splitLines(source_);

    auto appendPlain = [&](const std::string& text) {
        if (text.empty()) return;
        if (!plain_.empty()) plain_ += '\n';
        plain_ += text;
    };

    auto makeBlock = [&](Block::Kind kind) {
        Block block;
        block.kind = kind;
        return block;
    };

    std::size_t i = 0;
    while (i < lines.size()) {
        const std::string raw = lines[i];
        const std::string trimmed = trim(raw);
        if (trimmed.empty()) {
            ++i;
            continue;
        }

        // ---- 围栏代码块 ----
        if (isFence(trimmed)) {
            const char fenceChar = trimmed[0];
            Block block = makeBlock(Block::Kind::Code);
            ++i;
            while (i < lines.size()) {
                const std::string closing = trim(lines[i]);
                if (isFence(closing) && closing[0] == fenceChar) {
                    ++i;
                    break;
                }
                block.codeLines.push_back(lines[i]);
                ++i;
            }
            if (block.codeLines.empty()) block.codeLines.push_back(std::string());
            for (const std::string& codeLine : block.codeLines) {
                appendPlain(codeLine);
            }
            blocks_.push_back(std::move(block));
            continue;
        }

        // ---- ATX 标题 ----
        {
            int32_t level = 0;
            std::string heading;
            if (parseAtxHeading(trimmed, &level, &heading)) {
                Block block = makeBlock(Block::Kind::Heading);
                block.level = level;
                block.runs = parseInline(heading);
                appendPlain(heading);
                blocks_.push_back(std::move(block));
                ++i;
                continue;
            }
        }

        // ---- Setext 标题 ----
        if (i + 1 < lines.size()) {
            const int32_t level = setextLevel(trim(lines[i + 1]));
            if (level > 0) {
                Block block = makeBlock(Block::Kind::Heading);
                block.level = level;
                block.runs = parseInline(trimmed);
                appendPlain(trimmed);
                blocks_.push_back(std::move(block));
                i += 2;
                continue;
            }
        }

        // ---- 分割线 ----
        if (isHorizontalRule(trimmed)) {
            blocks_.push_back(makeBlock(Block::Kind::Divider));
            ++i;
            continue;
        }

        // ---- 引用块 ----
        if (trimmed[0] == '>') {
            std::string joined;
            std::size_t j = i;
            while (j < lines.size()) {
                const std::string quote = trim(lines[j]);
                if (quote.empty() || quote[0] != '>') break;
                std::string content = quote.substr(1);
                while (!content.empty() &&
                       (content.front() == '>' || content.front() == ' ')) {
                    content.erase(content.begin());
                }
                content = trim(content);
                if (!content.empty()) joined = smartJoin(joined, content);
                ++j;
            }
            if (!joined.empty()) {
                Block block = makeBlock(Block::Kind::Quote);
                block.runs = parseInline(joined);
                appendPlain(joined);
                blocks_.push_back(std::move(block));
            }
            i = j;
            continue;
        }

        // ---- 表格 ----
        if (i + 1 < lines.size() &&
            isTableSeparator(trim(lines[i + 1]))) {
            const std::vector<std::string> headerCells = splitTableRow(trimmed);
            if (!headerCells.empty()) {
                Block block = makeBlock(Block::Kind::Table);
                const std::vector<std::string> separatorCells =
                    splitTableRow(trim(lines[i + 1]));

                const int32_t columns = static_cast<int32_t>(headerCells.size());
                block.tableAlign.assign(static_cast<std::size_t>(columns), 0);
                for (int32_t c = 0; c < columns; ++c) {
                    if (c < static_cast<int32_t>(separatorCells.size())) {
                        block.tableAlign[static_cast<std::size_t>(c)] =
                            tableAlignment(separatorCells[static_cast<std::size_t>(c)]);
                    }
                }

                std::vector<TableCell> headerRow;
                headerRow.resize(static_cast<std::size_t>(columns));
                for (int32_t c = 0; c < columns; ++c) {
                    headerRow[static_cast<std::size_t>(c)].runs =
                        parseInline(headerCells[static_cast<std::size_t>(c)]);
                }
                block.tableRows.push_back(std::move(headerRow));

                std::size_t j = i + 2;
                while (j < lines.size()) {
                    const std::string rowText = trim(lines[j]);
                    if (rowText.empty() || rowText.find('|') == std::string::npos) {
                        break;
                    }
                    const std::vector<std::string> cells = splitTableRow(rowText);
                    if (cells.empty()) break;

                    std::vector<TableCell> row;
                    row.resize(static_cast<std::size_t>(columns));
                    for (int32_t c = 0; c < columns; ++c) {
                        const std::string value =
                            c < static_cast<int32_t>(cells.size())
                                ? cells[static_cast<std::size_t>(c)]
                                : std::string();
                        row[static_cast<std::size_t>(c)].runs = parseInline(value);
                    }
                    block.tableRows.push_back(std::move(row));
                    ++j;
                }

                for (const auto& row : block.tableRows) {
                    std::string line;
                    for (const TableCell& cell : row) {
                        if (!line.empty()) line += " | ";
                        line += inlinePlainText(cell.runs);
                    }
                    appendPlain(line);
                }
                blocks_.push_back(std::move(block));
                i = j;
                continue;
            }
        }

        // ---- 列表项 ----
        ListMarker marker;
        if (parseListMarker(raw, &marker)) {
            std::string content = raw.substr(marker.contentStart);
            std::size_t j = i + 1;
            while (j < lines.size()) {
                const std::string next = trim(lines[j]);
                if (next.empty()) break;
                if (isFence(next) || isHorizontalRule(next) || next[0] == '>') break;

                int32_t headingLevel = 0;
                std::string headingText;
                if (parseAtxHeading(next, &headingLevel, &headingText)) break;

                ListMarker nextMarker;
                if (parseListMarker(lines[j], &nextMarker)) break;
                if (j + 1 < lines.size() &&
                    isTableSeparator(trim(lines[j + 1]))) {
                    break;
                }

                content = smartJoin(content, next);
                ++j;
            }

            Block block = makeBlock(Block::Kind::ListItem);
            block.level = marker.level;
            block.ordered = marker.ordered;
            block.number = marker.number;
            block.hasTask = marker.hasTask;
            block.checked = marker.checked;
            content = trim(content);
            block.runs = parseInline(content);

            std::string prefix;
            if (marker.ordered) {
                prefix = std::to_string(marker.number) + ". ";
            } else if (marker.hasTask) {
                prefix = marker.checked ? "[x] " : "[ ] ";
            } else {
                prefix = "• ";
            }
            appendPlain(prefix + content);
            blocks_.push_back(std::move(block));
            i = j;
            continue;
        }

        // ---- 普通段落 ----
        std::string joined = trimmed;
        std::size_t j = i + 1;
        while (j < lines.size()) {
            const std::string next = trim(lines[j]);
            if (next.empty()) break;
            if (isFence(next) || isHorizontalRule(next) || next[0] == '>') break;

            int32_t headingLevel = 0;
            std::string headingText;
            if (parseAtxHeading(next, &headingLevel, &headingText)) break;

            ListMarker nextMarker;
            if (parseListMarker(lines[j], &nextMarker)) break;
            if (j + 1 < lines.size() &&
                isTableSeparator(trim(lines[j + 1]))) {
                break;
            }
            if (setextLevel(next) > 0) break;

            joined = smartJoin(joined, next);
            ++j;
        }

        if (!joined.empty()) {
            Block block = makeBlock(Block::Kind::Paragraph);
            block.runs = parseInline(joined);
            appendPlain(joined);
            blocks_.push_back(std::move(block));
        }
        i = j;
    }

    if (blocks_.empty()) {
        Block block = makeBlock(Block::Kind::Paragraph);
        block.runs = parseInline("（空文档）");
        blocks_.push_back(std::move(block));
        plain_ = "（空文档）";
    }
}

// ============================================================================
// 排版
// ============================================================================

int32_t MarkdownView::headingFontSize(int32_t level) const {
    static const int32_t kScale[6] = {172, 150, 130, 115, 108, 100};
    const int32_t index = std::max(0, std::min(5, level - 1));
    return std::max(12, static_cast<int32_t>(
        std::lround(static_cast<double>(theme_.baseFontSize) *
                    kScale[index] / 100.0)));    
}

int32_t MarkdownView::headingLineHeight(int32_t level) const {
    const int32_t fontSize = headingFontSize(level);
    return std::max(fontSize + 6,
                    static_cast<int32_t>(std::lround(fontSize * 1.35)));
}

int32_t MarkdownView::gapBeforeFor(const Block& block,
                                   const Block* previous) const {
    if (!previous) return 0;
    switch (block.kind) {
        case Block::Kind::Heading:
            return block.level <= 2 ? 22 : 18;
        case Block::Kind::Paragraph:
            return 10;
        case Block::Kind::ListItem:
            return previous->kind == Block::Kind::ListItem ? 4 : 10;
        case Block::Kind::Quote:
            return 14;
        case Block::Kind::Code:
            return 14;
        case Block::Kind::Table:
            return 16;
        case Block::Kind::Divider:
            return 16;
        default:
            return 8;
    }
}

std::vector<markdown_detail::TextLine> MarkdownView::layoutText(
    const std::vector<InlineRun>& runs, int32_t fontSize, int32_t lineHeight,
    int32_t maxWidth, bool trimLines) const {
    std::vector<TextLine> lines;
    if (maxWidth <= 0) return lines;

    std::string plain;
    std::vector<std::size_t> runBegin(runs.size(), 0);
    std::vector<std::size_t> runEnd(runs.size(), 0);
    for (std::size_t index = 0; index < runs.size(); ++index) {
        runBegin[index] = plain.size();
        plain += runs[index].text;
        runEnd[index] = plain.size();
    }
    if (plain.empty()) return lines;

    const std::vector<std::pair<std::size_t, std::size_t>> spans =
        wrapSpans(canvas(), plain, fontSize, maxWidth, trimLines);

    for (const auto& span : spans) {
        TextLine line;
        line.height = lineHeight;
        int32_t cursorX = 0;

        for (std::size_t index = 0; index < runs.size(); ++index) {
            const std::size_t begin = std::max(span.first, runBegin[index]);
            const std::size_t end = std::min(span.second, runEnd[index]);
            if (end <= begin) continue;

            Segment segment;
            segment.text = plain.substr(begin, end - begin);
            if (segment.text.empty()) continue;
            segment.bold = runs[index].bold;
            segment.italic = runs[index].italic;
            segment.code = runs[index].code;
            segment.strike = runs[index].strike;
            segment.link = runs[index].link;
            segment.fontSize =
                segment.code ? theme_.codeFontSize : fontSize;
            segment.width =
                measureTextWidth(canvas(), segment.text, segment.fontSize);

            int32_t advance = segment.width;
            if (segment.code) {
                segment.x = cursorX + 4;
                advance += 8;
            } else {
                segment.x = cursorX;
            }
            if (segment.bold) advance += 1;
            cursorX += advance;
            line.segments.push_back(std::move(segment));
        }

        line.width = cursorX;
        if (!line.segments.empty()) lines.push_back(std::move(line));
    }
    return lines;
}

void MarkdownView::layoutTable(Block& block, int32_t contentWidth) const {
    const int32_t columns = static_cast<int32_t>(block.tableAlign.size());
    if (columns <= 0 || block.tableRows.empty()) {
        block.tableWidth = 0;
        block.rowHeights.clear();
        block.cellWidths.clear();
        block.height = 0;
        return;
    }

    const int32_t cellPaddingX = 8;
    const int32_t cellPaddingY = 6;
    const int32_t tableFont = std::max(11, theme_.baseFontSize - 1);
    const int32_t tableLine = std::max(16, tableFont + 5);
    const int32_t minColumn = std::max(40, std::min(56, contentWidth / std::max(1, columns)));

    block.cellWidths.assign(static_cast<std::size_t>(columns), minColumn);
    for (int32_t c = 0; c < columns; ++c) {
        int32_t widest = minColumn;
        for (const auto& row : block.tableRows) {
            if (c >= static_cast<int32_t>(row.size())) continue;
            const TableCell& cell = row[static_cast<std::size_t>(c)];
            const int32_t textWidth =
                measureTextWidth(canvas(), inlinePlainText(cell.runs), tableFont);
            widest = std::max(widest, textWidth + cellPaddingX * 2 + 2);
        }
        block.cellWidths[static_cast<std::size_t>(c)] = widest;
    }

    int32_t totalWidth = 0;
    for (int32_t width : block.cellWidths) totalWidth += width;

    if (totalWidth > contentWidth) {
        for (int32_t c = 0; c < columns; ++c) {
            block.cellWidths[static_cast<std::size_t>(c)] =
                std::max(minColumn,
                         block.cellWidths[static_cast<std::size_t>(c)] *
                             contentWidth / std::max(1, totalWidth));
        }
        totalWidth = 0;
        for (int32_t width : block.cellWidths) totalWidth += width;

        int32_t overflow = totalWidth - contentWidth;
        while (overflow > 0) {
            int32_t widestIndex = 0;
            for (int32_t c = 1; c < columns; ++c) {
                if (block.cellWidths[static_cast<std::size_t>(c)] >
                    block.cellWidths[static_cast<std::size_t>(widestIndex)]) {
                    widestIndex = c;
                }
            }
            const int32_t available =
                block.cellWidths[static_cast<std::size_t>(widestIndex)] - minColumn;
            if (available <= 0) break;
            const int32_t cut = std::min(overflow, available);
            block.cellWidths[static_cast<std::size_t>(widestIndex)] -= cut;
            overflow -= cut;
        }
    }

    totalWidth = 0;
    for (int32_t width : block.cellWidths) totalWidth += width;
    block.tableWidth = totalWidth;

    block.rowHeights.clear();
    for (auto& row : block.tableRows) {
        if (static_cast<int32_t>(row.size()) < columns) {
            row.resize(static_cast<std::size_t>(columns));
        }
        int32_t rowHeight = tableLine + cellPaddingY * 2;
        for (int32_t c = 0; c < columns; ++c) {
            TableCell& cell = row[static_cast<std::size_t>(c)];
            const int32_t innerWidth =
                std::max(8, block.cellWidths[static_cast<std::size_t>(c)] -
                                cellPaddingX * 2);
            cell.lines = layoutText(cell.runs, tableFont, tableLine, innerWidth,
                                    true);
            if (cell.lines.empty()) {
                TextLine empty;
                empty.height = tableLine;
                cell.lines.push_back(std::move(empty));
            }
            rowHeight = std::max(
                rowHeight,
                static_cast<int32_t>(cell.lines.size()) * tableLine +
                    cellPaddingY * 2);
        }
        block.rowHeights.push_back(rowHeight);
    }

    int32_t height = 0;
    for (int32_t rowHeight : block.rowHeights) height += rowHeight;
    block.height = height;
}

void MarkdownView::layoutBlocks() {
    const int32_t frameWidth = std::max(1, frame().w);
    const int32_t padding = theme_.padding;
    const int32_t scrollbarReserve = 8;
    const int32_t contentWidth =
        std::max(24, frameWidth - padding * 2 - scrollbarReserve);

    int32_t y = padding;
    const Block* previous = nullptr;

    for (Block& block : blocks_) {
        block.gapBefore = gapBeforeFor(block, previous);
        y += block.gapBefore;
        block.y = y;

        switch (block.kind) {
            case Block::Kind::Heading: {
                const int32_t fontSize = headingFontSize(block.level);
                const int32_t lineHeight = headingLineHeight(block.level);
                block.lines =
                    layoutText(block.runs, fontSize, lineHeight, contentWidth, true);
                block.height = static_cast<int32_t>(block.lines.size()) * lineHeight;
                break;
            }
            case Block::Kind::Paragraph: {
                block.lines = layoutText(block.runs, theme_.baseFontSize,
                                         theme_.lineHeight, contentWidth, true);
                block.height =
                    static_cast<int32_t>(block.lines.size()) * theme_.lineHeight;
                break;
            }
            case Block::Kind::ListItem: {
                const int32_t fontSize = theme_.baseFontSize;
                const int32_t lineHeight = theme_.lineHeight;
                const int32_t indentation = block.level * 18;
                int32_t markerWidth = 16;
                if (block.ordered) {
                    const std::string label = std::to_string(block.number) + ".";
                    markerWidth =
                        measureTextWidth(canvas(), label, fontSize) + 8;
                } else if (block.hasTask) {
                    markerWidth = 24;
                }
                const int32_t textWidth = std::max(
                    24, contentWidth - indentation - markerWidth);
                block.lines = layoutText(block.runs, fontSize, lineHeight,
                                         textWidth, true);
                block.height =
                    std::max(lineHeight, static_cast<int32_t>(block.lines.size()) *
                                             lineHeight);
                break;
            }
            case Block::Kind::Quote: {
                const int32_t fontSize = theme_.baseFontSize;
                const int32_t lineHeight = theme_.lineHeight;
                const int32_t textWidth = std::max(24, contentWidth - 18);
                block.lines = layoutText(block.runs, fontSize, lineHeight,
                                         textWidth, true);
                block.height = static_cast<int32_t>(block.lines.size()) *
                                   lineHeight +
                               8;
                break;
            }
            case Block::Kind::Code: {
                const int32_t fontSize = theme_.codeFontSize;
                const int32_t lineHeight = std::max(fontSize + 5,
                                                    static_cast<int32_t>(
                                                        std::lround(fontSize * 1.45)));
                const int32_t textWidth = std::max(24, contentWidth - 24);
                block.lines.clear();
                for (const std::string& codeLine : block.codeLines) {
                    InlineRun run;
                    run.text = codeLine;
                    std::vector<TextLine> wrapped =
                        layoutText({run}, fontSize, lineHeight, textWidth, false);
                    if (wrapped.empty()) {
                        TextLine empty;
                        empty.height = lineHeight;
                        block.lines.push_back(std::move(empty));
                    } else {
                        for (TextLine& line : wrapped) {
                            block.lines.push_back(std::move(line));
                        }
                    }
                }
                if (block.lines.empty()) {
                    TextLine empty;
                    empty.height = lineHeight;
                    block.lines.push_back(std::move(empty));
                }
                block.height =
                    static_cast<int32_t>(block.lines.size()) * lineHeight + 20;
                break;
            }
            case Block::Kind::Table: {
                layoutTable(block, contentWidth);
                break;
            }
            case Block::Kind::Divider: {
                block.height = 1;
                break;
            }
            case Block::Kind::Empty:
            default: {
                block.height = 0;
                break;
            }
        }

        y += block.height;
        previous = &block;
    }

    contentHeight_ = y + padding;
    scroll_ = std::max(0, std::min(maxScroll(), scroll_));
}

// ============================================================================
// 渲染
// ============================================================================

void MarkdownView::rebuildVisuals() {
    const int32_t width = std::max(1, canvas().width());
    const int32_t height = std::max(1, canvas().height());

    canvas().clear();

    if (theme_.background.a > 0) {
        Shape background;
        background.data = RectShape{0, 0, width, height};
        background.base.fill = Paint(theme_.background);
        background.base.z = layer_;
        backgroundId_ = canvas().addShape(std::move(background));
    } else {
        backgroundId_ = 0;
    }

    GroupData content;
    content.clipRect = Rect{0, 0, width, height};
    content.scrollOffset = Vec2i{0, -scroll_};

    int32_t z = 0;
    auto add = [&](Shape shape) {
        shape.base.z = z++;
        content.children.push_back(std::move(shape));
    };

    auto addRect = [&](int32_t x, int32_t y, int32_t w, int32_t h,
                       const Color& fill, const Color& stroke,
                       int32_t strokeWidth) {
        Shape shape;
        shape.data = RectShape{x, y, w, h};
        shape.base.fill = Paint(fill);
        if (strokeWidth > 0) {
            shape.base.stroke = Paint(stroke);
            shape.base.strokeWidth = strokeWidth;
        }
        add(std::move(shape));
    };

    auto addRRect = [&](int32_t x, int32_t y, int32_t w, int32_t h,
                       float radius, const Color& fill, const Color& stroke,
                       int32_t strokeWidth) {
        Shape shape;
        shape.data = RRectShape{x, y, w, h, radius, radius};
        shape.base.fill = Paint(fill);
        if (strokeWidth > 0) {
            shape.base.stroke = Paint(stroke);
            shape.base.strokeWidth = strokeWidth;
        }
        add(std::move(shape));
    };

    auto addLine = [&](int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                       const Color& color, int32_t strokeWidth) {
        Shape shape;
        shape.data = LineShape{x1, y1, x2, y2};
        shape.base.stroke = Paint(color);
        shape.base.strokeWidth = strokeWidth;
        add(std::move(shape));
    };

    auto addCircle = [&](int32_t cx, int32_t cy, float radius,
                        const Color& fill) {
        Shape shape;
        shape.data = CircleShape{cx, cy, radius};
        shape.base.fill = Paint(fill);
        add(std::move(shape));
    };

    auto addText = [&](const std::string& text, int32_t x, int32_t baseline,
                       int32_t fontSize, const Color& color, bool bold) {
        if (!hasVisibleText(text)) return;
        Shape shape;
        TextShape label;
        label.text = text;
        label.x = x;
        label.y = baseline;
        label.fontSize = fontSize;
        shape.data = std::move(label);
        shape.base.fill = Paint(color);
        add(std::move(shape));

        if (bold) {
            Shape boldShape;
            TextShape duplicate;
            duplicate.text = text;
            duplicate.x = x + 1;
            duplicate.y = baseline;
            duplicate.fontSize = fontSize;
            boldShape.data = std::move(duplicate);
            boldShape.base.fill = Paint(color);
            add(std::move(boldShape));
        }
    };

    auto lineBaseline = [&](const TextLine& line, int32_t lineTop,
                            int32_t lineHeight) {
        int32_t maxFont = theme_.baseFontSize;
        for (const Segment& segment : line.segments) {
            maxFont = std::max(maxFont, segment.fontSize);
        }
        const Rect metrics = measureTextRect(canvas(), "国Ag", maxFont);
        return lineTop + (lineHeight - metrics.h) / 2 - metrics.y;
    };

    auto drawLine = [&](const TextLine& line, int32_t lineTop, int32_t lineHeight,
                        int32_t originX, const Color& baseColor, bool bold) {
        const int32_t baseline = lineBaseline(line, lineTop, lineHeight);
        for (const Segment& segment : line.segments) {
            if (!hasVisibleText(segment.text)) continue;

            Color color = baseColor;
            if (segment.link) color = theme_.link;
            if (segment.code) {
                color = mixColorLocal(theme_.accent, Color::white(), 0.35f);
                addRRect(originX + segment.x - 4, lineTop + 2,
                         segment.width + 8, std::max(10, lineHeight - 4), 3.0f,
                         theme_.codeBackground, theme_.codeBorder, 1);
            }
            if (segment.strike) color = theme_.textMuted;

            addText(segment.text, originX + segment.x, baseline, segment.fontSize,
                    color, segment.bold || bold);

            if (segment.link) {
                addLine(originX + segment.x, baseline + 3,
                        originX + segment.x + segment.width, baseline + 3,
                        withAlphaLocal(theme_.link, 160), 1);
            }
            if (segment.strike) {
                addLine(originX + segment.x,
                        baseline - segment.fontSize / 3,
                        originX + segment.x + segment.width,
                        baseline - segment.fontSize / 3,
                        withAlphaLocal(theme_.textMuted, 180), 1);
            }
        }
    };

    const int32_t padding = theme_.padding;
    const int32_t contentWidth =
        std::max(24, width - padding * 2 - 8);
    const int32_t accentWidth = 3;

    for (const Block& block : blocks_) {
        switch (block.kind) {
            case Block::Kind::Heading: {
                const int32_t lineHeight = headingLineHeight(block.level);
                Color color = theme_.text;
                if (block.level <= 2) {
                    color = mixColorLocal(theme_.accent, Color::white(), 0.55f);
                } else if (block.level >= 4) {
                    color = withAlphaLocal(theme_.text, 210);
                }
                int32_t lineTop = block.y;
                for (const TextLine& line : block.lines) {
                    drawLine(line, lineTop, lineHeight, padding, color, true);
                    lineTop += lineHeight;
                }
                break;
            }

            case Block::Kind::Paragraph: {
                int32_t lineTop = block.y;
                for (const TextLine& line : block.lines) {
                    drawLine(line, lineTop, theme_.lineHeight, padding,
                             withAlphaLocal(theme_.text, 242), false);
                    lineTop += theme_.lineHeight;
                }
                break;
            }

            case Block::Kind::ListItem: {
                const int32_t indentation = block.level * 18;
                int32_t markerWidth = 16;
                int32_t textX = padding + indentation + markerWidth;
                if (block.ordered) {
                    const std::string label = std::to_string(block.number) + ".";
                    markerWidth =
                        measureTextWidth(canvas(), label, theme_.baseFontSize) + 8;
                    textX = padding + indentation + markerWidth;

                    const Rect metrics = measureTextRect(
                        canvas(), "国Ag", theme_.baseFontSize);
                    const int32_t baseline = block.y +
                        (theme_.lineHeight - metrics.h) / 2 - metrics.y;
                    addText(label, padding + indentation + 2, baseline,
                            theme_.baseFontSize, withAlphaLocal(theme_.accent, 220),
                            false);
                } else if (block.hasTask) {
                    markerWidth = 24;
                    textX = padding + indentation + markerWidth;
                    const int32_t boxSize = 12;
                    const int32_t boxY =
                        block.y + (theme_.lineHeight - boxSize) / 2;
                    addRRect(padding + indentation, boxY, boxSize, boxSize, 3.0f,
                             block.checked ? withAlphaLocal(theme_.accent, 90)
                                           : Color{0, 0, 0, 0},
                             withAlphaLocal(theme_.accent, 170), 1);
                    if (block.checked) {
                        addLine(padding + indentation + 2, boxY + 6,
                                padding + indentation + 5, boxY + 9,
                                theme_.accent, 2);
                        addLine(padding + indentation + 5, boxY + 9,
                                padding + indentation + 10, boxY + 3,
                                theme_.accent, 2);
                    }
                } else {
                    addCircle(padding + indentation + 6,
                              block.y + theme_.lineHeight / 2, 2.5f,
                              withAlphaLocal(theme_.accent, 210));
                }

                int32_t lineTop = block.y;
                for (const TextLine& line : block.lines) {
                    drawLine(line, lineTop, theme_.lineHeight, textX,
                             withAlphaLocal(theme_.text, 238), false);
                    lineTop += theme_.lineHeight;
                }
                break;
            }

            case Block::Kind::Quote: {
                addRRect(padding, block.y, contentWidth, block.height, 6.0f,
                         withAlphaLocal(theme_.accent, 16),
                         withAlphaLocal(theme_.accent, 40), 1);
                addRRect(padding, block.y, accentWidth, block.height, 1.5f,
                         withAlphaLocal(theme_.quoteBar, 190),
                         Color{0, 0, 0, 0}, 0);
                (void)accentWidth;

                int32_t lineTop = block.y + 4;
                for (const TextLine& line : block.lines) {
                    drawLine(line, lineTop, theme_.lineHeight, padding + 14,
                             withAlphaLocal(theme_.textMuted, 240), false);
                    lineTop += theme_.lineHeight;
                }
                break;
            }

            case Block::Kind::Code: {
                const int32_t lineHeight = std::max(
                    theme_.codeFontSize + 5,
                    static_cast<int32_t>(
                        std::lround(theme_.codeFontSize * 1.45)));
                addRRect(padding, block.y, contentWidth, block.height, 8.0f,
                         theme_.codeBackground, theme_.codeBorder, 1);
                int32_t lineTop = block.y + 10;
                const Color codeColor =
                    mixColorLocal(theme_.accent, Color::white(), 0.45f);
                for (const TextLine& line : block.lines) {
                    drawLine(line, lineTop, lineHeight, padding + 12,
                             codeColor, false);
                    lineTop += lineHeight;
                }
                break;
            }

            case Block::Kind::Table: {
                if (block.tableRows.empty() || block.cellWidths.empty()) break;

                const int32_t columns =
                    static_cast<int32_t>(block.cellWidths.size());
                const int32_t tableFont = std::max(11, theme_.baseFontSize - 1);
                const int32_t tableLine = std::max(16, tableFont + 5);
                const int32_t cellPaddingX = 8;
                const int32_t cellPaddingY = 6;
                const int32_t tableX = padding;
                const int32_t tableY = block.y;
                const int32_t tableWidth = block.tableWidth;

                int32_t rowTop = tableY;
                for (std::size_t r = 0; r < block.tableRows.size(); ++r) {
                    const int32_t rowHeight =
                        r < block.rowHeights.size() ? block.rowHeights[r]
                                                    : tableLine + cellPaddingY * 2;
                    if (r == 0) {
                        addRect(tableX, rowTop, tableWidth, rowHeight,
                                theme_.tableHeader, Color{0, 0, 0, 0}, 0);
                    }

                    int32_t cellX = tableX;
                    for (int32_t c = 0; c < columns; ++c) {
                        const int32_t cellWidth =
                            block.cellWidths[static_cast<std::size_t>(c)];
                        if (c < static_cast<int32_t>(block.tableRows[r].size())) {
                            const TableCell& cell =
                                block.tableRows[r][static_cast<std::size_t>(c)];
                            const int32_t innerWidth =
                                std::max(8, cellWidth - cellPaddingX * 2);
                            const int32_t alignment =
                                c < static_cast<int32_t>(block.tableAlign.size())
                                    ? block.tableAlign[static_cast<std::size_t>(c)]
                                    : 0;

                            int32_t contentHeight = 0;
                            contentHeight = static_cast<int32_t>(cell.lines.size()) *
                                             tableLine;
                            int32_t lineTop = rowTop +
                                (rowHeight - contentHeight) / 2;
                            const Color textColor =
                                r == 0 ? mixColorLocal(theme_.accent,
                                                       Color::white(), 0.45f)
                                       : withAlphaLocal(theme_.text, 240);

                            for (const TextLine& line : cell.lines) {
                                int32_t offsetX = 0;
                                if (alignment == 1) {
                                    offsetX = (innerWidth - line.width) / 2;
                                } else if (alignment == 2) {
                                    offsetX = innerWidth - line.width;
                                }
                                offsetX = std::max(0, offsetX);
                                drawLine(line, lineTop, tableLine,
                                         cellX + cellPaddingX + offsetX,
                                         textColor, r == 0);
                                lineTop += tableLine;
                            }
                        }
                        cellX += cellWidth;
                    }

                    rowTop += rowHeight;
                    addLine(tableX, rowTop, tableX + tableWidth, rowTop,
                            withAlphaLocal(theme_.tableBorder, 200), 1);
                }

                addRRect(tableX, tableY, tableWidth, block.height, 6.0f,
                         Color{0, 0, 0, 0}, theme_.tableBorder, 1);

                int32_t columnX = tableX;
                for (int32_t c = 0; c + 1 < columns; ++c) {
                    columnX += block.cellWidths[static_cast<std::size_t>(c)];
                    addLine(columnX, tableY, columnX, tableY + block.height,
                            withAlphaLocal(theme_.tableBorder, 150), 1);
                }
                break;
            }

            case Block::Kind::Divider: {
                addLine(padding, block.y, padding + contentWidth, block.y,
                        theme_.divider, 1);
                break;
            }

            case Block::Kind::Empty:
            default:
                break;
        }
    }

    Shape contentShape;
    contentShape.data = std::move(content);
    contentShape.base.z = layer_ + 2;
    contentGroupId_ = canvas().addShape(std::move(contentShape));

    scrollTrackId_ = 0;
    scrollThumbId_ = 0;

    const int32_t maximum = maxScroll();
    if (maximum > 0) {
        const int32_t trackWidth = 3;
        const int32_t trackX = width - trackWidth - 4;
        const int32_t trackY = 4;
        const int32_t trackHeight = std::max(8, height - 8);

        Shape track;
        track.data = RRectShape{trackX, trackY, trackWidth, trackHeight,
                                trackWidth * 0.5f, trackWidth * 0.5f};
        track.base.fill = Paint(theme_.scrollbarTrack);
        track.base.z = layer_ + 3;
        scrollTrackId_ = canvas().addShape(std::move(track));

        const int32_t thumbHeight = std::max(
            24, static_cast<int32_t>(std::lround(
                    static_cast<double>(trackHeight) * height /
                    std::max(1, contentHeight_))));
        const int32_t thumbY = trackY + static_cast<int32_t>(std::lround(
            static_cast<double>(trackHeight - thumbHeight) * scroll_ /
            std::max(1, maximum)));

        Shape thumb;
        thumb.data = RRectShape{trackX, thumbY, trackWidth,
                                std::min(trackHeight, thumbHeight),
                                trackWidth * 0.5f, trackWidth * 0.5f};
        thumb.base.fill = Paint(theme_.scrollbarThumb);
        thumb.base.z = layer_ + 4;
        scrollThumbId_ = canvas().addShape(std::move(thumb));
    }
}

void MarkdownView::applyScroll() {
    if (contentGroupId_ != 0) {
        canvas().updateShape(contentGroupId_, [&](Shape& shape) {
            if (auto* group = std::get_if<GroupData>(&shape.data)) {
                group->scrollOffset = Vec2i{0, -scroll_};
            }
        });
    }

    if (scrollThumbId_ == 0) return;

    const int32_t height = std::max(1, canvas().height());
    const int32_t trackHeight = std::max(8, height - 8);
    const int32_t maximum = maxScroll();
    if (maximum <= 0) return;

    const int32_t thumbHeight = std::max(
        24, static_cast<int32_t>(std::lround(
                static_cast<double>(trackHeight) * height /
                std::max(1, contentHeight_))));
    const int32_t thumbY = 4 + static_cast<int32_t>(std::lround(
        static_cast<double>(trackHeight - thumbHeight) * scroll_ /
        std::max(1, maximum)));

    canvas().updateShape(scrollThumbId_, [&](Shape& shape) {
        if (auto* rrect = std::get_if<RRectShape>(&shape.data)) {
            rrect->y = thumbY;
        }
    });
}

// ============================================================================
// 滚动 / Widget 接口
// ============================================================================

int32_t MarkdownView::maxScroll() const {
    return std::max(0, contentHeight_ - std::max(1, frame().h));
}

void MarkdownView::scrollTo(int32_t offset) {
    const int32_t next = std::max(0, std::min(maxScroll(), offset));
    if (next == scroll_) return;
    scroll_ = next;
    applyScroll();
}

void MarkdownView::scrollBy(int32_t delta) {
    scrollTo(scroll_ + delta);
}

Vec2i MarkdownView::onMeasure(const Vec2i& available) const {
    const bool bounded = available.x > 0 && available.x < FlexStyle::kUnbounded;
    const int32_t intrinsicWidth = bounded ? available.x : 480;
    int32_t intrinsicHeight = contentHeight_;
    if (intrinsicHeight <= 0) intrinsicHeight = 220;
    return Vec2i{intrinsicWidth, intrinsicHeight};
}

void MarkdownView::onLayout(const Rect& frame) {
    const int32_t width = std::max(1, frame.w);
    const int32_t height = std::max(1, frame.h);

    if (canvas().width() != width || canvas().height() != height) {
        canvas().resize(width, height);
        layoutWidth_ = -1;
    }

    if (!parsed_) {
        parseMarkdown();
    }

    if (layoutWidth_ != width || layoutPadding_ != theme_.padding) {
        layoutWidth_ = width;
        layoutHeight_ = height;
        layoutPadding_ = theme_.padding;
        layoutBlocks();
        rebuildVisuals();
    } else if (layoutHeight_ != height) {
        layoutHeight_ = height;
        scroll_ = std::max(0, std::min(maxScroll(), scroll_));
        rebuildVisuals();
    }
}

bool MarkdownView::onEvent(const Event& ev) {
    switch (ev.type) {
        case EventType::MouseWheel: {
            // Ctrl / 触控板捏合交给父控件做缩放，避免和滚动抢事件。
            if ((ev.modifiers & ModifierControl) != 0) return false;
            const int32_t step = std::max(8, theme_.lineHeight * 3);
            scrollBy(-static_cast<int32_t>(
                std::lround(ev.wheelDelta * static_cast<float>(step))));
            return true;
        }

        case EventType::MouseDown:
        case EventType::MouseUp:
            // 触控事件向上冒泡，让外层容器实现双指捏合缩放。
            if (ev.pointerType == PointerType::Touch) return false;
            return ev.button == MouseButtonLeft;

        case EventType::KeyDown: {
            if (ev.key == kKeyPageUp) {
                scrollBy(-std::max(24, frame().h * 9 / 10));
                return true;
            }
            if (ev.key == kKeyPageDown) {
                scrollBy(std::max(24, frame().h * 9 / 10));
                return true;
            }
            if (ev.key == kKeyHome) {
                scrollTo(0);
                return true;
            }
            if (ev.key == kKeyEnd) {
                scrollTo(maxScroll());
                return true;
            }
            if (ev.key == kKeyUp) {
                scrollBy(-theme_.lineHeight * 2);
                return true;
            }
            if (ev.key == kKeyDown) {
                scrollBy(theme_.lineHeight * 2);
                return true;
            }
            return false;
        }

        default:
            return false;
    }
}

} // namespace fluent
} // namespace modern_ui
