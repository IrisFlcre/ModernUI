// ============================================================================
// AiCards.cpp
// ============================================================================

#include "AiCards.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using modern_ui::Canvas;
using modern_ui::CircleShape;
using modern_ui::Color;
using modern_ui::EllipseShape;
using modern_ui::GroupData;
using modern_ui::LineShape;
using modern_ui::Paint;
using modern_ui::PolylineShape;
using modern_ui::Rect;
using modern_ui::RRectShape;
using modern_ui::Shape;
using modern_ui::SvgShape;
using modern_ui::TextShape;
using modern_ui::Vec2f;
using modern_ui::Vec2i;

namespace annotation {

namespace {

constexpr int32_t kCardBandBase = -2400;   // 卡片 z 区间起点（低于 Toast / 工具栏）
constexpr int32_t kCardBandStep = 200;
constexpr int32_t kContentZOffset = 50;    // 卡片背景占用 band + 0..49
constexpr int32_t kScrollZOffset = 150;

// 拖动重建节流：拖动期间最多每 8ms 重建一次卡片视觉，位置本身逐事件更新。
constexpr uint64_t kDragBuildIntervalMs = 8;
constexpr int32_t kDragThresholdPx = 4;

uint64_t monotonicMillis() {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point origin = Clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - origin).count());
}

bool isAsciiSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string trimAscii(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && isAsciiSpace(text[begin])) ++begin;
    while (end > begin && isAsciiSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

std::string trimLeftAscii(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && isAsciiSpace(text[begin])) ++begin;
    return text.substr(begin);
}

std::string trimRightAscii(const std::string& text) {
    std::size_t end = text.size();
    while (end > 0 && isAsciiSpace(text[end - 1])) --end;
    return text.substr(0, end);
}

std::string lowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

void appendUtf8(std::string& out, uint32_t codepoint) {
    if (codepoint <= 0x7Fu) {
        out += static_cast<char>(codepoint);
    } else if (codepoint <= 0x7FFu) {
        out += static_cast<char>(0xC0u | (codepoint >> 6));
        out += static_cast<char>(0x80u | (codepoint & 0x3Fu));
    } else if (codepoint <= 0xFFFFu) {
        out += static_cast<char>(0xE0u | (codepoint >> 12));
        out += static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (codepoint & 0x3Fu));
    } else {
        out += static_cast<char>(0xF0u | (codepoint >> 18));
        out += static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu));
        out += static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (codepoint & 0x3Fu));
    }
}

std::vector<std::size_t> codepointOffsets(const std::string& text) {
    std::vector<std::size_t> offsets;
    offsets.reserve(text.size() + 1);

    std::size_t index = 0;
    while (index < text.size()) {
        offsets.push_back(index);
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t step = 1;
        if (lead >= 0xF0) step = 4;
        else if (lead >= 0xE0) step = 3;
        else if (lead >= 0xC0) step = 2;
        index += std::min(step, text.size() - index);
    }
    offsets.push_back(text.size());
    return offsets;
}

// ----------------------------------------------------------------------------
// 文本度量 / 换行
// ----------------------------------------------------------------------------

Rect measureLine(Canvas& canvas, const std::string& text, int32_t fontSize) {
    if (text.empty()) return Rect{0, 0, 0, 0};

    TextShape probe;
    probe.text = text;
    probe.fontSize = fontSize;
    canvas.measureTextShape(probe);
    if (probe.measured && !probe.measuredBounds.isNull()) return probe.measuredBounds;

    const int32_t advance = std::max(1, fontSize / 2);
    return Rect{0, -fontSize,
                std::max(1, static_cast<int32_t>(text.size()) * advance),
                fontSize + 4};
}

bool isBreakCodepoint(const std::string& codepoint) {
    static const char* const kBreaks[] = {
        " ", "\t", "-", "/",
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
        "\xEF\xBC\x8C",  // （重复项无副作用）
        ",", ".", ";", ":", "!", "?", ")", "]",
    };
    for (const char* item : kBreaks) {
        if (codepoint == item) return true;
    }
    return false;
}

std::vector<std::string> wrapText(Canvas& canvas, const std::string& text,
                                  int32_t fontSize, int32_t maxWidth) {
    std::vector<std::string> lines;
    if (text.empty() || maxWidth <= 0) return lines;

    std::size_t paragraphStart = 0;
    while (paragraphStart <= text.size()) {
        const std::size_t newline = text.find('\n', paragraphStart);
        std::string paragraph = text.substr(
            paragraphStart,
            newline == std::string::npos ? std::string::npos : newline - paragraphStart);
        paragraph = trimAscii(paragraph);

        while (!paragraph.empty()) {
            if (measureLine(canvas, paragraph, fontSize).w <= maxWidth) {
                lines.push_back(paragraph);
                break;
            }

            const std::vector<std::size_t> offsets = codepointOffsets(paragraph);
            const std::size_t count = offsets.size() - 1;

            std::size_t low = 1;
            std::size_t high = count;
            std::size_t best = 1;
            while (low <= high) {
                const std::size_t middle = (low + high) / 2;
                const std::string candidate = paragraph.substr(0, offsets[middle]);
                if (measureLine(canvas, candidate, fontSize).w <= maxWidth) {
                    best = middle;
                    low = middle + 1;
                } else {
                    if (middle == 1) break;
                    high = middle - 1;
                }
            }

            // 在候选行尾附近找一个更自然的断点（空格 / 中文标点）。
            std::size_t cut = best;
            const std::size_t floorCut =
                std::max<std::size_t>(1, best - std::max<std::size_t>(1, best * 2 / 5));
            for (std::size_t k = best; k > floorCut; --k) {
                const std::string codepoint =
                    paragraph.substr(offsets[k - 1], offsets[k] - offsets[k - 1]);
                if (isBreakCodepoint(codepoint)) {
                    cut = k;
                    break;
                }
            }
            if (cut == 0) cut = 1;

            std::string line = trimRightAscii(paragraph.substr(0, offsets[cut]));
            if (line.empty()) {
                cut = 1;
                line = paragraph.substr(0, offsets[1]);
            }
            lines.push_back(line);

            const std::string rest = paragraph.substr(offsets[cut]);
            const std::string trimmed = trimLeftAscii(rest);
            if (trimmed.size() == rest.size()) {
                paragraph = rest;
            } else {
                paragraph = trimmed;
            }
        }

        if (newline == std::string::npos) break;
        paragraphStart = newline + 1;
    }
    return lines;
}

std::string ellipsize(Canvas& canvas, const std::string& text, int32_t fontSize,
                      int32_t maxWidth) {
    if (text.empty() || maxWidth <= 0) return text;
    if (measureLine(canvas, text, fontSize).w <= maxWidth) return text;

    const std::string ellipsis = "\xE2\x80\xA6";   // …
    const std::vector<std::size_t> offsets = codepointOffsets(text);
    for (std::size_t count = offsets.size() - 1; count > 0; --count) {
        const std::string candidate = text.substr(0, offsets[count]) + ellipsis;
        if (measureLine(canvas, candidate, fontSize).w <= maxWidth) return candidate;
    }
    return ellipsis;
}

Color withAlpha(Color color, uint8_t alpha) {
    color.a = alpha;
    return color;
}

Color mixColor(const Color& a, const Color& b, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return Color{
        static_cast<uint8_t>(std::lround(a.r + (b.r - a.r) * t)),
        static_cast<uint8_t>(std::lround(a.g + (b.g - a.g) * t)),
        static_cast<uint8_t>(std::lround(a.b + (b.b - a.b) * t)),
        static_cast<uint8_t>(std::lround(a.a + (b.a - a.a) * t))};
}

// ----------------------------------------------------------------------------
// 图标（SVG 染色）
// ----------------------------------------------------------------------------

std::string joinPath(const std::string& directory, const std::string& file) {
    if (directory.empty()) return file;
    const char last = directory.back();
    if (last == '/' || last == '\\') return directory + file;
    return directory + "/" + file;
}

std::string readTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

std::string iconFileForName(const std::string& rawName) {
    const std::string name = lowerAscii(trimAscii(rawName));
    if (name.empty()) return {};

    struct Alias {
        const char* name;
        const char* file;
    };
    static const Alias kAliases[] = {
        {"book", "book.svg"},
        {"knowledge", "book.svg"},
        {"知识点", "book.svg"},
        {"explain", "explain.svg"},
        {"解析", "explain.svg"},
        {"讲解", "explain.svg"},
        {"inspiration", "inspiration.svg"},
        {"idea", "inspiration.svg"},
        {"思路", "inspiration.svg"},
        {"question", "question.svg"},
        {"提问", "question.svg"},
        {"问题", "question.svg"},
        {"tips", "tips.svg"},
        {"提示", "tips.svg"},
        {"技巧", "tips.svg"},
        {"warning", "tips.svg"},
        {"易错", "tips.svg"},
        {"roadmap", "arrow-roadmap.svg"},
        {"steps", "arrow-roadmap.svg"},
        {"步骤", "arrow-roadmap.svg"},
        {"流程", "arrow-roadmap.svg"},
    };

    for (const Alias& alias : kAliases) {
        if (name == alias.name) return alias.file;
    }
    return {};
}

