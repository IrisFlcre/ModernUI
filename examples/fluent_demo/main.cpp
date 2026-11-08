// ============================================================================
// ModernUI — Windows 11 Fluent Design Gallery
// ----------------------------------------------------------------------------
// 展示内容：
//   - std_widgets：Fluent 主题 / 按钮 / 选择 / 输入 / 进度 / 集合 / 反馈 / 媒体
//   - Sprite 2D 动画：程序化生成图集 → SpriteSheet / SpriteClip / SpriteAnimator
//   - Motion：Tween（OutBack）+ SpringValue 的实时动画
//   - 明暗主题切换：themeRevision 驱动全部控件重绘
//
// 交互：滚轮 / 拖拽 Slider / 点击控件 / ComboBox 浮出列表 / Tab 切换焦点 / Esc 退出
// ============================================================================

#include "Animation.h"
#include "App.h"
#include "Blend2DRenderer.h"
#include "Easing.h"
#include "Event.h"
#include "FluentAdvanced.h"
#include "FluentButtons.h"
#include "FluentCollections.h"
#include "FluentFeedback.h"
#include "FluentInput.h"
#include "FluentMedia.h"
#include "FluentProgress.h"
#include "FluentSelection.h"
#include "FluentTheme.h"
#include "FluentTypography.h"
#include "SpriteAnimation.h"
#include "View.h"
#include "Win32LayeredWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace modern_ui;
using namespace modern_ui::fluent;
using modern_ui::anim::SpriteClip;
using modern_ui::anim::SpriteSheet;

namespace {

constexpr int32_t kWindowWidth = 1280;
constexpr int32_t kWindowHeight = 820;
constexpr int32_t kSpriteFrame = 64;
constexpr int32_t kSpriteFrames = 12;

// ===========================================================================
// 程序化 Sprite 图集：12 帧弹跳光球（BGRA / 预乘 alpha）
// ===========================================================================
void blendPixel(std::vector<uint8_t>& pixels, int32_t width, int32_t x, int32_t y,
                float r, float g, float b, float a) {
    if (x < 0 || y < 0 || x >= width || y >= static_cast<int32_t>(pixels.size() / (width * 4))) return;
    if (a <= 0.0f) return;
    a = std::min(1.0f, a);

    uint8_t* dst = &pixels[(static_cast<size_t>(y) * static_cast<size_t>(width) +
                            static_cast<size_t>(x)) * 4];
    const float srcR = r * a * 255.0f;
    const float srcG = g * a * 255.0f;
    const float srcB = b * a * 255.0f;
    const float inv = 1.0f - a;

    dst[0] = static_cast<uint8_t>(std::lround(std::min(255.0f, srcB + static_cast<float>(dst[0]) * inv)));
    dst[1] = static_cast<uint8_t>(std::lround(std::min(255.0f, srcG + static_cast<float>(dst[1]) * inv)));
    dst[2] = static_cast<uint8_t>(std::lround(std::min(255.0f, srcR + static_cast<float>(dst[2]) * inv)));
    dst[3] = static_cast<uint8_t>(std::lround(std::min(255.0f, (a + (static_cast<float>(dst[3]) / 255.0f) * inv) * 255.0f)));
}

uint64_t createSpriteAtlas(Blend2DRenderer& renderer) {
    const int32_t atlasWidth = kSpriteFrame * kSpriteFrames;
    const int32_t atlasHeight = kSpriteFrame;
    std::vector<uint8_t> pixels(static_cast<size_t>(atlasWidth) * atlasHeight * 4, 0);

    for (int32_t frame = 0; frame < kSpriteFrames; ++frame) {
        const int32_t ox = frame * kSpriteFrame;
        const float phase = 6.2831853f * static_cast<float>(frame) / static_cast<float>(kSpriteFrames);
        const float lift = std::fabs(std::sin(phase));
        const float cx = 32.0f + std::sin(phase) * 5.0f;
        const float cy = 38.0f - lift * 13.0f;

        // 阴影
        const float shadowRadiusX = 11.0f + lift * 2.0f;
        const float shadowRadiusY = 3.6f;
        for (int32_t y = 48; y < 60; ++y) {
            for (int32_t x = 10; x < 54; ++x) {
                const float nx = (static_cast<float>(x) - cx) / shadowRadiusX;
                const float ny = (static_cast<float>(y) - 55.0f) / shadowRadiusY;
                const float dist = nx * nx + ny * ny;
                if (dist >= 1.0f) continue;
                const float coverage = std::min(1.0f, (1.0f - dist) * 4.0f);
                blendPixel(pixels, atlasWidth, ox + x, y, 0.0f, 0.0f, 0.0f,
                           0.20f * coverage * (1.0f - lift * 0.35f));
            }
        }

        // 光球（径向渐变 + 抗锯齿边缘）
        const float radius = 13.0f;
        for (int32_t y = 8; y < 58; ++y) {
            for (int32_t x = 4; x < 60; ++x) {
                const float dx = static_cast<float>(x) - cx;
                const float dy = static_cast<float>(y) - cy;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist > radius + 1.0f) continue;

                const float coverage = std::min(1.0f, std::max(0.0f, radius + 0.5f - dist));
                const float k = std::min(1.0f, dist / radius);
                const float r = 190.0f + (45.0f - 190.0f) * k;
                const float g = 238.0f + (150.0f - 238.0f) * k;
                const float b = 255.0f + (220.0f - 255.0f) * k;
                blendPixel(pixels, atlasWidth, ox + x, y, r / 255.0f, g / 255.0f, b / 255.0f, coverage);
            }
        }

        // 高光
        for (int32_t y = static_cast<int32_t>(cy) - 10; y < static_cast<int32_t>(cy) + 3; ++y) {
            for (int32_t x = static_cast<int32_t>(cx) - 10; x < static_cast<int32_t>(cx) + 3; ++x) {
                const float dx = static_cast<float>(x) - (cx - 4.0f);
                const float dy = static_cast<float>(y) - (cy - 4.0f);
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist > 4.6f) continue;
                blendPixel(pixels, atlasWidth, ox + x, y, 1.0f, 1.0f, 1.0f,
                           std::max(0.0f, 1.0f - dist / 4.6f) * 0.65f);
            }
        }
    }

    return renderer.createImageFromPixels(atlasWidth, atlasHeight, pixels.data());
}

