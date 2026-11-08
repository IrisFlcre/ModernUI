// ============================================================================
// ModernUI × Blend2D — 控件层 Demo
// ----------------------------------------------------------------------------
// 演示内容：
//   - 每个控件独占一个子 Canvas，控件内几何体全部是相对坐标
//   - View 负责 Flexbox 排版（direction / grow / shrink / justify / align /
//     gap / wrap / absolute / padding）
//   - View 只合成"Canvas::isDirty()"的控件，不做逐帧逐控件比较
//   - 事件：hover / 点击 / 按压捕获（Slider 拖出控件仍可拖动）/ Tab 切换焦点
// ============================================================================

#include "App.h"
#include "Blend2DRenderer.h"
#include "Event.h"
#include "View.h"
#include "Win32LayeredWindow.h"

#include "controls/BarChart.h"
#include "controls/Button.h"
#include "controls/Label.h"
#include "controls/Panel.h"
#include "controls/ProgressBar.h"
#include "controls/Slider.h"
#include "controls/Toggle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace modern_ui;
using namespace demo;

namespace {

constexpr int32_t kWindowWidth  = 1020;
constexpr int32_t kWindowHeight = 660;
constexpr uint64_t kAnimateIntervalMs = 60;

const Color kTextDim{126, 138, 170, 255};
const Color kTextMain{212, 220, 242, 255};
const Color kCardFill{26, 29, 40, 255};
const Color kCardStroke{58, 66, 94, 150};
const Color kInnerFill{20, 22, 31, 255};

// ---------------------------------------------------------------------------
// 根视图：View 本身也是控件 —— 它的子 Canvas 就是最终交给渲染后端的 Canvas
// ---------------------------------------------------------------------------
class DemoView : public View {
public:
    DemoView(int32_t width, int32_t height) : View(width, height) {
        Shape card;
        card.base.fill = Color{22, 24, 33, 255};
        card.base.stroke = Color{52, 58, 82, 170};
        card.base.strokeWidth = 1;
        card.data = RRectShape{0, 0, 1, 1, 20.0f, 20.0f};
        cardId_ = canvas().addShape(std::move(card));

        canvas().addShape(makeText("ModernUI — 控件层 / Widget Layer", 30, 52, 22, Color{238, 243, 255, 255}));
        canvas().addShape(makeText("每个控件独占子 Canvas · Flexbox 排版 · View 只镜像脏控件", 30, 76, 13, kTextDim));
    }

    void onLayout(const Rect& frame) override {
        // 背景卡片跟随窗口尺寸（依然是本控件子 Canvas 内的相对坐标）
        canvas().updateShape(cardId_, [&frame](Shape& shape) {
            shape.data = RRectShape{12, 12, frame.w - 24, frame.h - 24, 20.0f, 20.0f};
        });
    }

private:
    static Shape makeText(std::string text, int32_t x, int32_t baselineY, int32_t fontSize, Color color) {
        Shape shape;
        shape.base.fill = color;
        TextShape textShape;
        textShape.text = std::move(text);
        textShape.fontSize = fontSize;
        textShape.x = x;
        textShape.y = baselineY;
        shape.data = std::move(textShape);
        return shape;
    }

