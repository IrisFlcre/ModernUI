// ============================================================================
// Widget.cpp — 控件接口层实现
// ============================================================================

#include "Widget.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <typeinfo>
#include <utility>

#include "Css.h"
#include "Flexbox.h"
#include "StyleEngine.h"
#include "View.h"

namespace modern_ui {
namespace {

// RTTI 名称 → 简化类名（Button / StackPanel / View …）
std::string simplifyTypeName(const char* rawName) {
    std::string name = rawName ? rawName : "";

    const std::string classPrefix = "class ";
    const std::string structPrefix = "struct ";
    if (name.rfind(classPrefix, 0) == 0) name.erase(0, classPrefix.size());
    else if (name.rfind(structPrefix, 0) == 0) name.erase(0, structPrefix.size());

    // Itanium ABI：N10modern_ui6fluent6ButtonE → 取最后一个长度前缀段
    if (!name.empty() && name[0] == 'N') {
        std::string last;
        size_t index = 1;
        while (index < name.size() && name[index] != 'E') {
            size_t length = 0;
            while (index < name.size() && std::isdigit(static_cast<unsigned char>(name[index]))) {
                length = length * 10 + static_cast<size_t>(name[index] - '0');
                ++index;
            }
            if (length == 0 || index + length > name.size()) break;
            last = name.substr(index, length);
            index += length;
        }
        if (!last.empty()) return last;
    }

    const size_t scope = name.rfind("::");
    return scope == std::string::npos ? name : name.substr(scope + 2);
}

// ============================================================================
// .css 布局属性 → FlexStyle
// ============================================================================
//
// Widget 的 style() 由 C++ 代码描述结构；这里允许样式表覆盖布局参数，
// 使 .css 也能完成响应式 / 主题化的"排布"（与 .ui 属性同一套命名）。
// 只读取显式出现的属性，不会重置 C++ 侧已设置的值。

std::vector<std::string> splitCssTokens(const std::string& text) {
    std::vector<std::string> tokens;
    size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index]))) {
            ++index;
        }
        const size_t begin = index;
        while (index < text.size() && !std::isspace(static_cast<unsigned char>(text[index]))) {
            ++index;
        }
        if (index > begin) tokens.push_back(text.substr(begin, index - begin));
    }
    return tokens;
}

// 读取 others 中的布局属性，并展开 var(--name) / {entry}
std::string cssLayoutValue(const StyleSheet& sheet, const char* key) {
    auto it = sheet.others.find(key);
    if (it == sheet.others.end()) return {};

    const StyleSheet::Value value = sheet.resolvedText(it->second);
    switch (value.kind) {
    case StyleSheet::Value::Kind::Number: return style::numberToCss(value.number);
    case StyleSheet::Value::Kind::Text:   return style::trim(value.text);
    case StyleSheet::Value::Kind::Color:  return style::colorToCss(value.color);
    case StyleSheet::Value::Kind::None:
    default:                              return {};
    }
}

std::string cssFirstValue(const StyleSheet& sheet,
                          const std::initializer_list<const char*>& keys) {
    for (const char* key : keys) {
        const std::string value = cssLayoutValue(sheet, key);
        if (!value.empty()) return value;
    }
    return {};
}

bool cssBool(const std::string& text, bool fallback) {
    const std::string value = style::toLower(style::trim(text));
    if (value == "true" || value == "1" || value == "yes") return true;
    if (value == "false" || value == "0" || value == "no") return false;
    return fallback;
}

bool parseCssDimension(const std::string& text, Dimension& out) {
    const std::string value = style::toLower(style::trim(text));
    if (value.empty()) return false;
    if (value == "auto" || value == "none") {
        out = Dimension{};
        return true;
    }

    float number = 0.0f;
    if (!style::parseNumber(value, number)) return false;
    const bool percent = value.back() == '%';
    out = percent ? Dimension::percent(number) : Dimension::px(number);
    return true;
}