std::string colorHex(const Color& color) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X",
                  static_cast<int>(color.r), static_cast<int>(color.g),
                  static_cast<int>(color.b));
    return std::string(buffer);
}

// 把 SVG 内所有 <path> 的 fill 替换成目标色（与工具栏染色逻辑一致）。
std::string tintSvg(const std::string& markup, const Color& color) {
    if (markup.empty()) return markup;

    const std::string fillAttr = "fill=\"" + colorHex(color) + "\"";
    std::string result;
    result.reserve(markup.size() + 64);

    std::size_t position = 0;
    while (true) {
        const std::size_t pathStart = markup.find("<path", position);
        if (pathStart == std::string::npos) {
            result.append(markup, position, std::string::npos);
            break;
        }

        const std::size_t tagEnd = markup.find('>', pathStart);
        if (tagEnd == std::string::npos) {
            result.append(markup, position, std::string::npos);
            break;
        }

        result.append(markup, position, pathStart - position);
        std::string tag = markup.substr(pathStart, tagEnd - pathStart + 1);

        const std::size_t existing = tag.find("fill=\"");
        if (existing != std::string::npos) {
            const std::size_t valueStart = existing + 6;
            const std::size_t valueEnd = tag.find('"', valueStart);
            if (valueEnd != std::string::npos) {
                tag.replace(existing, valueEnd - existing + 1, fillAttr);
            }
        } else {
            const std::size_t insertAt =
                (tag.size() > 2 && tag[tag.size() - 2] == '/') ? tag.size() - 2
                                                               : tag.size() - 1;
            tag.insert(insertAt, " " + fillAttr);
        }

        result += tag;
        position = tagEnd + 1;
    }
    return result;
}

// ----------------------------------------------------------------------------
// XML 解析
// ----------------------------------------------------------------------------

void replaceAll(std::string& text, const std::string& needle,
                const std::string& replacement) {
    if (needle.empty()) return;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        text.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
}

void removeComments(std::string& text) {
    std::size_t start = 0;
    while ((start = text.find("<!--", start)) != std::string::npos) {
        const std::size_t end = text.find("-->", start + 4);
        if (end == std::string::npos) {
            text.erase(start);
            break;
        }
        text.erase(start, end + 3 - start);
    }
}

std::string decodeEntities(const std::string& text) {
    if (text.find('&') == std::string::npos) return text;

    std::string out;
    out.reserve(text.size());

    std::size_t index = 0;
    while (index < text.size()) {
        if (text[index] != '&') {
            out += text[index++];
            continue;
        }

        const std::size_t semicolon = text.find(';', index + 1);
        if (semicolon == std::string::npos || semicolon - index > 12) {
            out += text[index++];
            continue;
        }

        const std::string entity = text.substr(index + 1, semicolon - index - 1);
        if (entity == "lt") out += '<';
        else if (entity == "gt") out += '>';
        else if (entity == "amp") out += '&';
        else if (entity == "quot") out += '"';
        else if (entity == "apos") out += '\'';
        else if (entity == "nbsp") out += ' ';
        else if (entity.size() > 1 && entity[0] == '#') {
            uint32_t codepoint = 0;
            if (entity[1] == 'x' || entity[1] == 'X') {
                codepoint = static_cast<uint32_t>(
                    std::strtoul(entity.c_str() + 2, nullptr, 16));
            } else {
                codepoint = static_cast<uint32_t>(
                    std::strtoul(entity.c_str() + 1, nullptr, 10));
            }
            if (codepoint > 0 && codepoint <= 0x10FFFF) appendUtf8(out, codepoint);
        } else {
            out += text.substr(index, semicolon - index + 1);
        }
        index = semicolon + 1;
    }
    return out;
}

std::string attrValue(const std::string& tag, const char* name) {
    const std::string key(name);
    std::size_t position = 0;
    while ((position = tag.find(key, position)) != std::string::npos) {
        const bool leftOk = position == 0 ||
            !(std::isalnum(static_cast<unsigned char>(tag[position - 1])) ||
              tag[position - 1] == '_' || tag[position - 1] == '-');

        std::size_t cursor = position + key.size();
        while (cursor < tag.size() &&
               std::isspace(static_cast<unsigned char>(tag[cursor]))) {
            ++cursor;
        }
        if (!leftOk || cursor >= tag.size() || tag[cursor] != '=') {
            position += key.size();
            continue;
        }

        ++cursor;
        while (cursor < tag.size() &&
               std::isspace(static_cast<unsigned char>(tag[cursor]))) {
            ++cursor;
        }
        if (cursor >= tag.size()) return {};
        const char quote = tag[cursor];
        if (quote != '"' && quote != '\'') return {};
        const std::size_t end = tag.find(quote, cursor + 1);
        if (end == std::string::npos) return {};
        return tag.substr(cursor + 1, end - cursor - 1);
    }
    return {};
}

bool parseDouble(const std::string& raw, double* out) {
    const std::string value = trimAscii(raw);
    if (value.empty()) return false;
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) return false;
    *out = parsed;
    return true;
}

float parseAnchor(const std::string& raw, int32_t axis, float fallback) {
    std::string value = trimAscii(raw);
    if (value.empty()) return fallback;

    bool percent = false;
    if (value.back() == '%') {
        percent = true;
        value.pop_back();
    }

    double number = 0.0;
    if (!parseDouble(value, &number)) return fallback;

    double anchor = percent ? number / 100.0
                            : number / static_cast<double>(std::max(1, axis));
    anchor = std::max(0.0, std::min(1.0, anchor));
    return static_cast<float>(anchor);
}

int32_t parseSize(const std::string& raw, int32_t reference) {
    std::string value = trimAscii(raw);
    if (value.empty()) return 0;

    bool percent = false;
    if (value.back() == '%') {
        percent = true;
        value.pop_back();
    }

    double number = 0.0;
    if (!parseDouble(value, &number)) return 0;

    const double pixels = percent
        ? number / 100.0 * static_cast<double>(std::max(1, reference))
        : number;
    if (pixels <= 0.0) return 0;
    return static_cast<int32_t>(std::lround(pixels));
}

Color parseColor(const std::string& raw, const Color& fallback) {
    std::string value = trimAscii(raw);
    if (value.size() < 4 || value[0] != '#') return fallback;
    value.erase(0, 1);

    auto hexDigit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    if (value.size() == 3) {
        const int r = hexDigit(value[0]);
        const int g = hexDigit(value[1]);
        const int b = hexDigit(value[2]);
        if (r < 0 || g < 0 || b < 0) return fallback;
        return Color{static_cast<uint8_t>(r * 17), static_cast<uint8_t>(g * 17),
                     static_cast<uint8_t>(b * 17), 255};
    }

    if (value.size() != 6 && value.size() != 8) return fallback;
    int channels[4] = {0, 0, 0, 255};
    for (std::size_t i = 0; i < value.size() / 2; ++i) {
        const int high = hexDigit(value[i * 2]);
        const int low = hexDigit(value[i * 2 + 1]);
        if (high < 0 || low < 0) return fallback;
        channels[i] = high * 16 + low;
    }
    return Color{static_cast<uint8_t>(channels[0]),
                 static_cast<uint8_t>(channels[1]),
                 static_cast<uint8_t>(channels[2]),
                 static_cast<uint8_t>(channels[3])};
}

void parseBlocks(const std::string& inner, std::vector<AiCardBlock>* blocks) {
    const std::string lowerInner = lowerAscii(inner);

    auto appendText = [&](const std::string& raw, AiCardBlock::Kind kind) {
        std::string text = decodeEntities(trimAscii(raw));
        if (text.empty()) return;
        blocks->push_back(AiCardBlock{kind, std::move(text)});
    };

    std::size_t position = 0;
    while (position < inner.size()) {
        const std::size_t open = inner.find('<', position);
        if (open == std::string::npos) {
            appendText(inner.substr(position), AiCardBlock::Kind::Paragraph);
            break;
        }
        if (open > position) {
            appendText(inner.substr(position, open - position),
                       AiCardBlock::Kind::Paragraph);
        }

        const std::size_t tagEnd = inner.find('>', open);
        if (tagEnd == std::string::npos) break;

        std::string tagText = inner.substr(open + 1, tagEnd - open - 1);
        if (!tagText.empty() && tagText.back() == '/') tagText.pop_back();

        std::size_t nameEnd = 0;
        while (nameEnd < tagText.size() &&
               !std::isspace(static_cast<unsigned char>(tagText[nameEnd]))) {
            ++nameEnd;
        }
        const std::string name = lowerAscii(tagText.substr(0, nameEnd));

        if (name.empty() || name[0] == '/') {
            position = tagEnd + 1;
            continue;
        }

        AiCardBlock::Kind kind = AiCardBlock::Kind::Paragraph;
        bool known = true;
        if (name == "h" || name == "h1" || name == "h2" || name == "h3" ||
            name == "title") {
            kind = AiCardBlock::Kind::Heading;
        } else if (name == "p" || name == "text" || name == "div" ||
                   name == "content" || name == "body") {
            kind = AiCardBlock::Kind::Paragraph;
            known = false;   // 作为容器：继续扫描内部节点
        } else if (name == "li" || name == "item" || name == "bullet") {
            kind = AiCardBlock::Kind::Bullet;
        } else if (name == "note" || name == "tip" || name == "warning" ||
                   name == "reminder") {
            kind = AiCardBlock::Kind::Note;
        } else {
            known = false;
        }

        if (!known) {
            position = tagEnd + 1;
            continue;
        }

        const std::string closing = "</" + name + ">";
        const std::size_t close = lowerInner.find(closing, tagEnd + 1);
        if (close == std::string::npos) {
            // 容忍模型漏写闭合标签：只取到下一个标签之前。
            const std::size_t nextOpen = inner.find('<', tagEnd + 1);
            const std::size_t end =
                nextOpen == std::string::npos ? inner.size() : nextOpen;
            appendText(inner.substr(tagEnd + 1, end - tagEnd - 1), kind);
            position = end;
            continue;
        }

        appendText(inner.substr(tagEnd + 1, close - tagEnd - 1), kind);
        position = close + closing.size();
    }
}

} // namespace