// ===========================================================================
// 程序化壁纸：用于 ImageView 演示
// ===========================================================================
uint64_t createWallpaper(Blend2DRenderer& renderer) {
    constexpr int32_t width = 420;
    constexpr int32_t height = 240;
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);

    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(width);
            const float v = static_cast<float>(y) / static_cast<float>(height);

            float r = 32.0f + (10.0f - 32.0f) * u;
            float g = 28.0f + (32.0f - 28.0f) * v;
            float b = 62.0f + (20.0f - 62.0f) * v;

            const float blob1 = std::max(0.0f, 1.0f - std::sqrt(
                (u - 0.22f) * (u - 0.22f) * 3.2f + (v - 0.30f) * (v - 0.30f) * 3.2f));
            const float blob2 = std::max(0.0f, 1.0f - std::sqrt(
                (u - 0.78f) * (u - 0.78f) * 3.0f + (v - 0.64f) * (v - 0.64f) * 3.0f));

            r += blob1 * 35.0f + blob2 * 88.0f;
            g += blob1 * 110.0f + blob2 * 22.0f;
            b += blob1 * 140.0f + blob2 * 96.0f;

            const int32_t index = (y * width + x) * 4;
            pixels[index + 0] = static_cast<uint8_t>(std::min(255.0f, b));
            pixels[index + 1] = static_cast<uint8_t>(std::min(255.0f, g));
            pixels[index + 2] = static_cast<uint8_t>(std::min(255.0f, r));
            pixels[index + 3] = 255;
        }
    }

    return renderer.createImageFromPixels(width, height, pixels.data());
}

// ===========================================================================
// DemoView — 根视图 + Mica 风格背景（随主题切换）
// ===========================================================================
class DemoView : public View {
public:
    DemoView(int32_t width, int32_t height) : View(width, height) {
        Shape background;
        background.data = RectShape{0, 0, 1, 1};
        backgroundId_ = canvas().addShape(std::move(background));

        Shape glowA;
        glowA.data = CircleShape{0, 0, 1.0f};
        glowAId_ = canvas().addShape(std::move(glowA));

        Shape glowB;
        glowB.data = CircleShape{0, 0, 1.0f};
        glowBId_ = canvas().addShape(std::move(glowB));

        lastThemeRevision_ = themeRevision();
        updateBackground();
    }

    void onLayout(const Rect& frame) override {
        localFrame_ = Rect{0, 0, frame.w, frame.h};
        updateBackground();
    }

    void onTick(float dtSeconds) override {
        (void)dtSeconds;
        if (themeRevision() != lastThemeRevision_) {
            lastThemeRevision_ = themeRevision();
            updateBackground();
        }
    }

private:
    void updateBackground() {
        const FluentTheme& t = currentTheme();
        const int32_t width = std::max(1, localFrame_.w);
        const int32_t height = std::max(1, localFrame_.h);

        canvas().setBackgroundColor(t.background);

        LinearGradient background;
        background.from = Vec2f{0.0f, 0.0f};
        background.to = Vec2f{static_cast<float>(width), static_cast<float>(height)};
        if (t.mode == FluentTheme::Mode::Dark) {
            background.stops = {
                GradientStop{0.0f, Color{32, 33, 42, 255}},
                GradientStop{0.55f, Color{19, 20, 27, 255}},
                GradientStop{1.0f, Color{11, 12, 17, 255}}
            };
        } else {
            background.stops = {
                GradientStop{0.0f, Color{249, 249, 252, 255}},
                GradientStop{0.55f, Color{240, 241, 246, 255}},
                GradientStop{1.0f, Color{231, 233, 240, 255}}
            };
        }

        canvas().updateShape(backgroundId_, [&](Shape& shape) {
            shape.data = RectShape{0, 0, width, height};
            shape.base.fill = Paint::linear(background);
            shape.base.stroke = Paint{};
            shape.base.visible = true;
        });

        const Color accent = t.accent;
        const float glowAlpha = (t.mode == FluentTheme::Mode::Dark) ? 0.16f : 0.10f;

        RadialGradient glow1;
        glow1.center = Vec2f{static_cast<float>(width) * 0.22f, -static_cast<float>(height) * 0.08f};
        glow1.radius = static_cast<float>(width) * 0.55f;
        glow1.stops = {
            GradientStop{0.0f, withOpacity(accent, glowAlpha)},
            GradientStop{0.55f, withOpacity(accent, glowAlpha * 0.35f)},
            GradientStop{1.0f, withOpacity(accent, 0.0f)}
        };

        RadialGradient glow2;
        glow2.center = Vec2f{static_cast<float>(width) * 0.90f, static_cast<float>(height) * 0.95f};
        glow2.radius = static_cast<float>(width) * 0.45f;
        glow2.stops = {
            GradientStop{0.0f, withOpacity(Color{180, 120, 255, 255}, glowAlpha * 0.85f)},
            GradientStop{1.0f, withOpacity(Color{180, 120, 255, 255}, 0.0f)}
        };

        canvas().updateShape(glowAId_, [&](Shape& shape) {
            shape.data = CircleShape{static_cast<int32_t>(glow1.center.x),
                                     static_cast<int32_t>(glow1.center.y),
                                     glow1.radius};
            shape.base.fill = Paint::radial(glow1);
            shape.base.stroke = Paint{};
            shape.base.visible = true;
        });

        canvas().updateShape(glowBId_, [&](Shape& shape) {
            shape.data = CircleShape{static_cast<int32_t>(glow2.center.x),
                                     static_cast<int32_t>(glow2.center.y),
                                     glow2.radius};
            shape.base.fill = Paint::radial(glow2);
            shape.base.stroke = Paint{};
            shape.base.visible = true;
        });
    }

    Rect localFrame_{0, 0, kWindowWidth, kWindowHeight};
    uint32_t lastThemeRevision_ = 1;
    uint32_t backgroundId_ = 0;
    uint32_t glowAId_ = 0;
    uint32_t glowBId_ = 0;
};

// ===========================================================================
// MotionPlayground — Tween + SpringValue 演示
// ===========================================================================
class MotionPlayground final : public Widget {
public:
    MotionPlayground() {
        Shape track = makeRRectShape(Rect{0, 0, 1, 4}, 2.0f,
                                     withOpacity(currentTheme().textPrimary, 0.16f));
        trackId_ = canvas().addShape(std::move(track));

        Shape glow = makeCircleShape(0, 0, 1.0f, withOpacity(currentTheme().accent, 0.16f));
        glowId_ = canvas().addShape(std::move(glow));

        Shape ball = makeCircleShape(0, 0, 1.0f, currentTheme().accent);
        ballId_ = canvas().addShape(std::move(ball));

        tween_.setRange(0.08f, 0.92f);
        tween_.setDuration(0.85f);
        tween_.setEasing(anim::Easing::OutBack);
        tween_.onUpdate = [this](float v) {
            progress_ = v;
            updateGeometry();
        };

        spring_.value = 0.0f;
        spring_.target = 0.0f;
        spring_.stiffness = 260.0f;
        spring_.damping = 13.0f;

        lastThemeRevision_ = themeRevision();
    }

    void play() { tween_.play(); }
    void kick() { spring_.velocity = 4.6f; }

    Vec2i onMeasure(const Vec2i& available) const override {
        (void)available;
        return Vec2i{360, 120};
    }

    void onLayout(const Rect& frame) override {
        localFrame_ = Rect{0, 0, frame.w, frame.h};
        updateGeometry();
    }