bool parseCssEdges(const std::string& text, Edges& out) {
    const std::vector<std::string> tokens = splitCssTokens(text);
    if (tokens.empty() || tokens.size() > 4) return false;

    int32_t values[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < tokens.size(); ++i) {
        float number = 0.0f;
        if (!style::parseNumber(tokens[i], number)) return false;
        values[i] = static_cast<int32_t>(number);
    }

    switch (tokens.size()) {
    case 1: out = Edges{values[0]}; break;
    case 2: out = Edges{values[0], values[1]}; break;
    case 3: out = Edges{values[0], values[1], values[2], values[1]}; break;
    default: out = Edges{values[0], values[1], values[2], values[3]}; break;
    }
    return true;
}

} // namespace

Widget::Widget() : canvas_(1, 1) {
    // 子 Canvas 任何标脏都上报给控件 → View 维护 O(1) 的脏子树标记。
    canvas_.setInvalidateCallback([this] { markContentDirty(); });
}

void Widget::markContentDirty() {
    contentDirty_ = true;
    if (view_) {
        view_->noteSubtreeDirty(*this);
    } else {
        subtreeDirty_ = true;
    }
}

// ============================================================================
// 标识
// ============================================================================

const char* Widget::typeName() const {
    static std::map<std::string, std::string> cache;
    const char* raw = typeid(*this).name();
    auto it = cache.find(raw);
    if (it == cache.end()) {
        it = cache.emplace(raw, simplifyTypeName(raw)).first;
    }
    return it->second.c_str();
}

Widget& Widget::setId(std::string id) {
    if (id_ == id) return *this;
    id_ = std::move(id);
    if (view_) refreshStyle();
    return *this;
}

Widget& Widget::setClassList(const std::string& classList) {
    std::vector<std::string> parsed;
    size_t index = 0;
    while (index < classList.size()) {
        while (index < classList.size() && std::isspace(static_cast<unsigned char>(classList[index]))) {
            ++index;
        }
        const size_t begin = index;
        while (index < classList.size() && !std::isspace(static_cast<unsigned char>(classList[index]))) {
            ++index;
        }
        if (index > begin) {
            std::string name = classList.substr(begin, index - begin);
            if (std::find(parsed.begin(), parsed.end(), name) == parsed.end()) {
                parsed.push_back(std::move(name));
            }
        }
    }
    if (parsed == classes_) return *this;
    classes_ = std::move(parsed);
    if (view_) refreshStyle();
    return *this;
}

Widget& Widget::addClass(const std::string& name) {
    if (name.empty() || hasClass(name)) return *this;
    classes_.push_back(name);
    if (view_) refreshStyle();
    return *this;
}

Widget& Widget::removeClass(const std::string& name) {
    auto it = std::find(classes_.begin(), classes_.end(), name);
    if (it == classes_.end()) return *this;
    classes_.erase(it);
    if (view_) refreshStyle();
    return *this;
}

bool Widget::hasClass(const std::string& name) const {
    return std::find(classes_.begin(), classes_.end(), name) != classes_.end();
}

// ============================================================================
// 样式表
// ============================================================================

StyleSheet Widget::defaultStyleSheet() const {
    return StyleSheet{};
}

void Widget::refreshStyle() {
    StyleSheet sheet = defaultStyleSheet();

    if (view_) {
        view_->styleEngine().applyTo(*this, sheet);
    }

    if (!manualStyle_.empty()) {
        sheet.merge(manualStyle_.withOrigin(StyleSheet::Origin::Stylesheet));
    }

    if (!inlineStyle_.empty()) {
        std::vector<CssDeclaration> declarations;
        if (CssDocument::parseDeclarations(inlineStyle_, declarations, nullptr)) {
            for (const CssDeclaration& declaration : declarations) {
                applyDeclaration(sheet, declaration.property, declaration.value,
                                 StyleSheet::Origin::Inline, CssSelector::State::None);
            }
        }
    }

    styleSheet_ = std::move(sheet);
    // .css 中的布局属性（padding / margin / gap / width / flex …）同样参与级联
    const bool layoutChanged = applyLayoutStyle();
    onStyleChanged();
    if (layoutChanged) invalidateLayout();
    canvas_.markDirtyAll();
}

Widget& Widget::applyStyleSheet(const StyleSheet& overlay) {
    manualStyle_.merge(overlay);
    refreshStyle();
    return *this;
}

Widget& Widget::clearStyleSheetOverrides() {
    if (manualStyle_.empty()) return *this;
    manualStyle_.clear();
    refreshStyle();
    return *this;
}