// ============================================================================
// 解析入口
// ============================================================================

std::vector<AiCardSpec> parseAiCardsXml(const std::string& xml,
                                        int32_t screenWidth,
                                        int32_t screenHeight) {
    std::vector<AiCardSpec> cards;
    if (xml.empty()) return cards;

    std::string text = xml;
    replaceAll(text, "```XML", "");
    replaceAll(text, "```xml", "");
    replaceAll(text, "```", "");
    removeComments(text);

    std::size_t position = 0;
    while (position < text.size()) {
        const std::size_t open = text.find("<card", position);
        if (open == std::string::npos) break;

        const std::size_t nameEnd = open + 5;
        if (nameEnd < text.size() &&
            (std::isalnum(static_cast<unsigned char>(text[nameEnd])) ||
             text[nameEnd] == '_' || text[nameEnd] == '-')) {
            position = nameEnd;
            continue;
        }

        const std::size_t tagEnd = text.find('>', open);
        if (tagEnd == std::string::npos) break;

        const std::string tag = text.substr(open + 5, tagEnd - open - 5);
        if (!tag.empty() && tag.back() == '/') {
            position = tagEnd + 1;
            continue;
        }

        std::size_t close = text.find("</card>", tagEnd + 1);
        if (close == std::string::npos) close = text.size();

        AiCardSpec spec;
        spec.title = decodeEntities(trimAscii(attrValue(tag, "title")));
        spec.icon = trimAscii(attrValue(tag, "icon"));
        spec.x = parseAnchor(attrValue(tag, "x"), screenWidth, spec.x);
        spec.y = parseAnchor(attrValue(tag, "y"), screenHeight, spec.y);
        spec.width = parseSize(attrValue(tag, "width"), screenWidth);
        spec.height = parseSize(attrValue(tag, "height"), screenHeight);
        spec.accent = parseColor(attrValue(tag, "accent"), spec.accent);
        spec.background = parseColor(attrValue(tag, "background"), spec.background);
        spec.text = parseColor(attrValue(tag, "text"), spec.text);

        parseBlocks(text.substr(tagEnd + 1, close - tagEnd - 1), &spec.blocks);
        if (!spec.title.empty() || !spec.blocks.empty()) {
            cards.push_back(std::move(spec));
        }

        if (cards.size() >= 4) break;   // 最多 4 张，保证 z 区间可控
        position = close + 7;
    }
    return cards;
}

// ============================================================================
// 屏幕批注解析
// ============================================================================

namespace {

std::vector<int32_t> parseLineRefs(const std::string& value) {
    std::vector<int32_t> refs;
    std::string token;

    auto flush = [&]() {
        if (token.empty()) return;
        const std::size_t dash = token.find('-');
        if (dash != std::string::npos) {
            const int32_t first = static_cast<int32_t>(
                std::strtol(token.substr(0, dash).c_str(), nullptr, 10));
            const int32_t last = static_cast<int32_t>(
                std::strtol(token.substr(dash + 1).c_str(), nullptr, 10));
            if (first > 0 && last >= first && last - first < 64) {
                for (int32_t number = first; number <= last; ++number) {
                    refs.push_back(number);
                }
            }
        } else {
            const int32_t number = static_cast<int32_t>(
                std::strtol(token.c_str(), nullptr, 10));
            if (number > 0) refs.push_back(number);
        }
        token.clear();
    };

    for (const char c : value) {
        if (c == ',' || c == ';' || isAsciiSpace(c)) {
            flush();
        } else {
            token += c;
        }
    }
    flush();
    return refs;
}

Rect clampToScreen(double x, double y, double width, double height,
                   int32_t screenWidth, int32_t screenHeight) {
    int32_t rx = static_cast<int32_t>(std::lround(x));
    int32_t ry = static_cast<int32_t>(std::lround(y));
    int32_t rw = static_cast<int32_t>(std::lround(width));
    int32_t rh = static_cast<int32_t>(std::lround(height));

    if (rw <= 0 || rh <= 0) return Rect::null();
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > screenWidth) rw = screenWidth - rx;
    if (ry + rh > screenHeight) rh = screenHeight - ry;
    if (rw <= 0 || rh <= 0) return Rect::null();
    return Rect{rx, ry, rw, rh};
}

bool explicitRectFromTag(const std::string& tag, int32_t screenWidth,
                         int32_t screenHeight, Rect* out) {
    const std::string x = attrValue(tag, "x");
    const std::string y = attrValue(tag, "y");
    const std::string width = attrValue(tag, "width");
    const std::string height = attrValue(tag, "height");
    if (x.empty() || y.empty() || width.empty() || height.empty()) return false;

    const int32_t w = parseSize(width, screenWidth);
    const int32_t h = parseSize(height, screenHeight);
    if (w <= 0 || h <= 0) return false;

    const float fx = parseAnchor(x, screenWidth, -1.0f);
    const float fy = parseAnchor(y, screenHeight, -1.0f);
    if (fx < 0.0f || fy < 0.0f) return false;

    *out = clampToScreen(static_cast<double>(fx) * screenWidth,
                         static_cast<double>(fy) * screenHeight,
                         w, h, screenWidth, screenHeight);
    return !out->isNull();
}

bool explicitPointFromTag(const std::string& tag, const char* xName,
                          const char* yName, int32_t screenWidth,
                          int32_t screenHeight, Vec2f* out) {
    const std::string x = attrValue(tag, xName);
    const std::string y = attrValue(tag, yName);
    if (x.empty() || y.empty()) return false;

    const float fx = parseAnchor(x, screenWidth, -1.0f);
    const float fy = parseAnchor(y, screenHeight, -1.0f);
    if (fx < 0.0f || fy < 0.0f) return false;

    out->x = fx * static_cast<float>(screenWidth);
    out->y = fy * static_cast<float>(screenHeight);
    return true;
}

Vec2f centerOf(const Rect& rect) {
    return Vec2f{static_cast<float>(rect.x) + static_cast<float>(rect.w) * 0.5f,
                 static_cast<float>(rect.y) + static_cast<float>(rect.h) * 0.5f};
}

// 两个 OCR 行之间画箭头：从较左行的右侧指向较右行的左侧。
bool endpointsFromLines(const Rect& first, const Rect& second, Vec2f* from,
                        Vec2f* to) {
    const float firstCenterY = static_cast<float>(first.y + first.h / 2);
    const float secondCenterY = static_cast<float>(second.y + second.h / 2);

    if (second.x >= first.right()) {
        *from = Vec2f{static_cast<float>(first.right()) + 6.0f, firstCenterY};
        *to = Vec2f{static_cast<float>(second.x) - 8.0f, secondCenterY};
        return true;
    }
    if (first.x >= second.right()) {
        *from = Vec2f{static_cast<float>(first.x) - 6.0f, firstCenterY};
        *to = Vec2f{static_cast<float>(second.right()) + 8.0f, secondCenterY};
        return true;
    }
    *from = centerOf(first);
    *to = centerOf(second);
    return true;
}

float distanceToSegment(const Vec2f& point, const Vec2f& a, const Vec2f& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lengthSquared = dx * dx + dy * dy;

    float t = 0.0f;
    if (lengthSquared > 1.0e-6f) {
        t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared;
        t = std::max(0.0f, std::min(1.0f, t));
    }

    const float closestX = a.x + dx * t;
    const float closestY = a.y + dy * t;
    const float offsetX = point.x - closestX;
    const float offsetY = point.y - closestY;
    return std::sqrt(offsetX * offsetX + offsetY * offsetY);
}

