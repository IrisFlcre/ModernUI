// ============================================================================
// StyleSheet.cpp — 样式表 / 值解析 / 引用展开
// ============================================================================

#include "StyleSheet.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace modern_ui {
namespace style {
namespace {

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string stripped(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && isSpace(text[begin])) ++begin;
    while (end > begin && isSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseHexColor(const std::string& body, Color& out) {
    if (body.empty()) return false;
    auto digit = [&](size_t index) { return hexDigit(body[index]); };

    if (body.size() == 3 || body.size() == 4) {
        const int r = digit(0);
        const int g = digit(1);
        const int b = digit(2);
        const int a = body.size() == 4 ? digit(3) : 15;
        if (r < 0 || g < 0 || b < 0 || a < 0) return false;
        out = Color{static_cast<uint8_t>(r * 17), static_cast<uint8_t>(g * 17),
                    static_cast<uint8_t>(b * 17), static_cast<uint8_t>(a * 17)};
        return true;
    }
    if (body.size() == 6 || body.size() == 8) {
        int channels[4] = {0, 0, 0, 255};
        for (size_t i = 0; i < body.size() / 2; ++i) {
            const int hi = digit(i * 2);
            const int lo = digit(i * 2 + 1);
            if (hi < 0 || lo < 0) return false;
            channels[i] = hi * 16 + lo;
        }
        out = Color{static_cast<uint8_t>(channels[0]), static_cast<uint8_t>(channels[1]),
                    static_cast<uint8_t>(channels[2]), static_cast<uint8_t>(channels[3])};
        return true;
    }
    return false;
}

bool parseChannel(const std::string& text, float& out) {
    const std::string token = stripped(text);
    if (token.empty()) return false;

    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end == token.c_str()) return false;
    const bool percent = *end == '%';
    out = static_cast<float>(percent ? value * 2.55 : value);
    return true;
}

bool parseFunctionColor(const std::string& text, Color& out) {
    const size_t open = text.find('(');
    if (open == std::string::npos || text.back() != ')') return false;

    const std::string fn = toLower(stripped(text.substr(0, open)));
    if (fn != "rgb" && fn != "rgba") return false;

    const std::string body = text.substr(open + 1, text.size() - open - 2);
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= body.size(); ++i) {
        if (i == body.size() || body[i] == ',') {
            parts.push_back(body.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() < 3) return false;

    float r = 0.0f, g = 0.0f, b = 0.0f, a = 255.0f;
    if (!parseChannel(parts[0], r) || !parseChannel(parts[1], g) || !parseChannel(parts[2], b)) {
        return false;
    }
    if (parts.size() >= 4) {
        const std::string alpha = stripped(parts[3]);
        float av = 0.0f;
        char* end = nullptr;
        const double raw = std::strtod(alpha.c_str(), &end);
        if (end == alpha.c_str()) return false;
        av = *end == '%' ? static_cast<float>(raw * 2.55) : static_cast<float>(raw * 255.0);
        a = av;
    }

    auto clampByte = [](float value) {
        return static_cast<uint8_t>(std::lround(std::max(0.0f, std::min(255.0f, value))));
    };
    out = Color{clampByte(r), clampByte(g), clampByte(b), clampByte(a)};
    return true;
}

const std::map<std::string, Color>& namedColors() {
    static const std::map<std::string, Color> colors = {
        {"transparent", Color{0, 0, 0, 0}},
        {"black",       Color{0, 0, 0, 255}},
        {"white",       Color{255, 255, 255, 255}},
        {"red",         Color{255, 0, 0, 255}},
        {"green",       Color{0, 128, 0, 255}},
        {"lime",        Color{0, 255, 0, 255}},
        {"blue",        Color{0, 0, 255, 255}},
        {"yellow",      Color{255, 255, 0, 255}},
        {"orange",      Color{255, 165, 0, 255}},
        {"purple",      Color{128, 0, 128, 255}},
        {"gray",        Color{128, 128, 128, 255}},
        {"grey",        Color{128, 128, 128, 255}},
        {"silver",      Color{192, 192, 192, 255}},
        {"navy",        Color{0, 0, 128, 255}},
        {"teal",        Color{0, 128, 128, 255}},
        {"currentColor", Color{255, 255, 255, 255}}
    };
    return colors;
}

} // namespace

std::string trim(const std::string& text) {
    return stripped(text);
}

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool parseColor(const std::string& text, Color& out) {
    const std::string value = toLower(stripped(text));
    if (value.empty()) return false;

    if (value[0] == '#') return parseHexColor(value.substr(1), out);

    auto named = namedColors().find(value);
    if (named != namedColors().end()) {
        out = named->second;
        return true;
    }

    if (value.rfind("rgb", 0) == 0) return parseFunctionColor(value, out);
    return false;
}

bool parseNumber(const std::string& text, float& out) {
    std::string value = toLower(stripped(text));
    if (value.empty()) return false;

    float scale = 1.0f;
    auto stripSuffix = [&](const char* suffix, float factor) {
        const size_t length = std::strlen(suffix);
        if (value.size() > length &&
            value.compare(value.size() - length, length, suffix) == 0) {
            value.resize(value.size() - length);
            scale = factor;
            return true;
        }
        return false;
    };

    if (!stripSuffix("px", 1.0f) && !stripSuffix("ms", 0.001f) &&
        !stripSuffix("s", 1.0f) && !stripSuffix("%", 1.0f)) {
        scale = 1.0f;
    }

    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) return false;
    out = static_cast<float>(parsed) * scale;
    return true;
}

std::string colorToCss(const Color& color) {
    static const char* hex = "0123456789abcdef";
    std::string out = "#";
    const uint8_t channels[4] = {color.r, color.g, color.b, color.a};
    for (uint8_t channel : channels) {
        out.push_back(hex[(channel >> 4) & 0x0F]);
        out.push_back(hex[channel & 0x0F]);
    }
    return out;
}

std::string numberToCss(float value) {
    std::string out;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
    out = buffer;
    return out;
}

} // namespace style

