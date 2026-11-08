// ============================================================================
// Ui.cpp — XML 风格 .ui 解析器实现
// ============================================================================

#include "Ui.h"

#include "StyleSheet.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace modern_ui {
namespace {

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool isNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 ||
           c == '_' || c == '-' || c == ':' || c == '.';
}

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

// 解析 XML 实体
std::string decodeEntities(const std::string& text) {
    std::string out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }

        const size_t semicolon = text.find(';', i);
        if (semicolon == std::string::npos) {
            out.push_back(text[i]);
            continue;
        }

        const std::string entity = text.substr(i + 1, semicolon - i - 1);
        if (entity == "amp") out.push_back('&');
        else if (entity == "lt") out.push_back('<');
        else if (entity == "gt") out.push_back('>');
        else if (entity == "quot") out.push_back('"');
        else if (entity == "apos") out.push_back('\'');
        else if (!entity.empty() && entity[0] == '#') {
            const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
            const long code = std::strtol(entity.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
            // 只处理 ASCII，避免手写 UTF-8 编码器
            if (code > 0 && code < 0x80) out.push_back(static_cast<char>(code));
            else out += text.substr(i, semicolon - i + 1);
        } else {
            out += text.substr(i, semicolon - i + 1);
        }
        i = semicolon;
    }
    return out;
}

class Parser {
public:
    Parser(std::string source, std::string* error)
        : source_(std::move(source)), error_(error) {}

    bool parse(UiNode& root) {
        skipProlog();
        if (!parseElement(root)) return false;
        skipWhitespace();
        if (index_ < source_.size()) {
            return fail("unexpected content after root element");
        }
        return true;
    }

private:
    bool fail(const std::string& message) {
        if (error_) *error_ = "line " + std::to_string(line_) + ": " + message;
        return false;
    }

    void advance(size_t count = 1) {
        for (size_t i = 0; i < count && index_ < source_.size(); ++i) {
            if (source_[index_] == '\n') ++line_;
            ++index_;
        }
    }

    void skipWhitespace() {
        while (index_ < source_.size() && isSpace(source_[index_])) advance();
    }

    void skipProlog() {
        while (true) {
            skipWhitespace();
            if (startsWith("<?")) {
                const size_t end = source_.find("?>", index_);
                if (end == std::string::npos) {
                    index_ = source_.size();
                    return;
                }
                advance(end + 2 - index_);
                continue;
            }
            if (startsWith("<!--")) {
                const size_t end = source_.find("-->", index_);
                if (end == std::string::npos) {
                    index_ = source_.size();
                    return;
                }
                advance(end + 3 - index_);
                continue;
            }
            if (startsWith("<!DOCTYPE") || startsWith("<!doctype")) {
                const size_t end = source_.find('>', index_);
                if (end == std::string::npos) {
                    index_ = source_.size();
                    return;
                }
                advance(end + 1 - index_);
                continue;
            }
            break;
        }
    }

    bool startsWith(const char* prefix) const {
        const size_t length = std::char_traits<char>::length(prefix);
        return source_.compare(index_, length, prefix) == 0;
    }

    bool parseElement(UiNode& node) {
        if (index_ >= source_.size() || source_[index_] != '<') {
            return fail("expected '<'");
        }
        advance();
        if (index_ < source_.size() && source_[index_] == '/') {
            return fail("unexpected closing tag");
        }

        node.line = line_;
        const size_t nameStart = index_;
        while (index_ < source_.size() && isNameChar(source_[index_])) advance();
        node.tag = source_.substr(nameStart, index_ - nameStart);
        if (node.tag.empty()) return fail("missing tag name");

        // ---- 属性 ----
        while (true) {
            skipWhitespace();
            if (index_ >= source_.size()) return fail("unterminated tag <" + node.tag + ">");

            if (source_[index_] == '>') {
                advance();
                break;
            }
            if (startsWith("/>")) {
                advance(2);
                return true;
            }

            const size_t attrStart = index_;
            while (index_ < source_.size() && isNameChar(source_[index_])) advance();
            std::string name = lower(source_.substr(attrStart, index_ - attrStart));
            if (name.empty()) return fail("invalid attribute in <" + node.tag + ">");

            skipWhitespace();
            std::string value;
            if (index_ < source_.size() && source_[index_] == '=') {
                advance();
                skipWhitespace();
                if (index_ >= source_.size() ||
                    (source_[index_] != '"' && source_[index_] != '\'')) {
                    return fail("attribute value must be quoted: " + name);
                }
                const char quote = source_[index_];
                advance();
                const size_t valueStart = index_;
                while (index_ < source_.size() && source_[index_] != quote) advance();
                if (index_ >= source_.size()) {
                    return fail("unterminated attribute value: " + name);
                }
                value = decodeEntities(source_.substr(valueStart, index_ - valueStart));
                advance();
            }
            node.attributes.push_back(UiAttribute{std::move(name), std::move(value)});
        }

        // ---- 内容 / 子节点 ----
        std::string text;
        while (index_ < source_.size()) {
            if (startsWith("</")) {
                advance(2);
                const size_t closeStart = index_;
                while (index_ < source_.size() && isNameChar(source_[index_])) advance();
                const std::string closeTag = source_.substr(closeStart, index_ - closeStart);
                if (closeTag != node.tag) {
                    return fail("closing tag </" + closeTag + "> does not match <" + node.tag + ">");
                }
                skipWhitespace();
                if (index_ >= source_.size() || source_[index_] != '>') {
                    return fail("unterminated closing tag </" + closeTag + ">");
                }
                advance();
                node.text = decodeEntities(style::trim(text));
                return true;
            }

            if (startsWith("<!--")) {
                const size_t end = source_.find("-->", index_);
                if (end == std::string::npos) return fail("unterminated comment");
                advance(end + 3 - index_);
                continue;
            }

            if (source_[index_] == '<') {
                UiNode child;
                if (!parseElement(child)) return false;
                node.children.push_back(std::move(child));
                continue;
            }

            const size_t textStart = index_;
            while (index_ < source_.size() && source_[index_] != '<') advance();
            text += source_.substr(textStart, index_ - textStart);
        }

        return fail("missing closing tag for <" + node.tag + ">");
    }

    std::string source_;
    std::string* error_ = nullptr;
    size_t index_ = 0;
    int line_ = 1;
};

} // namespace

void UiDocument::clear() {
    root_ = UiNode{};
}

bool UiDocument::parse(const std::string& text, std::string* error) {
    clear();

    std::string source = text;
    // UTF-8 BOM
    if (source.size() >= 3 && static_cast<unsigned char>(source[0]) == 0xEF &&
        static_cast<unsigned char>(source[1]) == 0xBB &&
        static_cast<unsigned char>(source[2]) == 0xBF) {
        source.erase(0, 3);
    }

    Parser parser(std::move(source), error);
    return parser.parse(root_);
}

bool UiDocument::parseFile(const std::string& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "cannot open ui file: " + path;
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return parse(stream.str(), error);
}

} // namespace modern_ui
