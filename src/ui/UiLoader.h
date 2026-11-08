// ============================================================================
// UiLoader.h — .ui 布局文件 → 控件树
// ============================================================================
//
// 使用流程：
//   UiLoader loader;
//   registerFluentWidgets(loader);            // 注册 C++ 控件工厂（std_widgets）
//   loader.registerHandler("submit", [&] { ... });
//   loader.loadFile("app.ui", view);          // 构建并挂载到 View
//
// .ui 标签 = 控件类型名；属性分三类：
//   1) 通用：id / class / style（内联 CSS）
//   2) 布局：width / height / padding / margin / gap / direction / justify /
//            align / flex-grow / absolute / x / y / clip …
//   3) 控件专属：text / value / checked / items / onClick …
//      （由各控件工厂的 configure 回调解释）
//
// 回调：属性值 = handler 名称，通过 registerHandler 注册为 void() 函数。
// ============================================================================

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Ui.h"
#include "Widget.h"

namespace modern_ui {

class ParentWidget;

class UiLoader {
public:
    struct Factory {
        std::function<std::unique_ptr<Widget>(const UiNode&)> create;
        std::function<void(Widget&, const UiNode&, UiLoader&)> configure;
        // 非 ParentWidget 的容器（Expander / ScrollViewer / NavigationView）
        std::function<void(Widget&, std::unique_ptr<Widget>)> attachChild;
    };

    void registerFactory(std::string tag, Factory factory);
    void registerHandler(std::string name, std::function<void()> handler);

    const Factory* findFactory(const std::string& tag) const;
    // 读取 onClick / onValueChanged 等属性并解析成已注册的 handler
    std::function<void()> handlerFor(const UiNode& node, const std::string& attribute) const;

    bool loadFile(const std::string& path, ParentWidget& parent, std::string* error = nullptr);
    bool loadText(const std::string& text, ParentWidget& parent, std::string* error = nullptr);
    // 构建单个节点（含子树）并挂到 parent
    bool buildInto(const UiNode& node, ParentWidget& parent, std::string* error = nullptr);

    // ---- 属性工具（供控件工厂使用）----
    static bool parseDimension(const std::string& text, Dimension& out);
    static bool parseEdges(const std::string& text, Edges& out);
    static int32_t intValue(const UiNode& node, const std::string& name, int32_t fallback);
    static float floatValue(const UiNode& node, const std::string& name, float fallback);
    static bool boolValue(const UiNode& node, const std::string& name, bool fallback);
    // 列表属性用 '|' 分隔（标签文本可安全包含逗号）
    static std::vector<std::string> listValue(const UiNode& node, const std::string& name);

private:
    std::unique_ptr<Widget> buildWidget(const UiNode& node, std::string* error);
    void applyCommonAttributes(Widget& widget, const UiNode& node);
    void applyLayoutAttributes(Widget& widget, const UiNode& node);

    std::map<std::string, Factory> factories_;
    std::map<std::string, std::function<void()>> handlers_;
};

} // namespace modern_ui
