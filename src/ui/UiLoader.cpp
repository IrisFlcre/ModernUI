// ============================================================================
// UiLoader.cpp — .ui 节点 → Widget 实例
// ============================================================================

#include "UiLoader.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include "StyleSheet.h"

namespace modern_ui {
namespace {

std::string trimmed(const std::string& text) {
    return style::trim(text);
}

std::string lower(std::string text) {
    return style::toLower(std::move(text));
}

float parseFloat(const std::string& text, float fallback) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end == text.c_str() ? fallback : static_cast<float>(value);
}

std::vector<std::string> splitNumbers(const std::string& text) {
    std::string normalized = text;
    for (char& c : normalized) {
        if (c == ',' || c == '\t' || c == '\n' || c == '\r') c = ' ';
    }

    std::vector<std::string> tokens;
    std::istringstream stream(normalized);
    std::string token;
    while (stream >> token) tokens.push_back(token);
    return tokens;
}

bool parseDirection(const std::string& value, FlexDirection& out) {
    const std::string name = lower(trimmed(value));
    if (name == "row") { out = FlexDirection::Row; return true; }
    if (name == "row-reverse") { out = FlexDirection::RowReverse; return true; }
    if (name == "column" || name == "col") { out = FlexDirection::Column; return true; }
    if (name == "column-reverse") { out = FlexDirection::ColumnReverse; return true; }
    return false;
}

bool parseWrap(const std::string& value, FlexWrap& out) {
    const std::string name = lower(trimmed(value));
    if (name == "nowrap" || name == "no-wrap") { out = FlexWrap::NoWrap; return true; }
    if (name == "wrap") { out = FlexWrap::Wrap; return true; }
    if (name == "wrap-reverse") { out = FlexWrap::WrapReverse; return true; }
    return false;
}

bool parseJustify(const std::string& value, JustifyContent& out) {
    const std::string name = lower(trimmed(value));
    if (name == "start" || name == "flex-start") { out = JustifyContent::FlexStart; return true; }
    if (name == "end" || name == "flex-end") { out = JustifyContent::FlexEnd; return true; }
    if (name == "center") { out = JustifyContent::Center; return true; }
    if (name == "space-between") { out = JustifyContent::SpaceBetween; return true; }
    if (name == "space-around") { out = JustifyContent::SpaceAround; return true; }
    if (name == "space-evenly") { out = JustifyContent::SpaceEvenly; return true; }
    return false;
}

bool parseAlignItems(const std::string& value, AlignItems& out) {
    const std::string name = lower(trimmed(value));
    if (name == "start" || name == "flex-start") { out = AlignItems::FlexStart; return true; }
    if (name == "end" || name == "flex-end") { out = AlignItems::FlexEnd; return true; }
    if (name == "center") { out = AlignItems::Center; return true; }
    if (name == "stretch") { out = AlignItems::Stretch; return true; }
    if (name == "baseline") { out = AlignItems::Baseline; return true; }
    return false;
}

bool parseAlignContent(const std::string& value, AlignContent& out) {
    const std::string name = lower(trimmed(value));
    if (name == "start" || name == "flex-start") { out = AlignContent::FlexStart; return true; }
    if (name == "end" || name == "flex-end") { out = AlignContent::FlexEnd; return true; }
    if (name == "center") { out = AlignContent::Center; return true; }
    if (name == "space-between") { out = AlignContent::SpaceBetween; return true; }
    if (name == "space-around") { out = AlignContent::SpaceAround; return true; }
    if (name == "space-evenly") { out = AlignContent::SpaceEvenly; return true; }
    if (name == "stretch") { out = AlignContent::Stretch; return true; }
    return false;
}

bool parseAlignSelf(const std::string& value, AlignSelf& out) {
    const std::string name = lower(trimmed(value));
    if (name == "auto") { out = AlignSelf::Auto; return true; }
    if (name == "start" || name == "flex-start") { out = AlignSelf::FlexStart; return true; }
    if (name == "end" || name == "flex-end") { out = AlignSelf::FlexEnd; return true; }
    if (name == "center") { out = AlignSelf::Center; return true; }
    if (name == "stretch") { out = AlignSelf::Stretch; return true; }
    if (name == "baseline") { out = AlignSelf::Baseline; return true; }
    return false;
}

} // namespace

// ============================================================================
// 注册
// ============================================================================
void UiLoader::registerFactory(std::string tag, Factory factory) {
    factories_[std::move(tag)] = std::move(factory);
}

void UiLoader::registerHandler(std::string name, std::function<void()> handler) {
    handlers_[std::move(name)] = std::move(handler);
}

const UiLoader::Factory* UiLoader::findFactory(const std::string& tag) const {
    auto it = factories_.find(tag);
    return it == factories_.end() ? nullptr : &it->second;
}

std::function<void()> UiLoader::handlerFor(const UiNode& node,
                                           const std::string& attribute) const {
    const std::string* name = node.findAttr(attribute);
    if (!name) return {};
    auto it = handlers_.find(*name);
    return it == handlers_.end() ? std::function<void()>{} : it->second;
}