    uint32_t cardId_ = 0;
};

// ---------------------------------------------------------------------------
// 控件树：全部位置 / 尺寸由 Flexbox 决定，控件只提供内容尺寸与相对几何体
// ---------------------------------------------------------------------------
struct UiState {
    int32_t clicks = 0;
    Label* clickLabel = nullptr;
    Label* valueLabel = nullptr;
    Toggle* animate = nullptr;
    BarChart* chart = nullptr;
};

void buildUI(DemoView& view, UiState& state) {
    // 根容器：纵向排列，顶部留给标题栏
    view.style().direction = FlexDirection::Column;
    view.style().gap = 14;
    view.style().padding = Edges{92, 26, 20, 26};
    view.style().alignItems = AlignItems::Stretch;

    // ======================= 主体两栏 =======================
    Panel* body = view.emplaceChild<Panel>();          // 透明布局容器
    body->style().direction = FlexDirection::Row;
    body->style().gap = 14;
    body->style().flexGrow = 1;
    body->style().alignItems = AlignItems::Stretch;

    Panel* left = body->emplaceChild<Panel>(kCardFill, kCardStroke, 16.0f);
    left->style().direction = FlexDirection::Column;
    left->style().gap = 12;
    left->style().padding = Edges{18};
    left->style().flexGrow = 3;

    Panel* right = body->emplaceChild<Panel>(Color{24, 26, 36, 255}, kCardStroke, 16.0f);
    right->style().direction = FlexDirection::Column;
    right->style().gap = 10;
    right->style().padding = Edges{18};
    right->style().flexGrow = 2;

    // ----------------------- 左栏：交互控件 -----------------------
    left->emplaceChild<Label>("BUTTONS — hover / press / click", 11, kTextDim);

    Panel* buttonRow = left->emplaceChild<Panel>();
    buttonRow->style().direction = FlexDirection::Row;
    buttonRow->style().gap = 10;
    buttonRow->style().alignItems = AlignItems::Center;

    Button* primary = buttonRow->emplaceChild<Button>("Primary");
    Button* ghost = buttonRow->emplaceChild<Button>("Ghost");
    ghost->setColors(Color{52, 58, 80, 255}, Color{72, 80, 110, 255}, Color{38, 44, 62, 255});

    Panel* buttonSpacer = buttonRow->emplaceChild<Panel>();
    buttonSpacer->style().flexGrow = 1;                // 撑开剩余宽度

    state.clickLabel = buttonRow->emplaceChild<Label>("Clicks: 0", 13, kTextDim);
    primary->onClick = [&state]() {
        ++state.clicks;
        state.clickLabel->setText("Clicks: " + std::to_string(state.clicks));
    };
    ghost->onClick = [&state]() {
        state.clicks = 0;
        state.clickLabel->setText("Clicks: 0");
    };

    Panel* toggleRow = left->emplaceChild<Panel>();
    toggleRow->style().direction = FlexDirection::Row;
    toggleRow->style().gap = 10;
    toggleRow->style().alignItems = AlignItems::Center;
    state.animate = toggleRow->emplaceChild<Toggle>();
    toggleRow->emplaceChild<Label>("Animate chart", 13, kTextMain);
    Panel* toggleSpacer = toggleRow->emplaceChild<Panel>();
    toggleSpacer->style().flexGrow = 1;
    toggleRow->emplaceChild<Label>("Toggle 状态驱动几何体", 11, kTextDim);

    left->emplaceChild<Label>("SLIDER → PROGRESS BAR — 拖出控件松手请观察按压捕获", 11, kTextDim);

    Panel* sliderRow = left->emplaceChild<Panel>();
    sliderRow->style().direction = FlexDirection::Row;
    sliderRow->style().gap = 14;
    sliderRow->style().alignItems = AlignItems::Center;

    Slider* slider = sliderRow->emplaceChild<Slider>(0.42f);
    slider->style().flexGrow = 1;
    state.valueLabel = sliderRow->emplaceChild<Label>("42%", 13, kTextMain);
    state.valueLabel->style().minWidth = 46;           // 位数变化时布局不跳动

    ProgressBar* progress = left->emplaceChild<ProgressBar>(0.42f);
    slider->onValueChanged = [&state, progress](float value) {
        state.valueLabel->setText(std::to_string(static_cast<int>(std::lround(value * 100.0f))) + "%");
        progress->setValue(value);
    };

    left->emplaceChild<Label>("SUB-CANVAS — 图表在自身子 Canvas 内按 frame 重排相对坐标", 11, kTextDim);

    state.chart = left->emplaceChild<BarChart>(std::vector<float>{0.35f, 0.62f, 0.48f, 0.86f, 0.54f, 0.72f, 0.4f, 0.66f, 0.5f, 0.58f, 0.44f});
    state.chart->style().flexGrow = 1;
    state.chart->style().minHeight = 84;

    // ----------------------- 右栏：Flexbox 能力 -----------------------
    right->emplaceChild<Label>("FLEXBOX", 11, kTextDim);

    Panel* alignCard = right->emplaceChild<Panel>(kInnerFill, kCardStroke, 12.0f);
    alignCard->style().direction = FlexDirection::Column;
    alignCard->style().justify = JustifyContent::SpaceBetween;
    alignCard->style().flexGrow = 1;
    alignCard->style().padding = Edges{14};
    alignCard->emplaceChild<Label>("justify-content: space-between", 12, kTextMain);
    alignCard->emplaceChild<Label>("align-self: center", 12, Color{150, 196, 255, 255})
        ->style().alignSelf = AlignSelf::Center;
    alignCard->emplaceChild<Label>("align-self: flex-end", 12, Color{255, 196, 140, 255})
        ->style().alignSelf = AlignSelf::FlexEnd;

    Panel* wrapCard = right->emplaceChild<Panel>(kInnerFill, kCardStroke, 12.0f);
    wrapCard->style().direction = FlexDirection::Row;
    wrapCard->style().wrap = FlexWrap::Wrap;
    wrapCard->style().gap = 8;
    wrapCard->style().padding = Edges{12};
    wrapCard->style().height = Dimension::px(104);      // 固定高度 + wrap，超出自动换行
    for (int i = 0; i < 8; ++i) {
        Panel* chip = wrapCard->emplaceChild<Panel>(
            Color{static_cast<uint8_t>(48 + i * 10), 92, static_cast<uint8_t>(148 + i * 8), 255},
            Color::nullColor(), 8.0f);
        chip->style().width = Dimension::px(58);
        chip->style().height = Dimension::px(30);
    }

    Panel* absoluteCard = right->emplaceChild<Panel>(kInnerFill, kCardStroke, 12.0f);
    absoluteCard->style().height = Dimension::px(92);
    absoluteCard->style().padding = Edges{12};
    absoluteCard->emplaceChild<Label>("position: relative（常规流）", 12, kTextMain);
    Label* absoluteLabel = absoluteCard->emplaceChild<Label>("position: absolute  x=120 y=52", 12, Color{140, 240, 214, 255});
    absoluteLabel->style().position = Position::Absolute;
    absoluteLabel->style().x = 120;
    absoluteLabel->style().y = 52;

    // ======================= 底部状态行 =======================
    view.emplaceChild<Label>(
        "Slider 拖动时 View 保持按压捕获 · Tab 切换焦点 · Esc 退出 · 窗口缩放后 Flexbox 重新排版",
        12, kTextDim);
}

} // namespace

