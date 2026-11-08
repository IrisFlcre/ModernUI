// ============================================================================
// ModernUI — style_demo
// ----------------------------------------------------------------------------
// 演示"控件由 C++ 定义，样式由 .css 覆盖，布局由 .ui 管理并回调 C++"：
//   1. View 挂载 StyleEngine，读取 demo.css
//   2. UiLoader 读取 demo.ui，按标签创建 Fluent 控件、套用布局属性
//   3. .ui 中的 onClick / onValueChanged … 绑定到 registerHandler 的 C++ 函数
//
// 运行：
//   modernui_style_demo            打开窗口
//   modernui_style_demo --dump     无窗口校验解析 / 级联结果（返回非 0 表示失败）
//   --css <path> --ui <path>       指定资源路径
// ============================================================================

#include "Blend2DRenderer.h"
#include "Event.h"
#include "Fluent.h"
#include "FluentUi.h"
#include "UiLoader.h"
#include "View.h"
#include "Win32LayeredWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace modern_ui;
using namespace modern_ui::fluent;

namespace {

constexpr int32_t kWindowWidth = 1180;
constexpr int32_t kWindowHeight = 760;

bool fileExists(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return static_cast<bool>(file);
}

std::string resolveAsset(const std::string& fileName) {
    const std::vector<std::string> roots = {
        "assets/",
        "../assets/",
        "../../assets/",
        "examples/style_demo/assets/",
        "../examples/style_demo/assets/",
        "../../examples/style_demo/assets/"
    };
    for (const std::string& root : roots) {
        const std::string path = root + fileName;
        if (fileExists(path)) return path;
    }
    return {};
}

Widget* findById(Widget& node, const std::string& id) {
    if (node.id() == id) return &node;
    for (const auto& child : node.children()) {
        if (Widget* found = findById(*child, id)) return found;
    }
    return nullptr;
}

void collectByClass(Widget& node, const std::string& className, std::vector<Widget*>& out) {
    if (node.hasClass(className)) out.push_back(&node);
    for (const auto& child : node.children()) collectByClass(*child, className, out);
}

Widget* findButtonByLabel(Widget& node, const std::string& label) {
    if (node.hasClass("calendar-day")) {
        if (auto* button = dynamic_cast<Button*>(&node)) {
            if (button->label() == label) return &node;
        }
    }
    for (const auto& child : node.children()) {
        if (Widget* found = findButtonByLabel(*child, label)) return found;
    }
    return nullptr;
}

void clickWidget(View& view, const Widget& widget, int32_t localX, int32_t localY) {
    const Rect frame = widget.absoluteFrame();
    Event event;
    event.type = EventType::MouseDown;
    event.button = MouseButtonLeft;
    event.x = frame.x + localX;
    event.y = frame.y + localY;
    view.handleEvent(event);

    event.type = EventType::MouseUp;
    view.handleEvent(event);
}

size_t countWidgets(const Widget& node) {
    size_t count = 1;
    for (const auto& child : node.children()) count += countWidgets(*child);
    return count;
}

bool isExtendedControl(const Widget& node) {
    return dynamic_cast<const SegmentedControl*>(&node) != nullptr ||
           dynamic_cast<const RangeSlider*>(&node) != nullptr ||
           dynamic_cast<const Chip*>(&node) != nullptr ||
           dynamic_cast<const RadialGauge*>(&node) != nullptr ||
           dynamic_cast<const Sparkline*>(&node) != nullptr ||
           dynamic_cast<const Skeleton*>(&node) != nullptr;
}

size_t countExtendedControls(const Widget& node) {
    size_t count = isExtendedControl(node) ? 1 : 0;
    for (const auto& child : node.children()) count += countExtendedControls(*child);
    return count;
}

std::string colorText(const Color& color) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x",
                  color.r, color.g, color.b, color.a);
    return buffer;
}

