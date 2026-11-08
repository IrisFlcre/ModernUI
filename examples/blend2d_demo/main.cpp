// ============================================================================
// ModernUI × Blend2D Demo
// ----------------------------------------------------------------------------
// 验证内容：
//   - Win32 无边框分层窗口（AC_SRC_ALPHA 逐像素透明）
//   - Canvas Retain Mode 场景图 + 脏区域 Tile 渲染
//   - Blend2D 后端：Rect/RRect/Circle/Ellipse/Arc/Pie/Ring/Star/Polygon/
//     Line/Polyline/Path/Text/Image/Svg
//   - Paint：纯色 / 线性渐变 / 径向渐变 / spread / 渐变描边 / 渐变文字
//   - SVG：Canvas 只提供原文，后端 nanosvg 解析 + BLPath/BLGradient 缓存
//   - Group clipRect + scrollOffset 滚动区域
//   - 事件系统：wheel 滚动、resize、DPI、Esc 退出
// ============================================================================

#include "Blend2DRenderer.h"
#include "Canvas.h"
#include "Event.h"
#include "Win32LayeredWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace modern_ui;

namespace {

constexpr int32_t kWindowWidth = 980;
constexpr int32_t kWindowHeight = 900;

constexpr int32_t kViewportX = 40;
constexpr int32_t kViewportY = 132;
constexpr int32_t kViewportW = 480;
constexpr int32_t kViewportH = 458;

constexpr int32_t kRowHeight = 36;
constexpr int32_t kRowStep = 44;
constexpr int32_t kRowCount = 14;
constexpr int32_t kMaxScroll = kRowCount * kRowStep - kViewportH;

Shape makeRRect(int32_t x, int32_t y, int32_t w, int32_t h,
                float radius, Color fill, Color stroke = Color::nullColor(),
                int32_t strokeWidth = 0, int32_t z = 0) {
    Shape shape;
    shape.base.fill = fill;
    shape.base.stroke = stroke;
    shape.base.strokeWidth = strokeWidth;
    shape.base.z = z;
    shape.data = RRectShape{x, y, w, h, radius, radius};
    return shape;
}

Shape makeText(std::string text, int32_t x, int32_t baselineY, int32_t size,
               Color color, int32_t z) {
    Shape shape;
    shape.base.fill = color;
    shape.base.z = z;
    TextShape ts;
    ts.text = std::move(text);
    ts.x = x;
    ts.y = baselineY;
    ts.fontSize = size;
    shape.data = std::move(ts);
    return shape;
}

Shape makeSvg(std::string markup, int32_t x, int32_t y, int32_t w, int32_t h,
              int32_t z, bool preserveAspectRatio = true) {
    Shape shape;
    shape.base.z = z;
    SvgShape svg;
    svg.data = std::move(markup);
    svg.x = x;
    svg.y = y;
    svg.w = w;
    svg.h = h;
    svg.preserveAspectRatio = preserveAspectRatio;
    shape.data = std::move(svg);
    return shape;
}

PathShape makeStarPath(float cx, float cy, float outer, float inner) {
    PathShape path;
    constexpr int kPoints = 5;
    constexpr float kPi = 3.14159265358979323846f;

    for (int i = 0; i < kPoints * 2; ++i) {
        const float radius = (i % 2 == 0) ? outer : inner;
        const float angle = -kPi * 0.5f + static_cast<float>(i) * kPi / kPoints;

        PathShape::Step step{};
        step.cmd = (i == 0) ? PathShape::Cmd::MoveTo : PathShape::Cmd::LineTo;
        step.pts[0] = Vec2f{cx + std::cos(angle) * radius,
                            cy + std::sin(angle) * radius};
        path.steps.push_back(step);
    }

    PathShape::Step close{};
    close.cmd = PathShape::Cmd::Close;
    path.steps.push_back(close);
    return path;
}

// --- SVG 演示素材（Canvas 只传原文，解析/缓存由后端完成）---

// 内部含线性渐变 + 径向渐变 + 透明度
const char* kSvgChip = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <defs>
    <linearGradient id="chipFill" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#5ee7ff"/>
      <stop offset="100%" stop-color="#3b6bff"/>
    </linearGradient>
    <radialGradient id="chipGlow" cx="42%" cy="32%" r="62%">
      <stop offset="0%" stop-color="#ffffff" stop-opacity="0.9"/>
      <stop offset="100%" stop-color="#7c9cff" stop-opacity="0"/>
    </radialGradient>
  </defs>
  <rect x="3" y="3" width="58" height="58" rx="15" fill="url(#chipFill)"/>
  <circle cx="27" cy="23" r="20" fill="url(#chipGlow)"/>
  <path d="M22 46 L32 25 L42 46 Z" fill="#ffffff" fill-opacity="0.85"/>
</svg>)svg";

