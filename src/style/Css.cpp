// ============================================================================
// Css.cpp — .css 解析 / 选择器匹配 / 声明应用
// ============================================================================

#include "Css.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace modern_ui {
namespace {

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
}

std::string trimmed(const std::string& text) {
    return style::trim(text);
}

// 去掉 /* ... */ 注释（跳过字符串内部的伪注释）
std::string stripComments(const std::string& source) {
    std::string out;
    out.reserve(source.size());

    char quote = 0;
    for (size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (quote != 0) {
            out.push_back(c);
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            out.push_back(c);
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            i += 2;
            while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/')) ++i;
            ++i;
            continue;
        }
        out.push_back(c);
    }
    return out;
}

size_t findTopLevel(const std::string& text, size_t start, char target) {
    int depth = 0;
    char quote = 0;
    for (size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (quote != 0) {
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        else if (c == target && depth == 0) return i;
    }
    return std::string::npos;
}

size_t findMatchingBrace(const std::string& text, size_t openIndex) {
    int depth = 0;
    char quote = 0;
    for (size_t i = openIndex; i < text.size(); ++i) {
        const char c = text[i];
        if (quote != 0) {
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) return i;
        }
    }
    return std::string::npos;
}

std::vector<std::string> splitTopLevel(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t hit = findTopLevel(text, start, delimiter);
        if (hit == std::string::npos) {
            parts.push_back(trimmed(text.substr(start)));
            break;
        }
        parts.push_back(trimmed(text.substr(start, hit - start)));
        start = hit + 1;
    }
    return parts;
}

std::string lower(std::string text) {
    return style::toLower(std::move(text));
}

std::string readIdent(const std::string& text, size_t& index) {
    const size_t begin = index;
    while (index < text.size() && isIdentChar(text[index])) ++index;
    return text.substr(begin, index - begin);
}

void parseSelectorPart(const std::string& part, CssSelector& selector) {
    size_t index = 0;

    if (index < part.size() && part[index] == '*') {
        ++index;
    } else if (index < part.size() && part[index] != '.' && part[index] != '#' &&
               part[index] != ':') {
        selector.tag = readIdent(part, index);
    }

    CssSelector::State state = CssSelector::State::None;
    auto upgradeState = [&state](CssSelector::State candidate) {
        auto rank = [](CssSelector::State s) {
            switch (s) {
            case CssSelector::State::Pressed:  return 4;
            case CssSelector::State::Disabled: return 3;
            case CssSelector::State::Focus:    return 2;
            case CssSelector::State::Hover:    return 1;
            default:                           return 0;
            }
        };
        if (rank(candidate) > rank(state)) state = candidate;
    };

    while (index < part.size()) {
        const char marker = part[index++];
        if (marker == '.') {
            const std::string name = readIdent(part, index);
            if (!name.empty()) selector.classes.push_back(name);
        } else if (marker == '#') {
            const std::string name = readIdent(part, index);
            if (!name.empty()) selector.id = name;
        } else if (marker == ':') {
            const std::string pseudo = lower(readIdent(part, index));
            if (pseudo == "hover") upgradeState(CssSelector::State::Hover);
            else if (pseudo == "pressed" || pseudo == "active") upgradeState(CssSelector::State::Pressed);
            else if (pseudo == "focus" || pseudo == "focus-visible") upgradeState(CssSelector::State::Focus);
            else if (pseudo == "disabled") upgradeState(CssSelector::State::Disabled);
        } else if (marker == '>') {
            // 只支持单级复合选择器：忽略组合符，避免静默误匹配
            break;
        } else if (!isSpace(marker)) {
            // 未知字符：跳过当前复合选择器剩余部分
            break;
        }
    }
    selector.state = state;
}

bool isReferenceValue(const std::string& value) {
    const std::string text = trimmed(value);
    if (text.rfind("var(", 0) == 0) return true;
    return text.size() > 2 && text.front() == '{' && text.back() == '}';
}

} // namespace

// ============================================================================
// CssSelector
// ============================================================================
int CssSelector::specificity() const {
    int score = 0;
    if (!id.empty()) score += 100;
    if (state != State::None) score += 10;
    score += static_cast<int>(classes.size()) * 10;
    if (!tag.empty() && tag != "*") score += 1;
    return score;
}