bool markHitByEraser(const AiMarkSpec& spec, const Vec2f& point, float radius) {
    const float grow = std::max(2.0f, radius + 4.0f);

    switch (spec.kind) {
        case AiMarkSpec::Kind::Highlight:
        case AiMarkSpec::Kind::Circle:
        case AiMarkSpec::Kind::Box: {
            for (const Rect& rect : spec.rects) {
                if (rect.inflate(static_cast<int32_t>(std::lround(grow)),
                                 static_cast<int32_t>(std::lround(grow)))
                        .contains(static_cast<int32_t>(std::lround(point.x)),
                                  static_cast<int32_t>(std::lround(point.y)))) {
                    return true;
                }
            }
            return false;
        }

        case AiMarkSpec::Kind::Underline: {
            for (const Rect& rect : spec.rects) {
                const Vec2f a{static_cast<float>(rect.x + 1),
                              static_cast<float>(rect.bottom() + 1)};
                const Vec2f b{static_cast<float>(rect.right() - 1),
                              static_cast<float>(rect.bottom() + 1)};
                if (distanceToSegment(point, a, b) <= grow) return true;
            }
            return false;
        }

        case AiMarkSpec::Kind::Arrow:
        case AiMarkSpec::Kind::Line:
            return distanceToSegment(point, spec.from, spec.to) <= grow;
    }
    return false;
}

} // namespace

std::vector<AiMarkSpec> parseAiMarksXml(
    const std::string& xml, int32_t screenWidth, int32_t screenHeight,
    const std::vector<Rect>& ocrLines) {
    std::vector<AiMarkSpec> marks;
    if (xml.empty()) return marks;

    std::string text = xml;
    replaceAll(text, "```XML", "");
    replaceAll(text, "```xml", "");
    replaceAll(text, "```", "");
    removeComments(text);

    const int32_t width = std::max(1, screenWidth);
    const int32_t height = std::max(1, screenHeight);

    auto lineRect = [&](int32_t number) -> const Rect* {
        if (number < 1 ||
            number > static_cast<int32_t>(ocrLines.size())) {
            return nullptr;
        }
        return &ocrLines[static_cast<std::size_t>(number - 1)];
    };

    std::size_t position = 0;
    while (position < text.size()) {
        const std::size_t open = text.find('<', position);
        if (open == std::string::npos) break;
        const std::size_t tagEnd = text.find('>', open);
        if (tagEnd == std::string::npos) break;

        std::string tag = text.substr(open + 1, tagEnd - open - 1);
        position = tagEnd + 1;
        if (!tag.empty() && tag.back() == '/') tag.pop_back();

        std::size_t nameEnd = 0;
        while (nameEnd < tag.size() &&
               !std::isspace(static_cast<unsigned char>(tag[nameEnd]))) {
            ++nameEnd;
        }
        const std::string name = lowerAscii(tag.substr(0, nameEnd));

        AiMarkSpec spec;
        if (name == "highlight" || name == "mark") {
            spec.kind = AiMarkSpec::Kind::Highlight;
            spec.color = Color{255, 214, 64, 90};
        } else if (name == "underline") {
            spec.kind = AiMarkSpec::Kind::Underline;
            spec.color = Color{255, 214, 64, 235};
        } else if (name == "circle") {
            spec.kind = AiMarkSpec::Kind::Circle;
            spec.color = Color{255, 107, 107, 235};
        } else if (name == "box" || name == "rect") {
            spec.kind = AiMarkSpec::Kind::Box;
            spec.color = Color{255, 107, 107, 235};
        } else if (name == "arrow") {
            spec.kind = AiMarkSpec::Kind::Arrow;
            spec.color = Color{255, 107, 107, 235};
        } else if (name == "line") {
            spec.kind = AiMarkSpec::Kind::Line;
            spec.color = Color{255, 107, 107, 235};
        } else {
            continue;
        }

        spec.color = parseColor(attrValue(tag, "color"), spec.color);
        if (spec.kind == AiMarkSpec::Kind::Highlight) {
            // 高亮必须保持半透明：AI 给的 #RRGGBB 是不透明色，直接填会盖住文字。
            spec.color.a = 76;
        } else if (spec.color.a < 210) {
            // 线条 / 圈选 / 框选保持醒目。
            spec.color.a = 235;
        }
        spec.strokeWidth = std::max(
            0, std::min(16, parseSize(attrValue(tag, "stroke"), width)));

        const bool areaMark = (spec.kind == AiMarkSpec::Kind::Highlight ||
                               spec.kind == AiMarkSpec::Kind::Underline ||
                               spec.kind == AiMarkSpec::Kind::Circle ||
                               spec.kind == AiMarkSpec::Kind::Box);

        if (areaMark) {
            Rect explicitBox;
            if (explicitRectFromTag(tag, width, height, &explicitBox)) {
                spec.rects.push_back(explicitBox);
            }

            const std::vector<int32_t> refs =
                parseLineRefs(attrValue(tag, "lines"));
            if (!refs.empty()) {
                if (spec.kind == AiMarkSpec::Kind::Circle ||
                    spec.kind == AiMarkSpec::Kind::Box) {
                    // 圈选 / 框选多行：合并成一个区域
                    Rect united = Rect::null();
                    for (const int32_t number : refs) {
                        if (const Rect* rect = lineRect(number)) {
                            united = united.unite(*rect);
                        }
                    }
                    if (!united.isNull()) spec.rects.push_back(united);
                } else {
                    for (const int32_t number : refs) {
                        if (const Rect* rect = lineRect(number)) {
                            spec.rects.push_back(*rect);
                        }
                    }
                }
            }

            if (spec.rects.empty()) continue;
        } else {
            Vec2f from{};
            Vec2f to{};
            bool resolved = false;

            const std::vector<int32_t> fromRefs =
                parseLineRefs(attrValue(tag, "from"));
            const std::vector<int32_t> toRefs =
                parseLineRefs(attrValue(tag, "to"));
            if (!fromRefs.empty() && !toRefs.empty()) {
                const Rect* first = lineRect(fromRefs.front());
                const Rect* second = lineRect(toRefs.front());
                if (first && second) {
                    resolved = endpointsFromLines(*first, *second, &from, &to);
                }
            }

            if (!resolved &&
                !(explicitPointFromTag(tag, "x1", "y1", width, height, &from) &&
                  explicitPointFromTag(tag, "x2", "y2", width, height, &to))) {
                continue;
            }

            spec.from = from;
            spec.to = to;
        }

        marks.push_back(std::move(spec));
        if (marks.size() >= 12) break;
    }
    return marks;
}

// ============================================================================
// 屏幕批注图层
// ============================================================================

void AiAnnotationLayer::initialize(Canvas* canvas) {
    canvas_ = canvas;
}

void AiAnnotationLayer::clear() {
    if (canvas_) {
        for (Mark& mark : marks_) {
            for (const uint32_t id : mark.ids) {
                if (id != 0) canvas_->removeShape(id);
            }
        }
    }
    marks_.clear();
}

void AiAnnotationLayer::setMarks(std::vector<AiMarkSpec> marks) {
    clear();
    if (!canvas_ || marks.empty()) return;

    scale_ = std::max(0.85f, static_cast<float>(canvas_->height()) / 900.0f);

    marks_.reserve(marks.size());
    for (AiMarkSpec& spec : marks) {
        Mark mark;
        mark.spec = std::move(spec);
        marks_.push_back(std::move(mark));
    }
    for (std::size_t i = 0; i < marks_.size(); ++i) {
        marks_[i].delay = static_cast<float>(i) * 0.07f;
        buildMark(marks_[i], i);
    }
}

void AiAnnotationLayer::buildMark(Mark& mark, std::size_t index) {
    if (!canvas_) return;

    const int32_t baseZ = -2800 + static_cast<int32_t>(index) * 4;
    const int32_t stroke = mark.spec.strokeWidth > 0
        ? std::max(1, static_cast<int32_t>(std::lround(
              static_cast<float>(mark.spec.strokeWidth) * scale_)))
        : std::max(2, static_cast<int32_t>(std::lround(3.0f * scale_)));
    mark.spec.strokeWidth = stroke;

    int32_t z = baseZ;
    auto add = [&](Shape shape) {
        shape.base.z = z++;
        shape.base.opacity = 0.0f;
        mark.ids.push_back(canvas_->addShape(std::move(shape)));
    };

    switch (mark.spec.kind) {
        case AiMarkSpec::Kind::Highlight: {
            for (const Rect& rect : mark.spec.rects) {
                Shape shape;
                shape.data = RRectShape{rect.x - 2, rect.y - 1, rect.w + 4,
                                        rect.h + 2, 3.0f * scale_,
                                        3.0f * scale_};
                shape.base.fill = Paint(mark.spec.color);
                add(std::move(shape));
            }
            break;
        }

        case AiMarkSpec::Kind::Underline: {
            for (const Rect& rect : mark.spec.rects) {
                const int32_t x = rect.x + 1;
                const int32_t y = rect.bottom() + 1;
                Shape shape;
                shape.data = LineShape{x, y, x, y};
                shape.base.stroke = Paint(mark.spec.color);
                shape.base.strokeWidth = stroke;
                add(std::move(shape));
            }
            break;
        }

        case AiMarkSpec::Kind::Circle: {
            const Rect& rect = mark.spec.rects.front();
            const Vec2f center = centerOf(rect);
            Shape shape;
            shape.data = EllipseShape{
                static_cast<int32_t>(std::lround(center.x)),
                static_cast<int32_t>(std::lround(center.y)),
                static_cast<float>(rect.w) * 0.5f + 6.0f * scale_,
                static_cast<float>(rect.h) * 0.5f + 4.0f * scale_};
            shape.base.stroke = Paint(mark.spec.color);
            shape.base.strokeWidth = stroke;
            add(std::move(shape));
            break;
        }

        case AiMarkSpec::Kind::Box: {
            const Rect& rect = mark.spec.rects.front();
            Shape shape;
            shape.data = RRectShape{rect.x - 5, rect.y - 4, rect.w + 10,
                                    rect.h + 8, 6.0f * scale_,
                                    6.0f * scale_};
            shape.base.stroke = Paint(mark.spec.color);
            shape.base.strokeWidth = stroke;
            add(std::move(shape));
            break;
        }

        case AiMarkSpec::Kind::Line:
        case AiMarkSpec::Kind::Arrow: {
            const int32_t startX =
                static_cast<int32_t>(std::lround(mark.spec.from.x));
            const int32_t startY =
                static_cast<int32_t>(std::lround(mark.spec.from.y));

            Shape shaft;
            shaft.data = LineShape{startX, startY, startX, startY};
            shaft.base.stroke = Paint(mark.spec.color);
            shaft.base.strokeWidth = stroke;
            add(std::move(shaft));

            if (mark.spec.kind == AiMarkSpec::Kind::Arrow) {
                Shape head;
                PolylineShape polyline;
                polyline.closed = false;
                polyline.points = {Vec2i{startX, startY}, Vec2i{startX, startY},
                                   Vec2i{startX, startY}};
                head.data = std::move(polyline);
                head.base.stroke = Paint(mark.spec.color);
                head.base.strokeWidth = stroke;
                add(std::move(head));
            }
            break;
        }
    }
}