// 贝塞尔心形 + 线性渐变
const char* kSvgHeart = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <defs>
    <linearGradient id="heartFill" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" stop-color="#ff9a9a"/>
      <stop offset="100%" stop-color="#ff2d55"/>
    </linearGradient>
  </defs>
  <path d="M32 56 C10 40 7 25 17 17 C24 11 32 16 32 23 C32 16 40 11 47 17 C57 25 54 40 32 56 Z"
        fill="url(#heartFill)"/>
</svg>)svg";

// 虚线描边 + 圆角连接 + 星形填充/描边
const char* kSvgDashedStar = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <circle cx="32" cy="32" r="23" fill="none" stroke="#7ad7ff" stroke-width="4"
          stroke-linecap="round" stroke-dasharray="7 5" stroke-opacity="0.9"/>
  <path d="M32 15 L37 27 L50 28 L40 36 L43 49 L32 42 L21 49 L24 36 L14 28 L27 27 Z"
        fill="#ffe08a" stroke="#ffb347" stroke-width="1.5" stroke-linejoin="round"/>
</svg>)svg";

// 非正方形 viewBox；演示 preserveAspectRatio = false 的拉伸模式
const char* kSvgWave = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 32">
  <defs>
    <linearGradient id="waveFill" x1="0%" y1="0%" x2="100%" y2="0%">
      <stop offset="0%" stop-color="#54e6c2"/>
      <stop offset="100%" stop-color="#4a7dff"/>
    </linearGradient>
  </defs>
  <path d="M2 25 C12 5 20 5 30 17 C40 29 48 29 62 9 L62 30 L2 30 Z"
        fill="url(#waveFill)"/>
</svg>)svg";

// 生成一张带 alpha 的 PNG，验证 ImageShape + 图片注册表 + 文件/内存解码链路
uint64_t createDemoImage(Blend2DRenderer& renderer) {
    BLImage image;
    if (image.create(96, 96, BL_FORMAT_PRGB32) != BL_SUCCESS) return 0;

    BLContext ctx(image);
    if (!ctx.is_valid()) return 0;

    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.clear_all();
    ctx.set_comp_op(BL_COMP_OP_SRC_OVER);

    ctx.fill_round_rect(BLRect(0, 0, 96, 96), 22.0, 22.0, BLRgba32(64, 132, 255, 255));
    ctx.fill_round_rect(BLRect(10, 10, 76, 76), 16.0, 16.0, BLRgba32(96, 180, 255, 180));
    ctx.fill_circle(48.0, 48.0, 22.0, BLRgba32(255, 255, 255, 230));
    ctx.fill_circle(48.0, 48.0, 10.0, BLRgba32(64, 132, 255, 255));
    ctx.flush(BL_CONTEXT_FLUSH_SYNC);

    BLImageCodec codec;
    if (codec.find_by_name("PNG") != BL_SUCCESS) return 0;

    BLArray<uint8_t> encoded;
    if (image.write_to_data(encoded, codec) != BL_SUCCESS) return 0;
    if (encoded.is_empty()) return 0;

    return renderer.loadImageFromMemory(encoded.data(), encoded.size());
}