Widget& Widget::setInlineStyle(std::string cssText) {
    if (inlineStyle_ == cssText) return *this;
    inlineStyle_ = std::move(cssText);
    refreshStyle();
    return *this;
}

std::string Widget::customStyle(const std::string& key, std::string fallback) const {
    return styleSheet_.custom(key, std::move(fallback));
}

float Widget::cssOpacity() const {
    const float value = styleSheet_.number(StyleSheet::Entry::Opacity, 1.0f);
    return std::max(0.0f, std::min(1.0f, value));
}

// ============================================================================
// .css 布局属性 → FlexStyle
// ============================================================================

bool Widget::applyLayoutStyle() {
    if (styleSheet_.others.empty()) return false;

    bool changed = false;

    auto applyDimension = [&](const std::initializer_list<const char*>& keys,
                              Dimension& target) {
        const std::string text = cssFirstValue(styleSheet_, keys);
        if (text.empty()) return;
        Dimension parsed;
        if (!parseCssDimension(text, parsed)) return;
        if (parsed.kind == target.kind && parsed.value == target.value) return;
        target = parsed;
        changed = true;
    };

    auto applyInt = [&](const std::initializer_list<const char*>& keys,
                        int32_t& target) {
        const std::string text = cssFirstValue(styleSheet_, keys);
        if (text.empty()) return;
        float number = 0.0f;
        if (!style::parseNumber(text, number)) return;
        const int32_t next = static_cast<int32_t>(number);
        if (next == target) return;
        target = next;
        changed = true;
    };

    // ---- 容器 ----
    const std::string direction = cssFirstValue(styleSheet_, {"direction", "flex-direction"});
    if (!direction.empty()) {
        const std::string value = style::toLower(direction);
        FlexDirection next = style_.direction;
        bool valid = true;
        if (value == "row") next = FlexDirection::Row;
        else if (value == "row-reverse") next = FlexDirection::RowReverse;
        else if (value == "column" || value == "col") next = FlexDirection::Column;
        else if (value == "column-reverse") next = FlexDirection::ColumnReverse;
        else valid = false;
        if (valid && next != style_.direction) {
            style_.direction = next;
            changed = true;
        }
    }

    const std::string wrap = cssFirstValue(styleSheet_, {"wrap", "flex-wrap"});
    if (!wrap.empty()) {
        const std::string value = style::toLower(wrap);
        FlexWrap next = style_.wrap;
        bool valid = true;
        if (value == "nowrap" || value == "no-wrap") next = FlexWrap::NoWrap;
        else if (value == "wrap") next = FlexWrap::Wrap;
        else if (value == "wrap-reverse") next = FlexWrap::WrapReverse;
        else valid = false;
        if (valid && next != style_.wrap) {
            style_.wrap = next;
            changed = true;
        }
    }

    const std::string justify = cssFirstValue(styleSheet_, {"justify", "justify-content"});
    if (!justify.empty()) {
        const std::string value = style::toLower(justify);
        JustifyContent next = style_.justify;
        bool valid = true;
        if (value == "start" || value == "flex-start") next = JustifyContent::FlexStart;
        else if (value == "end" || value == "flex-end") next = JustifyContent::FlexEnd;
        else if (value == "center") next = JustifyContent::Center;
        else if (value == "space-between") next = JustifyContent::SpaceBetween;
        else if (value == "space-around") next = JustifyContent::SpaceAround;
        else if (value == "space-evenly") next = JustifyContent::SpaceEvenly;
        else valid = false;
        if (valid && next != style_.justify) {
            style_.justify = next;
            changed = true;
        }
    }

    const std::string align = cssFirstValue(styleSheet_, {"align", "align-items"});
    if (!align.empty()) {
        const std::string value = style::toLower(align);
        AlignItems next = style_.alignItems;
        bool valid = true;
        if (value == "start" || value == "flex-start") next = AlignItems::FlexStart;
        else if (value == "end" || value == "flex-end") next = AlignItems::FlexEnd;
        else if (value == "center") next = AlignItems::Center;
        else if (value == "stretch") next = AlignItems::Stretch;
        else if (value == "baseline") next = AlignItems::Baseline;
        else valid = false;
        if (valid && next != style_.alignItems) {
            style_.alignItems = next;
            changed = true;
        }
    }

    applyInt({"gap"}, style_.gap);
    {
        const std::string crossGap = cssFirstValue(styleSheet_, {"cross-gap", "row-gap", "column-gap"});
        if (!crossGap.empty()) {
            float number = 0.0f;
            if (style::parseNumber(crossGap, number) &&
                static_cast<int32_t>(number) != style_.crossGap) {
                style_.crossGap = static_cast<int32_t>(number);
                changed = true;
            }
        }
    }

    // padding / margin：先 shorthand，再 longhand 覆盖
    {
        Edges edges;
        const std::string padding = cssFirstValue(styleSheet_, {"padding"});
        if (!padding.empty() && parseCssEdges(padding, edges)) {
            style_.padding = edges;
            changed = true;
        }
        struct SideEntry { const char* key; int32_t Edges::*member; };
        const SideEntry paddingSides[] = {
            {"padding-top", &Edges::top}, {"padding-right", &Edges::right},
            {"padding-bottom", &Edges::bottom}, {"padding-left", &Edges::left}
        };
        for (const SideEntry& side : paddingSides) {
            const std::string text = cssLayoutValue(styleSheet_, side.key);
            float number = 0.0f;
            if (!text.empty() && style::parseNumber(text, number)) {
                style_.padding.*(side.member) = static_cast<int32_t>(number);
                changed = true;
            }
        }

        const std::string margin = cssFirstValue(styleSheet_, {"margin"});
        if (!margin.empty() && parseCssEdges(margin, edges)) {
            style_.margin = edges;
            changed = true;
        }
        const SideEntry marginSides[] = {
            {"margin-top", &Edges::top}, {"margin-right", &Edges::right},
            {"margin-bottom", &Edges::bottom}, {"margin-left", &Edges::left}
        };
        for (const SideEntry& side : marginSides) {
            const std::string text = cssLayoutValue(styleSheet_, side.key);
            float number = 0.0f;
            if (!text.empty() && style::parseNumber(text, number)) {
                style_.margin.*(side.member) = static_cast<int32_t>(number);
                changed = true;
            }
        }
    }

    // ---- 子项尺寸 / 伸缩 ----
    applyDimension({"width"}, style_.width);
    applyDimension({"height"}, style_.height);
    applyDimension({"flex-basis"}, style_.flexBasis);

    {
        const std::string minWidth = cssFirstValue(styleSheet_, {"min-width"});
        const std::string minHeight = cssFirstValue(styleSheet_, {"min-height"});
        const std::string maxWidth = cssFirstValue(styleSheet_, {"max-width"});
        const std::string maxHeight = cssFirstValue(styleSheet_, {"max-height"});
        float number = 0.0f;
        if (!minWidth.empty() && style::parseNumber(minWidth, number)) {
            style_.minWidth = static_cast<int32_t>(number);
            changed = true;
        }
        if (!minHeight.empty() && style::parseNumber(minHeight, number)) {
            style_.minHeight = static_cast<int32_t>(number);
            changed = true;
        }
        if (!maxWidth.empty() && style::parseNumber(maxWidth, number)) {
            style_.maxWidth = static_cast<int32_t>(number);
            changed = true;
        }
        if (!maxHeight.empty() && style::parseNumber(maxHeight, number)) {
            style_.maxHeight = static_cast<int32_t>(number);
            changed = true;
        }
    }

    applyInt({"flex-grow"}, style_.flexGrow);
    applyInt({"flex-shrink"}, style_.flexShrink);

    // flex 简写：flex: <grow> [<shrink>] [<basis>] / auto / none
    {
        const std::string flex = cssFirstValue(styleSheet_, {"flex"});
        if (!flex.empty()) {
            const std::string value = style::toLower(flex);
            if (value == "auto") {
                style_.flexGrow = 1;
                style_.flexShrink = 1;
                style_.flexBasis = Dimension{};
                changed = true;
            } else if (value == "none") {
                style_.flexGrow = 0;
                style_.flexShrink = 0;
                style_.flexBasis = Dimension{};
                changed = true;
            } else {
                const std::vector<std::string> tokens = splitCssTokens(flex);
                if (!tokens.empty()) {
                    float grow = 0.0f;
                    if (style::parseNumber(tokens[0], grow)) {
                        style_.flexGrow = static_cast<int32_t>(grow);
                        style_.flexShrink = 1;
                        changed = true;
                    }
                    if (tokens.size() >= 2) {
                        float shrink = 1.0f;
                        if (style::parseNumber(tokens[1], shrink)) {
                            style_.flexShrink = static_cast<int32_t>(shrink);
                        }
                    }
                    if (tokens.size() >= 3) {
                        Dimension basis;
                        if (parseCssDimension(tokens[2], basis)) style_.flexBasis = basis;
                    } else {
                        style_.flexBasis = Dimension{};
                    }
                }
            }
        }
    }

    // ---- 定位 / 裁剪 ----
    {
        const std::string position = cssFirstValue(styleSheet_, {"position"});
        if (!position.empty()) {
            const std::string value = style::toLower(position);
            const Position next = value == "absolute" ? Position::Absolute : Position::Relative;
            if (next != style_.position) {
                style_.position = next;
                changed = true;
            }
        }
    }
    applyInt({"x"}, style_.x);
    applyInt({"y"}, style_.y);

    {
        const std::string clip = cssFirstValue(styleSheet_, {"clip"});
        if (!clip.empty()) {
            const bool next = cssBool(clip, style_.clip);
            if (next != style_.clip) {
                style_.clip = next;
                changed = true;
            }
        }
        const std::string overflow = cssFirstValue(styleSheet_, {"overflow", "overflow-x"});
        if (!overflow.empty()) {
            const std::string value = style::toLower(overflow);
            const bool next = value != "visible";
            if (next != style_.clip) {
                style_.clip = next;
                changed = true;
            }
        }
    }

    return changed;
}

