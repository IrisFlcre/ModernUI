// ============================================================================
// AiClient.cpp
// ============================================================================

#include "AiClient.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace annotation {

namespace {

constexpr wchar_t kApiHost[] = L"api.deepseek.com";
constexpr wchar_t kApiPath[] = L"/chat/completions";
constexpr char    kApiModel[] = "deepseek-chat";
constexpr size_t  kMaxResponseBytes = 4u * 1024u * 1024u;

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

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                           static_cast<int>(utf8.size()),
                                           nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), length);
    return wide;
}

std::wstring executableDirectory() {
    wchar_t buffer[MAX_PATH * 2] = L"";
    const DWORD length =
        GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) return L".";
    std::wstring path(buffer, length);
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".";
    return path.substr(0, slash);
}

std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 32);
    for (const unsigned char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x",
                                  static_cast<unsigned>(c));
                    out += escaped;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
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

// quotePos 指向开引号；成功时 out 为反转义后的内容，endPos 指向闭引号之后。
bool readJsonString(const std::string& json, std::size_t quotePos,
                    std::string* out, std::size_t* endPos) {
    if (quotePos >= json.size() || json[quotePos] != '"') return false;

    out->clear();
    std::size_t pos = quotePos + 1;
    while (pos < json.size()) {
        const char c = json[pos];
        if (c == '"') {
            *endPos = pos + 1;
            return true;
        }
        if (c != '\\') {
            *out += c;
            ++pos;
            continue;
        }

        ++pos;
        if (pos >= json.size()) return false;
        switch (json[pos]) {
            case '"':  *out += '"';  break;
            case '\\': *out += '\\'; break;
            case '/':  *out += '/';  break;
            case 'b':  *out += '\b'; break;
            case 'f':  *out += '\f'; break;
            case 'n':  *out += '\n'; break;
            case 'r':  *out += '\r'; break;
            case 't':  *out += '\t'; break;
            case 'u': {
                auto hexValue = [&](std::size_t index, uint32_t* value) {
                    if (index + 4 > json.size()) return false;
                    uint32_t result = 0;
                    for (std::size_t i = 0; i < 4; ++i) {
                        const char h = json[index + i];
                        uint32_t digit = 0;
                        if (h >= '0' && h <= '9') digit = static_cast<uint32_t>(h - '0');
                        else if (h >= 'a' && h <= 'f') digit = static_cast<uint32_t>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') digit = static_cast<uint32_t>(h - 'A' + 10);
                        else return false;
                        result = (result << 4) | digit;
                    }
                    *value = result;
                    return true;
                };

                uint32_t codepoint = 0;
                if (!hexValue(pos + 1, &codepoint)) return false;
                pos += 4;

                if (codepoint >= 0xD800u && codepoint <= 0xDBFFu &&
                    pos + 6 < json.size() && json[pos + 1] == '\\' &&
                    json[pos + 2] == 'u') {
                    uint32_t low = 0;
                    if (hexValue(pos + 3, &low) && low >= 0xDC00u && low <= 0xDFFFu) {
                        codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) +
                                    (low - 0xDC00u);
                        pos += 6;
                    }
                }
                appendUtf8(*out, codepoint);
                break;
            }
            default:
                *out += json[pos];
                break;
        }
        ++pos;
    }
    return false;
}

bool findJsonStringField(const std::string& json, const char* field,
                         std::size_t from, std::string* out) {
    const std::string key = std::string("\"") + field + "\"";
    std::size_t pos = json.find(key, from);
    if (pos == std::string::npos) return false;

    pos = json.find(':', pos + key.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && isAsciiSpace(json[pos])) ++pos;
    if (pos >= json.size() || json[pos] != '"') return false;

    std::size_t endPos = 0;
    return readJsonString(json, pos, out, &endPos);
}

std::string extractApiError(const std::string& json) {
    std::size_t errorPos = json.find("\"error\"");
    if (errorPos == std::string::npos) return {};

    std::string message;
    if (findJsonStringField(json, "message", errorPos, &message)) return message;
    if (findJsonStringField(json, "error_msg", errorPos, &message)) return message;
    return {};
}

bool extractReplyContent(const std::string& json, std::string* content,
                         std::string* error) {
    const std::size_t choices = json.find("\"choices\"");
    if (choices == std::string::npos) {
        std::string apiError = extractApiError(json);
        if (apiError.empty()) apiError = "AI 响应缺少 choices 字段";
        *error = std::move(apiError);
        return false;
    }

    std::size_t searchFrom = json.find("\"message\"", choices);
    if (searchFrom == std::string::npos) searchFrom = choices;

    if (!findJsonStringField(json, "content", searchFrom, content)) {
        *error = "AI 响应缺少 content 字段";
        return false;
    }
    if (content->empty()) {
        *error = "AI 返回了空内容";
        return false;
    }
    return true;
}

std::string buildRequestBody(const std::string& systemPrompt,
                             const std::string& userText) {
    std::string body;
    body.reserve(systemPrompt.size() + userText.size() + 256);
    body += "{\"model\":\"";
    body += kApiModel;
    body += "\",\"messages\":[{\"role\":\"system\",\"content\":\"";
    body += jsonEscape(systemPrompt);
    body += "\"},{\"role\":\"user\",\"content\":\"";
    body += jsonEscape(userText);
    body += "\"}],\"temperature\":0.5,\"max_tokens\":1800,\"stream\":false}";
    return body;
}

