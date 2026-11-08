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

} // namespace

const char* documentParserSystemPrompt() {
    return
        "你是文档原文还原器。用户会给你一段 OCR 原始识别结果（每行带精确坐标），"
        "请把它还原成 Markdown。\n"
        "\n"
        "输入格式：\n"
        "- 第一行是屏幕分辨率；之后每行形如 "
        "[x1,y1 x2,y2 x3,y3 x4,y4 | 置信度] 文本；\n"
        "- 四个点依次是该行文字框的左上、右上、右下、左下角，单位为像素，"
        "原点在左上角、y 轴向下；\n"
        "- 行出现的先后是 OCR 引擎输出顺序，不代表阅读顺序。\n"
        "\n"
        "必须遵守：\n"
        "1. 原文照录：只允许整理排版，绝对不得改写、润色、扩写、缩写、总结、"
        "翻译、补充或删除任何原文内容；不得自行添加标题、小标题、说明、评论、"
        "答案、注解。\n"
        "2. 修复乱码：根据上下文推断 OCR 最可能对应的正确字符与标点，"
        "修正明显由识别错误造成的乱码；无法确定时保留最可能的原样，"
        "禁止凭空编造原文没有的内容。\n"
        "3. 先分区再还原：利用坐标把画面划分成独立区域（分栏、卡片、侧边栏、"
        "表格、页眉页脚等），逐区域整理。同一高度但横向相距很远的文字属于"
        "不同区域 / 不同栏，绝对不能按 OCR 顺序拼接或合并成同一段；"
        "不同区域的文字不要混在一起。\n"
        "4. 区域内部按从上到下、从左到右的自然阅读顺序输出；多栏内容按栏依次输出，"
        "不要各栏逐行交错。跨区域的内容不要强行连成一句。\n"
        "5. 还原结构：只合并同一区域内位置连续、明显被 OCR 拆断的同一行 / 句子；"
        "保留原文的段落、换行、列表、表格、公式与编号；原文没有的结构不要添加。\n"
        "6. 保留原文语言与大小写，不要翻译。\n"
        "7. 只输出 Markdown 正文本身：不要用 ```markdown 代码块包裹，"
        "不要输出任何解释、前言、后记或“以下是整理结果”等文字。\n"
        "\n"
        "排版要求（只用 Markdown 语法表达原文已有的结构，不得新增任何文字）：\n"
        "8. 原文本身是标题 / 小节名的行，用 #、##、### 体现层级（最多三级），"
        "标题文字必须与原文完全一致。\n"
        "9. 原文中的并列条目用列表（- 或 1.），每个条目单独成行并保持统一缩进；"
        "续行用两个空格缩进，使其渲染在同一个条目内。\n"
        "10. 原文中的表格式内容用 Markdown 表格（| 列 | 列 |），"
        "并补上 | --- | --- | 表头分隔行；同一行的单元格文字按 x 坐标归入对应列，"
        "单元格内容保持原文，允许增删空格对齐。\n"
        "11. 原文中的引文 / 摘录用 > 引用块；代码、命令行、等宽数据用 ``` 代码块；"
        "原文中明显的分隔线用 ---。\n"
        "12. 不同区域之间用空行分隔；段落之间空一行；段落内部不要为了凑行宽"
        "而强行换行，让 Markdown 渲染器自动折行。\n"
        "13. 以上语法只在原文确实存在对应结构时使用；不得为了“好看”新增标题、"
        "列表、表格、分隔线、加粗或任何原文没有的内容。\n"
        "\n"
        "示例：OCR 输入“[100,80 420,80 420,110 100,110 | 0.98] 3 . 2 二次 函数"
        "[100,120 780,120 780,150 100,150 | 0.96] 图 像 是 抛物 线” → 输出 "
        "“### 3.2 二次函数\\n\\n图像是抛物线”。";
}

std::string buildDocumentOcrPayload(int32_t screenWidth, int32_t screenHeight,
                                    const std::vector<OcrLine>& lines) {
    std::string text;
    text.reserve(32768);

    char header[256];
    std::snprintf(header, sizeof(header),
                  "屏幕分辨率：%d×%d（像素，原点在左上角，y 轴向下）\n"
                  "以下是 OCR 原始识别结果，顺序为引擎输出顺序（未按阅读顺序排序）。\n"
                  "每行格式：[左上x,y 右上x,y 右下x,y 左下x,y | 置信度] 文本\n\n",
                  static_cast<int>(screenWidth), static_cast<int>(screenHeight));
    text = header;

    const auto roundPx = [](float value) {
        return static_cast<int32_t>(std::lround(value));
    };

    char box[192];
    for (const OcrLine& line : lines) {
        const std::string value = trimAscii(line.text);
        if (value.empty()) continue;

        std::snprintf(
            box, sizeof(box), "[%d,%d %d,%d %d,%d %d,%d | %.2f] ",
            roundPx(line.quad[0].x), roundPx(line.quad[0].y),
            roundPx(line.quad[1].x), roundPx(line.quad[1].y),
            roundPx(line.quad[2].x), roundPx(line.quad[2].y),
            roundPx(line.quad[3].x), roundPx(line.quad[3].y),
            static_cast<double>(std::clamp(line.confidence, 0.0f, 1.0f)));

        text += box;
        text += value;
        text += '\n';

        if (text.size() > 24000) break;
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