Widget::~Widget() {
    // 删除几何体前的准备：
    //   1) 先让 View 把本子树在渲染 Canvas 中的镜像 Group 全部摘掉
    //      （几何体先离开场景图，避免留下悬空引用）
    //   2) 再释放子树：子控件此时镜像已清理，析构不会再重复操作
    //   3) 最后清空自己的子 Canvas，控件几何体被显式释放
    if (view_) view_->detachSubtree(*this);
    children_.clear();
    canvas_.clear();
}

Vec2i Widget::onMeasure(const Vec2i& available) const {
    // 默认：把自己当作容器预排一遍，返回内容包围盒
    return Flexbox::measureIntrinsic(*this, available);
}

bool Widget::hitTest(const Vec2i& localPoint) const {
    return localPoint.x >= 0 && localPoint.y >= 0 &&
           localPoint.x < frame_.w && localPoint.y < frame_.h;
}

void Widget::invalidateLayout() {
    // 向上传染到根；遇到已经脏的祖先即可停止（它的祖先此前已被标记）
    for (Widget* node = this; node; node = node->parent_) {
        if (node->layoutDirty_) break;
        node->layoutDirty_ = true;
    }
}

// ============================================================================
// 链式布局语法糖（薄封装 + 仅在值变化时触发重排）
// ============================================================================

