// ============================================================================
// DocumentParser.cpp
// ============================================================================

#include "DocumentParser.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace annotation {

namespace {

std::string trimAscii(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (begin < end && isSpace(text[begin])) ++begin;
    while (end > begin && isSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

float quadTop(const OcrLine& line) {
    return std::min(std::min(line.quad[0].y, line.quad[1].y),
                    std::min(line.quad[2].y, line.quad[3].y));
}

float quadBottom(const OcrLine& line) {
    return std::max(std::max(line.quad[0].y, line.quad[1].y),
                    std::max(line.quad[2].y, line.quad[3].y));
}

} // namespace

const char* documentParserSystemPrompt() {
    return
        "你是文档原文还原器。用户会给你一段 OCR 识别结果，请把它还原成 Markdown。\n"
        "\n"
        "必须遵守：\n"
        "1. 原文照录：只允许整理排版，绝对不得改写、润色、扩写、缩写、总结、"
        "翻译、补充或删除任何原文内容；不得自行添加标题、小标题、说明、评论、"
        "答案、注解。\n"
        "2. 修复乱码：根据上下文推断 OCR 最可能对应的正确字符与标点，"
        "修正明显由识别错误造成的乱码；无法确定时保留最可能的原样，"
        "禁止凭空编造原文没有的内容。\n"
        "3. 还原结构：合并被 OCR 拆断的同一行 / 句子；保留原文的段落、"
        "换行、列表、表格、公式与编号；原文没有的结构不要添加。\n"
        "4. 保留原文语言与大小写，不要翻译。\n"
        "5. 只输出 Markdown 正文本身：不要用 ```markdown 代码块包裹，"
        "不要输出任何解释、前言、后记或“以下是整理结果”等文字。\n"
        "\n"
        "示例：OCR 输入“3 . 2 二次 函数 / 图 像 是 抛物 线” → 输出 "
        "“3.2 二次函数\\n\\n图像是抛物线”。";
}

std::string buildDocumentOcrPayload(int32_t screenWidth, int32_t screenHeight,
                                    const std::vector<OcrLine>& lines) {
    std::string text;
    text.reserve(8192);

    char header[160];
    std::snprintf(header, sizeof(header),
                  "屏幕分辨率：%d×%d\n"
                  "下面是按阅读顺序排列的 OCR 结果（每行一条；空行表示原文可能是分段）：\n\n",
                  static_cast<int>(screenWidth), static_cast<int>(screenHeight));
    text = header;

    float previousBottom = 0.0f;
    float previousHeight = 0.0f;
    bool first = true;

    for (const OcrLine& line : lines) {
        const std::string value = trimAscii(line.text);
        if (value.empty()) continue;

        if (!first) {
            const float top = quadTop(line);
            const float height =
                std::max(1.0f, quadBottom(line) - top);
            const float gap = top - previousBottom;
            if (gap > std::max(previousHeight, height) * 1.05f) {
                text += '\n';
            }
        }

        text += value;
        text += '\n';

        previousBottom = quadBottom(line);
        previousHeight =
            std::max(1.0f, previousBottom - quadTop(line));
        first = false;

        if (text.size() > 16000) break;
    }

    return text;
}

std::string extractMarkdownFromReply(const std::string& reply) {
    std::string text = reply;

    // 去掉 UTF-8 BOM。
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }

    text = trimAscii(text);

    // 只有整段被代码围栏包住时才剥掉围栏，避免误删正文里的代码块。
    if (text.size() >= 3 &&
        text.compare(0, 3, "```") == 0) {
        const std::size_t firstNewline = text.find('\n');
        if (firstNewline != std::string::npos) {
            const std::size_t closing = text.rfind("```");
            if (closing != std::string::npos && closing > firstNewline) {
                const std::string inside =
                    trimAscii(text.substr(firstNewline + 1,
                                          closing - firstNewline - 1));
                if (!inside.empty()) text = inside;
            }
        }
    }

    return trimAscii(text);
}

} // namespace annotation
