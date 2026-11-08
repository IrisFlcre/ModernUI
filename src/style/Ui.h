// ============================================================================
// Ui.h — .ui 布局文件解析器（XML 风格自定义标签）
// ============================================================================
//
// 语法示例：
//   <?xml version="1.0"?>
//   <StackPanel class="root" direction="column" gap="12" padding="24">
//     <TextBlock class="title" text="ModernUI"/>
//     <Button id="ok" class="primary" text="OK" onClick="submit"/>
//   </StackPanel>
//
// 约束：
//   - 标签名 = 控件类型名（Widget::typeName()，例如 Button / StackPanel）
//   - 属性名统一小写处理；值用单 / 双引号包裹
//   - 支持注释、自闭合标签、文本节点、常用 XML 实体
// ============================================================================

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace modern_ui {

struct UiAttribute {
    std::string name;
    std::string value;
};

struct UiNode {
    std::string                  tag;
    std::vector<UiAttribute>     attributes;
    std::string                  text;
    std::vector<UiNode>          children;
    int                          line = 0;

    const std::string* findAttr(const std::string& name) const {
        for (const UiAttribute& attribute : attributes) {
            if (attribute.name == name) return &attribute.value;
        }
        return nullptr;
    }

    bool hasAttr(const std::string& name) const { return findAttr(name) != nullptr; }

    std::string attr(const std::string& name, std::string fallback = {}) const {
        const std::string* value = findAttr(name);
        return value ? *value : std::move(fallback);
    }
};

class UiDocument {
public:
    void clear();
    bool parse(const std::string& text, std::string* error = nullptr);
    bool parseFile(const std::string& path, std::string* error = nullptr);

    const UiNode& root() const { return root_; }
    UiNode& root() { return root_; }

private:
    UiNode root_;
};

} // namespace modern_ui