namespace {

bool sameEdges(const Edges& a, const Edges& b) {
    return a.top == b.top && a.right == b.right && a.bottom == b.bottom && a.left == b.left;
}

bool sameDimension(const Dimension& a, const Dimension& b) {
    return a.kind == b.kind && a.value == b.value;
}

} // namespace

Widget& Widget::padding(int32_t all) { return padding(Edges{all}); }

Widget& Widget::padding(int32_t vertical, int32_t horizontal) {
    return padding(Edges{vertical, horizontal});
}

Widget& Widget::padding(const Edges& edges) {
    if (sameEdges(style_.padding, edges)) return *this;
    style_.padding = edges;
    invalidateLayout();
    return *this;
}

Widget& Widget::margin(int32_t all) { return margin(Edges{all}); }

Widget& Widget::margin(int32_t vertical, int32_t horizontal) {
    return margin(Edges{vertical, horizontal});
}

Widget& Widget::margin(const Edges& edges) {
    if (sameEdges(style_.margin, edges)) return *this;
    style_.margin = edges;
    invalidateLayout();
    return *this;
}

Widget& Widget::gap(int32_t value) {
    if (style_.gap == value) return *this;
    style_.gap = value;
    invalidateLayout();
    return *this;
}

Widget& Widget::grow(int32_t value) {
    if (style_.flexGrow == value) return *this;
    style_.flexGrow = value;
    invalidateLayout();
    return *this;
}