void AiAnnotationLayer::applyProgress(Mark& mark, float progress) {
    if (!canvas_ || mark.ids.empty()) return;

    const float p = std::max(0.0f, std::min(1.0f, progress));

    switch (mark.spec.kind) {
        case AiMarkSpec::Kind::Highlight:
        case AiMarkSpec::Kind::Circle:
        case AiMarkSpec::Kind::Box: {
            for (const uint32_t id : mark.ids) {
                canvas_->updateShape(id, [&](Shape& shape) {
                    shape.base.opacity = p;
                });
            }
            break;
        }

        case AiMarkSpec::Kind::Underline: {
            const std::size_t count = mark.ids.size();
            for (std::size_t i = 0; i < count; ++i) {
                const float local = std::max(
                    0.0f, std::min(1.0f,
                        p * static_cast<float>(count) - static_cast<float>(i)));
                const Rect& rect = mark.spec.rects[i];
                const int32_t x1 = rect.x + 1;
                const int32_t x2 = rect.right() - 1;
                const int32_t current = x1 + static_cast<int32_t>(std::lround(
                    static_cast<double>(x2 - x1) * local));
                const int32_t y = rect.bottom() + 1;

                canvas_->updateShape(mark.ids[i], [&](Shape& shape) {
                    if (auto* line = std::get_if<LineShape>(&shape.data)) {
                        line->x1 = x1;
                        line->y1 = y;
                        line->x2 = current;
                        line->y2 = y;
                    }
                    shape.base.opacity = local > 0.01f ? 1.0f : 0.0f;
                });
            }
            break;
        }

        case AiMarkSpec::Kind::Line:
        case AiMarkSpec::Kind::Arrow: {
            const Vec2f& from = mark.spec.from;
            const Vec2f& to = mark.spec.to;
            const float endX = from.x + (to.x - from.x) * p;
            const float endY = from.y + (to.y - from.y) * p;

            canvas_->updateShape(mark.ids[0], [&](Shape& shape) {
                if (auto* line = std::get_if<LineShape>(&shape.data)) {
                    line->x2 = static_cast<int32_t>(std::lround(endX));
                    line->y2 = static_cast<int32_t>(std::lround(endY));
                }
                shape.base.opacity = p > 0.01f ? 1.0f : 0.0f;
            });

            if (mark.spec.kind == AiMarkSpec::Kind::Arrow &&
                mark.ids.size() > 1 && p > 0.05f) {
                const float dx = endX - from.x;
                const float dy = endY - from.y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length > 1.0f) {
                    const float ux = dx / length;
                    const float uy = dy / length;
                    const float headLength = 12.0f * scale_;
                    const float wing = 6.0f * scale_;
                    const float baseX = endX - ux * headLength;
                    const float baseY = endY - uy * headLength;

                    canvas_->updateShape(mark.ids[1], [&](Shape& shape) {
                        if (auto* polyline =
                                std::get_if<PolylineShape>(&shape.data)) {
                            polyline->points = {
                                Vec2i{static_cast<int32_t>(std::lround(
                                          baseX - uy * wing)),
                                      static_cast<int32_t>(std::lround(
                                          baseY + ux * wing))},
                                Vec2i{static_cast<int32_t>(std::lround(endX)),
                                      static_cast<int32_t>(std::lround(endY))},
                                Vec2i{static_cast<int32_t>(std::lround(
                                          baseX + uy * wing)),
                                      static_cast<int32_t>(std::lround(
                                          baseY - ux * wing))}};
                        }
                        shape.base.opacity = 1.0f;
                    });
                }
            }
            break;
        }
    }
}

void AiAnnotationLayer::tick(float dtSeconds) {
    for (Mark& mark : marks_) {
        if (mark.settled) continue;

        mark.age += dtSeconds;
        if (mark.age < mark.delay) continue;

        const float t = std::min(1.0f, (mark.age - mark.delay) / 0.32f);
        const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        applyProgress(mark, eased);
        if (t >= 1.0f) mark.settled = true;
    }
}

bool AiAnnotationLayer::eraseAt(const Vec2f& point, float radius) {
    if (!canvas_ || marks_.empty()) return false;

    bool erased = false;
    for (std::size_t i = 0; i < marks_.size();) {
        Mark& mark = marks_[i];
        if (!markHitByEraser(mark.spec, point, radius)) {
            ++i;
            continue;
        }

        for (const uint32_t id : mark.ids) {
            if (id != 0) canvas_->removeShape(id);
        }
        marks_.erase(marks_.begin() + static_cast<std::ptrdiff_t>(i));
        erased = true;
    }
    return erased;
}

const char* aiCardSystemPrompt() {
    return R"(你是一位课堂智能助教，负责把老师当前屏幕上的内容整理成教学卡片。
用户会提供屏幕 OCR 文本。请判断画面内容（题目 / 课件 / 文档 / 作业），给出对课堂讲解最有用的提示。

只输出 XML，不要输出 Markdown 代码块、解释或任何其他文字。格式：
<cards>
  <card title="卡片标题" icon="book" x="62%" y="16%" width="380" height="300" accent="#5BC8FF" background="#12151E" text="#E9EEF8">
    <h>小标题</h>
    <p>正文段落</p>
    <li>要点</li>
    <note>易错提醒</note>
  </card>
</cards>

如果需要，还可以在画面上直接做批注（划线 / 高亮 / 圈选 / 框选 / 箭头 / 直线），写在 <cards> 后面：
<annotations>
  <underline lines="12" color="#FFD24A"/>
  <highlight lines="3,4" color="#FFD24A"/>
  <circle lines="8" color="#FF6B6B"/>
  <box lines="5-7" color="#7CD4FD"/>
  <arrow from="3" to="8" color="#FF6B6B"/>
  <line x1="20%" y1="40%" x2="45%" y2="40%" color="#FFFFFF" stroke="3"/>
</annotations>

规则：
1. 输出 1~3 张卡片，每张卡片正文不超过 260 字，语言简洁，适合投屏讲解。
2. x、y 是卡片左上角相对屏幕的百分比（0~100%）；width 是像素宽度（建议 320~460）；height 可省略，省略时按内容自适应。
3. 颜色必须用 #RRGGBB：accent 是强调色，background 是深色背景，text 是正文颜色。
4. icon 选填，只能从以下名称中选一个：book（知识点 / 教材）、explain（解析 / 讲解）、inspiration（思路 / 启发）、question（提问 / 思考）、tips（提示 / 易错）、roadmap（步骤 / 流程）。
5. 用户提供的每一行 OCR 文本前都有 #编号 和 [x%, y%, 宽%, 高%] 位置信息（相对屏幕的占比）。批注的 lines / from / to 直接引用 #编号（可写 "3,4" 或 "5-7"）；卡片和批注都请利用这些位置避开题目主体和关键板书。
6. 卡片优先放在屏幕左侧或中部；多张卡片纵向排列、互不重叠；右侧约 20% 区域是工具栏，避免遮挡。
7. 内容顺序：先给结论 / 答案，再给关键步骤，最后给易错提醒；可组合使用 <h>、<p>、<li>、<note>。
8. 批注最多 6 个，只标记最关键的内容（结论 / 易错点 / 关键步骤），不要整段划线；颜色醒目但不要遮挡文字。
9. 只使用 OCR 文本中能推断出的信息；识别错字按上下文纠正；不要编造细节。
10. 不要在卡片里重复整段 OCR 原文。
)";
}

// ============================================================================
// 卡片层
// ============================================================================

void AiCardLayer::initialize(Canvas* canvas, const std::string& iconDirectory) {
    canvas_ = canvas;
    iconDirectory_ = iconDirectory;
}