    void onTick(float dtSeconds) override {
        tween_.tick(dtSeconds);
        if (spring_.tick(dtSeconds)) updateGeometry();
        if (themeRevision() != lastThemeRevision_) {
            lastThemeRevision_ = themeRevision();
            updateGeometry();
        }
    }

private:
    void updateGeometry() {
        const int32_t width = localFrame_.w;
        const int32_t height = localFrame_.h;
        if (width <= 40 || height <= 0) return;

        const int32_t trackX = 20;
        const int32_t trackWidth = std::max(1, width - 40);
        const int32_t centerY = height / 2;

        canvas().updateShape(trackId_, [&](Shape& shape) {
            shape.data = RRectShape{trackX, centerY - 2, trackWidth, 4, 2.0f, 2.0f};
            shape.base.fill = withOpacity(currentTheme().textPrimary, 0.16f);
            shape.base.visible = true;
        });

        const int32_t ballX = trackX + static_cast<int32_t>(std::lround(
            static_cast<float>(trackWidth) * std::max(0.0f, std::min(1.0f, progress_))));
        const float springRadius = std::max(0.0f, spring_.value);
        const float radius = 11.0f + springRadius;
        const float glowRadius = radius + 9.0f + springRadius;

        canvas().updateShape(glowId_, [&](Shape& shape) {
            shape.data = CircleShape{ballX, centerY, glowRadius};
            shape.base.fill = withOpacity(currentTheme().accent, 0.15f);
            shape.base.visible = true;
        });

        canvas().updateShape(ballId_, [&](Shape& shape) {
            shape.data = CircleShape{ballX, centerY, radius};
            shape.base.fill = currentTheme().accent;
            shape.base.stroke = currentTheme().card;
            shape.base.strokeWidth = 2;
            shape.base.visible = true;
        });
    }

    anim::Tween<float> tween_;
    anim::SpringValue spring_;
    float progress_ = 0.08f;
    Rect localFrame_{};
    uint32_t trackId_ = 0;
    uint32_t glowId_ = 0;
    uint32_t ballId_ = 0;
    uint32_t lastThemeRevision_ = 1;
};

// ===========================================================================
// EffectOrb — 旋转叶片 + 呼吸光晕（演示 Shape 级 rotation / opacity）
// ===========================================================================
class EffectOrb final : public Widget {
public:
    EffectOrb() {
        RadialGradient glow;
        glow.stops.push_back(GradientStop{0.0f, withOpacity(currentTheme().accent, 0.30f)});
        glow.stops.push_back(GradientStop{0.55f, withOpacity(currentTheme().accent, 0.10f)});
        glow.stops.push_back(GradientStop{1.0f, withOpacity(currentTheme().accent, 0.0f)});
        Shape halo;
        halo.data = CircleShape{0, 0, 1.0f};
        halo.base.fill = Paint::radial(glow);
        haloId_ = canvas().addShape(std::move(halo));

        for (int i = 0; i < 3; ++i) {
            LinearGradient blade;
            blade.from = Vec2f{0.0f, 0.0f};
            blade.to = Vec2f{1.0f, 0.0f};
            blade.stops.push_back(GradientStop{0.0f, withOpacity(currentTheme().accent, 0.05f)});
            blade.stops.push_back(GradientStop{0.55f, withOpacity(currentTheme().accent, 0.70f)});
            blade.stops.push_back(GradientStop{1.0f, currentTheme().accentHover});
            Shape shape;
            shape.data = RRectShape{0, 0, 1, 1, 1.0f, 1.0f};
            shape.base.fill = Paint::linear(blade);
            shape.base.rotation = static_cast<float>(i) * 60.0f;
            bladeIds_.push_back(canvas().addShape(std::move(shape)));
        }

        Shape core = makeCircleShape(0, 0, 1.0f, currentTheme().textOnAccent);
        coreId_ = canvas().addShape(std::move(core));
        lastThemeRevision_ = themeRevision();
    }

    Vec2i onMeasure(const Vec2i& available) const override {
        (void)available;
        return Vec2i{150, 150};
    }

    void onLayout(const Rect& frame) override {
        localFrame_ = Rect{0, 0, frame.w, frame.h};
        updateVisuals();
    }

    void onTick(float dtSeconds) override {
        angle_ += std::max(0.0f, dtSeconds) * 42.0f;
        pulse_ += std::max(0.0f, dtSeconds) * 1.6f;
        if (themeRevision() != lastThemeRevision_) {
            lastThemeRevision_ = themeRevision();
            themeDirty_ = true;
        }
        updateVisuals();
    }

private:
    static RadialGradient haloGradient() {
        RadialGradient glow;
        glow.stops.push_back(GradientStop{0.0f, withOpacity(currentTheme().accent, 0.30f)});
        glow.stops.push_back(GradientStop{0.55f, withOpacity(currentTheme().accent, 0.10f)});
        glow.stops.push_back(GradientStop{1.0f, withOpacity(currentTheme().accent, 0.0f)});
        return glow;
    }

    static LinearGradient bladeGradient() {
        LinearGradient blade;
        blade.from = Vec2f{0.0f, 0.0f};
        blade.to = Vec2f{1.0f, 0.0f};
        blade.stops.push_back(GradientStop{0.0f, withOpacity(currentTheme().accent, 0.05f)});
        blade.stops.push_back(GradientStop{0.55f, withOpacity(currentTheme().accent, 0.70f)});
        blade.stops.push_back(GradientStop{1.0f, currentTheme().accentHover});
        return blade;
    }

    void updateVisuals() {
        const int32_t width = localFrame_.w;
        const int32_t height = localFrame_.h;
        if (width <= 0 || height <= 0) return;

        const float cx = static_cast<float>(width) * 0.5f;
        const float cy = static_cast<float>(height) * 0.5f;
        const float size = static_cast<float>(std::min(width, height));
        const bool rebuildPaint = themeDirty_;
        themeDirty_ = false;

        const float breath = 0.5f + 0.5f * std::sin(pulse_);
        canvas().updateShape(haloId_, [&](Shape& shape) {
            shape.data = CircleShape{width / 2, height / 2, size * 0.48f};
            if (rebuildPaint) shape.base.fill = Paint::radial(haloGradient());
            shape.base.opacity = 0.55f + breath * 0.45f;
            shape.base.visible = true;
        });

        const float bladeW = size * 0.60f;
        const float bladeH = std::max(5.0f, size * 0.07f);
        for (size_t i = 0; i < bladeIds_.size(); ++i) {
            canvas().updateShape(bladeIds_[i], [&](Shape& shape) {
                shape.data = RRectShape{
                    static_cast<int32_t>(std::lround(cx - bladeW * 0.5f)),
                    static_cast<int32_t>(std::lround(cy - bladeH * 0.5f)),
                    static_cast<int32_t>(std::lround(bladeW)),
                    static_cast<int32_t>(std::lround(bladeH)),
                    bladeH * 0.5f, bladeH * 0.5f};
                if (rebuildPaint) shape.base.fill = Paint::linear(bladeGradient());
                shape.base.rotation = angle_ + static_cast<float>(i) * 60.0f;
                shape.base.opacity = 0.86f;
                shape.base.visible = true;
            });
        }

        canvas().updateShape(coreId_, [&](Shape& shape) {
            shape.data = CircleShape{width / 2, height / 2, std::max(4.0f, size * 0.075f)};
            shape.base.fill = currentTheme().card;
            shape.base.opacity = 1.0f;
            shape.base.visible = true;
        });
    }