Widget& Widget::shrink(int32_t value) {
    if (style_.flexShrink == value) return *this;
    style_.flexShrink = value;
    invalidateLayout();
    return *this;
}

Widget& Widget::basis(int32_t pixels) {
    const Dimension next = Dimension::px(static_cast<float>(pixels));
    if (sameDimension(style_.flexBasis, next)) return *this;
    style_.flexBasis = next;
    invalidateLayout();
    return *this;
}

Widget& Widget::size(int32_t width, int32_t height) {
    const Dimension w = Dimension::px(static_cast<float>(width));
    const Dimension h = Dimension::px(static_cast<float>(height));
    if (sameDimension(style_.width, w) && sameDimension(style_.height, h)) return *this;
    style_.width = w;
    style_.height = h;
    invalidateLayout();
    return *this;
}

Widget& Widget::minSize(int32_t width, int32_t height) {
    if (style_.minWidth == width && style_.minHeight == height) return *this;
    style_.minWidth = width;
    style_.minHeight = height;
    invalidateLayout();
    return *this;
}

Widget& Widget::maxSize(int32_t width, int32_t height) {
    if (style_.maxWidth == width && style_.maxHeight == height) return *this;
    style_.maxWidth = width;
    style_.maxHeight = height;
    invalidateLayout();
    return *this;
}

Widget& Widget::row() {
    if (style_.direction == FlexDirection::Row) return *this;
    style_.direction = FlexDirection::Row;
    invalidateLayout();
    return *this;
}

Widget& Widget::column() {
    if (style_.direction == FlexDirection::Column) return *this;
    style_.direction = FlexDirection::Column;
    invalidateLayout();
    return *this;
}

Widget& Widget::justify(JustifyContent value) {
    if (style_.justify == value) return *this;
    style_.justify = value;
    invalidateLayout();
    return *this;
}

Widget& Widget::align(AlignItems value) {
    if (style_.alignItems == value) return *this;
    style_.alignItems = value;
    invalidateLayout();
    return *this;
}

Widget& Widget::alignSelf(AlignSelf value) {
    if (style_.alignSelf == value) return *this;
    style_.alignSelf = value;
    invalidateLayout();
    return *this;
}

Widget& Widget::absolute(int32_t x, int32_t y) {
    if (style_.position == Position::Absolute && style_.x == x && style_.y == y) return *this;
    style_.position = Position::Absolute;
    style_.x = x;
    style_.y = y;
    invalidateLayout();
    return *this;
}

Widget& Widget::clip(bool on) {
    if (style_.clip == on) return *this;
    style_.clip = on;
    invalidateLayout();
    return *this;
}

View* Widget::rootView() const {
    for (Widget* node = const_cast<Widget*>(this); node; node = node->parent_) {
        if (View* view = dynamic_cast<View*>(node)) return view;
    }
    return nullptr;
}

// ============================================================================
// ParentWidget
// ============================================================================

Widget* ParentWidget::addChild(std::unique_ptr<Widget> child) {
    if (!child) return nullptr;

    Widget* raw = child.get();
    // 已挂在其它 View / 父节点：先摘除旧镜像，避免残留
    if (raw->view_) raw->view_->detachSubtree(*raw);
    if (raw->parent_) raw->parent_ = nullptr;

    raw->parent_ = this;
    children_.push_back(std::move(child));

    // 已经挂在某个 View 上：立刻接入（下发渲染后端 + 建立镜像占位）
    if (View* view = rootView()) view->attachSubtree(*raw);
    invalidateLayout();
    return raw;
}

std::unique_ptr<Widget> ParentWidget::removeChild(Widget* child) {
    if (!child) return nullptr;

    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if (it->get() != child) continue;

        std::unique_ptr<Widget> owned = std::move(*it);
        children_.erase(it);
        if (View* view = rootView()) view->detachSubtree(*owned);
        invalidateLayout();
        return owned;
    }
    return nullptr;
}

void ParentWidget::clearChildren() {
    while (!children_.empty()) {
        removeChild(children_.back().get());
    }
}

} // namespace modern_ui