bool CssSelector::matches(const std::string& tagName,
                          const std::string& widgetId,
                          const std::vector<std::string>& classList) const {
    if (!tag.empty() && tag != "*" && tag != tagName) return false;
    if (!id.empty() && id != widgetId) return false;

    for (const std::string& expected : classes) {
        if (std::find(classList.begin(), classList.end(), expected) == classList.end()) {
            return false;
        }
    }
    return true;
}

std::string CssSelector::text() const {
    std::string out;
    if (!tag.empty()) out += tag;
    for (const std::string& name : classes) {
        out += "." + name;
    }
    if (!id.empty()) out += "#" + id;
    switch (state) {
    case State::Hover:    out += ":hover"; break;
    case State::Pressed:  out += ":pressed"; break;
    case State::Focus:    out += ":focus"; break;
    case State::Disabled: out += ":disabled"; break;
    default: break;
    }
    return out;
}

// ============================================================================
// CssDocument
// ============================================================================
void CssDocument::clear() {
    rules_.clear();
    variables_.clear();
}

bool CssDocument::parse(const std::string& text, std::string* error) {
    clear();

    const std::string source = stripComments(text);
    size_t index = 0;
    int order = 0;

    while (index < source.size()) {
        while (index < source.size() && (isSpace(source[index]) || source[index] == ';' ||
                                        source[index] == '}')) {
            ++index;
        }
        if (index >= source.size()) break;

        // 跳过 @media / @font-face 等 at-rule（解析其块会导致选择器语义错误）
        if (source[index] == '@') {
            const size_t brace = findTopLevel(source, index, '{');
            const size_t semicolon = findTopLevel(source, index, ';');
            if (semicolon != std::string::npos &&
                (brace == std::string::npos || semicolon < brace)) {
                index = semicolon + 1;
                continue;
            }
            if (brace == std::string::npos) break;
            const size_t close = findMatchingBrace(source, brace);
            if (close == std::string::npos) {
                if (error) *error = "unterminated at-rule block";
                return false;
            }
            index = close + 1;
            continue;
        }

        const size_t brace = findTopLevel(source, index, '{');
        if (brace == std::string::npos) {
            if (error) *error = "missing '{' in stylesheet";
            return false;
        }
        const size_t close = findMatchingBrace(source, brace);
        if (close == std::string::npos) {
            if (error) *error = "unterminated rule block";
            return false;
        }

        const std::string selectorText = trimmed(source.substr(index, brace - index));
        const std::string body = source.substr(brace + 1, close - brace - 1);

        std::vector<CssDeclaration> declarations;
        if (!parseDeclarations(body, declarations, error)) return false;

        if (lower(selectorText) == ":root") {
            for (const CssDeclaration& declaration : declarations) {
                if (declaration.property.rfind("--", 0) == 0) {
                    variables_[declaration.property] = declaration.value;
                }
            }
            index = close + 1;
            continue;
        }

        CssRule rule;
        rule.order = order++;
        rule.declarations = std::move(declarations);

        for (const std::string& part : splitTopLevel(selectorText, ',')) {
            if (part.empty()) continue;
            CssSelector selector;
            parseSelectorPart(part, selector);
            rule.selectors.push_back(std::move(selector));
        }

        if (!rule.selectors.empty()) rules_.push_back(std::move(rule));
        index = close + 1;
    }
    return true;
}

bool CssDocument::parseFile(const std::string& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "cannot open css file: " + path;
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return parse(stream.str(), error);
}

bool CssDocument::parseDeclarations(const std::string& text,
                                    std::vector<CssDeclaration>& out,
                                    std::string* error) {
    for (std::string statement : splitTopLevel(text, ';')) {
        if (statement.empty()) continue;

        // 去掉 !important（当前实现中优先级由顺序 / 选择器决定）
        const size_t important = lower(statement).rfind("!important");
        if (important != std::string::npos) {
            statement = trimmed(statement.substr(0, important));
            if (statement.empty()) continue;
        }

        const size_t colon = findTopLevel(statement, 0, ':');
        if (colon == std::string::npos) {
            if (error) *error = "invalid declaration: " + statement;
            return false;
        }

        CssDeclaration declaration;
        declaration.property = lower(trimmed(statement.substr(0, colon)));
        declaration.value = trimmed(statement.substr(colon + 1));
        if (declaration.property.empty() || declaration.value.empty()) continue;
        out.push_back(std::move(declaration));
    }
    return true;
}