    Rect localFrame_{0, 0, 150, 150};
    float angle_ = 0.0f;
    float pulse_ = 0.0f;
    bool themeDirty_ = true;
    uint32_t lastThemeRevision_ = 1;
    uint32_t haloId_ = 0;
    uint32_t coreId_ = 0;
    std::vector<uint32_t> bladeIds_;
};

// ===========================================================================
// UI 构建辅助
// ===========================================================================
struct UiState {
    TextBlock* status = nullptr;
    TextBlock* textEcho = nullptr;
    TextBlock* progressLabel = nullptr;
    ProgressBar* progressBar = nullptr;
    ProgressRing* progressRing = nullptr;
    AnimatedImage* sprite = nullptr;
    AnimatedImage* spritePingPong = nullptr;
    MotionPlayground* motion = nullptr;
    TextBox* textBox = nullptr;
    ScrollViewer* scroll = nullptr;
    int32_t clicks = 0;
    int32_t repeatCount = 0;
};

Card* addSection(StackPanel* gallery, const std::string& title, const std::string& subtitle) {
    Card* card = gallery->emplaceChild<Card>();
    card->style().direction = FlexDirection::Column;
    card->style().gap = 12;
    card->style().alignItems = AlignItems::Stretch;
    card->style().padding = Edges{20};

    card->emplaceChild<TextBlock>(title, 18);
    if (!subtitle.empty()) card->emplaceChild<TextBlock>(subtitle, 12);
    return card;
}

StackPanel* addRow(ParentWidget* parent, int32_t gap = 10) {
    StackPanel* row = parent->emplaceChild<StackPanel>(FlexDirection::Row, gap, 0);
    row->style().wrap = FlexWrap::Wrap;
    row->style().alignItems = AlignItems::Center;
    return row;
}

StackPanel* addColumn(ParentWidget* parent, int32_t width, int32_t gap = 10) {
    StackPanel* column = parent->emplaceChild<StackPanel>(FlexDirection::Column, gap, 0);
    column->style().alignItems = AlignItems::FlexStart;
    if (width > 0) column->style().width = Dimension::px(width);
    return column;
}

void fixSize(Widget& widget, int32_t width, int32_t height = -1) {
    if (width > 0) widget.style().width = Dimension::px(width);
    if (height > 0) widget.style().height = Dimension::px(height);
}

void setStatus(UiState& state, const std::string& text) {
    if (state.status) state.status->setText(text);
}

