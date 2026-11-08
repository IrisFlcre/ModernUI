// ============================================================================
// StyleSheet.h — 每个 Widget 持有的样式表（CSS 样式 Entry + 自定义 Entry）
// ============================================================================
//
// 设计：
//   - Base Entry：一套固定的语义化 CSS 样式项（背景 / 前景 / 描边 / 圆角 /
//     字号 / 尺寸 …），按 CSS 属性名（kebab-case）寻址。
//   - others：Map<String, String> 形式的自定义 Entry，控件可读取任意扩展属性
//     （例如 badge-color / icon-size），CSS 中未识别的属性都会落入这里。
//   - 引用值：Entry 可以写成 var(--name)（变量，支持回退值）或 {entry-name}
//     （引用同一张样式表中的另一个 Entry）。读取时惰性解析。
//   - Origin：记录值来自默认样式 / 样式表 / 内联样式。控件派生主题时用它判断
//     "CSS 是否显式覆盖"（默认值不应压过 CSS 的通用属性）。
//
// 用法：
//   StyleSheet sheet = widget.defaultStyleSheet();
//   sheet.setColor(StyleSheet::Entry::Background, Color{32, 32, 32, 255});
//   sheet.setVar("--accent", "#60CDFF");
//   sheet.set(Entry::Foreground, "var(--accent)");
//   Color text = sheet.color(Entry::Foreground, Color::white());
// ============================================================================

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>

#include "Canvas.h"

namespace modern_ui {

// ============================================================================
// style 工具：颜色 / 数值 / 字符串处理（实现见 StyleSheet.cpp）
// ============================================================================
namespace style {

std::string trim(const std::string& text);
std::string toLower(std::string text);

bool parseColor(const std::string& text, Color& out);
bool parseNumber(const std::string& text, float& out);   // 支持 px / s / ms
std::string colorToCss(const Color& color);
std::string numberToCss(float value);

} // namespace style

// ============================================================================
// StyleSheet
// ============================================================================
class StyleSheet {
public:
    enum class Entry : uint8_t {
        // ---- 表面 ----
        Background = 0,
        BackgroundHover,
        BackgroundPressed,
        BackgroundDisabled,
        Card,
        CardAlt,
        Layer,
        LayerAlt,
        Overlay,
        Scrim,
        ControlFill,
        ControlFillHover,
        ControlFillPressed,
        ControlFillDisabled,
        SubtleHover,
        SubtlePressed,

        // ---- 描边 / 圆角 / 焦点 ----
        BorderColor,
        BorderWidth,
        Radius,
        RadiusCard,
        RadiusOverlay,
        Stroke,
        StrokeStrong,
        FocusStroke,
        Divider,

        // ---- 前景 ----
        Foreground,
        ForegroundSecondary,
        ForegroundTertiary,
        ForegroundDisabled,
        ForegroundOnAccent,
        TextPrimary,
        TextSecondary,
        TextTertiary,
        TextDisabled,
        TextOnAccent,

        // ---- 强调 / 状态色 ----
        Accent,
        AccentHover,
        AccentPressed,
        AccentDisabled,
        Info,
        Success,
        Caution,
        Critical,

        // ---- 尺寸 / 动效 ----
        ControlHeight,
        ListItemHeight,
        MinControlWidth,
        FontSize,
        CaptionFontSize,
        SubtitleFontSize,
        TitleFontSize,
        DisplayFontSize,
        FastDuration,
        NormalDuration,
        SlowDuration,

        // ---- 视觉 ----
        Opacity,             // 控件整体不透明度 [0, 1]（映射到合成 Group）

        Count
    };

    enum class Origin : uint8_t {
        Default = 0,     // 控件默认样式（来自主题）
        Stylesheet = 1,  // 外部 .css 规则
        Inline = 2       // style="..." 内联样式
    };

    struct Value {
        enum class Kind : uint8_t { None, Color, Number, Text };

        Kind    kind   = Kind::None;
        Origin  origin = Origin::Default;
        Color   color{};
        float   number = 0.0f;
        std::string text;

        bool empty() const { return kind == Kind::None; }
    };

    StyleSheet() = default;

    // ---- 整体操作 ----
    void clear();
    bool empty() const;
    // 用 overlay 覆盖本表：非空 Entry / 变量 / others 覆盖；保留 overlay 的 origin
    void merge(const StyleSheet& overlay);
    // 复制并把所有非空 Entry 的 origin 强制成指定值
    StyleSheet withOrigin(Origin origin) const;

    // ---- Base Entry ----
    void set(Entry entry, const Value& value);
    void setColor(Entry entry, Color color, Origin origin = Origin::Default);
    void setNumber(Entry entry, float number, Origin origin = Origin::Default);
    void setText(Entry entry, std::string text, Origin origin = Origin::Default);

    bool has(Entry entry) const { return !raw(entry).empty(); }
    const Value& raw(Entry entry) const;
    Origin origin(Entry entry) const { return raw(entry).origin; }

    // 惰性解析引用值（var() / {entry}）后再读取
    Value resolved(Entry entry) const;
    // 把任意 CSS 值文本（可含 var(--name) / {entry}）展开成本表可读的值
    Value resolvedText(const std::string& text,
                       Origin origin = Origin::Stylesheet) const;
    Color color(Entry entry, Color fallback = Color::nullColor()) const;
    float number(Entry entry, float fallback = 0.0f) const;
    int32_t integer(Entry entry, int32_t fallback = 0) const;

    // ---- 变量（CSS 自定义属性 --name）----
    void setVar(std::string name, std::string value);
    const std::string* findVar(const std::string& name) const;
    const std::map<std::string, std::string>& variables() const { return variables_; }

    // ---- 自定义 Entry（Map<String, String>）----
    std::map<std::string, std::string> others;

    void setCustom(std::string key, std::string value) { others[std::move(key)] = std::move(value); }
    std::string custom(const std::string& key, std::string fallback = {}) const;

    // ---- 名称映射 ----
    static bool entryFromName(const std::string& name, Entry& entry);
    static const char* entryName(Entry entry);
    static bool isNumericEntry(Entry entry);

private:
    Value resolveValue(const Value& value, int depth) const;

    static constexpr size_t kEntryCount = static_cast<size_t>(Entry::Count);
    std::array<Value, kEntryCount> values_{};
    std::map<std::string, std::string> variables_;
};

} // namespace modern_ui