int32_t AiCardLayer::padding() const {
    return scaleInt(16.0f);
}

int32_t AiCardLayer::scaleInt(float value) const {
    return static_cast<int32_t>(std::lround(value * scale_));
}

void AiCardLayer::refreshScale() {
    if (!canvas_) return;

    // 卡片基准尺寸约 0.5x（字号在 layoutCard 中仍有可读性下限），
    // 再乘以用户可调的缩放系数（60%–200%）。
    constexpr float kCardSizeFactor = 0.5f;
    const float base = std::max(0.9f, std::min(2.2f,
        static_cast<float>(canvas_->height()) / 720.0f));
    scale_ = base * kCardSizeFactor * userScale_;
    scale_ = std::max(0.35f, std::min(2.6f, scale_));
}

void AiCardLayer::setUserScale(float factor) {
    const float clamped = std::max(0.6f, std::min(2.0f, factor));
    if (std::fabs(clamped - userScale_) < 0.001f) return;
    userScale_ = clamped;
    refreshScale();

    if (cards_.empty() || !canvas_) return;

    // 已有卡片：按新尺寸重新排版并重建，保留展开 / 收起状态。
    hoverIndex_ = -1;
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        Card& card = cards_[i];
        card.hovered = false;
        layoutCard(card);
        buildCard(card, static_cast<int32_t>(i));
    }
    canvas_->markDirtyAll();
}

void AiCardLayer::setCards(std::vector<AiCardSpec> specs) {
    clear();
    if (!canvas_) return;

    refreshScale();

    cards_.reserve(specs.size());
    for (AiCardSpec& spec : specs) {
        Card card;
        card.spec = std::move(spec);
        layoutCard(card);
        cards_.push_back(std::move(card));
    }

    for (std::size_t i = 0; i < cards_.size(); ++i) {
        buildCard(cards_[i], static_cast<int32_t>(i));
    }
}

void AiCardLayer::layoutCard(Card& card) {
    if (!canvas_) return;

    const int32_t canvasWidth = std::max(1, canvas_->width());
    const int32_t canvasHeight = std::max(1, canvas_->height());
    const int32_t pad = padding();

    card.headerHeight = std::max(40, scaleInt(54.0f));

    int32_t minWidth = scaleInt(300.0f);
    int32_t maxWidth = std::min(scaleInt(720.0f),
                                static_cast<int32_t>(canvasWidth * 0.62f));
    if (maxWidth < minWidth) maxWidth = std::max(minWidth, canvasWidth - 24);

    const int32_t requestedWidth =
        card.spec.width > 0 ? scaleInt(static_cast<float>(card.spec.width))
                            : scaleInt(380.0f);
    card.width = std::max(minWidth, std::min(maxWidth, requestedWidth));

    const int32_t bodyFont = std::max(12, scaleInt(14.0f));
    const int32_t bodyLine = std::max(17, scaleInt(21.0f));
    const int32_t headingFont = std::max(13, scaleInt(15.5f));
    const int32_t headingLine = std::max(19, scaleInt(24.0f));
    const int32_t gapHeading = scaleInt(12.0f);
    const int32_t gapParagraph = scaleInt(8.0f);
    const int32_t gapBullet = scaleInt(5.0f);
    const int32_t gapNote = scaleInt(10.0f);
    const int32_t bulletIndent = scaleInt(16.0f);
    const int32_t notePadding = scaleInt(9.0f);
    const int32_t innerWidth = std::max(32, card.width - pad * 2);

    card.layout.clear();
    int32_t contentHeight = 0;

    for (const AiCardBlock& block : card.spec.blocks) {
        LayoutBlock item;
        item.kind = block.kind;

        switch (block.kind) {
            case AiCardBlock::Kind::Heading:
                item.fontSize = headingFont;
                item.lineHeight = headingLine;
                item.gapBefore = card.layout.empty() ? 0 : gapHeading;
                item.wrapWidth = innerWidth;
                break;

            case AiCardBlock::Kind::Bullet:
                item.fontSize = bodyFont;
                item.lineHeight = bodyLine;
                item.gapBefore = gapBullet;
                item.indent = bulletIndent;
                item.wrapWidth = std::max(24, innerWidth - bulletIndent);
                break;

            case AiCardBlock::Kind::Note:
                item.fontSize = bodyFont;
                item.lineHeight = bodyLine;
                item.gapBefore = gapNote;
                item.boxPadding = notePadding;
                item.wrapWidth = std::max(24, innerWidth - notePadding * 2);
                break;

            default:
                item.fontSize = bodyFont;
                item.lineHeight = bodyLine;
                item.gapBefore = card.layout.empty() ? 0 : gapParagraph;
                item.wrapWidth = innerWidth;
                break;
        }

        item.lines = wrapText(*canvas_, block.text, item.fontSize,
                              std::max(24, item.wrapWidth));
        if (item.lines.empty()) continue;

        item.height = static_cast<int32_t>(item.lines.size()) * item.lineHeight +
                      item.boxPadding * 2;
        contentHeight += item.gapBefore + item.height;
        card.layout.push_back(std::move(item));
    }

    card.contentHeight = contentHeight;

    const int32_t autoHeight = card.headerHeight + pad * 2 + contentHeight;
    const int32_t requestedHeight =
        card.spec.height > 0 ? scaleInt(static_cast<float>(card.spec.height))
                             : autoHeight;
    const int32_t minHeight = card.headerHeight + scaleInt(90.0f);
    const int32_t maxHeight = std::max(
        minHeight,
        std::min(canvasHeight - 16,
                 static_cast<int32_t>(canvasHeight * 0.80f)));
    card.expandedHeight = std::max(minHeight, std::min(maxHeight, requestedHeight));
    card.viewportHeight =
        std::max(1, card.expandedHeight - card.headerHeight - pad * 2);
    card.height = card.expanded ? card.expandedHeight : card.headerHeight;

    const int32_t margin = 10;
    int32_t x = static_cast<int32_t>(std::lround(card.spec.x * canvasWidth));
    int32_t y = static_cast<int32_t>(std::lround(card.spec.y * canvasHeight));
    x = std::max(margin, std::min(canvasWidth - card.width - margin, x));
    y = std::max(margin, std::min(canvasHeight - card.expandedHeight - margin, y));
    if (x < margin) x = margin;
    if (y < margin) y = margin;

    card.frame = Rect{x, y, card.width, card.height};
    card.contentTop = y + card.headerHeight + pad;
    card.scroll = 0;
}

void AiCardLayer::destroyCard(Card& card) {
    if (!canvas_) return;
    for (uint32_t id : {card.groupId, card.contentGroupId,
                        card.trackId, card.thumbId}) {
        if (id != 0) canvas_->removeShape(id);
    }
    card.groupId = 0;
    card.contentGroupId = 0;
    card.trackId = 0;
    card.thumbId = 0;
}

