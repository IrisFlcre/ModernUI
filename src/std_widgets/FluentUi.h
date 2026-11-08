// ============================================================================
// FluentUi.h — 将全部 Fluent 控件注册到 UiLoader（.ui 标签工厂）
// ============================================================================
//
// .ui 通用属性：
//   id / class / style（内联 CSS）
//   布局：direction / wrap / justify / align / align-self / gap / padding /
//         margin / width / height / min-* / max-* / flex-grow / flex-shrink /
//         flex-basis / absolute / x / y / clip
//   交互：onClick / onToggled / onCheckedChanged / onValueChanged /
//         onSelectionChanged / onTextChanged / onSubmit / onExpandedChanged /
//         onDateChanged / onColorChanged / onTabChanged / onTabClosed /
//         onItemClicked / onClose / onPrimary / onSecondary / onClosed …
//         （属性值 = UiLoader::registerHandler 注册的名字）
//   控件：text / label / placeholder / value / low / high / checked / items
//         （'|' 分隔）/ variant（Button 外观）/ enabled / font-size /
//         selected-index / values（Sparkline 数值列表）…
//
// 示例：
//   <StackPanel direction="column" gap="12" padding="24">
//     <TextBlock class="title" text="Settings"/>
//     <Button class="primary" text="Apply" onClick="apply"/>
//     <CheckBox text="Enable animations" checked="true" onCheckedChanged="toggle"/>
//     <SegmentedControl items="Day|Week|Month" selected-index="1"/>
//     <RangeSlider low="0.2" high="0.8" onValueChanged="range"/>
//     <Sparkline values="12|18|15|24|31" width="240" height="64"/>
//   </StackPanel>
// ============================================================================

#pragma once

namespace modern_ui {

class UiLoader;

// 注册 Button / Selection / Input / Progress / Collections / Feedback /
// Media / Advanced / Typography 全部 Fluent 控件的 .ui 工厂。
void registerFluentWidgets(UiLoader& loader);

} // namespace modern_ui