uint32_t buildScene(Canvas& canvas, Blend2DRenderer& renderer) {
    // --- 背景卡片 ---
    canvas.addShape(makeRRect(24, 100, 512, 508, 20.0f,
                              Color{30, 32, 42, 255}, Color{72, 82, 112, 255}, 2, 0));
    canvas.addShape(makeRRect(548, 100, 408, 508, 20.0f,
                              Color{28, 30, 40, 255}, Color{64, 70, 92, 255}, 1, 0));

    // --- 标题 ---
    canvas.addShape(makeText("ModernUI × Blend2D", 36, 58, 26,
                             Color{240, 244, 255, 255}, 20));
    canvas.addShape(makeText("Per-Pixel Alpha Layered Window · Dirty Region Renderer",
                             36, 84, 13, Color{146, 156, 184, 255}, 20));
    canvas.addShape(makeText("SCROLL AREA — use mouse wheel", 40, 124, 11,
                             Color{120, 130, 158, 255}, 5));
    canvas.addShape(makeText("BLEND2D SHAPES", 572, 132, 11,
                             Color{120, 130, 158, 255}, 5));

    // --- Scroll Area：Group(clipRect + scrollOffset) ---
    Shape scrollArea;
    scrollArea.base.z = 10;
    GroupData group;
    group.clipRect = Rect{kViewportX, kViewportY, kViewportW, kViewportH};
    group.scrollOffset = Vec2i{0, 0};

    for (int32_t i = 0; i < kRowCount; ++i) {
        const int32_t y = 136 + i * kRowStep;
        const bool odd = (i % 2) != 0;
        group.children.push_back(makeRRect(
            44, y, 472, kRowHeight, 10.0f,
            odd ? Color{38, 42, 56, 255} : Color{44, 48, 64, 255},
            Color{58, 66, 92, 255}, 1, 10));

        group.children.push_back(makeText(
            "Item " + std::to_string(i + 1) + "   —   clipped by GroupData.clipRect",
            60, y + 23, 13,
            odd ? Color{196, 204, 228, 255} : Color{212, 220, 244, 255}, 11));
    }

    scrollArea.data = std::move(group);
    const uint32_t scrollAreaId = canvas.addShape(std::move(scrollArea));

    // --- 右侧图形 ---
    Shape circle;
    {
        RadialGradient gradient;
        gradient.center = Vec2f{752.0f, 252.0f};
        gradient.radius = 88.0f;
        gradient.focal = Vec2f{734.0f, 230.0f};
        gradient.stops = {
            {0.00f, Color{255, 198, 152, 235}},
            {0.55f, Color{255, 118, 84, 235}},
            {1.00f, Color{150, 38, 86, 245}},
        };
        circle.base.fill = std::move(gradient);
    }
    circle.base.stroke = Color{255, 255, 255, 50};
    circle.base.strokeWidth = 2;
    circle.base.z = 12;
    circle.data = CircleShape{752, 252, 88.0f};
    canvas.addShape(std::move(circle));

    Shape ellipse;
    ellipse.base.fill = Color{84, 208, 186, 195};
    ellipse.base.stroke = Color{200, 255, 245, 120};
    ellipse.base.strokeWidth = 2;
    ellipse.base.z = 12;
    ellipse.data = EllipseShape{836, 404, 62.0f, 28.0f};
    canvas.addShape(std::move(ellipse));

    Shape line;
    {
        LinearGradient gradient;
        gradient.from = Vec2f{590.0f, 470.0f};
        gradient.to = Vec2f{800.0f, 470.0f};
        gradient.stops = {
            {0.00f, Color{110, 160, 255, 255}},
            {1.00f, Color{205, 120, 255, 255}},
        };
        line.base.stroke = std::move(gradient);
    }
    line.base.strokeWidth = 3;
    line.base.z = 12;
    line.data = LineShape{590, 470, 800, 470};
    canvas.addShape(std::move(line));

    Shape polyline;
    polyline.base.stroke = Color{255, 196, 92, 255};
    polyline.base.strokeWidth = 3;
    polyline.base.z = 13;
    PolylineShape pl;
    pl.points = {{592, 500}, {628, 462}, {664, 500}, {700, 462}, {736, 500}, {772, 462}};
    pl.closed = false;
    polyline.data = std::move(pl);
    canvas.addShape(std::move(polyline));

    Shape path;
    {
        LinearGradient gradient;
        gradient.from = Vec2f{872.0f, 176.0f};
        gradient.to = Vec2f{828.0f, 248.0f};
        gradient.stops = {
            {0.00f, Color{255, 236, 150, 235}},
            {1.00f, Color{255, 150, 64, 235}},
        };
        path.base.fill = std::move(gradient);
    }
    path.base.z = 13;
    PathShape ps;
    PathShape::Step step{};
    step.cmd = PathShape::Cmd::MoveTo;
    step.pts[0] = Vec2f{872.0f, 176.0f};
    ps.steps.push_back(step);
    step.cmd = PathShape::Cmd::LineTo;
    step.pts[0] = Vec2f{916.0f, 248.0f};
    ps.steps.push_back(step);
    step.cmd = PathShape::Cmd::LineTo;
    step.pts[0] = Vec2f{828.0f, 248.0f};
    ps.steps.push_back(step);
    step.cmd = PathShape::Cmd::Close;
    ps.steps.push_back(step);
    path.data = std::move(ps);
    canvas.addShape(std::move(path));

    // --- ImageShape（PNG 内存解码 + handle 注册表）---
    const uint64_t imageHandle = createDemoImage(renderer);
    if (imageHandle != 0) {
        Shape image;
        image.base.z = 13;
        image.data = ImageShape{imageHandle, 880, 462, 64, 64, false};
        canvas.addShape(std::move(image));
    }

    canvas.addShape(makeText("wheel = scroll  ·  Esc = quit  ·  resize = allowed",
                             572, 588, 12, Color{128, 138, 166, 255}, 20));

    // --- 底部：Paint & SVG 展示 ---
    canvas.addShape(makeRRect(24, 620, 932, 108, 16.0f,
                              Color{30, 32, 42, 255}, Color{72, 82, 112, 140}, 1, 0));
    canvas.addShape(makeText("PAINT & SVG", 44, 648, 11, Color{120, 130, 158, 255}, 5));
    canvas.addShape(makeText("linear / radial  ·  gradient stroke & text  ·  nanosvg cache",
                             152, 648, 11, Color{96, 106, 134, 255}, 5));

    // 1) Canvas 线性渐变填充（RRect）
    {
        Shape tile = makeRRect(44, 660, 84, 52, 12.0f,
                               Color::nullColor(), Color{90, 100, 132, 90}, 1, 10);
        LinearGradient gradient;
        gradient.from = Vec2f{44.0f, 660.0f};
        gradient.to = Vec2f{128.0f, 712.0f};
        gradient.stops = {{0.0f, Color{94, 231, 255, 255}},
                          {1.0f, Color{59, 107, 255, 255}}};
        tile.base.fill = std::move(gradient);
        canvas.addShape(std::move(tile));
    }

    // 2) Canvas 径向渐变填充（Circle + 焦点偏移）
    {
        Shape dot;
        RadialGradient gradient;
        gradient.center = Vec2f{166.0f, 686.0f};
        gradient.radius = 34.0f;
        gradient.focal = Vec2f{156.0f, 676.0f};
        gradient.stops = {{0.0f, Color{255, 255, 255, 240}},
                          {0.55f, Color{146, 122, 255, 255}},
                          {1.0f, Color{64, 40, 160, 255}}};
        dot.base.fill = std::move(gradient);
        dot.base.z = 10;
        dot.data = CircleShape{166, 686, 26.0f};
        canvas.addShape(std::move(dot));
    }

    // 3) Canvas 渐变描边（Path star）
    {
        Shape star;
        LinearGradient gradient;
        gradient.from = Vec2f{204.0f, 660.0f};
        gradient.to = Vec2f{256.0f, 712.0f};
        gradient.stops = {{0.0f, Color{255, 210, 90, 255}},
                          {1.0f, Color{255, 92, 178, 255}}};
        star.base.stroke = std::move(gradient);
        star.base.strokeWidth = 3;
        star.base.z = 10;
        star.data = makeStarPath(230.0f, 686.0f, 24.0f, 10.0f);
        canvas.addShape(std::move(star));
    }

    // 4) SVG 图标（渐变 / 贝塞尔 / 虚线 / 拉伸）
    canvas.addShape(makeSvg(kSvgChip, 268, 660, 52, 52, 10));
    canvas.addShape(makeSvg(kSvgHeart, 332, 660, 52, 52, 10));
    canvas.addShape(makeSvg(kSvgDashedStar, 396, 660, 52, 52, 10));
    canvas.addShape(makeSvg(kSvgWave, 460, 660, 120, 52, 10, false));

    // 5) 渐变文字
    {
        Shape text = makeText("Gradient", 600, 698, 32, Color::white(), 10);
        LinearGradient gradient;
        gradient.from = Vec2f{600.0f, 668.0f};
        gradient.to = Vec2f{744.0f, 700.0f};
        gradient.stops = {{0.0f, Color{94, 231, 255, 255}},
                          {0.5f, Color{146, 122, 255, 255}},
                          {1.0f, Color{255, 110, 220, 255}}};
        text.base.fill = std::move(gradient);
        canvas.addShape(std::move(text));
    }

    // 6) 渐变描边按钮
    {
        Shape pill = makeRRect(760, 660, 180, 52, 26.0f, Color{20, 22, 30, 255},
                               Color::nullColor(), 0, 10);
        LinearGradient border;
        border.from = Vec2f{760.0f, 660.0f};
        border.to = Vec2f{940.0f, 712.0f};
        border.stops = {{0.0f, Color{255, 110, 220, 255}},
                        {1.0f, Color{94, 231, 255, 255}}};
        pill.base.stroke = std::move(border);
        pill.base.strokeWidth = 2;
        canvas.addShape(std::move(pill));
        canvas.addShape(makeText("gradient stroke", 798, 692, 13,
                                 Color{212, 220, 244, 255}, 11));
    }

    // --- 底部：新增几何体（Arc / Pie / Ring / Star / Polygon）---
    canvas.addShape(makeRRect(24, 744, 932, 132, 16.0f,
                              Color{30, 32, 42, 255}, Color{72, 82, 112, 140}, 1, 0));
    canvas.addShape(makeText("GEOMETRY PRIMITIVES", 44, 770, 11,
                             Color{120, 130, 158, 255}, 5));
    canvas.addShape(makeText("arc · pie · ring · star · polygon",
                             196, 770, 11, Color{96, 106, 134, 255}, 5));

    // 1) Arc：渐变描边圆弧
    {
        Shape arc;
        LinearGradient gradient;
        gradient.from = Vec2f{87.0f, 788.0f};
        gradient.to = Vec2f{147.0f, 848.0f};
        gradient.stops = {{0.0f, Color{94, 231, 255, 255}},
                          {1.0f, Color{176, 110, 255, 255}}};
        arc.base.stroke = std::move(gradient);
        arc.base.strokeWidth = 5;
        arc.base.z = 10;
        arc.data = ArcShape{117, 818, 30.0f, 30.0f, -140.0f, 280.0f};
        canvas.addShape(std::move(arc));
    }

    // 2) Pie：扇形填充
    {
        Shape pie;
        LinearGradient gradient;
        gradient.from = Vec2f{274.0f, 788.0f};
        gradient.to = Vec2f{334.0f, 848.0f};
        gradient.stops = {{0.0f, Color{84, 230, 194, 255}},
                          {1.0f, Color{74, 125, 255, 255}}};
        pie.base.fill = std::move(gradient);
        pie.base.stroke = Color{200, 240, 255, 90};
        pie.base.strokeWidth = 1;
        pie.base.z = 10;
        pie.data = PieShape{304, 818, 30.0f, 30.0f, -90.0f, 140.0f};
        canvas.addShape(std::move(pie));
    }

    // 3) Ring：内圈镂空 + 渐变填充
    {
        Shape ring;
        LinearGradient gradient;
        gradient.from = Vec2f{460.0f, 788.0f};
        gradient.to = Vec2f{520.0f, 848.0f};
        gradient.stops = {{0.0f, Color{255, 210, 90, 255}},
                          {1.0f, Color{255, 110, 220, 255}}};
        ring.base.fill = std::move(gradient);
        ring.base.stroke = Color{255, 255, 255, 40};
        ring.base.strokeWidth = 1;
        ring.base.z = 10;
        ring.data = RingShape{490, 818, 30.0f, 30.0f, 9.0f};
        canvas.addShape(std::move(ring));
    }

    // 4) Star：多角星填充
    {
        Shape star;
        LinearGradient gradient;
        gradient.from = Vec2f{646.0f, 786.0f};
        gradient.to = Vec2f{706.0f, 850.0f};
        gradient.stops = {{0.0f, Color{255, 236, 150, 255}},
                          {1.0f, Color{255, 150, 64, 255}}};
        star.base.fill = std::move(gradient);
        star.base.stroke = Color{255, 210, 120, 120};
        star.base.strokeWidth = 1;
        star.base.z = 10;
        star.data = StarShape{676, 818, 32.0f, 14.0f, 5, 0.0f};
        canvas.addShape(std::move(star));
    }

    // 5) Polygon：正六边形 + 渐变描边
    {
        Shape polygon;
        LinearGradient gradient;
        gradient.from = Vec2f{833.0f, 788.0f};
        gradient.to = Vec2f{893.0f, 848.0f};
        gradient.stops = {{0.0f, Color{110, 160, 255, 255}},
                          {1.0f, Color{205, 120, 255, 255}}};
        polygon.base.fill = Color{64, 90, 160, 70};
        polygon.base.stroke = std::move(gradient);
        polygon.base.strokeWidth = 2;
        polygon.base.z = 10;
        polygon.data = PolygonShape{863, 818, 30.0f, 6, 0.0f};
        canvas.addShape(std::move(polygon));
    }

    canvas.addShape(makeText("Arc", 108, 866, 11, Color{150, 160, 188, 255}, 11));
    canvas.addShape(makeText("Pie", 296, 866, 11, Color{150, 160, 188, 255}, 11));
    canvas.addShape(makeText("Ring", 478, 866, 11, Color{150, 160, 188, 255}, 11));
    canvas.addShape(makeText("Star", 666, 866, 11, Color{150, 160, 188, 255}, 11));
    canvas.addShape(makeText("Polygon", 843, 866, 11, Color{150, 160, 188, 255}, 11));

    return scrollAreaId;
}

} // namespace