// ===========================================================================
// 构建界面
// ===========================================================================
void buildUI(DemoView& view, UiState& state, uint64_t spriteHandle, uint64_t wallpaperHandle) {
    NavigationView* nav = view.emplaceChild<NavigationView>();
    nav->style().flexGrow = 1;
    nav->setPaneTitle("Fluent Gallery");
    nav->setItems({
        "Overview", "Buttons & Selection", "Text & Input", "Collections",
        "Progress & Status", "Feedback & Media", "Motion & Sprites", "Advanced Controls",
        "Extended Controls"
    });
    nav->setSelectedIndex(0);

    ScrollViewer* scroll = new ScrollViewer();
    state.scroll = scroll;
    std::unique_ptr<ScrollViewer> scrollOwned(scroll);
    nav->setContent(std::move(scrollOwned));

    auto galleryOwned = std::make_unique<StackPanel>(FlexDirection::Column, 16, 4);
    StackPanel* gallery = galleryOwned.get();
    gallery->style().alignItems = AlignItems::Stretch;
    scroll->setContent(std::move(galleryOwned));

    const SpriteSheet sheet = SpriteSheet::grid(spriteHandle, kSpriteFrame, kSpriteFrame,
                                               kSpriteFrames, 1);
    SpriteClip idle = SpriteClip::allFrames(sheet, 14.0f, true, "idle");
    SpriteClip ping = SpriteClip::allFrames(sheet, 9.0f, true, "ping");
    ping.pingPong = true;

    std::vector<Widget*> sections;

    // ======================= Hero =======================
    AcrylicPanel* hero = gallery->emplaceChild<AcrylicPanel>();
    hero->style().direction = FlexDirection::Row;
    hero->style().alignItems = AlignItems::Center;
    hero->style().gap = 22;
    hero->style().padding = Edges{24};
    fixSize(*hero, -1, 152);

    StackPanel* heroText = addColumn(hero, 0, 4);
    heroText->emplaceChild<TextBlock>("Windows 11 Fluent Design", 28);
    heroText->emplaceChild<TextBlock>("std_widgets · Blend2D · Sprite 2D motion", 13);

    StackPanel* heroSpacer = hero->emplaceChild<StackPanel>(FlexDirection::Row);
    heroSpacer->style().flexGrow = 1;

    AnimatedImage* heroSprite = hero->emplaceChild<AnimatedImage>();
    fixSize(*heroSprite, 96, 96);
    heroSprite->setSheet(sheet);
    heroSprite->addClip(idle);
    heroSprite->play("idle");

    StackPanel* themeColumn = addColumn(hero, 160, 8);
    ToggleSwitch* darkToggle = themeColumn->emplaceChild<ToggleSwitch>("Dark mode", true);
    darkToggle->onToggled = [](bool on) {
        setThemeMode(on ? FluentTheme::Mode::Dark : FluentTheme::Mode::Light);
    };
    themeColumn->emplaceChild<TextBlock>("Fluent Motion · 60 fps tick", 11);

    // ======================= Buttons =======================
    Card* buttons = addSection(gallery, "Buttons & Selection",
        "Button / ToggleButton / RepeatButton / Hyperlink / SplitButton / CheckBox / RadioButton / ToggleSwitch / Slider / Rating");
    sections.push_back(buttons);

    StackPanel* statusRow = addRow(buttons);
    state.status = statusRow->emplaceChild<TextBlock>("Ready · Fluent theme loaded.", 12);

    StackPanel* buttonRow = addRow(buttons);
    buttonRow->emplaceChild<Button>("Standard");
    buttonRow->emplaceChild<Button>("Accent", Button::Style::Accent);
    buttonRow->emplaceChild<Button>("Subtle", Button::Style::Subtle);
    ToggleButton* toggle = buttonRow->emplaceChild<ToggleButton>("Toggle");
    toggle->setChecked(true);
    Button* disabled = buttonRow->emplaceChild<Button>("Disabled");
    disabled->setEnabled(false);

    StackPanel* actionRow = addRow(buttons);
    RepeatButton* repeat = actionRow->emplaceChild<RepeatButton>("Repeat");
    TextBlock* repeatLabel = actionRow->emplaceChild<TextBlock>("0", 13);
    repeat->onClick = [&state, repeatLabel]() {
        ++state.repeatCount;
        repeatLabel->setText(std::to_string(state.repeatCount));
    };

    HyperlinkButton* link = actionRow->emplaceChild<HyperlinkButton>("Hyperlink");
    link->onClick = [&state]() {
        ++state.clicks;
        setStatus(state, "Hyperlink clicked · " + std::to_string(state.clicks));
    };

    DropDownButton* drop = actionRow->emplaceChild<DropDownButton>("DropDown");
    drop->onDropDown = [&state]() { setStatus(state, "DropDownButton pressed."); };

    SplitButton* split = actionRow->emplaceChild<SplitButton>("Split");
    split->onPrimaryClick = [&state]() { setStatus(state, "SplitButton primary."); };
    split->onSecondaryClick = [&state]() { setStatus(state, "SplitButton secondary."); };

    StackPanel* selectionRow = addRow(buttons);
    CheckBox* check = selectionRow->emplaceChild<CheckBox>("CheckBox", true);
    check->onCheckedChanged = [&state](bool on) {
        setStatus(state, on ? "CheckBox checked." : "CheckBox unchecked.");
    };

    RadioButton* radioA = selectionRow->emplaceChild<RadioButton>("Radio A", true);
    RadioButton* radioB = selectionRow->emplaceChild<RadioButton>("Radio B");
    RadioButton* radioC = selectionRow->emplaceChild<RadioButton>("Radio C");
    radioA->setGroup("demo");
    radioB->setGroup("demo");
    radioC->setGroup("demo");
    auto radioStatus = [&state](const char* name) {
        setStatus(state, std::string("Radio selected: ") + name);
    };
    radioA->onCheckedChanged = [radioStatus](bool on) { if (on) radioStatus("A"); };
    radioB->onCheckedChanged = [radioStatus](bool on) { if (on) radioStatus("B"); };
    radioC->onCheckedChanged = [radioStatus](bool on) { if (on) radioStatus("C"); };

    ToggleSwitch* wifi = selectionRow->emplaceChild<ToggleSwitch>("Wi-Fi", true);
    wifi->onToggled = [&state](bool on) {
        setStatus(state, on ? "Wi-Fi on." : "Wi-Fi off.");
    };

    StackPanel* sliderRow = addRow(buttons);
    Slider* demSlider = sliderRow->emplaceChild<Slider>(0.35f);
    fixSize(*demSlider, 240);
    TextBlock* sliderLabel = sliderRow->emplaceChild<TextBlock>("35%", 13);
    ProgressBar* localProgress = sliderRow->emplaceChild<ProgressBar>(0.35f);
    fixSize(*localProgress, 220);
    demSlider->onValueChanged = [sliderLabel, localProgress](float value) {
        sliderLabel->setText(std::to_string(static_cast<int32_t>(std::lround(value * 100.0f))) + "%");
        localProgress->setProgress(value);
    };

    StackPanel* ratingRow = addRow(buttons);
    RatingControl* rating = ratingRow->emplaceChild<RatingControl>(3.0f);
    TextBlock* ratingLabel = ratingRow->emplaceChild<TextBlock>("Rating: 3", 13);
    rating->onValueChanged = [ratingLabel](float value) {
        ratingLabel->setText("Rating: " + std::to_string(static_cast<int32_t>(std::lround(value))));
    };
    ratingRow->emplaceChild<InfoBadge>(3, InfoBadge::Kind::Attention);
    ratingRow->emplaceChild<InfoBadge>(12, InfoBadge::Kind::Informational);
    ratingRow->emplaceChild<InfoBadge>(-1, InfoBadge::Kind::Success);
    ratingRow->emplaceChild<InfoBadge>(-1, InfoBadge::Kind::Critical);

    // ======================= Text & Input =======================
    Card* input = addSection(gallery, "Text & Input",
        "TextBox / PasswordBox / NumberBox / ComboBox — 焦点下划线、光标闪烁、浮出列表");
    sections.push_back(input);

    StackPanel* inputRow = addRow(input);
    state.textBox = inputRow->emplaceChild<TextBox>("Type something…");
    fixSize(*state.textBox, 240);
    state.textBox->onTextChanged = [&state](const std::string& text) {
        if (state.textEcho) state.textEcho->setText(text.empty() ? "Text: <empty>" : "Text: " + text);
    };

    PasswordBox* password = inputRow->emplaceChild<PasswordBox>("Password");
    fixSize(*password, 210);

    NumberBox* number = inputRow->emplaceChild<NumberBox>(42.0, 0.0, 100.0, 5.0);
    fixSize(*number, 150);
    number->onValueChanged = [&state](double value) {
        setStatus(state, "NumberBox: " + std::to_string(static_cast<int32_t>(value)));
    };

    ComboBox* combo = inputRow->emplaceChild<ComboBox>(
        std::vector<std::string>{"Windows 11", "Fluent Design", "WinUI 3", "Blend2D"});
    fixSize(*combo, 210);
    combo->setSelectedIndex(1);
    combo->onSelectionChanged = [&state](int32_t index) {
        setStatus(state, "ComboBox selected index: " + std::to_string(index));
    };

    StackPanel* inputRow2 = addRow(input);
    state.textEcho = inputRow2->emplaceChild<TextBlock>("Text: <empty>", 12);
    Button* setTextButton = inputRow2->emplaceChild<Button>("Set sample", Button::Style::Subtle);
    setTextButton->onClick = [&state]() {
        if (state.textBox) state.textBox->setText("Hello Fluent Design");
    };
    Button* focusButton = inputRow2->emplaceChild<Button>("Focus hint", Button::Style::Subtle);
    focusButton->onClick = [&state]() {
        setStatus(state, "Click into TextBox / press Tab to move focus.");
    };
    inputRow2->emplaceChild<TextBlock>("UTF-8 · Backspace · Delete · Enter submit", 11);

    // ======================= Collections =======================
    Card* collections = addSection(gallery, "Collections & Navigation",
        "ListView / GridView / TabView / BreadcrumbBar / MenuFlyout / CommandBar / NavigationView");
    sections.push_back(collections);

    CommandBar* commandBar = collections->emplaceChild<CommandBar>();
    commandBar->addCommand("Back", [&state]() { setStatus(state, "CommandBar: Back"); });
    commandBar->addCommand("Forward", [&state]() { setStatus(state, "CommandBar: Forward"); });
    commandBar->addCommand("Refresh", [&state]() { setStatus(state, "CommandBar: Refresh"); });

    MenuBar* menuBar = collections->emplaceChild<MenuBar>();
    menuBar->setItems({"File", "Edit", "View", "Help"});
    menuBar->onItemClicked = [&state](int32_t index) {
        setStatus(state, "MenuBar item: " + std::to_string(index));
    };

    StackPanel* collectionRow = addRow(collections, 14);
    collectionRow->style().alignItems = AlignItems::FlexStart;

    StackPanel* listColumn = addColumn(collectionRow, 264, 8);
    listColumn->emplaceChild<TextBlock>("ListView", 13);
    ListView* list = listColumn->emplaceChild<ListView>();
    fixSize(*list, 260, 236);
    list->setItems({"Design tokens", "Fluent typography", "Rounded corners",
                    "Mica backdrop", "Navigation", "Sprite animation",
                    "Tween & Spring", "Accessibility"});
    list->setSelectedIndex(1);
    list->onSelectionChanged = [&state](int32_t index) {
        setStatus(state, "ListView row: " + std::to_string(index));
    };

    StackPanel* gridColumn = addColumn(collectionRow, 352, 8);
    gridColumn->emplaceChild<TextBlock>("GridView", 13);
    GridView* grid = gridColumn->emplaceChild<GridView>();
    fixSize(*grid, 344, 268);
    grid->setItems({"Buttons", "Inputs", "Progress", "Media", "Motion", "Menus", "Cards", "Sprites"});
    grid->setSelectedIndex(0);
    grid->onSelectionChanged = [&state](int32_t index) {
        setStatus(state, "GridView tile: " + std::to_string(index));
    };

    StackPanel* miscColumn = addColumn(collectionRow, 300, 10);
    TabView* tabs = miscColumn->emplaceChild<TabView>();
    tabs->setTabs({"Design", "Motion", "Sprite"});
    tabs->setSelectedIndex(1);
    tabs->onTabChanged = [&state](int32_t index) {
        setStatus(state, "TabView tab: " + std::to_string(index));
    };

    BreadcrumbBar* crumbs = miscColumn->emplaceChild<BreadcrumbBar>();
    crumbs->setItems({"Home", "Controls", "Collections"});
    crumbs->setSelectedIndex(2);
    crumbs->onItemClicked = [&state](int32_t index) {
        setStatus(state, "Breadcrumb item: " + std::to_string(index));
    };

    MenuFlyout* flyout = miscColumn->emplaceChild<MenuFlyout>();
    flyout->addItem("New window", [&state]() { setStatus(state, "MenuFlyout: New window"); });
    flyout->addItem("Open file", [&state]() { setStatus(state, "MenuFlyout: Open file"); });
    flyout->addSeparator();
    flyout->addItem("Save", [&state]() { setStatus(state, "MenuFlyout: Save"); });
    flyout->addItem("Close", [&state]() { setStatus(state, "MenuFlyout: Close"); });

    // ======================= Progress =======================
    Card* progressCard = addSection(gallery, "Progress & Status",
        "ProgressBar / ProgressRing / InfoBadge / InfoBar — 确定进度与不确定动画");
    sections.push_back(progressCard);

    StackPanel* progressRow = addRow(progressCard);
    Slider* master = progressRow->emplaceChild<Slider>(0.62f);
    fixSize(*master, 260);
    state.progressLabel = progressRow->emplaceChild<TextBlock>("62%", 13);
    state.progressBar = progressRow->emplaceChild<ProgressBar>(0.62f);
    fixSize(*state.progressBar, 240);
    state.progressRing = progressRow->emplaceChild<ProgressRing>(0.62f);
    fixSize(*state.progressRing, 44, 44);

    master->onValueChanged = [&state](float value) {
        if (state.progressLabel) {
            state.progressLabel->setText(
                std::to_string(static_cast<int32_t>(std::lround(value * 100.0f))) + "%");
        }
        if (state.progressBar) state.progressBar->setProgress(value);
        if (state.progressRing) state.progressRing->setProgress(value);
    };

    StackPanel* indeterminateRow = addRow(progressCard);
    indeterminateRow->emplaceChild<TextBlock>("Indeterminate", 12);
    ProgressBar* indeterminate = indeterminateRow->emplaceChild<ProgressBar>(0.0f, true);
    fixSize(*indeterminate, 220);
    ProgressRing* indeterminateRing = indeterminateRow->emplaceChild<ProgressRing>(0.0f, true);
    fixSize(*indeterminateRing, 36, 36);

    InfoBar* info = progressCard->emplaceChild<InfoBar>(
        "Sync complete", "Your Fluent settings are up to date.",
        InfoBar::Severity::Success);
    info->onClose = [&state]() { setStatus(state, "InfoBar closed."); };

    InfoBar* warning = progressCard->emplaceChild<InfoBar>(
        "Preview channel", "Sprite animation runs on the UI tick loop.",
        InfoBar::Severity::Warning);
    warning->setClosable(false);

    // ======================= Feedback & Media =======================
    Card* feedback = addSection(gallery, "Feedback & Media",
        "TeachingTip / ToolTip / ContentDialog / Expander / PersonPicture / ImageView");
    sections.push_back(feedback);

    StackPanel* feedbackRow = addRow(feedback);
    feedbackRow->style().alignItems = AlignItems::FlexStart;

    StackPanel* expanderColumn = addColumn(feedbackRow, 340, 10);
    Expander* expander = expanderColumn->emplaceChild<Expander>("Advanced options", false);
    fixSize(*expander, 334);
    auto contentOwned = std::make_unique<StackPanel>(FlexDirection::Column, 8, 0);
    StackPanel* expanderContent = contentOwned.get();
    expanderContent->emplaceChild<TextBlock>("Expander 内容区由 Flexbox 自动排版。", 12);
    expanderContent->emplaceChild<ToggleSwitch>("Enable telemetry", false);
    expander->setContent(std::move(contentOwned));

    TeachingTip* tip = expanderColumn->emplaceChild<TeachingTip>(
        "Sprite 动画", "SpriteSheet + SpriteAnimator 驱动 ImageShape。",
        true);
    fixSize(*tip, 334);
    tip->onClose = [&state]() { setStatus(state, "TeachingTip closed."); };

    ToolTip* tooltip = expanderColumn->emplaceChild<ToolTip>("ToolTip · Fluent overlay");
    fixSize(*tooltip, 220);

    StackPanel* mediaColumn = addColumn(feedbackRow, 270, 10);
    StackPanel* peopleRow = addRow(mediaColumn);
    PersonPicture* ada = peopleRow->emplaceChild<PersonPicture>("Ada Lovelace", 48);
    ada->setStatus(PersonPicture::Status::Online);
    PersonPicture* grace = peopleRow->emplaceChild<PersonPicture>("Grace Hopper", 48);
    grace->setStatus(PersonPicture::Status::Away);
    peopleRow->emplaceChild<TextBlock>("Ada · Grace", 12);

    ImageView* image = mediaColumn->emplaceChild<ImageView>(wallpaperHandle, 420, 240);
    fixSize(*image, 248, 142);
    image->setStretch(ImageView::Stretch::Uniform);
    mediaColumn->emplaceChild<TextBlock>("ImageView · 程序化壁纸", 11);

    StackPanel* dialogColumn = addColumn(feedbackRow, 420, 10);
    ContentDialog* dialog = dialogColumn->emplaceChild<ContentDialog>(
        "Create a new project?", "Choose a Fluent template and start building.",
        "Create", "Cancel");
    fixSize(*dialog, 404, 208);
    dialog->onPrimary = [&state]() { setStatus(state, "ContentDialog primary."); };
    dialog->onSecondary = [&state]() { setStatus(state, "ContentDialog secondary."); };
    dialog->onClosed = [&state]() { setStatus(state, "ContentDialog closed."); };

    // ======================= Motion & Sprites =======================
    Card* motionCard = addSection(gallery, "Motion & Sprites",
        "SpriteAnimator 帧动画 / Tween OutBack / SpringValue 弹簧 — 全部由 View::tick(dt) 驱动");
    sections.push_back(motionCard);

    StackPanel* motionRow = addRow(motionCard);
    motionRow->style().alignItems = AlignItems::FlexStart;

    StackPanel* spriteColumn = addColumn(motionRow, 230, 10);
    spriteColumn->emplaceChild<TextBlock>("Sprite · 12 frames · 14 fps", 13);
    state.sprite = spriteColumn->emplaceChild<AnimatedImage>();
    fixSize(*state.sprite, 128, 128);
    state.sprite->setSheet(sheet);
    state.sprite->addClip(idle);
    state.sprite->play("idle");

    StackPanel* spriteButtons = addRow(spriteColumn, 6);
    Button* playButton = spriteButtons->emplaceChild<Button>("Play", Button::Style::Subtle);
    Button* pauseButton = spriteButtons->emplaceChild<Button>("Pause", Button::Style::Subtle);
    Button* stopButton = spriteButtons->emplaceChild<Button>("Stop", Button::Style::Subtle);
    playButton->onClick = [&state]() { if (state.sprite) state.sprite->play("idle"); };
    pauseButton->onClick = [&state]() { if (state.sprite) state.sprite->pause(); };
    stopButton->onClick = [&state]() { if (state.sprite) state.sprite->stop(); };

    Slider* speed = spriteColumn->emplaceChild<Slider>(1.0f, 0.2f, 2.5f);
    fixSize(*speed, 190);
    speed->onValueChanged = [&state](float value) {
        if (state.sprite) state.sprite->setSpeed(value);
        if (state.spritePingPong) state.spritePingPong->setSpeed(value * 0.8f);
    };

    StackPanel* pingColumn = addColumn(motionRow, 170, 10);
    pingColumn->emplaceChild<TextBlock>("PingPong · 9 fps", 13);
    state.spritePingPong = pingColumn->emplaceChild<AnimatedImage>();
    fixSize(*state.spritePingPong, 128, 128);
    state.spritePingPong->setSheet(sheet);
    state.spritePingPong->addClip(ping);
    state.spritePingPong->play("ping");
    state.spritePingPong->setSpeed(0.8f);

    StackPanel* motionColumn = addColumn(motionRow, 380, 10);
    motionColumn->style().flexGrow = 1;
    motionColumn->emplaceChild<TextBlock>("Tween OutBack + SpringValue", 13);
    state.motion = motionColumn->emplaceChild<MotionPlayground>();
    fixSize(*state.motion, 370, 120);
    StackPanel* motionButtons = addRow(motionColumn, 8);
    Button* tweenButton = motionButtons->emplaceChild<Button>("Play tween", Button::Style::Accent);
    Button* kickButton = motionButtons->emplaceChild<Button>("Kick spring", Button::Style::Subtle);
    tweenButton->onClick = [&state]() { if (state.motion) state.motion->play(); };
    kickButton->onClick = [&state]() { if (state.motion) state.motion->kick(); };
    motionButtons->emplaceChild<TextBlock>("View::tick(dt) → onTick → 子 Canvas 标脏", 11);

    // ======================= Advanced Controls =======================
    Card* advanced = addSection(gallery, "Advanced Controls",
        "TreeView / CalendarView / ColorPicker / PipsPager");
    sections.push_back(advanced);

    StackPanel* advancedRow = addRow(advanced, 16);
    advancedRow->style().alignItems = AlignItems::FlexStart;

    StackPanel* treeColumn = addColumn(advancedRow, 270, 8);
    treeColumn->emplaceChild<TextBlock>("TreeView", 13);
    TreeView* tree = treeColumn->emplaceChild<TreeView>();
    fixSize(*tree, 264, 240);
    tree->setNodes({
        {"Documents", 0, true, true},
        {"Projects", 1, true, true},
        {"ModernUI", 2, false, false},
        {"Fluent Widgets", 2, false, false},
        {"Design assets", 1, true, false},
        {"Icons", 2, false, false},
        {"Media", 1, true, true},
        {"Sprites", 2, false, false},
        {"Wallpapers", 2, false, false},
        {"Music", 0, true, false},
        {"Playlists", 1, false, false}
    });
    tree->setSelectedIndex(2);
    tree->onSelectionChanged = [&state](int32_t index) {
        setStatus(state, "TreeView node: " + std::to_string(index));
    };

    StackPanel* calendarColumn = addColumn(advancedRow, 300, 8);
    calendarColumn->emplaceChild<TextBlock>("CalendarView", 13);
    CalendarView* calendar = calendarColumn->emplaceChild<CalendarView>(2026, 10, 26);
    fixSize(*calendar, 290, 292);
    calendar->onDateChanged = [&state](int32_t year, int32_t month, int32_t day) {
        setStatus(state, "Calendar: " + std::to_string(year) + "-" +
                         std::to_string(month) + "-" + std::to_string(day));
    };

    StackPanel* paletteColumn = addColumn(advancedRow, 320, 10);
    paletteColumn->emplaceChild<TextBlock>("ColorPicker", 13);
    ColorPicker* palette = paletteColumn->emplaceChild<ColorPicker>();
    fixSize(*palette, 296);
    palette->setSelectedIndex(2);
    palette->onColorChanged = [&state](Color color) {
        setStatus(state, "ColorPicker: #" + std::to_string(color.r) +
                         std::to_string(color.g) + std::to_string(color.b));
    };

    paletteColumn->emplaceChild<TextBlock>("PipsPager", 13);
    PipsPager* pips = paletteColumn->emplaceChild<PipsPager>(5, 1);
    pips->onSelectedPageChanged = [&state](int32_t page) {
        setStatus(state, "PipsPager page: " + std::to_string(page));
    };

    // ======================= Extended Controls =======================
    Card* extended = addSection(gallery, "Extended Controls",
        "SegmentedControl / RangeSlider / Chip / RadialGauge / Sparkline / Skeleton / Shape rotation + opacity");
    sections.push_back(extended);

    StackPanel* segmentRow = addRow(extended);
    SegmentedControl* segmented = segmentRow->emplaceChild<SegmentedControl>(
        std::vector<std::string>{"Overview", "Analytics", "Settings"});
    fixSize(*segmented, 342);
    TextBlock* segmentLabel = segmentRow->emplaceChild<TextBlock>("Segment: Overview", 12);
    segmented->onSelectionChanged = [&state, segmented, segmentLabel](int32_t index) {
        segmentLabel->setText("Segment: " + segmented->items()[static_cast<size_t>(index)]);
        setStatus(state, "SegmentedControl index: " + std::to_string(index));
    };

    StackPanel* rangeRow = addRow(extended);
    RangeSlider* range = rangeRow->emplaceChild<RangeSlider>(0.24f, 0.68f, 0.0f, 1.0f);
    fixSize(*range, 280);
    TextBlock* rangeLabel = rangeRow->emplaceChild<TextBlock>("Range: 24 – 68", 12);
    range->onRangeChanged = [&state, rangeLabel](float low, float high) {
        rangeLabel->setText("Range: " + std::to_string(static_cast<int32_t>(std::lround(low * 100.0f))) +
                            " – " + std::to_string(static_cast<int32_t>(std::lround(high * 100.0f))));
        setStatus(state, "RangeSlider changed.");
    };

    StackPanel* chipRow = addRow(extended, 8);
    Chip* chipDesign = chipRow->emplaceChild<Chip>("Design", true);
    Chip* chipMotion = chipRow->emplaceChild<Chip>("Motion");
    Chip* chipRemovable = chipRow->emplaceChild<Chip>("Removable");
    chipRemovable->setClosable(true);
    chipDesign->onSelectedChanged = [&state](bool on) {
        setStatus(state, on ? "Chip selected." : "Chip cleared.");
    };
    chipMotion->onSelectedChanged = [&state](bool on) {
        setStatus(state, on ? "Motion chip on." : "Motion chip off.");
    };
    chipRemovable->onClose = [&state]() { setStatus(state, "Chip close requested."); };

    StackPanel* vizRow = addRow(extended, 18);
    vizRow->style().alignItems = AlignItems::FlexStart;

    StackPanel* gaugeColumn = addColumn(vizRow, 176, 8);
    gaugeColumn->emplaceChild<TextBlock>("RadialGauge", 13);
    RadialGauge* gauge = gaugeColumn->emplaceChild<RadialGauge>(0.72f, "CPU");
    fixSize(*gauge, 160, 160);
    Slider* gaugeSlider = gaugeColumn->emplaceChild<Slider>(0.72f);
    fixSize(*gaugeSlider, 160);
    gaugeSlider->onValueChanged = [gauge](float value) { gauge->setValue(value); };

    StackPanel* sparkColumn = addColumn(vizRow, 316, 8);
    sparkColumn->emplaceChild<TextBlock>("Sparkline", 13);
    Sparkline* spark = sparkColumn->emplaceChild<Sparkline>(
        std::vector<float>{18.0f, 22.0f, 19.0f, 28.0f, 24.0f, 36.0f,
                           31.0f, 44.0f, 39.0f, 52.0f, 47.0f, 58.0f});
    fixSize(*spark, 308, 78);
    StackPanel* sparkActions = addRow(sparkColumn, 8);
    Button* resample = sparkActions->emplaceChild<Button>("Resample", Button::Style::Subtle);
    sparkActions->emplaceChild<TextBlock>("渐变面积 · 数据过渡", 11);
    resample->onClick = [spark]() {
        static int32_t sample = 0;
        ++sample;
        if (sample % 2 == 1) {
            spark->setValues({52.0f, 46.0f, 49.0f, 38.0f, 42.0f, 30.0f,
                              34.0f, 24.0f, 28.0f, 19.0f, 23.0f, 16.0f});
        } else {
            spark->setValues({18.0f, 22.0f, 19.0f, 28.0f, 24.0f, 36.0f,
                              31.0f, 44.0f, 39.0f, 52.0f, 47.0f, 58.0f});
        }
    };

    StackPanel* skeletonColumn = addColumn(vizRow, 254, 8);
    skeletonColumn->emplaceChild<TextBlock>("Skeleton shimmer", 13);
    StackPanel* skeletonRow = addRow(skeletonColumn, 12);
    Skeleton* avatar = skeletonRow->emplaceChild<Skeleton>(0.0f, true);
    fixSize(*avatar, 44, 44);
    StackPanel* skeletonLines = addColumn(skeletonRow, 170, 8);
    Skeleton* lineA = skeletonLines->emplaceChild<Skeleton>(5.0f, false);
    fixSize(*lineA, 170, 12);
    Skeleton* lineB = skeletonLines->emplaceChild<Skeleton>(5.0f, false);
    fixSize(*lineB, 132, 12);
    Skeleton* lineC = skeletonLines->emplaceChild<Skeleton>(5.0f, false);
    fixSize(*lineC, 96, 12);
    skeletonColumn->emplaceChild<TextBlock>("流光亮带由单个几何体的渐变驱动", 11);

    StackPanel* orbColumn = addColumn(vizRow, 170, 8);
    orbColumn->emplaceChild<TextBlock>("Rotation + opacity", 13);
    EffectOrb* orb = orbColumn->emplaceChild<EffectOrb>();
    fixSize(*orb, 156, 156);
    orbColumn->emplaceChild<TextBlock>("Shape 级变换 / 透明度", 11);

    // ======================= 导航联动 =======================
    // 先完成一次排版，这样每个 Section 的 frame 已确定；导航点击时直接滚动到对应位置。
    view.layoutNow();
    nav->onSelectionChanged = [scroll, sections](int32_t index) {
        if (index <= 0 || index - 1 >= static_cast<int32_t>(sections.size())) {
            scroll->scrollTo(0.0f);
            return;
        }
        Widget* section = sections[static_cast<size_t>(index - 1)];
        scroll->scrollTo(static_cast<float>(section->frame().y) - 4.0f);
    };
}

} // namespace

