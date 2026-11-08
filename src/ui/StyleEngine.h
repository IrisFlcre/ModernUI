// ============================================================================
// StyleEngine.h — 样式级联引擎（.css 规则 → 具体 Widget 的 StyleSheet）
// ============================================================================
//
// 职责：
//   - 持有已解析的 CssDocument；
//   - 按类型名 / id / class 匹配规则，按 specificity + 源顺序级联；
//   - 把命中规则写入 Widget 的 StyleSheet（Origin = Stylesheet）；
//   - 把 :root / 规则中的自定义属性写入变量，供 var(--name) 引用。
//
// 只负责"覆写样式表"，不修改布局 / 不直接操作 Canvas。
// View 在控件挂载、id/class 变化、CSS 重新加载时调用 applyTo()。
// ============================================================================

#pragma once

#include <string>

#include "Css.h"
#include "StyleSheet.h"

namespace modern_ui {

class Widget;

class StyleEngine {
public:
    bool loadText(const std::string& css, std::string* error = nullptr);
    bool loadFile(const std::string& path, std::string* error = nullptr);
    void clear();

    bool empty() const { return document_.empty(); }
    const CssDocument& document() const { return document_; }

    // 把匹配到的规则覆盖到 sheet（不会清空 sheet 中已有的默认值）
    void applyTo(Widget& widget, StyleSheet& sheet) const;

private:
    CssDocument document_;
};

} // namespace modern_ui
