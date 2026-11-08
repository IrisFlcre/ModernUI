// ============================================================================
// StyleEngine.cpp — 选择器匹配 / specificity 级联 / 声明应用
// ============================================================================

#include "StyleEngine.h"

#include <algorithm>
#include <vector>

#include "Widget.h"

namespace modern_ui {

bool StyleEngine::loadText(const std::string& css, std::string* error) {
    return document_.parse(css, error);
}

bool StyleEngine::loadFile(const std::string& path, std::string* error) {
    return document_.parseFile(path, error);
}

void StyleEngine::clear() {
    document_.clear();
}

void StyleEngine::applyTo(Widget& widget, StyleSheet& sheet) const {
    if (document_.empty()) return;

    // 全局变量（:root）
    for (const auto& variable : document_.variables()) {
        sheet.setVar(variable.first, variable.second);
    }

    struct Candidate {
        const CssDeclaration* declaration = nullptr;
        const CssSelector*    selector = nullptr;
        int                   specificity = 0;
        int                   order = 0;
    };

    std::vector<Candidate> candidates;
    const std::string typeName = widget.typeName();
    const std::string& id = widget.id();
    const std::vector<std::string>& classes = widget.classes();

    for (const CssRule& rule : document_.rules()) {
        for (const CssSelector& selector : rule.selectors) {
            if (!selector.matches(typeName, id, classes)) continue;

            for (const CssDeclaration& declaration : rule.declarations) {
                Candidate candidate;
                candidate.declaration = &declaration;
                candidate.selector = &selector;
                candidate.specificity = selector.specificity();
                candidate.order = rule.order;
                candidates.push_back(candidate);
            }
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) {
                         if (a.specificity != b.specificity) return a.specificity < b.specificity;
                         return a.order < b.order;
                     });

    for (const Candidate& candidate : candidates) {
        applyDeclaration(sheet, candidate.declaration->property,
                         candidate.declaration->value, StyleSheet::Origin::Stylesheet,
                         candidate.selector->state);
    }
}

} // namespace modern_ui
