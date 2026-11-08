// ============================================================================
// Css.h — .css 解析器（选择器 / 声明 / 变量 / 状态伪类）
// ============================================================================
//
// 支持的语法子集：
//   选择器   Tag、.class、#id、组合（Button.primary#ok）、*、逗号列表
//   伪类     :hover / :pressed / :focus / :disabled（可组合，按优先级取状态）
//   声明     任意 property: value;  value 可含 var(--name, fallback)、{entry}
//   变量     :root { --name: value; }（全局）或规则内 --name
//   注释     /* ... */
//
// 布局声明（padding / margin / gap / width / height / min-* / max-* /
// flex / flex-grow / flex-shrink / flex-basis / direction / justify / align /
// align-self / position / x / y / clip / overflow）不会被丢弃，而是进入
// StyleSheet::others，由 Widget 在 refreshStyle 时写入 FlexStyle。
// 因此 .css 也能描述排布，与 .ui 属性共用同一套命名。
//
// 状态伪类不是"重新匹配"，而是在应用声明时改写到对应的状态 Entry：
//   Button:hover { background: X }  → background-hover
//   Button:pressed { background: X } → background-pressed
//   Button:focus { border-color: X } → focus-stroke
//   Button:disabled { color: X }     → text-disabled
// 因此一张 StyleSheet 同时携带 rest / hover / pressed / focus / disabled 的值，
// 控件无需在状态变化时重新做选择器匹配。
// ============================================================================

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "StyleSheet.h"

namespace modern_ui {

// ============================================================================
// CssSelector
// ============================================================================
struct CssSelector {
    enum class State : uint8_t { None = 0, Hover, Pressed, Focus, Disabled };

    std::string              tag;      // 空 = 匹配任意标签
    std::string              id;
    std::vector<std::string> classes;
    State                    state = State::None;

    int specificity() const;
    bool matches(const std::string& tagName,
                 const std::string& widgetId,
                 const std::vector<std::string>& classList) const;
    std::string text() const;
};

struct CssDeclaration {
    std::string property;
    std::string value;
};

struct CssRule {
    std::vector<CssSelector>     selectors;
    std::vector<CssDeclaration>  declarations;
    int                          order = 0;
};

// ============================================================================
// CssDocument
// ============================================================================
class CssDocument {
public:
    void clear();
    bool empty() const { return rules_.empty() && variables_.empty(); }

    bool parse(const std::string& text, std::string* error = nullptr);
    bool parseFile(const std::string& path, std::string* error = nullptr);

    const std::vector<CssRule>& rules() const { return rules_; }
    const std::map<std::string, std::string>& variables() const { return variables_; }

    // 样式表文本 → 声明列表（也用于 style="..." 内联样式）
    static bool parseDeclarations(const std::string& text,
                                  std::vector<CssDeclaration>& out,
                                  std::string* error = nullptr);

private:
    std::vector<CssRule> rules_;
    std::map<std::string, std::string> variables_;
};

// 状态伪类 → 状态 Entry 名称改写
std::string stateProperty(const std::string& property, CssSelector::State state);

// 把一条声明写入样式表（未知属性写入 others，--name 写入变量）
bool applyDeclaration(StyleSheet& sheet, const std::string& property,
                      const std::string& value, StyleSheet::Origin origin,
                      CssSelector::State state);

} // namespace modern_ui