// ===========================================================================
int main(int argc, char** argv) {
    // 调试辅助：--scroll N 定位内容偏移；--light 启动后切到亮色主题（验证 themeRevision 重绘）
    float initialScroll = -1.0f;
    bool forceLight = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--scroll" && i + 1 < argc) {
            initialScroll = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--light") {
            forceLight = true;
        }
    }

    UiState state;

    // 顶层 API：窗口 / 渲染后端 / 字体 / 文本度量 / 消息循环由 App 统一接管。
    // 自定义根 View（Mica 背景动画）通过 viewFactory 注入。
    App app(L"ModernUI — Windows 11 Fluent Design", kWindowWidth, kWindowHeight);
    app.background(currentTheme().background)
       .viewFactory([](int32_t width, int32_t height) {
           return std::unique_ptr<View>(new DemoView(width, height));
       })
       .content([&](View& view) {
           // 渲染后端此时已就绪：Sprite 图集 / 壁纸纹理在这里创建
           const uint64_t spriteHandle = createSpriteAtlas(app.renderer());
           const uint64_t wallpaperHandle = createWallpaper(app.renderer());
           buildUI(static_cast<DemoView&>(view), state, spriteHandle, wallpaperHandle);

           if (initialScroll >= 0.0f && state.scroll) {
               state.scroll->scrollTo(initialScroll);
           }
           if (forceLight) setThemeMode(FluentTheme::Mode::Light);
       });

    return app.run();
}