// ============================================================================
// 名称映射
// ============================================================================
namespace {

struct EntryInfo {
    const char* name;
    bool numeric;
};

const EntryInfo kEntries[] = {
    {"background",            false},
    {"background-hover",      false},
    {"background-pressed",    false},
    {"background-disabled",   false},
    {"card",                  false},
    {"card-alt",              false},
    {"layer",                 false},
    {"layer-alt",             false},
    {"overlay",               false},
    {"scrim",                 false},
    {"control-fill",          false},
    {"control-fill-hover",    false},
    {"control-fill-pressed",  false},
    {"control-fill-disabled", false},
    {"subtle-hover",          false},
    {"subtle-pressed",        false},
    {"border-color",          false},
    {"border-width",          true},
    {"border-radius",         true},
    {"radius-card",           true},
    {"radius-overlay",        true},
    {"stroke",                false},
    {"stroke-strong",         false},
    {"focus-stroke",          false},
    {"divider",               false},
    {"foreground",            false},
    {"foreground-secondary",  false},
    {"foreground-tertiary",   false},
    {"foreground-disabled",   false},
    {"foreground-on-accent",  false},
    {"text-primary",          false},
    {"text-secondary",        false},
    {"text-tertiary",         false},
    {"text-disabled",         false},
    {"text-on-accent",        false},
    {"accent",                false},
    {"accent-hover",          false},
    {"accent-pressed",        false},
    {"accent-disabled",       false},
    {"info",                  false},
    {"success",               false},
    {"caution",               false},
    {"critical",              false},
    {"control-height",        true},
    {"list-item-height",      true},
    {"min-control-width",     true},
    {"font-size",             true},
    {"caption-font-size",     true},
    {"subtitle-font-size",    true},
    {"title-font-size",       true},
    {"display-font-size",     true},
    {"fast-duration",         true},
    {"normal-duration",       true},
    {"slow-duration",         true},
    {"opacity",               true}
};

const std::map<std::string, modern_ui::StyleSheet::Entry>& aliases() {
    using Entry = modern_ui::StyleSheet::Entry;
    static const std::map<std::string, Entry> table = {
        {"background-color", Entry::Background},
        {"color",            Entry::Foreground},
        {"text-color",       Entry::Foreground},
        {"radius",           Entry::Radius},
        {"border-radius",    Entry::Radius},
        {"accent-color",     Entry::Accent}
    };
    return table;
}

} // namespace

