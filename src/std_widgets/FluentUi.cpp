// ============================================================================
// FluentUi.cpp — 全部 Fluent 控件的 .ui 工厂 / 属性 / 回调绑定
// ============================================================================

#include "FluentUi.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "FluentAdvanced.h"
#include "FluentButtons.h"
#include "FluentCollections.h"
#include "FluentFeedback.h"
#include "FluentInput.h"
#include "FluentMedia.h"
#include "FluentProgress.h"
#include "FluentSelection.h"
#include "FluentTypography.h"
#include "UiLoader.h"

namespace modern_ui {
namespace {

using namespace modern_ui::fluent;

std::string lower(std::string text) {
    return style::toLower(std::move(text));
}

std::string nodeText(const UiNode& node, const char* primary = "text") {
    if (const std::string* value = node.findAttr(primary)) return *value;
    if (const std::string* value = node.findAttr("label")) return *value;
    return node.text;
}

template <typename T>
T& as(Widget& widget) {
    return static_cast<T&>(widget);
}

void configureControl(FluentControl& control, const UiNode& node, UiLoader& loader,
                      bool bindClick = true) {
    control.setEnabled(UiLoader::boolValue(node, "enabled", true));
    if (bindClick) {
        if (auto handler = loader.handlerFor(node, "onClick")) control.onClick = handler;
    }
}

Button::Style parseButtonVariant(const std::string& value) {
    const std::string name = lower(value);
    if (name == "accent") return Button::Style::Accent;
    if (name == "subtle") return Button::Style::Subtle;
    if (name == "selected") return Button::Style::Selected;
    return Button::Style::Standard;
}

Color parseColorAttr(const UiNode& node, const std::string& name, Color fallback = Color::nullColor()) {
    const std::string* value = node.findAttr(name);
    if (!value) return fallback;
    Color color = fallback;
    return style::parseColor(*value, color) ? color : fallback;
}

std::vector<Color> parseColorList(const UiNode& node, const std::string& name) {
    std::vector<Color> colors;
    for (const std::string& item : UiLoader::listValue(node, name)) {
        Color color;
        if (style::parseColor(item, color)) colors.push_back(color);
    }
    return colors;
}

std::vector<float> parseFloatList(const UiNode& node, const std::string& name) {
    std::vector<float> numbers;
    for (const std::string& item : UiLoader::listValue(node, name)) {
        char* end = nullptr;
        const double value = std::strtod(item.c_str(), &end);
        if (end != item.c_str()) numbers.push_back(static_cast<float>(value));
    }
    return numbers;
}

FlexDirection parseDirectionAttr(const UiNode& node) {
    const std::string name = lower(node.attr("direction", "column"));
    if (name == "row") return FlexDirection::Row;
    if (name == "row-reverse") return FlexDirection::RowReverse;
    if (name == "column-reverse") return FlexDirection::ColumnReverse;
    return FlexDirection::Column;
}

// ---- 控件专属属性解析 ----
InfoBadge::Kind parseBadgeKind(const std::string& value) {
    const std::string name = lower(value);
    if (name == "informational") return InfoBadge::Kind::Informational;
    if (name == "success") return InfoBadge::Kind::Success;
    if (name == "critical") return InfoBadge::Kind::Critical;
    return InfoBadge::Kind::Attention;
}

InfoBar::Severity parseSeverity(const std::string& value) {
    const std::string name = lower(value);
    if (name == "success") return InfoBar::Severity::Success;
    if (name == "warning") return InfoBar::Severity::Warning;
    if (name == "error") return InfoBar::Severity::Error;
    return InfoBar::Severity::Informational;
}

PersonPicture::Status parseStatus(const std::string& value) {
    const std::string name = lower(value);
    if (name == "online") return PersonPicture::Status::Online;
    if (name == "away") return PersonPicture::Status::Away;
    if (name == "offline") return PersonPicture::Status::Offline;
    return PersonPicture::Status::None;
}

ImageView::Stretch parseStretch(const std::string& value) {
    const std::string name = lower(value);
    if (name == "fill") return ImageView::Stretch::Fill;
    if (name == "uniformtofill" || name == "uniform-to-fill") {
        return ImageView::Stretch::UniformToFill;
    }
    return ImageView::Stretch::Uniform;
}

TextBlock::Align parseTextAlign(const std::string& value) {
    const std::string name = lower(value);
    if (name == "center") return TextBlock::Align::Center;
    if (name == "right") return TextBlock::Align::Right;
    return TextBlock::Align::Left;
}

} // namespace

// ============================================================================
// 注册
// ============================================================================
void registerFluentWidgets(UiLoader& loader) {
    // ======================================================================
    // Typography
    // ======================================================================
    loader.registerFactory("TextBlock", UiLoader::Factory{
        [](const UiNode& node) {
            Color color = Color::nullColor();
            if (const std::string* value = node.findAttr("color")) {
                style::parseColor(*value, color);
            }
            return std::unique_ptr<Widget>(std::make_unique<TextBlock>(
                nodeText(node), UiLoader::intValue(node, "font-size", 14), color));
        },
        [](Widget& widget, const UiNode& node, UiLoader&) {
            auto& text = as<TextBlock>(widget);
            if (const std::string* value = node.findAttr("text-align")) {
                text.setAlign(parseTextAlign(*value));
            }
            if (const std::string* value = node.findAttr("text")) text.setText(*value);
            if (const std::string* value = node.findAttr("font-size")) {
                text.setFontSize(static_cast<int32_t>(std::strtol(value->c_str(), nullptr, 10)));
            }
        },
        {}
    });

    loader.registerFactory("Divider", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(
                std::make_unique<Divider>(UiLoader::boolValue(node, "vertical", false)));
        },
        {}, {}
    });

    // ======================================================================
    // Buttons
    // ======================================================================
    loader.registerFactory("Button", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<Button>(
                nodeText(node), parseButtonVariant(node.attr("variant", "standard")),
                UiLoader::intValue(node, "font-size", 14)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            configureControl(as<Button>(widget), node, loader);
        },
        {}
    });

    loader.registerFactory("ToggleButton", UiLoader::Factory{
        [](const UiNode& node) {
            auto button = std::make_unique<ToggleButton>(
                nodeText(node), parseButtonVariant(node.attr("variant", "standard")),
                UiLoader::intValue(node, "font-size", 14));
            button->setChecked(UiLoader::boolValue(node, "checked", false));
            return std::unique_ptr<Widget>(std::move(button));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& button = as<ToggleButton>(widget);
            configureControl(button, node, loader);
            if (auto handler = loader.handlerFor(node, "onToggled")) {
                button.onToggled = [handler](bool) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("RepeatButton", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<RepeatButton>(nodeText(node)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            configureControl(as<RepeatButton>(widget), node, loader);
        },
        {}
    });

    loader.registerFactory("HyperlinkButton", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<HyperlinkButton>(
                nodeText(node), UiLoader::intValue(node, "font-size", 14)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            configureControl(as<HyperlinkButton>(widget), node, loader);
        },
        {}
    });

    loader.registerFactory("DropDownButton", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(
                std::make_unique<DropDownButton>(nodeText(node, "text")));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& button = as<DropDownButton>(widget);
            configureControl(button, node, loader);
            if (auto handler = loader.handlerFor(node, "onDropDown")) {
                button.onDropDown = handler;
            }
        },
        {}
    });

    loader.registerFactory("SplitButton", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(
                std::make_unique<SplitButton>(nodeText(node, "text")));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& button = as<SplitButton>(widget);
            configureControl(button, node, loader);
            if (auto handler = loader.handlerFor(node, "onPrimary")) {
                button.onPrimaryClick = handler;
            }
            if (auto handler = loader.handlerFor(node, "onSecondary")) {
                button.onSecondaryClick = handler;
            }
        },
        {}
    });

    // ======================================================================
    // Selection
    // ======================================================================
    loader.registerFactory("CheckBox", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<CheckBox>(
                nodeText(node), UiLoader::boolValue(node, "checked", false)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& check = as<CheckBox>(widget);
            configureControl(check, node, loader);
            if (auto handler = loader.handlerFor(node, "onCheckedChanged")) {
                check.onCheckedChanged = [handler](bool) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("RadioButton", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<RadioButton>(
                nodeText(node), UiLoader::boolValue(node, "checked", false)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& radio = as<RadioButton>(widget);
            configureControl(radio, node, loader);
            radio.setGroup(node.attr("group", ""));
            if (auto handler = loader.handlerFor(node, "onCheckedChanged")) {
                radio.onCheckedChanged = [handler](bool) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("ToggleSwitch", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<ToggleSwitch>(
                nodeText(node), UiLoader::boolValue(node, "checked", false)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& toggle = as<ToggleSwitch>(widget);
            configureControl(toggle, node, loader);
            if (auto handler = loader.handlerFor(node, "onToggled")) {
                toggle.onToggled = [handler](bool) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("Slider", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<Slider>(
                UiLoader::floatValue(node, "value", 0.0f),
                UiLoader::floatValue(node, "min", 0.0f),
                UiLoader::floatValue(node, "max", 1.0f),
                UiLoader::floatValue(node, "step", 0.0f)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& slider = as<Slider>(widget);
            configureControl(slider, node, loader);
            if (auto handler = loader.handlerFor(node, "onValueChanged")) {
                slider.onValueChanged = [handler](float) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("ComboBox", UiLoader::Factory{
        [](const UiNode& node) {
            auto combo = std::make_unique<ComboBox>(UiLoader::listValue(node, "items"));
            combo->setSelectedIndex(UiLoader::intValue(node, "selected-index", -1));
            if (const std::string* placeholder = node.findAttr("placeholder")) {
                combo->setPlaceholder(*placeholder);
            }
            return std::unique_ptr<Widget>(std::move(combo));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& combo = as<ComboBox>(widget);
            configureControl(combo, node, loader);
            if (auto handler = loader.handlerFor(node, "onSelectionChanged")) {
                combo.onSelectionChanged = [handler](int32_t) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("RatingControl", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<RatingControl>(
                UiLoader::floatValue(node, "value", 0.0f),
                UiLoader::intValue(node, "max", 5)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& rating = as<RatingControl>(widget);
            configureControl(rating, node, loader);
            if (auto handler = loader.handlerFor(node, "onValueChanged")) {
                rating.onValueChanged = [handler](float) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("SegmentedControl", UiLoader::Factory{
        [](const UiNode& node) {
            auto segmented = std::make_unique<SegmentedControl>(
                UiLoader::listValue(node, "items"));
            segmented->setSelectedIndex(UiLoader::intValue(node, "selected-index", 0));
            return std::unique_ptr<Widget>(std::move(segmented));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& segmented = as<SegmentedControl>(widget);
            configureControl(segmented, node, loader);
            if (auto handler = loader.handlerFor(node, "onSelectionChanged")) {
                segmented.onSelectionChanged = [handler](int32_t) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("RangeSlider", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<RangeSlider>(
                UiLoader::floatValue(node, "low", 0.25f),
                UiLoader::floatValue(node, "high", 0.75f),
                UiLoader::floatValue(node, "min", 0.0f),
                UiLoader::floatValue(node, "max", 1.0f),
                UiLoader::floatValue(node, "step", 0.0f)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& range = as<RangeSlider>(widget);
            configureControl(range, node, loader);
            if (auto handler = loader.handlerFor(node, "onValueChanged")) {
                range.onRangeChanged = [handler](float, float) { handler(); };
            }
        },
        {}
    });

    // ======================================================================
    // Input
    // ======================================================================
    loader.registerFactory("TextBox", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<TextBox>(
                node.attr("placeholder", "Enter text"),
                UiLoader::intValue(node, "font-size", 14)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& box = as<TextBox>(widget);
            configureControl(box, node, loader);
            if (const std::string* text = node.findAttr("text")) box.setText(*text);
            if (auto handler = loader.handlerFor(node, "onTextChanged")) {
                box.onTextChanged = [handler](const std::string&) { handler(); };
            }
            if (auto handler = loader.handlerFor(node, "onSubmit")) {
                box.onSubmit = [handler](const std::string&) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("PasswordBox", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(
                std::make_unique<PasswordBox>(node.attr("placeholder", "Enter password")));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& box = as<PasswordBox>(widget);
            configureControl(box, node, loader);
        },
        {}
    });

    loader.registerFactory("NumberBox", UiLoader::Factory{
        [](const UiNode& node) {
            auto box = std::make_unique<NumberBox>(
                static_cast<double>(UiLoader::floatValue(node, "value", 0.0f)),
                static_cast<double>(UiLoader::floatValue(node, "min", -1.0e9f)),
                static_cast<double>(UiLoader::floatValue(node, "max", 1.0e9f)),
                static_cast<double>(UiLoader::floatValue(node, "step", 1.0f)));
            box->setDecimals(UiLoader::intValue(node, "decimals", 0));
            return std::unique_ptr<Widget>(std::move(box));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& box = as<NumberBox>(widget);
            configureControl(box, node, loader);
            if (auto handler = loader.handlerFor(node, "onValueChanged")) {
                box.onValueChanged = [handler](double) { handler(); };
            }
        },
        {}
    });

    // ======================================================================
    // Progress / Status
    // ======================================================================
    loader.registerFactory("ProgressBar", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<ProgressBar>(
                UiLoader::floatValue(node, "progress", 0.0f),
                UiLoader::boolValue(node, "indeterminate", false)));
        },
        {}, {}
    });

    loader.registerFactory("ProgressRing", UiLoader::Factory{
        [](const UiNode& node) {
            auto ring = std::make_unique<ProgressRing>(
                UiLoader::floatValue(node, "progress", 0.0f),
                UiLoader::boolValue(node, "indeterminate", false));
            ring->setStrokeWidth(UiLoader::intValue(node, "stroke-width", 3));
            return std::unique_ptr<Widget>(std::move(ring));
        },
        {}, {}
    });

    loader.registerFactory("RadialGauge", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<RadialGauge>(
                UiLoader::floatValue(node, "value", 0.0f), node.attr("label", "")));
        },
        [](Widget& widget, const UiNode& node, UiLoader&) {
            auto& gauge = as<RadialGauge>(widget);
            if (const std::string* value = node.findAttr("value")) {
                char* end = nullptr;
                const double parsed = std::strtod(value->c_str(), &end);
                if (end != value->c_str()) gauge.setValue(static_cast<float>(parsed));
            }
        },
        {}
    });

    loader.registerFactory("Skeleton", UiLoader::Factory{
        [](const UiNode& node) {
            auto skeleton = std::make_unique<Skeleton>(
                UiLoader::floatValue(node, "radius", 4.0f),
                UiLoader::boolValue(node, "circle", false));
            skeleton->setShimmer(UiLoader::boolValue(node, "shimmer", true));
            return std::unique_ptr<Widget>(std::move(skeleton));
        },
        {}, {}
    });

    loader.registerFactory("InfoBadge", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<InfoBadge>(
                UiLoader::intValue(node, "value", -1),
                parseBadgeKind(node.attr("kind", "attention"))));
        },
        {}, {}
    });

    loader.registerFactory("InfoBar", UiLoader::Factory{
        [](const UiNode& node) {
            auto bar = std::make_unique<InfoBar>(
                node.attr("title", ""), node.attr("message", ""),
                parseSeverity(node.attr("severity", "informational")));
            bar->setClosable(UiLoader::boolValue(node, "closable", true));
            return std::unique_ptr<Widget>(std::move(bar));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& bar = as<InfoBar>(widget);
            configureControl(bar, node, loader);
            if (auto handler = loader.handlerFor(node, "onClose")) bar.onClose = handler;
        },
        {}
    });

    // ======================================================================
    // Collections / Navigation
    // ======================================================================
    loader.registerFactory("StackPanel", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<StackPanel>(
                parseDirectionAttr(node),
                UiLoader::intValue(node, "gap", 0),
                UiLoader::intValue(node, "padding", 0)));
        },
        {}, {}
    });

    loader.registerFactory("Card", UiLoader::Factory{
        [](const UiNode& node) {
            if (node.hasAttr("fill") || node.hasAttr("stroke")) {
                auto card = std::make_unique<Card>(
                    parseColorAttr(node, "fill", Color::nullColor()),
                    parseColorAttr(node, "stroke", Color::nullColor()),
                    UiLoader::floatValue(node, "radius", -1.0f),
                    UiLoader::intValue(node, "padding", 16));
                return std::unique_ptr<Widget>(std::move(card));
            }
            return std::unique_ptr<Widget>(std::make_unique<Card>());
        },
        [](Widget& widget, const UiNode& node, UiLoader&) {
            auto& card = as<Card>(widget);
            if (node.hasAttr("fill")) card.setFill(parseColorAttr(node, "fill"));
            if (node.hasAttr("stroke")) card.setStroke(parseColorAttr(node, "stroke"));
            if (node.hasAttr("radius")) card.setRadius(UiLoader::floatValue(node, "radius", -1.0f));
            if (node.hasAttr("padding")) card.setPadding(UiLoader::intValue(node, "padding", 16));
        },
        {}
    });

    loader.registerFactory("AcrylicPanel", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<AcrylicPanel>(
                parseColorAttr(node, "tint", Color::nullColor()),
                UiLoader::floatValue(node, "radius", 8.0f)));
        },
        {}, {}
    });

    loader.registerFactory("Chip", UiLoader::Factory{
        [](const UiNode& node) {
            auto chip = std::make_unique<Chip>(
                nodeText(node), UiLoader::boolValue(node, "selected", false));
            chip->setClosable(UiLoader::boolValue(node, "closable", false));
            chip->setSelectable(UiLoader::boolValue(node, "selectable", true));
            return std::unique_ptr<Widget>(std::move(chip));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& chip = as<Chip>(widget);
            configureControl(chip, node, loader);
            if (auto handler = loader.handlerFor(node, "onSelectionChanged")) {
                chip.onSelectedChanged = [handler](bool) { handler(); };
            }
            if (auto handler = loader.handlerFor(node, "onClose")) chip.onClose = handler;
        },
        {}
    });

    loader.registerFactory("Expander", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<Expander>(
                node.attr("header", "Expander"),
                UiLoader::boolValue(node, "expanded", false)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& expander = as<Expander>(widget);
            configureControl(expander, node, loader);
            if (auto handler = loader.handlerFor(node, "onExpandedChanged")) {
                expander.onExpandedChanged = [handler](bool) { handler(); };
            }
        },
        [](Widget& widget, std::unique_ptr<Widget> child) {
            as<Expander>(widget).setContent(std::move(child));
        }
    });

    loader.registerFactory("ScrollViewer", UiLoader::Factory{
        [](const UiNode&) {
            return std::unique_ptr<Widget>(std::make_unique<ScrollViewer>());
        },
        {},
        [](Widget& widget, std::unique_ptr<Widget> child) {
            as<ScrollViewer>(widget).setContent(std::move(child));
        }
    });

    loader.registerFactory("ListView", UiLoader::Factory{
        [](const UiNode& node) {
            auto list = std::make_unique<ListView>();
            list->setItems(UiLoader::listValue(node, "items"));
            list->setSelectedIndex(UiLoader::intValue(node, "selected-index", -1));
            return std::unique_ptr<Widget>(std::move(list));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& list = as<ListView>(widget);
            configureControl(list, node, loader);
            if (auto handler = loader.handlerFor(node, "onSelectionChanged")) {
                list.onSelectionChanged = [handler](int32_t) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("GridView", UiLoader::Factory{
        [](const UiNode& node) {
            auto grid = std::make_unique<GridView>();
            grid->setItems(UiLoader::listValue(node, "items"));
            grid->setSelectedIndex(UiLoader::intValue(node, "selected-index", -1));
            return std::unique_ptr<Widget>(std::move(grid));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& grid = as<GridView>(widget);
            configureControl(grid, node, loader);
            if (auto handler = loader.handlerFor(node, "onSelectionChanged")) {
                grid.onSelectionChanged = [handler](int32_t) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("NavigationView", UiLoader::Factory{
        [](const UiNode& node) {
            auto navigation = std::make_unique<NavigationView>();
            navigation->setItems(UiLoader::listValue(node, "items"));
            navigation->setSelectedIndex(UiLoader::intValue(node, "selected-index", -1));
            navigation->setPaneTitle(node.attr("pane-title", "ModernUI"));
            navigation->setCompact(UiLoader::boolValue(node, "compact", false));
            return std::unique_ptr<Widget>(std::move(navigation));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& navigation = as<NavigationView>(widget);
            configureControl(navigation, node, loader);
            if (auto handler = loader.handlerFor(node, "onSelectionChanged")) {
                navigation.onSelectionChanged = [handler](int32_t) { handler(); };
            }
        },
        [](Widget& widget, std::unique_ptr<Widget> child) {
            as<NavigationView>(widget).setContent(std::move(child));
        }
    });

    loader.registerFactory("TabView", UiLoader::Factory{
        [](const UiNode& node) {
            auto tabs = std::make_unique<TabView>();
            tabs->setTabs(UiLoader::listValue(node, "tabs"));
            tabs->setSelectedIndex(UiLoader::intValue(node, "selected-index", 0));
            return std::unique_ptr<Widget>(std::move(tabs));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& tabs = as<TabView>(widget);
            configureControl(tabs, node, loader);
            if (auto handler = loader.handlerFor(node, "onTabChanged")) {
                tabs.onTabChanged = [handler](int32_t) { handler(); };
            }
            if (auto handler = loader.handlerFor(node, "onTabClosed")) {
                tabs.onTabClosed = [handler](int32_t) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("BreadcrumbBar", UiLoader::Factory{
        [](const UiNode& node) {
            auto bar = std::make_unique<BreadcrumbBar>();
            bar->setItems(UiLoader::listValue(node, "items"));
            bar->setSelectedIndex(UiLoader::intValue(node, "selected-index", 0));
            return std::unique_ptr<Widget>(std::move(bar));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& bar = as<BreadcrumbBar>(widget);
            configureControl(bar, node, loader);
            if (auto handler = loader.handlerFor(node, "onItemClicked")) {
                bar.onItemClicked = [handler](int32_t) { handler(); };
            }
        },
        {}
    });

    // ======================================================================
    // Feedback / Menus
    // ======================================================================
    loader.registerFactory("TeachingTip", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<TeachingTip>(
                node.attr("title", "Tip"), node.attr("message", ""),
                UiLoader::boolValue(node, "closable", true)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& tip = as<TeachingTip>(widget);
            configureControl(tip, node, loader);
            if (auto handler = loader.handlerFor(node, "onClose")) tip.onClose = handler;
        },
        {}
    });

    loader.registerFactory("ToolTip", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(
                std::make_unique<ToolTip>(nodeText(node, "text")));
        },
        {}, {}
    });

    loader.registerFactory("ContentDialog", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<ContentDialog>(
                node.attr("title", ""), node.attr("message", ""),
                node.attr("primary-text", "OK"), node.attr("secondary-text", "")));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& dialog = as<ContentDialog>(widget);
            configureControl(dialog, node, loader);
            if (auto handler = loader.handlerFor(node, "onPrimary")) dialog.onPrimary = handler;
            if (auto handler = loader.handlerFor(node, "onSecondary")) {
                dialog.onSecondary = handler;
            }
            if (auto handler = loader.handlerFor(node, "onClosed")) dialog.onClosed = handler;
        },
        {}
    });

    loader.registerFactory("MenuFlyout", UiLoader::Factory{
        [](const UiNode& node) {
            auto menu = std::make_unique<MenuFlyout>();
            for (const std::string& item : UiLoader::listValue(node, "items")) {
                menu->addItem(item);
            }
            return std::unique_ptr<Widget>(std::move(menu));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& menu = as<MenuFlyout>(widget);
            configureControl(menu, node, loader);
            if (auto handler = loader.handlerFor(node, "onDismiss")) menu.onDismiss = handler;
        },
        {}
    });

    loader.registerFactory("MenuBar", UiLoader::Factory{
        [](const UiNode& node) {
            auto bar = std::make_unique<MenuBar>();
            bar->setItems(UiLoader::listValue(node, "items"));
            return std::unique_ptr<Widget>(std::move(bar));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& bar = as<MenuBar>(widget);
            configureControl(bar, node, loader);
            if (auto handler = loader.handlerFor(node, "onItemClicked")) {
                bar.onItemClicked = [handler](int32_t) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("CommandBar", UiLoader::Factory{
        [](const UiNode&) {
            return std::unique_ptr<Widget>(std::make_unique<CommandBar>());
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& bar = as<CommandBar>(widget);
            configureControl(bar, node, loader);
            auto handler = loader.handlerFor(node, "onCommand");
            for (const std::string& label : UiLoader::listValue(node, "commands")) {
                bar.addCommand(label, handler);
            }
        },
        {}
    });

    // ======================================================================
    // Media
    // ======================================================================
    loader.registerFactory("ImageView", UiLoader::Factory{
        [](const UiNode& node) {
            auto image = std::make_unique<ImageView>();
            image->setStretch(parseStretch(node.attr("stretch", "uniform")));
            return std::unique_ptr<Widget>(std::move(image));
        },
        {}, {}
    });

    loader.registerFactory("PersonPicture", UiLoader::Factory{
        [](const UiNode& node) {
            auto person = std::make_unique<PersonPicture>(
                node.attr("display-name", ""), UiLoader::intValue(node, "size", 48));
            person->setStatus(parseStatus(node.attr("status", "none")));
            return std::unique_ptr<Widget>(std::move(person));
        },
        {}, {}
    });

    loader.registerFactory("AnimatedImage", UiLoader::Factory{
        [](const UiNode& node) {
            auto image = std::make_unique<AnimatedImage>();
            image->setScale(UiLoader::floatValue(node, "scale", 1.0f));
            return std::unique_ptr<Widget>(std::move(image));
        },
        {}, {}
    });

    // ======================================================================
    // Advanced
    // ======================================================================
    loader.registerFactory("TreeView", UiLoader::Factory{
        [](const UiNode& node) {
            std::vector<TreeViewNode> nodes;
            for (const std::string& label : UiLoader::listValue(node, "items")) {
                nodes.push_back(TreeViewNode{label, 0, false, false});
            }
            auto tree = std::make_unique<TreeView>();
            tree->setNodes(std::move(nodes));
            tree->setSelectedIndex(UiLoader::intValue(node, "selected-index", -1));
            return std::unique_ptr<Widget>(std::move(tree));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& tree = as<TreeView>(widget);
            configureControl(tree, node, loader);
            if (auto handler = loader.handlerFor(node, "onSelectionChanged")) {
                tree.onSelectionChanged = [handler](int32_t) { handler(); };
            }
            if (auto handler = loader.handlerFor(node, "onExpandedChanged")) {
                tree.onExpandedChanged = [handler](int32_t, bool) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("CalendarView", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<CalendarView>(
                UiLoader::intValue(node, "year", 2026),
                UiLoader::intValue(node, "month", 10),
                UiLoader::intValue(node, "day", 26)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& calendar = as<CalendarView>(widget);
            configureControl(calendar, node, loader);
            if (auto handler = loader.handlerFor(node, "onDateChanged")) {
                calendar.onDateChanged = [handler](int32_t, int32_t, int32_t) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("ColorPicker", UiLoader::Factory{
        [](const UiNode& node) {
            auto picker = std::make_unique<ColorPicker>();
            std::vector<Color> colors = parseColorList(node, "colors");
            if (!colors.empty()) picker->setColors(std::move(colors));
            picker->setSelectedIndex(UiLoader::intValue(node, "selected-index", 0));
            return std::unique_ptr<Widget>(std::move(picker));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& picker = as<ColorPicker>(widget);
            configureControl(picker, node, loader);
            if (auto handler = loader.handlerFor(node, "onColorChanged")) {
                picker.onColorChanged = [handler](Color) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("PipsPager", UiLoader::Factory{
        [](const UiNode& node) {
            return std::unique_ptr<Widget>(std::make_unique<PipsPager>(
                UiLoader::intValue(node, "pages", 5),
                UiLoader::intValue(node, "selected-page", 0)));
        },
        [](Widget& widget, const UiNode& node, UiLoader& loader) {
            auto& pager = as<PipsPager>(widget);
            configureControl(pager, node, loader);
            if (auto handler = loader.handlerFor(node, "onSelectedPageChanged")) {
                pager.onSelectedPageChanged = [handler](int32_t) { handler(); };
            }
        },
        {}
    });

    loader.registerFactory("Sparkline", UiLoader::Factory{
        [](const UiNode& node) {
            auto chart = std::make_unique<Sparkline>(parseFloatList(node, "values"));
            chart->setLineWidth(UiLoader::intValue(node, "line-width", 2));
            chart->setShowArea(UiLoader::boolValue(node, "show-area", true));
            chart->setShowDot(UiLoader::boolValue(node, "show-dot", true));
            return std::unique_ptr<Widget>(std::move(chart));
        },
        {}, {}
    });
}

} // namespace modern_ui