void AiCardLayer::buildCard(Card& card, int32_t index) {
    if (!canvas_) return;

    destroyCard(card);

    card.height = card.expanded ? card.expandedHeight : card.headerHeight;
    card.frame.h = card.height;

    const int32_t x = card.frame.x;
    const int32_t y = card.frame.y;
    const int32_t w = card.width;
    const int32_t h = card.height;
    const int32_t headerHeight = card.headerHeight;
    const int32_t pad = padding();
    const int32_t radius = static_cast<int32_t>(scaleInt(16.0f));

    const int32_t bandZ = kCardBandBase + index * kCardBandStep;
    const int32_t bgZ = bandZ;
    const int32_t contentZ = bandZ + kContentZOffset;
    const int32_t scrollZ = bandZ + kScrollZOffset;

    Color background = card.spec.background;
    background.a = std::max<uint8_t>(background.a, 226);
    const Color foreground = card.spec.text;
    Color accent = card.spec.accent;
    if (accent.a < 200) accent.a = 255;

    if (card.hovered) {
        background.r = static_cast<uint8_t>(std::min(255, background.r + 10));
        background.g = static_cast<uint8_t>(std::min(255, background.g + 10));
        background.b = static_cast<uint8_t>(std::min(255, background.b + 12));
    }

    GroupData group;
    int32_t nextZ = bgZ;

    auto addChild = [&](Shape shape) {
        shape.base.z = nextZ++;
        group.children.push_back(std::move(shape));
    };

    // 背景
    {
        Shape shape;
        shape.data = RRectShape{x, y, w, h, static_cast<float>(radius),
                                static_cast<float>(radius)};
        shape.base.fill = Paint(background);
        shape.base.stroke = Paint(Color{255, 255, 255,
                                        static_cast<uint8_t>(card.hovered ? 46 : 26)});
        shape.base.strokeWidth = 1;
        addChild(std::move(shape));
    }

    // 顶部细高光
    {
        Shape shape;
        shape.data = RRectShape{x + radius, y + 1,
                                std::max(1, w - radius * 2),
                                std::max(1, scaleInt(1.0f)), 1.0f, 1.0f};
        shape.base.fill = Paint(Color{255, 255, 255, 22});
        addChild(std::move(shape));
    }

    // 左侧强调条
    {
        const int32_t barWidth = std::max(3, scaleInt(3.0f));
        const int32_t barPad = scaleInt(9.0f);
        Shape shape;
        shape.data = RRectShape{x, y + barPad, barWidth,
                                std::max(barWidth, headerHeight - barPad * 2),
                                barWidth * 0.5f, barWidth * 0.5f};
        shape.base.fill = Paint(accent);
        addChild(std::move(shape));
    }

    // 标题图标 / 色点
    int32_t titleX = 0;
    {
        const std::string markup = card.spec.icon.empty()
            ? std::string()
            : iconMarkupFor(card.spec.icon, accent);
        const int32_t iconSize = scaleInt(24.0f);
        const int32_t iconX = x + scaleInt(18.0f);

        if (!markup.empty()) {
            Shape shape;
            shape.data = SvgShape{markup, iconX, y + (headerHeight - iconSize) / 2,
                                  iconSize, iconSize, true};
            addChild(std::move(shape));
            titleX = iconX + iconSize + scaleInt(10.0f);
        } else {
            Shape shape;
            shape.data = CircleShape{x + scaleInt(24.0f), y + headerHeight / 2,
                                     4.0f * scale_};
            shape.base.fill = Paint(accent);
            addChild(std::move(shape));
            titleX = x + scaleInt(38.0f);
        }
    }

    // 标题
    {
        const int32_t titleFont = std::max(13, scaleInt(15.5f));
        const int32_t titleRight = x + w - scaleInt(94.0f);
        const int32_t titleWidth = std::max(scaleInt(40.0f), titleRight - titleX);
        std::string title = card.spec.title.empty() ? std::string("AI 提示")
                                                    : card.spec.title;
        title = ellipsize(*canvas_, title, titleFont, titleWidth);

        const Rect measured = measureLine(*canvas_, title, titleFont);
        TextShape text;
        text.text = title;
        text.fontSize = titleFont;
        text.x = titleX;
        text.y = y + (headerHeight - measured.h) / 2 - measured.y;

        Shape shape;
        shape.data = std::move(text);
        shape.base.fill = Paint(withAlpha(foreground, 246));
        addChild(std::move(shape));
    }

    // 展开箭头
    {
        const int32_t centerX = x + w - scaleInt(58.0f);
        const int32_t centerY = y + headerHeight / 2;
        const int32_t arm = std::max(4, scaleInt(5.0f));
        const int32_t rise = std::max(2, scaleInt(3.0f));

        PolylineShape chevron;
        chevron.closed = false;
        if (card.expanded) {
            chevron.points = {Vec2i{centerX - arm, centerY + rise},
                              Vec2i{centerX, centerY - rise},
                              Vec2i{centerX + arm, centerY + rise}};
        } else {
            chevron.points = {Vec2i{centerX - arm, centerY - rise},
                              Vec2i{centerX, centerY + rise},
                              Vec2i{centerX + arm, centerY - rise}};
        }

        Shape shape;
        shape.data = std::move(chevron);
        shape.base.stroke = Paint(withAlpha(foreground,
                                            static_cast<uint8_t>(card.hovered ? 220 : 160)));
        shape.base.strokeWidth = std::max(1, scaleInt(1.8f));
        addChild(std::move(shape));
    }

    // 关闭按钮
    {
        const int32_t centerX = x + w - scaleInt(24.0f);
        const int32_t centerY = y + headerHeight / 2;
        const int32_t arm = std::max(3, scaleInt(4.5f));
        const uint8_t alpha = static_cast<uint8_t>(card.hovered ? 210 : 110);

        for (int32_t direction = -1; direction <= 1; direction += 2) {
            Shape shape;
            shape.data = LineShape{centerX - arm, centerY + direction * arm,
                                   centerX + arm, centerY - direction * arm};
            shape.base.stroke = Paint(withAlpha(foreground, alpha));
            shape.base.strokeWidth = std::max(1, scaleInt(1.4f));
            addChild(std::move(shape));
        }
    }

    // 分隔线
    if (card.expanded) {
        Shape shape;
        shape.data = LineShape{x + pad, y + headerHeight,
                               x + w - pad, y + headerHeight};
        shape.base.stroke = Paint(Color{255, 255, 255, 26});
        shape.base.strokeWidth = 1;
        addChild(std::move(shape));
    }

    Shape groupShape;
    groupShape.data = std::move(group);
    groupShape.base.z = bgZ;
    card.groupId = canvas_->addShape(std::move(groupShape));

    if (!card.expanded) return;

    // 正文（独立 Group：clipRect = 视口，scrollOffset = 滚动量）
    {
        GroupData content;
        content.clipRect = Rect{x + 1, y + headerHeight,
                                std::max(1, w - 2),
                                std::max(1, h - headerHeight)};
        content.scrollOffset = Vec2i{0, -card.scroll};

        int32_t childZ = contentZ;
        auto addContent = [&](Shape shape) {
            shape.base.z = childZ++;
            content.children.push_back(std::move(shape));
        };

        if (card.layout.empty()) {
            const int32_t fontSize = std::max(12, scaleInt(13.5f));
            const std::string text = "（暂无内容）";
            const Rect measured = measureLine(*canvas_, text, fontSize);
            TextShape label;
            label.text = text;
            label.fontSize = fontSize;
            label.x = x + pad;
            label.y = card.contentTop + (scaleInt(21.0f) - measured.h) / 2 - measured.y;

            Shape shape;
            shape.data = std::move(label);
            shape.base.fill = Paint(withAlpha(foreground, 150));
            addContent(std::move(shape));
        }

        int32_t cursor = card.contentTop;
        for (const LayoutBlock& block : card.layout) {
            cursor += block.gapBefore;

            const Color blockColor =
                block.kind == AiCardBlock::Kind::Heading
                    ? withAlpha(mixColor(accent, Color::white(), 0.55f), 255)
                    : withAlpha(foreground, static_cast<uint8_t>(
                          block.kind == AiCardBlock::Kind::Note ? 248 : 234));

            if (block.kind == AiCardBlock::Kind::Note) {
                Shape box;
                box.data = RRectShape{x + pad, cursor, std::max(1, w - pad * 2),
                                      block.height, static_cast<float>(scaleInt(10.0f)),
                                      static_cast<float>(scaleInt(10.0f))};
                box.base.fill = Paint(withAlpha(accent, 30));
                box.base.stroke = Paint(withAlpha(accent, 70));
                box.base.strokeWidth = 1;
                addContent(std::move(box));

                Shape bar;
                bar.data = RRectShape{x + pad + scaleInt(3.0f),
                                      cursor + block.boxPadding,
                                      std::max(2, scaleInt(2.5f)),
                                      std::max(2, block.height - block.boxPadding * 2),
                                      1.5f, 1.5f};
                bar.base.fill = Paint(withAlpha(accent, 170));
                addContent(std::move(bar));
            }

            if (block.kind == AiCardBlock::Kind::Bullet) {
                Shape bullet;
                bullet.data = CircleShape{
                    x + pad + scaleInt(3.0f),
                    cursor + block.height / 2,
                    std::max(2.0f, 2.6f * scale_)};
                bullet.base.fill = Paint(withAlpha(accent, 200));
                addContent(std::move(bullet));
            }

            const int32_t textX = x + pad + block.indent +
                (block.kind == AiCardBlock::Kind::Note ? block.boxPadding : 0);
            int32_t lineTop = cursor +
                (block.kind == AiCardBlock::Kind::Note ? block.boxPadding : 0);

            for (const std::string& line : block.lines) {
                const Rect measured = measureLine(*canvas_, line, block.fontSize);
                TextShape text;
                text.text = line;
                text.fontSize = block.fontSize;
                text.x = textX;
                text.y = lineTop + (block.lineHeight - measured.h) / 2 - measured.y;

                Shape shape;
                shape.data = std::move(text);
                shape.base.fill = Paint(blockColor);
                addContent(std::move(shape));

                lineTop += block.lineHeight;
            }

            cursor += block.height;
        }

        Shape contentShape;
        contentShape.data = std::move(content);
        contentShape.base.z = contentZ;
        card.contentGroupId = canvas_->addShape(std::move(contentShape));
    }

    // 滚动条
    if (card.contentHeight > card.viewportHeight) {
        const int32_t trackWidth = std::max(2, scaleInt(3.0f));
        const int32_t trackX = x + w - scaleInt(9.0f);
        const int32_t trackY = y + headerHeight + pad;
        const int32_t trackHeight = std::max(8, card.viewportHeight);

        const int32_t thumbHeight = std::max(
            scaleInt(28.0f),
            static_cast<int32_t>(std::lround(
                static_cast<double>(trackHeight) * card.viewportHeight /
                std::max(1, card.contentHeight))));
        const int32_t maxScroll =
            std::max(1, card.contentHeight - card.viewportHeight);
        const int32_t thumbY = trackY + static_cast<int32_t>(std::lround(
            static_cast<double>(trackHeight - thumbHeight) * card.scroll / maxScroll));

        Shape track;
        track.data = RRectShape{trackX, trackY, trackWidth, trackHeight,
                                trackWidth * 0.5f, trackWidth * 0.5f};
        track.base.fill = Paint(Color{255, 255, 255, 26});
        track.base.z = scrollZ;
        card.trackId = canvas_->addShape(std::move(track));

        Shape thumb;
        thumb.data = RRectShape{trackX, thumbY, trackWidth,
                                std::min(trackHeight, thumbHeight),
                                trackWidth * 0.5f, trackWidth * 0.5f};
        thumb.base.fill = Paint(withAlpha(accent, 170));
        thumb.base.z = scrollZ + 1;
        card.thumbId = canvas_->addShape(std::move(thumb));
    }
}