const StyleSheet::Value& StyleSheet::raw(Entry entry) const {
    static const Value empty;
    const size_t index = static_cast<size_t>(entry);
    if (index >= kEntryCount) return empty;
    return values_[index];
}

bool StyleSheet::entryFromName(const std::string& name, Entry& entry) {
    const std::string key = style::toLower(style::trim(name));
    if (key.empty()) return false;

    auto alias = aliases().find(key);
    if (alias != aliases().end()) {
        entry = alias->second;
        return true;
    }

    for (size_t i = 0; i < kEntryCount; ++i) {
        if (key == kEntries[i].name) {
            entry = static_cast<Entry>(i);
            return true;
        }
    }
    return false;
}

const char* StyleSheet::entryName(Entry entry) {
    const size_t index = static_cast<size_t>(entry);
    if (index >= kEntryCount) return "";
    return kEntries[index].name;
}

bool StyleSheet::isNumericEntry(Entry entry) {
    const size_t index = static_cast<size_t>(entry);
    if (index >= kEntryCount) return false;
    return kEntries[index].numeric;
}

// ============================================================================
// 读写
// ============================================================================
void StyleSheet::clear() {
    for (Value& value : values_) value = Value{};
    variables_.clear();
    others.clear();
}

bool StyleSheet::empty() const {
    if (!variables_.empty() || !others.empty()) return false;
    for (const Value& value : values_) {
        if (!value.empty()) return false;
    }
    return true;
}

void StyleSheet::set(Entry entry, const Value& value) {
    const size_t index = static_cast<size_t>(entry);
    if (index >= kEntryCount) return;
    values_[index] = value;
}

void StyleSheet::setColor(Entry entry, Color color, Origin origin) {
    Value value;
    value.kind = Value::Kind::Color;
    value.origin = origin;
    value.color = color;
    set(entry, value);
}

void StyleSheet::setNumber(Entry entry, float number, Origin origin) {
    Value value;
    value.kind = Value::Kind::Number;
    value.origin = origin;
    value.number = number;
    set(entry, value);
}

void StyleSheet::setText(Entry entry, std::string text, Origin origin) {
    Value value;
    value.kind = Value::Kind::Text;
    value.origin = origin;
    value.text = std::move(text);
    set(entry, value);
}

void StyleSheet::merge(const StyleSheet& overlay) {
    for (size_t i = 0; i < kEntryCount; ++i) {
        if (!overlay.values_[i].empty()) values_[i] = overlay.values_[i];
    }
    for (const auto& variable : overlay.variables_) variables_[variable.first] = variable.second;
    for (const auto& custom : overlay.others) others[custom.first] = custom.second;
}

StyleSheet StyleSheet::withOrigin(Origin origin) const {
    StyleSheet copy = *this;
    for (Value& value : copy.values_) {
        if (!value.empty()) value.origin = origin;
    }
    return copy;
}

void StyleSheet::setVar(std::string name, std::string value) {
    if (name.empty()) return;
    variables_[std::move(name)] = std::move(value);
}

const std::string* StyleSheet::findVar(const std::string& name) const {
    auto it = variables_.find(name);
    return it == variables_.end() ? nullptr : &it->second;
}