int main() {
    EventQueue events;
    Win32LayeredWindow window(events);

    Win32LayeredWindow::Options options;
    options.title = L"ModernUI × Blend2D — Layered Canvas";
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

    Canvas canvas(window.width(), window.height());
    canvas.setBackgroundColor(Color{18, 19, 26, 235});

    Blend2DRenderer renderer(&window);
    if (!renderer.loadDefaultFont(&error)) {
        std::fprintf(stderr, "[ModernUI] font warning: %s\n", error.c_str());
    }
    canvas.setRenderer(&renderer);

    const uint32_t scrollAreaId = buildScene(canvas, renderer);

    bool running = true;
    while (running) {
        if (!window.pumpMessages()) break;

        Event ev;
        while (events.poll(ev)) {
            switch (ev.type) {
            case EventType::Close:
                running = false;
                break;

            case EventType::Quit:
                running = false;
                break;

            case EventType::KeyDown:
                if (ev.key == VK_ESCAPE) running = false;
                break;

            case EventType::Resize:
                canvas.resize(std::max(1, ev.width), std::max(1, ev.height));
                break;

            case EventType::MouseWheel: {
                const Rect viewport{kViewportX, kViewportY, kViewportW, kViewportH};
                if (viewport.contains(ev.x, ev.y)) {
                    const int32_t steps = static_cast<int32_t>(ev.wheelDelta * 44.0f);
                    canvas.updateShape(scrollAreaId, [steps](Shape& shape) {
                        auto& group = std::get<GroupData>(shape.data);
                        group.scrollOffset.y = std::clamp(group.scrollOffset.y + steps,
                                                          -kMaxScroll, 0);
                    });
                }
                break;
            }

            default:
                break;
            }
        }

        if (!running) break;

        if (canvas.needsRedraw()) {
            canvas.drawFrame();
        }

        if (!window.waitMessages(8)) break;
    }

    window.destroy();
    return 0;
}