bool sameColor(const Color& a, const Color& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// 构建 UI 并注册命名回调
std::string buildDemo(View& view, UiLoader& loader, TextBlock*& statusText,
                      const std::string& cssPath, const std::string& uiPath) {
    registerFluentWidgets(loader);

    statusText = nullptr;
    auto setStatus = [&statusText](std::string message) {
        if (statusText) statusText->setText(std::move(message));
    };

    loader.registerHandler("applyTheme",
        [&setStatus]() { setStatus("Apply theme → C++ handler"); });
    loader.registerHandler("primaryAction",
        [&setStatus]() { setStatus("Primary action → C++ handler"); });
    loader.registerHandler("standardAction",
        [&setStatus]() { setStatus("Standard action → C++ handler"); });
    loader.registerHandler("subtleAction",
        [&setStatus]() { setStatus("Subtle action → C++ handler"); });
    loader.registerHandler("toggleAction",
        [&setStatus]() { setStatus("Toggle changed → C++ handler"); });
    loader.registerHandler("sliderAction",
        [&setStatus]() { setStatus("Slider value changed → C++ handler"); });
    loader.registerHandler("textAction",
        [&setStatus]() { setStatus("TextBox text changed → C++ handler"); });
    loader.registerHandler("comboAction",
        [&setStatus]() { setStatus("ComboBox selection changed → C++ handler"); });
    loader.registerHandler("pagerAction",
        [&setStatus]() { setStatus("PipsPager page changed → C++ handler"); });
    loader.registerHandler("listAction",
        [&setStatus]() { setStatus("ListView selection changed → C++ handler"); });
    loader.registerHandler("tabAction",
        [&setStatus]() { setStatus("TabView tab changed → C++ handler"); });
    loader.registerHandler("calendarAction",
        [&setStatus]() { setStatus("CalendarView date changed → C++ handler"); });
    loader.registerHandler("treeAction",
        [&setStatus]() { setStatus("TreeView node selected → C++ handler"); });
    loader.registerHandler("segmentAction",
        [&setStatus]() { setStatus("SegmentedControl changed → C++ handler"); });
    loader.registerHandler("rangeAction",
        [&setStatus]() { setStatus("RangeSlider changed → C++ handler"); });
    loader.registerHandler("chipAction",
        [&setStatus]() { setStatus("Chip selection changed → C++ handler"); });
    loader.registerHandler("chipCloseAction",
        [&setStatus]() { setStatus("Chip close → C++ handler"); });

    std::string error;
    if (!view.loadStyleSheetFile(cssPath, &error)) return error;
    if (!loader.loadFile(uiPath, view, &error)) return error;

    // TreeView 的层级数据由 C++ 注入；行控件与展开/选中逻辑仍在控件内部
    if (auto* tree = dynamic_cast<TreeView*>(findById(view, "demoTree"))) {
        tree->setNodes({
            {"Documents", 0, true, true},
            {"Projects", 1, true, true},
            {"ModernUI", 2, false, false},
            {"Fluent Widgets", 2, false, false},
            {"Music", 0, true, false},
            {"Playlists", 1, false, false}
        });
        tree->setSelectedIndex(2);
    }

    view.layoutNow();
    statusText = dynamic_cast<TextBlock*>(findById(view, "statusText"));
    return {};
}

int runDump(const std::string& cssPath, const std::string& uiPath) {
    View view(kWindowWidth, kWindowHeight);
    UiLoader loader;
    TextBlock* statusText = nullptr;
    const std::string error = buildDemo(view, loader, statusText, cssPath, uiPath);
    if (!error.empty()) {
        std::fprintf(stderr, "[style_demo] %s\n", error.c_str());
        return 1;
    }

    std::printf("[style_demo] css rules: %zu, widgets: %zu\n",
                view.styleEngine().document().rules().size(), countWidgets(view));

    int failures = 0;
    auto check = [&failures](const char* label, bool ok, const std::string& actual) {
        std::printf("  %-28s %s (%s)\n", label, ok ? "OK" : "FAIL", actual.c_str());
        if (!ok) ++failures;
    };

    std::vector<Widget*> rootCards;
    collectByClass(view, "root", rootCards);
    if (!rootCards.empty()) {
        const Color bg = rootCards.front()->styleSheet().color(
            StyleSheet::Entry::Background, Color{0, 0, 0, 0});
        check("Card.root background", sameColor(bg, Color{16, 20, 27, 255}), colorText(bg));
    } else {
        check("find Card.root", false, "missing");
    }

    if (Widget* demoCalendar = findById(view, "demoCalendar")) {
        std::printf("[style_demo] composed calendar at y=%d\n",
                    demoCalendar->absoluteFrame().y);
    }
    if (!view.children().empty()) {
        if (auto* scroll = dynamic_cast<ScrollViewer*>(view.children().front().get())) {
            const Widget* content = scroll->content();
            std::printf("[style_demo] scroll frame=%d content=%d viewport=%d offset=%.1f\n",
                        scroll->frame().h, content ? content->frame().h : -1,
                        scroll->height(), scroll->scrollOffset());
        }
    }

    Widget* apply = findById(view, "applyBtn");
    if (apply) {
        const Color bg = apply->styleSheet().color(StyleSheet::Entry::Background, Color{0, 0, 0, 0});
        const int32_t height = apply->styleSheet().integer(StyleSheet::Entry::ControlHeight, -1);
        const int32_t radius = apply->styleSheet().integer(StyleSheet::Entry::Radius, -1);
        check("Button.primary background", sameColor(bg, Color{110, 231, 183, 255}), colorText(bg));
        check("Button control-height", height == 36, std::to_string(height));
        check("Button border-radius", radius == 8, std::to_string(radius));
        check("Button hover background",
              sameColor(apply->styleSheet().color(StyleSheet::Entry::BackgroundHover, Color{0, 0, 0, 0}),
                        Color{157, 245, 208, 255}),
              colorText(apply->styleSheet().color(StyleSheet::Entry::BackgroundHover, Color{0, 0, 0, 0})));
    } else {
        check("find #applyBtn", false, "missing");
    }

    Widget* status = findById(view, "statusText");
    if (status) {
        const Color fg = status->styleSheet().color(StyleSheet::Entry::Foreground, Color{0, 0, 0, 0});
        const int32_t font = status->styleSheet().integer(StyleSheet::Entry::FontSize, -1);
        check("#statusText color", sameColor(fg, Color{110, 231, 183, 255}), colorText(fg));
        check("#statusText font-size", font == 15, std::to_string(font));
    } else {
        check("find #statusText", false, "missing");
    }

    // ---- CSS 布局 / opacity 级联 ----
    if (Widget* layoutCard = findById(view, "cssLayoutCard")) {
        const Edges& padding = layoutCard->style().padding;
        check("CSS padding", padding.left == 20 && padding.top == 20 && padding.right == 20 &&
                             padding.bottom == 20,
              std::to_string(padding.left) + "/" + std::to_string(padding.top));
    } else {
        check("find #cssLayoutCard", false, "missing");
    }

    if (Widget* subdued = findById(view, "subduedText")) {
        const float opacity = subdued->cssOpacity();
        check("CSS opacity", std::fabs(opacity - 0.55f) < 0.001f, std::to_string(opacity));
    } else {
        check("find #subduedText", false, "missing");
    }

    {
        std::vector<Widget*> rows;
        collectByClass(view, "css-row", rows);
        check("CSS row gap", !rows.empty() && rows.front()->style().gap == 16,
              rows.empty() ? "none" : std::to_string(rows.front()->style().gap));
    }

    {
        std::vector<Widget*> lines;
        collectByClass(view, "skeleton-line", lines);
        bool ok = !lines.empty();
        std::string actual = "none";
        if (ok) {
            const FlexStyle& st = lines.front()->style();
            ok = st.width.kind == Dimension::Kind::Percent &&
                 std::fabs(st.width.value - 100.0f) < 0.01f &&
                 st.height.kind == Dimension::Kind::Points &&
                 st.height.value == 14.0f;
            actual = "w=" + std::to_string(static_cast<int>(st.width.kind)) + ":" +
                     std::to_string(st.width.value) + " h=" +
                     std::to_string(static_cast<int>(st.height.kind)) + ":" +
                     std::to_string(st.height.value);
        }
        check("CSS skeleton size", ok, actual);
    }

    check("Extended controls", countExtendedControls(view) >= 9,
          std::to_string(countExtendedControls(view)));

    // ---- 新控件交互 ----
    {
        View probe(300, 60);
        SegmentedControl* segment = probe.emplaceChild<SegmentedControl>(
            std::vector<std::string>{"One", "Two", "Three"});
        segment->style().width = Dimension::px(300);
        segment->style().height = Dimension::px(36);
        int calls = 0;
        segment->onSelectionChanged = [&calls](int32_t) { ++calls; };
        probe.layoutNow();
        clickWidget(probe, *segment, 220, 18);
        check("Segmented click", segment->selectedIndex() == 2 && calls == 1,
              std::to_string(segment->selectedIndex()) + "/" + std::to_string(calls));
    }

    {
        View probe(200, 40);
        Chip* chip = probe.emplaceChild<Chip>("Design");
        probe.layoutNow();
        clickWidget(probe, *chip, 40, 14);
        check("Chip toggles", chip->selected(), chip->selected() ? "true" : "false");
    }

    // ---- 组合式 CalendarView：通过真实鼠标事件点击日期 / 换月 ----
    {
        View probe(320, 420);
        CalendarView* calendar = probe.emplaceChild<CalendarView>(2026, 10, 26);
        int dateCalls = 0;
        int lastDay = 0;
        calendar->onDateChanged = [&](int32_t, int32_t, int32_t value) {
            ++dateCalls;
            lastDay = value;
        };
        probe.layoutNow();

        std::vector<Widget*> days;
        collectByClass(*calendar, "calendar-day", days);
        check("Calendar day buttons", days.size() == 31, std::to_string(days.size()));

        std::vector<Widget*> weekdays;
        collectByClass(*calendar, "calendar-weekday", weekdays);
        std::vector<Widget*> titles;
        collectByClass(*calendar, "calendar-title", titles);
        const auto* title = titles.empty() ? nullptr : dynamic_cast<TextBlock*>(titles.front());
        check("Calendar title", title && title->text() == "October 2026",
              title ? title->text() : "missing");
        check("Calendar weekdays", weekdays.size() == 7, std::to_string(weekdays.size()));

        Widget* day1 = findButtonByLabel(*calendar, "1");
        Widget* day2 = findButtonByLabel(*calendar, "2");
        Widget* day5 = findButtonByLabel(*calendar, "5");
        if (day1 && day2 && day5) {
            const Rect first = day1->absoluteFrame();
            const Rect second = day2->absoluteFrame();
            const Rect nextRow = day5->absoluteFrame();
            check("Calendar grid row", first.y == second.y && first.x + first.w <= second.x,
                  std::to_string(first.x) + "," + std::to_string(first.y));
            check("Calendar grid wraps", nextRow.y > first.y,
                  std::to_string(nextRow.y));
        } else {
            check("Calendar grid cells", false, "missing");
        }

        Widget* day15 = findButtonByLabel(*calendar, "15");
        if (day15) {
            clickWidget(probe, *day15, day15->width() / 2, day15->height() / 2);
            check("Calendar click day 15",
                  calendar->day() == 15 && dateCalls == 1 && lastDay == 15,
                  std::to_string(calendar->day()));
        } else {
            check("Calendar find day 15", false, "missing");
        }

        std::vector<Widget*> navs;
        collectByClass(*calendar, "calendar-nav", navs);
        if (navs.size() >= 2) {
            clickWidget(probe, *navs[0], navs[0]->width() / 2, navs[0]->height() / 2);
            probe.layoutNow();
            days.clear();
            collectByClass(*calendar, "calendar-day", days);
            check("Calendar prev month", calendar->month() == 9 && days.size() == 30,
                  std::to_string(calendar->month()) + "/" + std::to_string(days.size()));
        } else {
            check("Calendar nav buttons", false, std::to_string(navs.size()));
        }
    }

    // ---- 组合式 TreeView：展开 / 选中 ----
    {
        View probe(320, 300);
        TreeView* tree = probe.emplaceChild<TreeView>();
        tree->setNodes({
            {"Documents", 0, true, false},
            {"Projects", 1, true, false},
            {"ModernUI", 2, false, false},
            {"Music", 0, true, false}
        });
        int expandedCalls = 0;
        int selectedCalls = 0;
        int selectedIndex = -1;
        tree->onExpandedChanged = [&](int32_t, bool) { ++expandedCalls; };
        tree->onSelectionChanged = [&](int32_t index) {
            ++selectedCalls;
            selectedIndex = index;
        };
        probe.layoutNow();

        std::vector<Widget*> rows;
        collectByClass(*tree, "tree-row", rows);
        check("Tree rows collapsed", rows.size() == 2, std::to_string(rows.size()));

        if (!rows.empty()) {
            clickWidget(probe, *rows[0], 10, rows[0]->height() / 2);
            check("Tree expand callback",
                  expandedCalls == 1 && tree->nodes()[0].expanded,
                  std::to_string(expandedCalls));

            probe.tick(0.016f);
            probe.layoutNow();
            rows.clear();
            collectByClass(*tree, "tree-row", rows);
            check("Tree rows expanded", rows.size() == 3, std::to_string(rows.size()));

            if (rows.size() >= 2) {
                const Rect first = rows[0]->absoluteFrame();
                const Rect second = rows[1]->absoluteFrame();
                check("Tree row layout", first.x == second.x && second.y == first.y + 36,
                      std::to_string(first.y) + "->" + std::to_string(second.y));

                clickWidget(probe, *rows[1], 80, rows[1]->height() / 2);
                check("Tree row selects",
                      selectedCalls == 1 && selectedIndex == 1 && tree->selectedIndex() == 1,
                      std::to_string(tree->selectedIndex()));
            }
        }
    }

    std::printf("[style_demo] %s\n", failures == 0 ? "dump passed" : "dump failed");
    return failures == 0 ? 0 : 2;
}

int runWindow(const std::string& cssPath, const std::string& uiPath, float scrollOffset) {
    EventQueue events;
    Win32LayeredWindow window(events);

    Win32LayeredWindow::Options options;
    options.title = L"ModernUI — CSS + UI";
    options.width = kWindowWidth;
    options.height = kWindowHeight;
    options.resizable = true;
    options.draggable = true;
    options.visible = true;

    std::string error;
    if (!window.create(options, &error)) {
        MessageBoxA(nullptr, error.c_str(), "ModernUI", MB_ICONERROR | MB_OK);
        return 1;
    }

    View view(window.width(), window.height());
    view.canvas().setBackgroundColor(Color{16, 20, 27, 255});

    Blend2DRenderer renderer(&window);
    if (!renderer.loadDefaultFont(&error)) {
        std::fprintf(stderr, "[ModernUI] font warning: %s\n", error.c_str());
    }
    view.setRenderer(&renderer);
    view.setTextMetricProvider([&renderer](const TextShape& text) {
        return renderer.measureText(text).value_or(Rect::null());
    });

    UiLoader loader;
    TextBlock* statusText = nullptr;
    const std::string loadError = buildDemo(view, loader, statusText, cssPath, uiPath);
    if (!loadError.empty()) {
        MessageBoxA(nullptr, loadError.c_str(), "ModernUI", MB_ICONERROR | MB_OK);
        window.destroy();
        return 1;
    }

    // --scroll N：页面根是 ScrollViewer，启动时定位到指定内容偏移
    if (scrollOffset >= 0.0f && !view.children().empty()) {
        if (auto* scroll = dynamic_cast<ScrollViewer*>(view.children().front().get())) {
            scroll->scrollTo(scrollOffset);
            view.layoutNow();
        }
    }

    bool running = true;
    LARGE_INTEGER frequency{}, previous{}, nowCounter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);

    while (running) {
        if (!window.pumpMessages()) break;

        Event ev;
        while (events.poll(ev)) {
            switch (ev.type) {
            case EventType::Close:
            case EventType::Quit:
                running = false;
                break;

            case EventType::KeyDown:
                if (ev.key == VK_ESCAPE) {
                    running = false;
                    break;
                }
                view.handleEvent(ev);
                break;

            case EventType::Resize:
                view.resize(std::max(1, ev.width), std::max(1, ev.height));
                break;

            default:
                view.handleEvent(ev);
                break;
            }
            if (!running) break;
        }
        if (!running) break;

        QueryPerformanceCounter(&nowCounter);
        float dt = static_cast<float>(
            static_cast<double>(nowCounter.QuadPart - previous.QuadPart) /
            static_cast<double>(frequency.QuadPart));
        previous = nowCounter;
        dt = std::min(dt, 0.05f);

        const bool changed = view.tick(dt);
        if (view.needsDraw() || changed) view.drawFrame();

        if (!window.waitMessages(8)) break;
    }

    window.destroy();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string cssPath = resolveAsset("demo.css");
    std::string uiPath = resolveAsset("demo.ui");
    bool dump = false;
    float scrollOffset = -1.0f;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--dump") dump = true;
        else if (arg == "--scroll" && i + 1 < argc) {
            scrollOffset = static_cast<float>(std::atof(argv[++i]));
        }
        else if (arg == "--css" && i + 1 < argc) cssPath = argv[++i];
        else if (arg == "--ui" && i + 1 < argc) uiPath = argv[++i];
    }

    if (cssPath.empty() || uiPath.empty()) {
        std::fprintf(stderr,
                     "[style_demo] cannot locate demo.css / demo.ui.\n"
                     "Run from the repository or build directory, or pass --css / --ui.\n");
        return 1;
    }

    return dump ? runDump(cssPath, uiPath) : runWindow(cssPath, uiPath, scrollOffset);
}