// ============================================================================
// 声明 → Entry
// ============================================================================
std::string stateProperty(const std::string& property, CssSelector::State state) {
    if (state == CssSelector::State::None) return property;

    const std::string name = lower(property);
    const bool disabled = state == CssSelector::State::Disabled;
    const bool pressed = state == CssSelector::State::Pressed;
    const bool focus = state == CssSelector::State::Focus;

    if (name == "background" || name == "background-color") {
        if (disabled) return "background-disabled";
        if (pressed) return "background-pressed";
        if (state == CssSelector::State::Hover) return "background-hover";
        return "background";
    }
    if (name == "color" || name == "text-color") {
        return disabled ? "text-disabled" : "foreground";
    }
    if (name == "border-color") {
        return focus ? "focus-stroke" : "border-color";
    }
    if (name == "accent" || name == "accent-color") {
        if (disabled) return "accent-disabled";
        if (pressed) return "accent-pressed";
        if (state == CssSelector::State::Hover) return "accent-hover";
        return "accent";
    }
    if (name == "control-fill") {
        if (disabled) return "control-fill-disabled";
        if (pressed) return "control-fill-pressed";
        if (state == CssSelector::State::Hover) return "control-fill-hover";
        return "control-fill";
    }
    if (name == "text-primary") {
        return disabled ? "text-disabled" : "text-primary";
    }
    if (name == "foreground") {
        return disabled ? "foreground-disabled" : "foreground";
    }
    return property;
}

bool applyDeclaration(StyleSheet& sheet, const std::string& property,
                      const std::string& value, StyleSheet::Origin origin,
                      CssSelector::State state) {
    const std::string name = lower(trimmed(property));
    const std::string rawValue = trimmed(value);
    if (name.empty() || rawValue.empty()) return false;

    if (name.rfind("--", 0) == 0) {
        sheet.setVar(name, rawValue);
        return true;
    }

    // border: <width> <style> <color> 简写 → border-width + border-color
    if (name == "border") {
        bool applied = false;
        size_t index = 0;
        while (index < rawValue.size()) {
            while (index < rawValue.size() && isSpace(rawValue[index])) ++index;
            const size_t begin = index;
            while (index < rawValue.size() && !isSpace(rawValue[index])) ++index;
            if (index <= begin) break;

            const std::string token = rawValue.substr(begin, index - begin);
            float width = 0.0f;
            Color color;
            if (style::parseNumber(token, width)) {
                sheet.setNumber(StyleSheet::Entry::BorderWidth, width, origin);
                applied = true;
            } else if (style::parseColor(token, color)) {
                sheet.setColor(StyleSheet::Entry::BorderColor, color, origin);
                applied = true;
            }
        }
        return applied;
    }

    const std::string mapped = stateProperty(name, state);

    StyleSheet::Entry entry;
    if (!StyleSheet::entryFromName(mapped, entry)) {
        // 未识别的属性进入 others（自定义 Entry）
        sheet.others[trimmed(property)] = rawValue;
        return false;
    }

    if (isReferenceValue(rawValue)) {
        sheet.setText(entry, rawValue, origin);
        return true;
    }

    if (StyleSheet::isNumericEntry(entry)) {
        float number = 0.0f;
        if (style::parseNumber(rawValue, number)) {
            sheet.setNumber(entry, number, origin);
        } else {
            sheet.setText(entry, rawValue, origin);
        }
        return true;
    }

    Color color;
    if (style::parseColor(rawValue, color)) {
        sheet.setColor(entry, color, origin);
    } else {
        sheet.setText(entry, rawValue, origin);
    }
    return true;
}

} // namespace modern_ui