// ============================================================================
// 加载
// ============================================================================
bool UiLoader::loadFile(const std::string& path, ParentWidget& parent, std::string* error) {
    UiDocument document;
    if (!document.parseFile(path, error)) return false;
    return buildInto(document.root(), parent, error);
}

bool UiLoader::loadText(const std::string& text, ParentWidget& parent, std::string* error) {
    UiDocument document;
    if (!document.parse(text, error)) return false;
    return buildInto(document.root(), parent, error);
}

bool UiLoader::buildInto(const UiNode& node, ParentWidget& parent, std::string* error) {
    std::unique_ptr<Widget> widget = buildWidget(node, error);
    if (!widget) return false;
    parent.addChild(std::move(widget));
    return true;
}

std::unique_ptr<Widget> UiLoader::buildWidget(const UiNode& node, std::string* error) {
    const Factory* factory = findFactory(node.tag);
    if (!factory || !factory->create) {
        if (error) {
            *error = "line " + std::to_string(node.line) + ": unknown widget tag <" +
                     node.tag + ">";
        }
        return nullptr;
    }

    std::unique_ptr<Widget> widget = factory->create(node);
    if (!widget) {
        if (error) *error = "line " + std::to_string(node.line) + ": failed to create <" + node.tag + ">";
        return nullptr;
    }

    applyCommonAttributes(*widget, node);
    applyLayoutAttributes(*widget, node);
    if (factory->configure) factory->configure(*widget, node, *this);

    if (!node.children.empty()) {
        // 优先使用工厂的 attachChild：ScrollViewer / Expander / NavigationView
        // 虽然是 ParentWidget，但只通过 setContent 管理唯一内容子控件。
        if (factory->attachChild) {
            for (const UiNode& child : node.children) {
                std::unique_ptr<Widget> childWidget = buildWidget(child, error);
                if (!childWidget) return nullptr;
                factory->attachChild(*widget, std::move(childWidget));
            }
        } else if (auto* parentWidget = dynamic_cast<ParentWidget*>(widget.get())) {
            for (const UiNode& child : node.children) {
                if (!buildInto(child, *parentWidget, error)) return nullptr;
            }
        } else {
            if (error) {
                *error = "line " + std::to_string(node.line) + ": <" + node.tag +
                         "> cannot contain child widgets";
            }
            return nullptr;
        }
    }

    return widget;
}

// ============================================================================
// 属性
// ============================================================================
void UiLoader::applyCommonAttributes(Widget& widget, const UiNode& node) {
    if (const std::string* id = node.findAttr("id")) widget.setId(*id);
    if (const std::string* classes = node.findAttr("class")) widget.setClassList(*classes);
    if (const std::string* style = node.findAttr("style")) widget.setInlineStyle(*style);
}

