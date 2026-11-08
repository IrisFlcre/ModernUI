// ============================================================================
// DocumentParser.h — OCR 原文还原为 Markdown（DeepSeek 提示词 / 结果清理）
// ============================================================================
//
// 数据流：DocumentParser 按钮 → 全屏截图 → OCR → 本文件的提示词 →
//        AiClient → 模型只输出 Markdown 原文 → MarkdownView 面板展示。
//
// 约束：模型只能整理 / 修复 OCR 乱码，不得增删原文内容。
// ============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "OcrEngine.h"

namespace annotation {

// 文档还原系统提示词（强调"原文照录 + 只修复乱码"）。
const char* documentParserSystemPrompt();

// 把 OCR 行按阅读顺序拼成模型输入：行间 '\n'，检测到大段间距时插入空行提示分段。
std::string buildDocumentOcrPayload(int32_t screenWidth, int32_t screenHeight,
                                    const std::vector<OcrLine>& lines);

// 去掉模型偶尔包在外层的 ```markdown / ``` 围栏、BOM 与首尾空白。
std::string extractMarkdownFromReply(const std::string& reply);

} // namespace annotation