int main() {
    UiState state;
    uint64_t lastAnimateMs = 0;

    // 顶层 API：窗口 / 渲染后端 / 文本度量 / 消息循环全部由 App 封装。
    // 需要自定义根 View（在根 Canvas 里画背景）时用 viewFactory 注入。
    App app(L"ModernUI × Blend2D — Widget Layer", kWindowWidth, kWindowHeight);
    app.background(Color{14, 15, 22, 240})
       .viewFactory([](int32_t width, int32_t height) {
           return std::unique_ptr<View>(new DemoView(width, height));
       })
       .content([&state](View& view) {
           buildUI(static_cast<DemoView&>(view), state);
       })
       .onTick([&state, &lastAnimateMs](View&, float) {
           // 动画：只改图表控件的子 Canvas → View 只重新镜像这一个控件
           const uint64_t now = GetTickCount64();
           if (!state.animate || !state.animate->isOn() ||
               now - lastAnimateMs < kAnimateIntervalMs) {
               return;
           }
           lastAnimateMs = now;
           std::vector<float> values(state.chart->values().size());
           for (size_t i = 0; i < values.size(); ++i) {
               const float phase = static_cast<float>(now) * 0.0016f + static_cast<float>(i) * 0.62f;
               values[i] = 0.44f + 0.42f * std::sin(phase);
           }
           state.chart->setValues(std::move(values));
       });

    return app.run();
}