void UiLoader::applyLayoutAttributes(Widget& widget, const UiNode& node) {
    FlexStyle& style = widget.style();

    if (const std::string* value = node.findAttr("direction")) {
        parseDirection(*value, style.direction);
    }
    if (const std::string* value = node.findAttr("wrap")) {
        parseWrap(*value, style.wrap);
    }
    if (const std::string* value = node.findAttr("justify")) {
        parseJustify(*value, style.justify);
    }
    if (const std::string* value = node.findAttr("align")) {
        parseAlignItems(*value, style.alignItems);
    }
    if (const std::string* value = node.findAttr("align-items")) {
        parseAlignItems(*value, style.alignItems);
    }
    if (const std::string* value = node.findAttr("align-content")) {
        parseAlignContent(*value, style.alignContent);
    }
    if (const std::string* value = node.findAttr("align-self")) {
        parseAlignSelf(*value, style.alignSelf);
    }

    if (const std::string* value = node.findAttr("gap")) {
        style.gap = static_cast<int32_t>(parseFloat(*value, 0.0f));
    }
    if (const std::string* value = node.findAttr("row-gap")) {
        style.gap = static_cast<int32_t>(parseFloat(*value, 0.0f));
    }
    if (const std::string* value = node.findAttr("column-gap")) {
        style.crossGap = static_cast<int32_t>(parseFloat(*value, 0.0f));
    }

    if (const std::string* value = node.findAttr("padding")) {
        parseEdges(*value, style.padding);
    }
    if (const std::string* value = node.findAttr("margin")) {
        parseEdges(*value, style.margin);
    }

    auto edges = [&node](const char* name, Edges& target, int32_t Edges::*member) {
        if (const std::string* value = node.findAttr(name)) {
            target.*member = static_cast<int32_t>(parseFloat(*value, 0.0f));
        }
    };
    edges("padding-top", style.padding, &Edges::top);
    edges("padding-right", style.padding, &Edges::right);
    edges("padding-bottom", style.padding, &Edges::bottom);
    edges("padding-left", style.padding, &Edges::left);
    edges("margin-top", style.margin, &Edges::top);
    edges("margin-right", style.margin, &Edges::right);
    edges("margin-bottom", style.margin, &Edges::bottom);
    edges("margin-left", style.margin, &Edges::left);

    if (const std::string* value = node.findAttr("width")) parseDimension(*value, style.width);
    if (const std::string* value = node.findAttr("height")) parseDimension(*value, style.height);
    if (const std::string* value = node.findAttr("flex-basis")) parseDimension(*value, style.flexBasis);

    if (const std::string* value = node.findAttr("min-width")) {
        style.minWidth = static_cast<int32_t>(parseFloat(*value, 0.0f));
    }
    if (const std::string* value = node.findAttr("min-height")) {
        style.minHeight = static_cast<int32_t>(parseFloat(*value, 0.0f));
    }
    if (const std::string* value = node.findAttr("max-width")) {
        style.maxWidth = static_cast<int32_t>(parseFloat(*value, static_cast<float>(style.maxWidth)));
    }
    if (const std::string* value = node.findAttr("max-height")) {
        style.maxHeight = static_cast<int32_t>(parseFloat(*value, static_cast<float>(style.maxHeight)));
    }

    if (const std::string* value = node.findAttr("flex-grow")) {
        style.flexGrow = static_cast<int32_t>(parseFloat(*value, 0.0f));
    }
    if (const std::string* value = node.findAttr("flex-shrink")) {
        style.flexShrink = static_cast<int32_t>(parseFloat(*value, 1.0f));
    }
    if (const std::string* value = node.findAttr("flex")) {
        style.flexGrow = static_cast<int32_t>(parseFloat(*value, 0.0f));
    }

    if (boolValue(node, "absolute", false)) {
        style.position = Position::Absolute;
    }
    if (const std::string* value = node.findAttr("x")) {
        style.x = static_cast<int32_t>(parseFloat(*value, 0.0f));
    }
    if (const std::string* value = node.findAttr("y")) {
        style.y = static_cast<int32_t>(parseFloat(*value, 0.0f));
    }
    if (const std::string* value = node.findAttr("position")) {
        style.position = lower(trimmed(*value)) == "absolute" ? Position::Absolute : Position::Relative;
    }
    if (!boolValue(node, "clip", true)) style.clip = false;
}

// ============================================================================
// 工具
// ============================================================================
bool UiLoader::parseDimension(const std::string& text, Dimension& out) {
    const std::string value = lower(trimmed(text));
    if (value.empty() || value == "auto") {
        out = Dimension{};
        return false;
    }
    if (!value.empty() && value.back() == '%') {
        out = Dimension::percent(parseFloat(value.substr(0, value.size() - 1), 0.0f));
        return true;
    }
    out = Dimension::px(parseFloat(value, 0.0f));
    return true;
}

bool UiLoader::parseEdges(const std::string& text, Edges& out) {
    const std::vector<std::string> tokens = splitNumbers(text);
    if (tokens.empty()) return false;

    auto value = [&tokens](size_t index) {
        return static_cast<int32_t>(parseFloat(tokens[std::min(index, tokens.size() - 1)], 0.0f));
    };

    switch (tokens.size()) {
    case 1:
        out = Edges{value(0)};
        break;
    case 2:
        out = Edges{value(0), value(1)};
        break;
    case 3:
        out = Edges{value(0), value(1), value(2), value(1)};
        break;
    default:
        out = Edges{value(0), value(1), value(2), value(3)};
        break;
    }
    return true;
}

int32_t UiLoader::intValue(const UiNode& node, const std::string& name, int32_t fallback) {
    const std::string* value = node.findAttr(name);
    return value ? static_cast<int32_t>(parseFloat(*value, static_cast<float>(fallback))) : fallback;
}

float UiLoader::floatValue(const UiNode& node, const std::string& name, float fallback) {
    const std::string* value = node.findAttr(name);
    return value ? parseFloat(*value, fallback) : fallback;
}

bool UiLoader::boolValue(const UiNode& node, const std::string& name, bool fallback) {
    const std::string* value = node.findAttr(name);
    if (!value) return fallback;
    const std::string nameLower = lower(trimmed(*value));
    if (nameLower == "true" || nameLower == "1" || nameLower == "yes") return true;
    if (nameLower == "false" || nameLower == "0" || nameLower == "no") return false;
    return fallback;
}

std::vector<std::string> UiLoader::listValue(const UiNode& node, const std::string& name) {
    std::vector<std::string> items;
    const std::string* value = node.findAttr(name);
    if (!value) return items;

    size_t start = 0;
    while (start <= value->size()) {
        const size_t hit = value->find('|', start);
        const std::string item = trimmed(value->substr(start, hit == std::string::npos
                                                               ? std::string::npos
                                                               : hit - start));
        if (!item.empty()) items.push_back(item);
        if (hit == std::string::npos) break;
        start = hit + 1;
    }
    return items;
}

} // namespace modern_ui