std::string StyleSheet::custom(const std::string& key, std::string fallback) const {
    auto it = others.find(key);
    return it == others.end() ? std::move(fallback) : it->second;
}

// ============================================================================
// 引用值解析：var(--name[, fallback]) / {entry-name}
// ============================================================================
namespace {

size_t findTopLevelComma(const std::string& text) {
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        else if (c == ',' && depth == 0) return i;
    }
    return std::string::npos;
}

} // namespace

StyleSheet::Value StyleSheet::resolved(Entry entry) const {
    return resolveValue(raw(entry), 0);
}

StyleSheet::Value StyleSheet::resolvedText(const std::string& text, Origin origin) const {
    Value value;
    value.kind = Value::Kind::Text;
    value.origin = origin;
    value.text = text;
    return resolveValue(value, 0);
}

StyleSheet::Value StyleSheet::resolveValue(const Value& value, int depth) const {
    if (value.kind != Value::Kind::Text || depth > 10) return value;

    const std::string text = style::trim(value.text);
    if (text.empty()) return value;

    // var(--name) / var(--name, fallback)
    if (text.rfind("var(", 0) == 0 && text.back() == ')') {
        const std::string inner = text.substr(4, text.size() - 5);
        const size_t comma = findTopLevelComma(inner);
        const std::string name = style::trim(inner.substr(0, comma));

        if (const std::string* variable = findVar(name)) {
            Value result;
            result.kind = Value::Kind::Text;
            result.origin = value.origin;
            result.text = *variable;
            return resolveValue(result, depth + 1);
        }
        if (comma != std::string::npos) {
            Value result;
            result.kind = Value::Kind::Text;
            result.origin = value.origin;
            result.text = style::trim(inner.substr(comma + 1));
            return resolveValue(result, depth + 1);
        }
        return value;
    }

    // {entry-name}
    if (text.size() > 2 && text.front() == '{' && text.back() == '}') {
        const std::string name = style::trim(text.substr(1, text.size() - 2));
        Entry reference;
        if (entryFromName(name, reference) && !raw(reference).empty()) {
            Value result = raw(reference);
            result.origin = value.origin;
            return resolveValue(result, depth + 1);
        }
        if (const std::string* variable = findVar(name)) {
            Value result;
            result.kind = Value::Kind::Text;
            result.origin = value.origin;
            result.text = *variable;
            return resolveValue(result, depth + 1);
        }
        return value;
    }

    Color color;
    if (style::parseColor(text, color)) {
        Value result;
        result.kind = Value::Kind::Color;
        result.origin = value.origin;
        result.color = color;
        return result;
    }

    // 百分比保留文本形式：布局解析需要知道 "%"（px 等线性单位不需要）
    const bool percent = text.back() == '%';
    float number = 0.0f;
    if (!percent && style::parseNumber(text, number)) {
        Value result;
        result.kind = Value::Kind::Number;
        result.origin = value.origin;
        result.number = number;
        return result;
    }

    return value;
}

Color StyleSheet::color(Entry entry, Color fallback) const {
    const Value value = resolved(entry);
    if (value.kind == Value::Kind::Color) return value.color;
    if (value.kind == Value::Kind::Text) {
        Color parsed;
        if (style::parseColor(value.text, parsed)) return parsed;
    }
    return fallback;
}

float StyleSheet::number(Entry entry, float fallback) const {
    const Value value = resolved(entry);
    if (value.kind == Value::Kind::Number) return value.number;
    if (value.kind == Value::Kind::Text) {
        float parsed = 0.0f;
        if (style::parseNumber(value.text, parsed)) return parsed;
    }
    return fallback;
}

int32_t StyleSheet::integer(Entry entry, int32_t fallback) const {
    const float value = number(entry, static_cast<float>(fallback));
    return static_cast<int32_t>(std::lround(value));
}

} // namespace modern_ui