void AiCardLayer::updateScroll(Card& card) {
    if (!canvas_) return;

    if (card.contentGroupId != 0) {
        canvas_->updateShape(card.contentGroupId, [&](Shape& shape) {
            if (auto* group = std::get_if<GroupData>(&shape.data)) {
                group->scrollOffset = Vec2i{0, -card.scroll};
            }
        });
    }

    if (card.thumbId == 0) return;

    const int32_t trackHeight = std::max(8, card.viewportHeight);
    const int32_t thumbHeight = std::max(
        scaleInt(28.0f),
        static_cast<int32_t>(std::lround(
            static_cast<double>(trackHeight) * card.viewportHeight /
            std::max(1, card.contentHeight))));
    const int32_t maxScroll = std::max(1, card.contentHeight - card.viewportHeight);
    const int32_t trackY = card.frame.y + card.headerHeight + padding();
    const int32_t thumbY = trackY + static_cast<int32_t>(std::lround(
        static_cast<double>(trackHeight - thumbHeight) * card.scroll / maxScroll));

    canvas_->updateShape(card.thumbId, [&](Shape& shape) {
        if (auto* rrect = std::get_if<RRectShape>(&shape.data)) {
            rrect->y = thumbY;
        }
    });
}

void AiCardLayer::clear() {
    for (Card& card : cards_) destroyCard(card);
    cards_.clear();
    hoverIndex_ = -1;
    dragIndex_ = -1;
    dragMoved_ = false;
    dragOnClose_ = false;
    dragBuilt_ = false;
}

bool AiCardLayer::hitTest(const Vec2i& point) const {
    for (const Card& card : cards_) {
        if (card.frame.contains(point)) return true;
    }
    return false;
}

Rect AiCardLayer::closeButtonRect(const Card& card) const {
    return Rect{
        card.frame.x + card.width - scaleInt(40.0f),
        card.frame.y,
        scaleInt(34.0f),
        card.headerHeight};
}

void AiCardLayer::commitCardPosition(Card& card) {
    if (!canvas_) return;
    // 记录为屏幕比例，后续缩放重建卡片时保持拖动后的位置。
    card.spec.x = static_cast<float>(card.frame.x) /
                  static_cast<float>(std::max(1, canvas_->width()));
    card.spec.y = static_cast<float>(card.frame.y) /
                  static_cast<float>(std::max(1, canvas_->height()));
}

bool AiCardLayer::handlePress(const Vec2i& point) {
    for (int32_t index = static_cast<int32_t>(cards_.size()) - 1;
         index >= 0; --index) {
        Card& card = cards_[static_cast<std::size_t>(index)];
        if (!card.frame.contains(point)) continue;

        dragIndex_ = index;
        dragStartPoint_ = point;
        dragStartFrame_ = card.frame;
        dragMoved_ = false;
        dragOnClose_ = closeButtonRect(card).contains(point);
        dragBuilt_ = false;
        return true;
    }
    return false;
}

bool AiCardLayer::handleDrag(const Vec2i& point) {
    if (dragIndex_ < 0 || dragIndex_ >= static_cast<int32_t>(cards_.size())) {
        return false;
    }
    if (dragOnClose_) return false;   // 关闭按钮上不触发拖动

    Card& card = cards_[static_cast<std::size_t>(dragIndex_)];

    if (!dragMoved_) {
        const int32_t dx = point.x - dragStartPoint_.x;
        const int32_t dy = point.y - dragStartPoint_.y;
        if (std::max(std::abs(dx), std::abs(dy)) < kDragThresholdPx) {
            return true;   // 尚未超过拖动阈值：仍视为点击候选
        }
        dragMoved_ = true;
    }

    if (!canvas_) return false;

    const int32_t margin = 10;
    const int32_t maxX = std::max(
        margin, canvas_->width() - card.frame.w - margin);
    const int32_t maxY = std::max(
        margin, canvas_->height() - card.frame.h - margin);
    const int32_t x = std::max(margin, std::min(
        maxX, dragStartFrame_.x + point.x - dragStartPoint_.x));
    const int32_t y = std::max(margin, std::min(
        maxY, dragStartFrame_.y + point.y - dragStartPoint_.y));

    if (x == card.frame.x && y == card.frame.y) return true;

    card.frame.x = x;
    card.frame.y = y;
    card.contentTop = y + card.headerHeight + padding();

    const uint64_t now = monotonicMillis();
    if (!dragBuilt_ || now - lastDragBuildMs_ >= kDragBuildIntervalMs) {
        lastDragBuildMs_ = now;
        dragBuilt_ = true;
        buildCard(card, dragIndex_);
    }
    return true;
}

bool AiCardLayer::handleRelease(const Vec2i& point) {
    if (dragIndex_ < 0 || dragIndex_ >= static_cast<int32_t>(cards_.size())) {
        dragIndex_ = -1;
        dragMoved_ = false;
        dragOnClose_ = false;
        dragBuilt_ = false;
        return false;
    }

    const int32_t index = dragIndex_;
    Card& card = cards_[static_cast<std::size_t>(index)];
    const bool moved = dragMoved_;
    dragIndex_ = -1;
    dragMoved_ = false;
    dragOnClose_ = false;

    commitCardPosition(card);

    if (moved) {
        // 节流可能让最后一帧视觉落后：松开时按最终位置重建一次。
        card.hovered = false;
        buildCard(card, index);
        dragBuilt_ = false;
        hoverIndex_ = -1;
        return true;
    }

    dragBuilt_ = false;
    if (!card.frame.contains(point)) return true;
    if (closeButtonRect(card).contains(point)) {
        removeCard(index);
        return true;
    }

    // 短按：展开 / 收起。
    card.expanded = !card.expanded;
    card.scroll = 0;
    buildCard(card, index);
    hoverIndex_ = index;
    return true;
}

bool AiCardLayer::handleWheel(const Vec2i& point, float wheelDelta) {
    for (int32_t index = static_cast<int32_t>(cards_.size()) - 1;
         index >= 0; --index) {
        Card& card = cards_[static_cast<std::size_t>(index)];
        if (!card.frame.contains(point)) continue;

        if (!card.expanded) return true;

        const int32_t maxScroll =
            std::max(0, card.contentHeight - card.viewportHeight);
        if (maxScroll <= 0) return true;

        const int32_t step = std::max(8, scaleInt(64.0f));
        const int32_t next = std::max(
            0, std::min(maxScroll,
                        card.scroll - static_cast<int32_t>(
                            std::lround(wheelDelta * static_cast<float>(step)))));
        if (next != card.scroll) {
            card.scroll = next;
            updateScroll(card);
        }
        return true;
    }
    return false;
}

void AiCardLayer::updateHover(const Vec2i& point) {
    int32_t hovered = -1;
    for (int32_t index = static_cast<int32_t>(cards_.size()) - 1;
         index >= 0; --index) {
        if (cards_[static_cast<std::size_t>(index)].frame.contains(point)) {
            hovered = index;
            break;
        }
    }
    if (hovered == hoverIndex_) return;

    if (hoverIndex_ >= 0 && hoverIndex_ < static_cast<int32_t>(cards_.size())) {
        Card& previous = cards_[static_cast<std::size_t>(hoverIndex_)];
        previous.hovered = false;
        buildCard(previous, hoverIndex_);
    }
    if (hovered >= 0) {
        Card& current = cards_[static_cast<std::size_t>(hovered)];
        current.hovered = true;
        buildCard(current, hovered);
    }
    hoverIndex_ = hovered;
}

void AiCardLayer::removeCard(int32_t index) {
    if (index < 0 || index >= static_cast<int32_t>(cards_.size())) return;
    destroyCard(cards_[static_cast<std::size_t>(index)]);
    cards_.erase(cards_.begin() + index);
    hoverIndex_ = -1;
}

Rect AiCardLayer::bounds() const {
    Rect result = Rect::null();
    for (const Card& card : cards_) result = result.unite(card.frame);
    return result;
}

std::vector<Rect> AiCardLayer::frames() const {
    std::vector<Rect> result;
    result.reserve(cards_.size());
    for (const Card& card : cards_) result.push_back(card.frame);
    return result;
}

std::string AiCardLayer::iconMarkupFor(const std::string& icon,
                                       const Color& accent) const {
    const std::string file = iconFileForName(icon);
    if (file.empty()) return {};

    std::string raw;
    for (const auto& entry : iconCache_) {
        if (entry.first == file) {
            raw = entry.second;
            break;
        }
    }
    if (raw.empty()) {
        raw = readTextFile(joinPath(iconDirectory_, file));
        iconCache_.emplace_back(file, raw);
    }
    return tintSvg(raw, accent);
}

} // namespace annotation
