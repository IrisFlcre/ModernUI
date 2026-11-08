// ============================================================================
// AiClient.h — DeepSeek 异步对话客户端
// ============================================================================
//
// 密钥来源：可执行文件同目录的 API_KEY.txt（UTF-8，允许首尾空白 / BOM / 引号）。
// 工作线程用 WinHTTP 调用 api.deepseek.com/chat/completions，
// UI 线程通过 poll() 取回结果。同一时间只保留最新的待处理请求。
// ============================================================================

#pragma once

#include <memory>
#include <string>

namespace annotation {

struct AiReply {
    int requestId = 0;
    bool ok = false;
    std::string error;
    std::string text;   // 模型回复正文（预期为 XML）
};

class AiClient {
public:
    AiClient();
    ~AiClient();

    AiClient(const AiClient&) = delete;
    AiClient& operator=(const AiClient&) = delete;

    // 读取 <exe 目录>/API_KEY.txt 并启动工作线程。
    void initialize();

    bool ready() const;
    bool busy() const;

    // 提交一次对话（覆盖尚未开始的旧请求）。返回请求 id；0 = 未提交。
    int ask(const std::string& systemPrompt, const std::string& userText);

    // UI 线程轮询；有结果返回 true。
    bool poll(AiReply* reply);

    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 读取可执行文件同目录下的 API_KEY.txt（去掉 BOM / 首尾空白 / 成对引号）。
std::string loadApiKeyFromExecutableDirectory();

} // namespace annotation