bool httpPostJson(const std::string& apiKey, const std::string& body,
                  std::string* response, std::string* error) {
    response->clear();

    HINTERNET session = WinHttpOpen(
        L"ModernUI-Annotation/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        *error = "WinHttpOpen 失败（" + std::to_string(GetLastError()) + "）";
        return false;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 15000, 45000);

    HINTERNET connect = WinHttpConnect(session, kApiHost,
                                       INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        *error = "无法连接 api.deepseek.com（WinHTTP " +
                 std::to_string(GetLastError()) + "）";
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(
        connect, L"POST", kApiPath, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        *error = "创建 HTTPS 请求失败（WinHTTP " +
                 std::to_string(GetLastError()) + "）";
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    const std::wstring headers = widen(
        "Content-Type: application/json\r\n"
        "Accept: application/json\r\n"
        "Authorization: Bearer " + apiKey + "\r\n");

    bool ok = false;
    do {
        if (!WinHttpSendRequest(request, headers.c_str(),
                                static_cast<DWORD>(-1),
                                const_cast<char*>(body.data()),
                                static_cast<DWORD>(body.size()),
                                static_cast<DWORD>(body.size()), 0)) {
            *error = "发送 AI 请求失败（WinHTTP " +
                     std::to_string(GetLastError()) + "）";
            break;
        }
        if (!WinHttpReceiveResponse(request, nullptr)) {
            *error = "等待 AI 响应超时（WinHTTP " +
                     std::to_string(GetLastError()) + "）";
            break;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                            WINHTTP_NO_HEADER_INDEX);

        std::string payload;
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::vector<char> chunk(available);
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
            payload.append(chunk.data(), read);
            if (payload.size() > kMaxResponseBytes) break;
        }

        if (status != 200) {
            const std::string detail = extractApiError(payload);
            *error = "HTTP " + std::to_string(status);
            if (!detail.empty()) *error += "：" + detail;
            break;
        }

        *response = std::move(payload);
        ok = true;
    } while (false);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

} // namespace

std::string loadApiKeyFromExecutableDirectory() {
    const std::filesystem::path path =
        std::filesystem::path(executableDirectory()) / L"API_KEY.txt";

    std::ifstream file(path, std::ios::binary);
    if (!file) return {};

    std::string raw((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());
    if (raw.size() >= 3 &&
        static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }

    raw = trimAscii(raw);
    while (raw.size() >= 2 &&
           ((raw.front() == '"' && raw.back() == '"') ||
            (raw.front() == '\'' && raw.back() == '\''))) {
        raw = trimAscii(raw.substr(1, raw.size() - 2));
    }
    return raw;
}

// ============================================================================
// 工作线程
// ============================================================================

struct AiClient::Impl {
    struct Job {
        int id = 0;
        std::string systemPrompt;
        std::string userText;
    };

    std::string apiKey;
    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable condition;

    std::unique_ptr<Job> pending;
    std::deque<AiReply> completed;
    int nextRequestId = 1;
    bool running = false;
    bool inFlight = false;
    bool quit = false;

    void run();
};

void AiClient::Impl::run() {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [this] { return quit || pending != nullptr; });
            if (quit && !pending) return;
            job = std::move(*pending);
            pending.reset();
            inFlight = true;
        }

        AiReply reply;
        reply.requestId = job.id;
        try {
            std::string response;
            std::string error;
            if (!httpPostJson(apiKey, buildRequestBody(job.systemPrompt, job.userText),
                              &response, &error)) {
                reply.ok = false;
                reply.error = error.empty() ? "AI 请求失败" : error;
            } else {
                std::string content;
                std::string parseError;
                if (extractReplyContent(response, &content, &parseError)) {
                    reply.ok = true;
                    reply.text = std::move(content);
                } else {
                    reply.ok = false;
                    reply.error = std::move(parseError);
                }
            }
        } catch (const std::exception& exception) {
            reply.ok = false;
            reply.error = exception.what();
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            inFlight = false;
            completed.push_back(std::move(reply));
        }
    }
}

// ============================================================================
// 公共接口
// ============================================================================

AiClient::AiClient() : impl_(std::make_unique<Impl>()) {}

AiClient::~AiClient() {
    shutdown();
}

void AiClient::initialize() {
    if (!impl_ || impl_->running) return;

    impl_->apiKey = loadApiKeyFromExecutableDirectory();
    impl_->quit = false;
    impl_->running = true;
    impl_->worker = std::thread([impl = impl_.get()] { impl->run(); });
}

bool AiClient::ready() const {
    return impl_ && !impl_->apiKey.empty();
}

bool AiClient::busy() const {
    if (!impl_) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->inFlight || impl_->pending != nullptr;
}

int AiClient::ask(const std::string& systemPrompt, const std::string& userText) {
    if (!impl_ || !impl_->running || impl_->apiKey.empty()) return 0;

    auto job = std::make_unique<Impl::Job>();
    int requestId = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        job->id = impl_->nextRequestId++;
        requestId = job->id;
        job->systemPrompt = systemPrompt;
        job->userText = userText;
        // 尚未开始执行的旧请求直接丢弃（只处理最新画面）。
        impl_->pending = std::move(job);
    }
    impl_->condition.notify_one();
    return requestId;
}

bool AiClient::poll(AiReply* reply) {
    if (!impl_ || !reply) return false;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->completed.empty()) return false;
    *reply = std::move(impl_->completed.front());
    impl_->completed.pop_front();
    return true;
}

void AiClient::shutdown() {
    if (!impl_ || !impl_->running) return;

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->quit = true;
        impl_->pending.reset();
    }
    impl_->condition.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->running = false;
}

} // namespace annotation
