// ============================================================================
// Toolbar.cpp
// ============================================================================

#include "Toolbar.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

using modern_ui::CircleShape;
using modern_ui::Color;
using modern_ui::GroupData;
using modern_ui::LineShape;
using modern_ui::Paint;
using modern_ui::PathShape;
using modern_ui::Rect;
using modern_ui::RRectShape;
using modern_ui::Shape;
using modern_ui::SvgShape;
using modern_ui::TextShape;
using modern_ui::Vec2f;
using modern_ui::Vec2i;

namespace annotation {

namespace {

// 基准尺寸（未缩放）；最终 × uiScale（默认 1.5）
constexpr int32_t kCollapsedWidth = 68;
constexpr int32_t kExpandedWidth = 304;
constexpr int32_t kBaseHeight = 706;
constexpr int32_t kIconColumn = 68;
constexpr int32_t kButtonSize = 44;
constexpr int32_t kRowStep = 46;

const Color kTextPrimary{240, 243, 248, 255};
const Color kTextSecondary{170, 176, 190, 255};
const Color kTextTertiary{122, 128, 142, 255};
const Color kAccent{96, 205, 255, 255};

const char* const kIconFiles[9] = {
    "brush.svg",
    "Highlighter.svg",
    "editor-eraser.svg",
    "laser.svg",
    "085-pointer.svg",
    "screenshot.svg",
    "scan_type.svg",
    "lights.svg",
    "scan.svg",
};

// 0=trash, 1=add, 2=back, 3=forward, 4=quit
const char* const kUtilityFiles[5] = {
    "trash.svg",
    "add.svg",
    "back.svg",
    "forward.svg",
    "quit.svg",
};

const char* const kToolNames[8] = {
    "钢笔", "荧光笔", "橡皮擦", "激光笔", "指针模式", "截图", "文字识别", "AI 助手",
};

const std::array<Color, 10>& palette() {
    static const std::array<Color, 10> colors = {
        Color{255, 214, 64, 255},
        Color{255, 138, 61, 255},
        Color{255, 92, 92, 255},
        Color{255, 105, 180, 255},
        Color{178, 112, 255, 255},
        Color{86, 156, 255, 255},
        Color{84, 222, 255, 255},
        Color{105, 226, 138, 255},
        Color{244, 246, 250, 255},
        Color{38, 42, 52, 255},
    };
    return colors;
}

float clampf(float value, float lo, float hi) {
    return std::max(lo, std::min(hi, value));
}

float easeOutCubic(float t) {
    t = clampf(t, 0.0f, 1.0f);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

bool animate(float& value, float target, float rate, float dt, float epsilon = 0.003f) {
    if (std::fabs(value - target) <= 1.0e-6f) {
        value = target;
        return false;
    }
    const float alpha = 1.0f - std::exp(-std::max(0.0f, rate) * std::max(0.0f, dt));
    const float next = value + (target - value) * alpha;
    const bool changed = std::fabs(next - value) > 1.0e-6f;
    value = (std::fabs(next - target) < epsilon) ? target : next;
    return changed;
}

Color alphaOf(Color color, uint8_t alpha) {
    color.a = alpha;
    return color;
}

std::string colorHex(const Color& color) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X",
                  static_cast<int>(color.r), static_cast<int>(color.g),
                  static_cast<int>(color.b));
    return std::string(buffer);
}

// 把 SVG 内所有 path 的 fill 替换成指定颜色（图标原始颜色是黑/灰）。
std::string tintSvg(const std::string& markup, const Color& color) {
    if (markup.empty()) return markup;

    const std::string fillAttr = "fill=\"" + colorHex(color) + "\"";
    std::string result;
    result.reserve(markup.size() + 64);

    size_t position = 0;
    while (true) {
        const size_t pathStart = markup.find("<path", position);
        if (pathStart == std::string::npos) {
            result.append(markup, position, std::string::npos);
            break;
        }

        const size_t tagEnd = markup.find('>', pathStart);
        if (tagEnd == std::string::npos) {
            result.append(markup, position, std::string::npos);
            break;
        }

        result.append(markup, position, pathStart - position);
        std::string tag = markup.substr(pathStart, tagEnd - pathStart + 1);

        const size_t existing = tag.find("fill=\"");
        if (existing != std::string::npos) {
            const size_t valueStart = existing + 6;
            const size_t valueEnd = tag.find('"', valueStart);
            if (valueEnd != std::string::npos) {
                tag.replace(existing, valueEnd - existing + 1, fillAttr);
            }
        } else {
            const size_t insertAt = (tag.size() > 2 && tag[tag.size() - 2] == '/')
                ? tag.size() - 2
                : tag.size() - 1;
            tag.insert(insertAt, " " + fillAttr);
        }

        result += tag;
        position = tagEnd + 1;
    }

    return result;
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string joinPath(const std::string& directory, const char* file) {
    if (directory.empty()) return file;
    const char last = directory.back();
    if (last == '/' || last == '\\') return directory + file;
    return directory + "/" + file;
}

} // namespace

// ============================================================================
// 构造 / 布局
// ============================================================================

AnnotationToolbar::AnnotationToolbar() {
    clip(true);
    canvas().setBackgroundColor(Color{0, 0, 0, 0});
    groupId_ = canvas().group().add();
}

void AnnotationToolbar::loadIcons(const std::string& directory) {
    for (int32_t i = 0; i < 9; ++i) {
        const std::string markup = readFile(joinPath(directory, kIconFiles[i]));
        iconWhite_[static_cast<size_t>(i)] =
            tintSvg(markup, Color{238, 242, 250, 255});
        iconAccent_[static_cast<size_t>(i)] = tintSvg(markup, kAccent);
        iconDim_[static_cast<size_t>(i)] =
            tintSvg(markup, Color{122, 128, 142, 255});
    }

    for (int32_t i = 0; i < 5; ++i) {
        const std::string markup = readFile(joinPath(directory, kUtilityFiles[i]));
        utilityWhite_[static_cast<size_t>(i)] =
            tintSvg(markup, Color{238, 242, 250, 255});
        utilityAccent_[static_cast<size_t>(i)] = tintSvg(markup, kAccent);
        utilityDim_[static_cast<size_t>(i)] =
            tintSvg(markup, Color{122, 128, 142, 255});
    }

    const std::string settingsMarkup = readFile(joinPath(directory, "icon-config.svg"));
    settingsIconWhite_ = tintSvg(settingsMarkup, Color{238, 242, 250, 255});
    settingsIconAccent_ = tintSvg(settingsMarkup, kAccent);
    settingsIconDim_ = tintSvg(settingsMarkup, Color{122, 128, 142, 255});

    visualDirty_ = true;
}

void AnnotationToolbar::setScreenSize(int32_t width, int32_t height) {
    screenWidth_ = std::max(1, width);
    screenHeight_ = std::max(1, height);
    updateFrameGeometry();
    visualDirty_ = true;
}

int32_t AnnotationToolbar::scaledInt(float value) const {
    return static_cast<int32_t>(std::lround(value * uiScale_));
}

Vec2i AnnotationToolbar::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{scaledInt(kCollapsedWidth), scaledInt(kBaseHeight)};
}

void AnnotationToolbar::onLayout(const Rect& frame) {
    const int32_t w = std::max(1, frame.w);
    const int32_t h = std::max(1, frame.h);
    if (canvas().width() != w || canvas().height() != h) {
        canvas().resize(w, h);
    }
    layoutValid_ = true;
    rebuildVisuals();
}

void AnnotationToolbar::updateFrameGeometry() {
    // 用户可调 80%–220%；屏幕高度不足时按可用高度等比收缩，避免底部按钮被裁掉。
    const float desiredScale =
        (draggingSlider_ == 4) ? appliedScale_ : uiScaleSetting_;
    const float available = std::max(320.0f, static_cast<float>(screenHeight_) - 24.0f);
    uiScale_ = std::min(desiredScale, available / static_cast<float>(kBaseHeight));
    uiScale_ = std::max(0.72f, uiScale_);
    if (draggingSlider_ != 4) appliedScale_ = uiScale_;

    const int32_t targetWidth = scaledInt(
        static_cast<float>(kCollapsedWidth) +
        static_cast<float>(kExpandedWidth - kCollapsedWidth) *
            easeOutCubic(expandProgress_));
    const int32_t height = scaledInt(kBaseHeight);
    const int32_t x = screenWidth_ - scaledInt(18.0f) - targetWidth;
    const int32_t y = std::max(scaledInt(8.0f), (screenHeight_ - height) / 2);

    if (frame().w != targetWidth || frame().h != height) {
        size(targetWidth, height);
    }
    absolute(x, y);
}

void AnnotationToolbar::onStateChanged() {
    if (!hovered()) {
        hoverAction_ = Action::None;
        hoverColor_ = -1;
        hoverProgress_ = 0.0f;
        visualDirty_ = true;
    }
}

void AnnotationToolbar::onTick(float dtSeconds) {
    bool changed = false;

    const float targetExpand = (panelMode_ == PanelMode::None) ? 0.0f : 1.0f;
    changed |= animate(expandProgress_, targetExpand, 16.0f, dtSeconds, 0.002f);

    const float targetHover =
        (hoverAction_ != Action::None || hoverColor_ >= 0) ? 1.0f : 0.0f;
    if (draggingSlider_ < 0) {
        changed |= animate(hoverProgress_, targetHover, 22.0f, dtSeconds, 0.004f);
    } else {
        hoverProgress_ = 1.0f;
    }

    updateFrameGeometry();

    if (changed || visualDirty_) {
        rebuildVisuals();
        visualDirty_ = false;
    }
}

// ============================================================================
// 状态同步
// ============================================================================

void AnnotationToolbar::setTool(Tool tool) {
    if (tool_ == tool) return;
    tool_ = tool;
    setPanelForTool();
    visualDirty_ = true;
}

void AnnotationToolbar::setColor(const Color& color) {
    if (color_.r == color.r && color_.g == color.g &&
        color_.b == color.b && color_.a == color.a) {
        return;
    }
    color_ = color;
    visualDirty_ = true;
}

void AnnotationToolbar::setWidth(float width) {
    const float clamped = clampf(width, 1.0f, 40.0f);
    if (std::fabs(clamped - width_) < 0.05f) return;
    width_ = clamped;
    visualDirty_ = true;
}

void AnnotationToolbar::setInkParameters(float smoothing01, float penTip01, float taper01) {
    const float s = clampf(smoothing01, 0.0f, 1.0f);
    const float p = clampf(penTip01, 0.0f, 1.0f);
    const float t = clampf(taper01, 0.0f, 1.0f);
    if (std::fabs(s - smoothing01_) < 0.001f &&
        std::fabs(p - penTip01_) < 0.001f &&
        std::fabs(t - taper01_) < 0.001f) {
        return;
    }
    smoothing01_ = s;
    penTip01_ = p;
    taper01_ = t;
    visualDirty_ = true;
}

void AnnotationToolbar::setEraserSize(float diameter) {
    const float clamped = clampf(diameter, 8.0f, 160.0f);
    if (std::fabs(clamped - eraserSize_) < 0.05f) return;
    eraserSize_ = clamped;
    visualDirty_ = true;
}

void AnnotationToolbar::setCardScale(float percent) {
    const float clamped = clampf(percent, 60.0f, 200.0f);
    if (std::fabs(clamped - cardScalePercent_) < 0.05f) return;
    cardScalePercent_ = clamped;
    visualDirty_ = true;
}

void AnnotationToolbar::setPageInfo(int32_t index, int32_t count) {
    const int32_t safeIndex = std::max(0, index);
    const int32_t safeCount = std::max(1, count);
    if (pageIndex_ == safeIndex && pageCount_ == safeCount) return;
    pageIndex_ = safeIndex;
    pageCount_ = safeCount;
    visualDirty_ = true;
}

void AnnotationToolbar::setHistoryState(bool canUndo) {
    if (canUndo_ == canUndo) return;
    canUndo_ = canUndo;
    visualDirty_ = true;
}

void AnnotationToolbar::setScreenshotActive(bool active) {
    if (screenshotActive_ == active) return;
    screenshotActive_ = active;
    visualDirty_ = true;
}

void AnnotationToolbar::setDocumentActive(bool active) {
    if (documentActive_ == active) return;
    documentActive_ = active;
    visualDirty_ = true;
}

void AnnotationToolbar::setOfficeState(bool active, bool wps) {
    if (officeActive_ == active && officeWps_ == wps) return;
    officeActive_ = active;
    officeWps_ = wps;
    visualDirty_ = true;
}

AnnotationToolbar::PanelMode AnnotationToolbar::panelModeForTool(Tool tool) const {
    switch (tool) {
        case Tool::Pen:
        case Tool::Highlighter:
            return PanelMode::Brush;
        case Tool::Eraser:
            return PanelMode::Eraser;
        default:
            // 激光 / 指针 / 截图 / 识别 / AI：再次点击显示快捷操作提示。
            return PanelMode::Info;
    }
}

void AnnotationToolbar::setPanelForTool() {
    const PanelMode target = panelModeForTool(tool_);
    panelMode_ = (target == PanelMode::Brush || target == PanelMode::Eraser)
        ? target
        : PanelMode::None;
}

const char* AnnotationToolbar::toolName() const {
    if (panelMode_ == PanelMode::Settings) return "设置";
    if (panelMode_ == PanelMode::Info) return "快捷操作";
    return kToolNames[static_cast<size_t>(tool_)];
}

const char* AnnotationToolbar::toolShortcut() const {
    if (panelMode_ == PanelMode::Settings) return "笔迹参数 / 界面缩放";
    if (panelMode_ == PanelMode::Info) return "课堂使用提示与快捷键";
    switch (tool_) {
        case Tool::Pen: return "贝塞尔平滑 + 速度笔锋";
        case Tool::Highlighter: return "半透明荧光标注";
        case Tool::Eraser: return "像素擦除 · 大小可调";
        case Tool::Laser: return "授课激光指示";
        case Tool::Pointer: return "点击透传给下层应用";
        case Tool::Screenshot: return "框选后复制 / 保存 PNG";
        case Tool::Ocr: return "框选文字 → 荧光高亮 / 点击复制";
        case Tool::Ai: return "全屏截图 → OCR → AI 教学卡片";
    }
    return "";
}

// ============================================================================
// 交互
// ============================================================================

AnnotationToolbar::Action AnnotationToolbar::actionAt(const Vec2i& point) const {
    for (const HitRegion& region : buttonRegions_) {
        if (region.rect.contains(point)) return region.action;
    }
    return Action::None;
}

int32_t AnnotationToolbar::colorAt(const Vec2i& point) const {
    for (size_t i = 0; i < colorSwatches_.size(); ++i) {
        if (colorSwatches_[i].contains(point)) return static_cast<int32_t>(i);
    }
    return -1;
}

int32_t AnnotationToolbar::sliderAt(const Vec2i& point) const {
    if (!widthSliderRect_.isNull() &&
        widthSliderRect_.inflate(0, scaledInt(12.0f)).contains(point)) {
        return 0;
    }
    if (!eraserSliderRect_.isNull() &&
        eraserSliderRect_.inflate(0, scaledInt(12.0f)).contains(point)) {
        return 6;
    }
    for (size_t i = 0; i < settingSliderRects_.size(); ++i) {
        if (!settingSliderRects_[i].isNull() &&
            settingSliderRects_[i].inflate(0, scaledInt(12.0f)).contains(point)) {
            return static_cast<int32_t>(i) + 1;
        }
    }
    return -1;
}

void AnnotationToolbar::setSliderValue(int32_t slider, int32_t localX) {
    if (slider == 0) {
        if (widthSliderRect_.isNull() || widthSliderRect_.w <= 0) return;
        const float t = clampf(
            static_cast<float>(localX - widthSliderRect_.x) /
                static_cast<float>(widthSliderRect_.w), 0.0f, 1.0f);
        const float value = 1.0f + t * 39.0f;
        if (std::fabs(value - width_) < 0.05f) return;
        width_ = value;
        visualDirty_ = true;
        if (onWidthChanged) onWidthChanged(width_);
        return;
    }

    if (slider == 6) {
        if (eraserSliderRect_.isNull() || eraserSliderRect_.w <= 0) return;
        const float t = clampf(
            static_cast<float>(localX - eraserSliderRect_.x) /
                static_cast<float>(eraserSliderRect_.w), 0.0f, 1.0f);
        const float value = 8.0f + t * 152.0f;
        if (std::fabs(value - eraserSize_) < 0.1f) return;
        eraserSize_ = value;
        visualDirty_ = true;
        if (onEraserSizeChanged) onEraserSizeChanged(eraserSize_);
        return;
    }

    const int32_t index = slider - 1;
    if (index < 0 || index >= static_cast<int32_t>(settingSliderRects_.size())) return;
    const Rect& track = settingSliderRects_[static_cast<size_t>(index)];
    if (track.isNull() || track.w <= 0) return;

    const float t = clampf(
        static_cast<float>(localX - track.x) / static_cast<float>(track.w),
        0.0f, 1.0f);

    if (index == 0) {
        smoothing01_ = t;
        visualDirty_ = true;
        if (onInkParametersChanged) {
            onInkParametersChanged(smoothing01_, penTip01_, taper01_);
        }
        return;
    }
    if (index == 1) {
        penTip01_ = t;
        visualDirty_ = true;
        if (onInkParametersChanged) {
            onInkParametersChanged(smoothing01_, penTip01_, taper01_);
        }
        return;
    }
    if (index == 2) {
        taper01_ = t;
        visualDirty_ = true;
        if (onInkParametersChanged) {
            onInkParametersChanged(smoothing01_, penTip01_, taper01_);
        }
        return;
    }

    if (index == 3) {
        // 拖拽期间不改变工具栏几何，避免滑杆跟随缩放抖动；松手后立即生效。
        uiScaleSetting_ = 0.8f + t * 1.4f;
        visualDirty_ = true;
        return;
    }

    // index == 4：AI 卡片缩放 60%–200%
    const float percent = 60.0f + t * 140.0f;
    if (std::fabs(percent - cardScalePercent_) < 0.5f) return;
    cardScalePercent_ = percent;
    visualDirty_ = true;
    if (onCardScaleChanged) onCardScaleChanged(cardScalePercent_ / 100.0f);
}

void AnnotationToolbar::activateTool(Tool tool) {
    if (tool_ == tool) {
        // 已选中的工具：再次点击按钮显示 / 隐藏属性侧边栏。
        const PanelMode target = panelModeForTool(tool);
        panelMode_ = (panelMode_ == target) ? PanelMode::None : target;
        visualDirty_ = true;
        return;
    }

    setTool(tool);
    if (onToolChanged) onToolChanged(tool);
}

void AnnotationToolbar::activate(Action action) {
    switch (action) {
        case Action::Pen: activateTool(Tool::Pen); break;
        case Action::Highlighter: activateTool(Tool::Highlighter); break;
        case Action::Eraser: activateTool(Tool::Eraser); break;
        case Action::Laser: activateTool(Tool::Laser); break;
        case Action::Pointer: activateTool(Tool::Pointer); break;
        case Action::Screenshot:
            if (screenshotActive_) {
                // 截图已激活：再次点击在“快捷操作”提示侧栏与收起之间切换。
                panelMode_ = (panelMode_ == PanelMode::Info) ? PanelMode::None
                                                             : PanelMode::Info;
            } else {
                screenshotActive_ = true;
                if (onScreenshot) onScreenshot();
            }
            break;
        case Action::Ocr: {
            const bool sameTool = (tool_ == Tool::Ocr);
            activateTool(Tool::Ocr);
            // 再次点击只切换提示侧边栏，不重复触发识别模式提示。
            if (!sameTool && onOcr) onOcr();
            break;
        }
        case Action::Ai:
            // AI 模式不切换绘制工具：点击后由视图完成截图 / OCR / 卡片生成。
            if (onAi) onAi();
            break;

        case Action::Document:
            // 文档解析：全屏 OCR → AI 还原原文 → Markdown 面板。
            if (onDocument) onDocument();
            break;

        case Action::Undo: if (onUndo) onUndo(); break;
        case Action::Clear: if (onClear) onClear(); break;
        case Action::Settings:
            panelMode_ = (panelMode_ == PanelMode::Settings) ? PanelMode::None
                                                              : PanelMode::Settings;
            break;
        case Action::AddPage: if (onAddPage) onAddPage(); break;
        case Action::PrevPage: if (onPrevPage) onPrevPage(); break;
        case Action::NextPage: if (onNextPage) onNextPage(); break;
        case Action::Close: if (onClose) onClose(); break;
        default: break;
    }
    visualDirty_ = true;
}

bool AnnotationToolbar::onEvent(const modern_ui::Event& ev) {
    using modern_ui::EventType;

    const Vec2i point{ev.x, ev.y};
    const bool inside = (point.x >= 0 && point.y >= 0 &&
                         point.x < frame().w && point.y < frame().h);

    switch (ev.type) {
        case EventType::MouseMove: {
            if (draggingSlider_ >= 0) {
                setSliderValue(draggingSlider_, point.x);
                return true;
            }

            const Action action = actionAt(point);
            const int32_t colorIndex = colorAt(point);
            if (action != hoverAction_ || colorIndex != hoverColor_) {
                hoverAction_ = action;
                hoverColor_ = colorIndex;
                hoverProgress_ = 0.0f;
                visualDirty_ = true;
            }
            return inside;
        }

        case EventType::MouseDown: {
            if (ev.button != modern_ui::MouseButtonLeft) return inside;

            const int32_t colorIndex = colorAt(point);
            if (colorIndex >= 0) {
                color_ = palette()[static_cast<size_t>(colorIndex)];
                visualDirty_ = true;
                if (onColorChanged) onColorChanged(color_);
                return true;
            }

            const int32_t slider = sliderAt(point);
            if (slider >= 0) {
                draggingSlider_ = slider;
                setSliderValue(slider, point.x);
                return true;
            }

            const Action action = actionAt(point);
            if (action != Action::None) {
                pressedAction_ = action;
                hoverAction_ = action;
                visualDirty_ = true;
                return true;
            }
            return inside;
        }

        case EventType::MouseUp: {
            if (draggingSlider_ >= 0) {
                const int32_t released = draggingSlider_;
                draggingSlider_ = -1;
                if (released == 4) updateFrameGeometry();
                visualDirty_ = true;
                return true;
            }

            const Action pressed = pressedAction_;
            pressedAction_ = Action::None;
            visualDirty_ = true;

            if (pressed != Action::None && actionAt(point) == pressed) {
                activate(pressed);
            }
            return inside || pressed != Action::None;
        }

        case EventType::MouseWheel: {
            if (!inside) return false;
            if (panelMode_ == PanelMode::Brush && widthSliderRect_.w > 0) {
                width_ = clampf(width_ + ev.wheelDelta * 1.2f, 1.0f, 40.0f);
                visualDirty_ = true;
                if (onWidthChanged) onWidthChanged(width_);
            } else if (panelMode_ == PanelMode::Eraser && eraserSliderRect_.w > 0) {
                eraserSize_ = clampf(eraserSize_ + ev.wheelDelta * 8.0f, 8.0f, 160.0f);
                visualDirty_ = true;
                if (onEraserSizeChanged) onEraserSizeChanged(eraserSize_);
            }
            return true;
        }

        case EventType::MouseLeave:
            hoverAction_ = Action::None;
            hoverColor_ = -1;
            hoverProgress_ = 0.0f;
            visualDirty_ = true;
            return false;

        default:
            return false;
    }
}

// ============================================================================
// 绘制
// ============================================================================

void AnnotationToolbar::rebuildVisuals() {
    if (!layoutValid_ || groupId_ == 0) return;

    const int32_t w = std::max(1, frame().w);
    const int32_t h = std::max(1, frame().h);
    const float panelAlpha = clampf(expandProgress_, 0.0f, 1.0f);

    const float s = uiScale_;
    auto S = [&](float value) { return value * s; };
    auto SI = [&](float value) {
        return static_cast<int32_t>(std::lround(value * s));
    };
    const auto sw1 = std::max(1, SI(1.0f));
    const auto sw2 = std::max(1, SI(2.0f));

    buttonRegions_.clear();
    colorSwatches_.clear();
    widthSliderRect_ = Rect::null();
    eraserSliderRect_ = Rect::null();
    for (Rect& rect : settingSliderRects_) rect = Rect::null();

    GroupData group;
    int32_t z = 0;

    auto add = [&](Shape shape) {
        shape.base.z = z++;
        group.children.push_back(std::move(shape));
    };

    auto addRect = [&](int32_t x, int32_t y, int32_t width, int32_t height,
                       float radius, Color fill, Color stroke, int32_t strokeWidth) {
        Shape shape;
        shape.data = RRectShape{x, y, width, height, radius, radius};
        shape.base.fill = Paint(fill);
        if (strokeWidth > 0) {
            shape.base.stroke = Paint(stroke);
            shape.base.strokeWidth = strokeWidth;
        }
        add(std::move(shape));
    };

    auto addCircle = [&](float cx, float cy, float radius, Color fill,
                         Color stroke, int32_t strokeWidth) {
        Shape shape;
        CircleShape circle;
        circle.cx = static_cast<int32_t>(std::lround(cx));
        circle.cy = static_cast<int32_t>(std::lround(cy));
        circle.r = radius;
        shape.data = circle;
        shape.base.fill = Paint(fill);
        if (strokeWidth > 0) {
            shape.base.stroke = Paint(stroke);
            shape.base.strokeWidth = strokeWidth;
        }
        add(std::move(shape));
    };

    auto addLine = [&](float x1, float y1, float x2, float y2, Color color,
                       int32_t strokeWidth, float opacity = 1.0f) {
        Shape shape;
        shape.data = LineShape{static_cast<int32_t>(std::lround(x1)),
                               static_cast<int32_t>(std::lround(y1)),
                               static_cast<int32_t>(std::lround(x2)),
                               static_cast<int32_t>(std::lround(y2))};
        shape.base.stroke = Paint(color);
        shape.base.strokeWidth = strokeWidth;
        shape.base.opacity = opacity;
        add(std::move(shape));
    };

    auto addText = [&](const std::string& text, float x, float baseline,
                       int32_t fontSize, Color color, float opacity = 1.0f) {
        if (text.empty()) return;
        Shape shape;
        TextShape textShape;
        textShape.text = text;
        textShape.x = static_cast<int32_t>(std::lround(x));
        textShape.y = static_cast<int32_t>(std::lround(baseline));
        textShape.fontSize = fontSize;
        shape.data = std::move(textShape);
        shape.base.fill = Paint(color);
        shape.base.opacity = opacity;
        add(std::move(shape));
    };

    auto addSvgIcon = [&](const std::string& markup, float x, float y, float size,
                          float opacity) {
        if (markup.empty()) return;
        Shape shape;
        SvgShape svg;
        svg.data = markup;
        svg.x = static_cast<int32_t>(std::lround(x));
        svg.y = static_cast<int32_t>(std::lround(y));
        svg.w = static_cast<int32_t>(std::lround(size));
        svg.h = static_cast<int32_t>(std::lround(size));
        svg.preserveAspectRatio = true;
        shape.data = std::move(svg);
        shape.base.opacity = opacity;
        add(std::move(shape));
    };

    auto addPath = [&](const std::vector<PathShape::Step>& steps, Color color,
                       int32_t strokeWidth, float opacity = 1.0f) {
        Shape shape;
        PathShape path;
        path.steps = steps;
        shape.data = std::move(path);
        shape.base.stroke = Paint(color);
        shape.base.strokeWidth = strokeWidth;
        shape.base.opacity = opacity;
        add(std::move(shape));
    };

    // 背景
    {
        Shape background;
        modern_ui::LinearGradient gradient;
        gradient.from = Vec2f{0.0f, 0.0f};
        gradient.to = Vec2f{0.0f, static_cast<float>(h)};
        gradient.stops.push_back({0.0f, Color{20, 23, 32, 242}});
        gradient.stops.push_back({1.0f, Color{10, 12, 18, 246}});
        background.data = RRectShape{0, 0, w, h, S(22.0f), S(22.0f)};
        background.base.fill = Paint(std::move(gradient));
        background.base.stroke = Paint(Color{255, 255, 255, 24});
        background.base.strokeWidth = sw1;
        add(std::move(background));

        addRect(SI(2), SI(2), w - SI(4), SI(1), S(0.5f),
                Color{255, 255, 255, 24}, Color{0, 0, 0, 0}, 0);
        addRect(w - SI(4), SI(24), SI(2), h - SI(48), S(1.0f),
                Color{96, 205, 255, 90}, Color{0, 0, 0, 0}, 0);
    }

    const int32_t iconColumn = SI(kIconColumn);
    const int32_t columnX = w - iconColumn;
    const int32_t button = SI(kButtonSize);
    const int32_t rowStep = SI(kRowStep);

    auto toolAction = [](Tool tool) {
        switch (tool) {
            case Tool::Pen: return Action::Pen;
            case Tool::Highlighter: return Action::Highlighter;
            case Tool::Eraser: return Action::Eraser;
            case Tool::Laser: return Action::Laser;
            case Tool::Pointer: return Action::Pointer;
            case Tool::Screenshot: return Action::Screenshot;
            case Tool::Ocr: return Action::Ocr;
            case Tool::Ai: return Action::Ai;
        }
        return Action::None;
    };

    auto drawUndoIcon = [&](const Rect& rect, bool disabled, float hover) {
        const Color color = disabled
            ? Color{118, 124, 138, 255}
            : (hover > 0.0f ? Color{245, 248, 255, 255} : Color{220, 226, 238, 255});
        const float cx = static_cast<float>(rect.x + rect.w / 2);
        const float cy = static_cast<float>(rect.y + rect.h / 2);

        std::vector<PathShape::Step> steps;
        PathShape::Step move;
        move.cmd = PathShape::Cmd::MoveTo;
        move.pts[0] = Vec2f{cx + S(9), cy + S(9)};
        steps.push_back(move);
        PathShape::Step quad;
        quad.cmd = PathShape::Cmd::QuadTo;
        quad.pts[0] = Vec2f{cx + S(8), cy - S(8)};
        quad.pts[1] = Vec2f{cx - S(9), cy - S(6)};
        steps.push_back(quad);
        addPath(steps, color, sw2, 1.0f);

        addLine(cx - S(9), cy - S(6), cx - S(2), cy - S(12), color, sw2);
        addLine(cx - S(9), cy - S(6), cx - S(13), cy + S(2), color, sw2);
    };

    auto drawToolButton = [&](Action action, const Rect& rect, bool disabled,
                              bool selected, const std::string& icon,
                              const std::string& accentIcon,
                              const std::string& dimIcon) {
        const float hover = (hoverAction_ == action) ? hoverProgress_ : 0.0f;
        const bool pressed = (pressedAction_ == action);

        Color fill{0, 0, 0, 0};
        Color stroke{0, 0, 0, 0};
        int32_t strokeWidth = 0;

        if (selected) {
            fill = Color{96, 205, 255, static_cast<uint8_t>(28 + hover * 12)};
            stroke = Color{96, 205, 255, 95};
            strokeWidth = sw1;
        } else if (pressed) {
            fill = Color{255, 255, 255, 34};
        } else if (hover > 0.02f) {
            fill = Color{255, 255, 255, static_cast<uint8_t>(18 * hover)};
        }

        if (fill.a > 0) {
            addRect(rect.x, rect.y, rect.w, rect.h, S(12.0f), fill, stroke, strokeWidth);
        }

        if (selected) {
            addRect(columnX + SI(1), rect.y + SI(13), SI(3),
                    std::max(SI(4), rect.h - SI(26)), S(1.5f), kAccent,
                    Color{0, 0, 0, 0}, 0);
        }

        const std::string& chosen = disabled ? dimIcon
                                  : selected ? accentIcon
                                             : icon;
        addSvgIcon(chosen, static_cast<float>(rect.x + SI(10)),
                   static_cast<float>(rect.y + SI(10)), S(24.0f),
                   selected ? 1.0f : (disabled ? 0.55f : 1.0f));
    };

    auto addRegion = [&](const Rect& rect, Action action) {
        buttonRegions_.push_back(HitRegion{rect, action, -1});
    };

    // -------------------- 面板内容 --------------------
    const int32_t panelX = SI(16);
    const int32_t panelRight = columnX - SI(14);
    const int32_t panelWidth = panelRight - panelX;

    if (panelMode_ != PanelMode::None && panelWidth > SI(80)) {
        addText(toolName(), static_cast<float>(panelX), S(30.0f), SI(15),
                kTextPrimary, panelAlpha);
        addText(toolShortcut(), static_cast<float>(panelX), S(48.0f), SI(10),
                kTextSecondary, panelAlpha * 0.9f);
        addLine(static_cast<float>(panelX), S(58.0f),
                static_cast<float>(panelRight), S(58.0f),
                Color{255, 255, 255, 16}, sw1, panelAlpha);

        if (panelMode_ == PanelMode::Brush) {
            const auto& colors = palette();
            const int32_t cell = SI(34);
            const int32_t diameter = SI(22);
            const float startY = S(78.0f);

            for (size_t i = 0; i < colors.size(); ++i) {
                const int32_t column = static_cast<int32_t>(i % 5);
                const int32_t row = static_cast<int32_t>(i / 5);
                const float cx = static_cast<float>(panelX + SI(11) + column * cell);
                const float cy = startY + S(11.0f) + static_cast<float>(row * cell);

                const bool selected =
                    (colors[i].r == color_.r && colors[i].g == color_.g &&
                     colors[i].b == color_.b);
                const bool hovered = (hoverColor_ == static_cast<int32_t>(i));

                addCircle(cx, cy, static_cast<float>(diameter) * 0.5f,
                          alphaOf(colors[i], 255),
                          selected ? Color{255, 255, 255, 235}
                                   : (hovered ? Color{255, 255, 255, 140}
                                              : Color{255, 255, 255, 55}),
                          selected ? sw2 : sw1);

                colorSwatches_.push_back(Rect{
                    static_cast<int32_t>(cx) - SI(15),
                    static_cast<int32_t>(cy) - SI(15), SI(30), SI(30)});
            }

            const float sliderY = startY + static_cast<float>(cell * 2) + S(26.0f);
            const bool highlighter = (tool_ == Tool::Highlighter);
            addText(highlighter ? "荧光笔宽" : "笔宽",
                    static_cast<float>(panelX), sliderY, SI(12), kTextSecondary,
                    panelAlpha);

            char valueText[32];
            std::snprintf(valueText, sizeof(valueText), "%.0f px", width_);
            addText(valueText, static_cast<float>(panelRight - SI(44)), sliderY,
                    SI(12), kAccent, panelAlpha);

            const int32_t trackX = panelX;
            const int32_t trackY = static_cast<int32_t>(sliderY + S(12.0f));
            const int32_t trackW = panelRight - panelX;
            const int32_t trackH = SI(6);
            const float t = clampf((width_ - 1.0f) / 39.0f, 0.0f, 1.0f);
            const int32_t fillW = static_cast<int32_t>(std::lround(
                static_cast<float>(trackW) * t));

            addRect(trackX, trackY, trackW, trackH, S(3.0f),
                    Color{255, 255, 255, 22}, Color{0, 0, 0, 0}, 0);
            addRect(trackX, trackY, std::max(SI(2), fillW), trackH, S(3.0f),
                    alphaOf(kAccent, static_cast<uint8_t>(230 * panelAlpha)),
                    Color{0, 0, 0, 0}, 0);
            addCircle(static_cast<float>(trackX + fillW),
                      static_cast<float>(trackY + trackH / 2), S(7.0f),
                      Color{250, 252, 255, 255}, Color{96, 205, 255, 255}, sw2);

            widthSliderRect_ = Rect{trackX, trackY, trackW, trackH};

            addText("速度模拟笔锋 · 书写越快越细",
                    static_cast<float>(panelX), sliderY + S(52.0f), SI(10),
                    kTextTertiary, panelAlpha * 0.95f);
        } else if (panelMode_ == PanelMode::Eraser) {
            const float startY = S(78.0f);
            const float t = clampf((eraserSize_ - 8.0f) / 152.0f, 0.0f, 1.0f);

            addText("橡皮直径", static_cast<float>(panelX), startY, SI(12),
                    kTextSecondary, panelAlpha);

            char valueText[32];
            std::snprintf(valueText, sizeof(valueText), "%.0f px",
                          static_cast<double>(eraserSize_));
            addText(valueText, static_cast<float>(panelRight - SI(52)), startY,
                    SI(12), kAccent, panelAlpha);

            const int32_t trackX = panelX;
            const int32_t trackY = static_cast<int32_t>(startY + S(12.0f));
            const int32_t trackW = panelRight - panelX;
            const int32_t trackH = SI(6);
            const int32_t fillW = static_cast<int32_t>(std::lround(
                static_cast<float>(trackW) * t));

            addRect(trackX, trackY, trackW, trackH, S(3.0f),
                    Color{255, 255, 255, 22}, Color{0, 0, 0, 0}, 0);
            addRect(trackX, trackY, std::max(SI(2), fillW), trackH, S(3.0f),
                    alphaOf(kAccent, static_cast<uint8_t>(230 * panelAlpha)),
                    Color{0, 0, 0, 0}, 0);
            addCircle(static_cast<float>(trackX + fillW),
                      static_cast<float>(trackY + trackH / 2), S(7.0f),
                      Color{250, 252, 255, 255}, Color{96, 205, 255, 255}, sw2);

            eraserSliderRect_ = Rect{trackX, trackY, trackW, trackH};

            addText("圆环即实际擦除范围", static_cast<float>(panelX),
                    startY + S(44.0f), SI(10), kTextTertiary,
                    panelAlpha * 0.95f);

            // 实时大小预览：直接按屏幕像素绘制圆环，与画布光标一致。
            const float previewCx =
                static_cast<float>((panelX + panelRight) / 2);
            const float previewCy = startY + S(118.0f);
            const float previewR = clampf(eraserSize_ * 0.5f, 4.0f, 54.0f);
            addCircle(previewCx, previewCy, previewR,
                      Color{255, 255, 255, 18}, Color{255, 255, 255, 110}, sw1);
            addCircle(previewCx, previewCy, S(1.5f), Color{255, 255, 255, 180},
                      Color{0, 0, 0, 0}, 0);
            addText("当前大小", previewCx - SI(20), previewCy + previewR + S(18.0f),
                    SI(10), kTextTertiary, panelAlpha * 0.95f);
        } else if (panelMode_ == PanelMode::Settings) {
            const float startY = S(78.0f);
            const float rowStep = S(56.0f);

            auto drawSetting = [&](int32_t index, const char* label,
                                   float fill01, const char* valueText) {
                const float y = startY + rowStep * static_cast<float>(index);
                addText(label, static_cast<float>(panelX), y, SI(12),
                        kTextSecondary, panelAlpha);
                addText(valueText, static_cast<float>(panelRight - SI(52)), y,
                        SI(12), kAccent, panelAlpha);

                const int32_t trackX = panelX;
                const int32_t trackY = static_cast<int32_t>(y + S(12.0f));
                const int32_t trackW = panelRight - panelX;
                const int32_t trackH = SI(6);
                const int32_t fillW = static_cast<int32_t>(std::lround(
                    static_cast<float>(trackW) * clampf(fill01, 0.0f, 1.0f)));

                addRect(trackX, trackY, trackW, trackH, S(3.0f),
                        Color{255, 255, 255, 22}, Color{0, 0, 0, 0}, 0);
                addRect(trackX, trackY, std::max(SI(2), fillW), trackH, S(3.0f),
                        alphaOf(kAccent, static_cast<uint8_t>(230 * panelAlpha)),
                        Color{0, 0, 0, 0}, 0);
                addCircle(static_cast<float>(trackX + fillW),
                          static_cast<float>(trackY + trackH / 2), S(7.0f),
                          Color{250, 252, 255, 255},
                          Color{96, 205, 255, 255}, sw2);

                settingSliderRects_[static_cast<size_t>(index)] =
                    Rect{trackX, trackY, trackW, trackH};
            };

            char valueText[32];
            std::snprintf(valueText, sizeof(valueText), "%.0f%%",
                          static_cast<double>(smoothing01_ * 100.0f));
            drawSetting(0, "EMA 平滑", smoothing01_, valueText);
            std::snprintf(valueText, sizeof(valueText), "%.0f%%",
                          static_cast<double>(penTip01_ * 100.0f));
            drawSetting(1, "笔锋强度", penTip01_, valueText);
            std::snprintf(valueText, sizeof(valueText), "%.0f%%",
                          static_cast<double>(taper01_ * 100.0f));
            drawSetting(2, "起收笔锥化", taper01_, valueText);

            addLine(static_cast<float>(panelX), startY + rowStep * 2.0f + S(44.0f),
                    static_cast<float>(panelRight), startY + rowStep * 2.0f + S(44.0f),
                    Color{255, 255, 255, 16}, sw1, panelAlpha);

            std::snprintf(valueText, sizeof(valueText), "×%.2f",
                          static_cast<double>(uiScaleSetting_));
            drawSetting(3, "工具栏缩放", (uiScaleSetting_ - 0.8f) / 1.4f,
                        valueText);
            std::snprintf(valueText, sizeof(valueText), "%.0f%%",
                          static_cast<double>(cardScalePercent_));
            drawSetting(4, "卡片缩放", (cardScalePercent_ - 60.0f) / 140.0f,
                        valueText);

            addText("平滑越大越稳，跟手延迟略增",
                    static_cast<float>(panelX), startY + rowStep * 5.0f, SI(10),
                    kTextTertiary, panelAlpha * 0.95f);
            addText("工具栏 / 卡片缩放即时生效",
                    static_cast<float>(panelX), startY + rowStep * 5.0f + S(16.0f),
                    SI(10), kTextTertiary, panelAlpha * 0.95f);
        } else if (panelMode_ == PanelMode::Info) {
            const float startY = S(78.0f);
            const float step = S(26.0f);
            float tipsY = startY;

            if (officeActive_) {
                addText(officeWps_ ? "已连接 WPS 演示（页码同步）"
                                   : "已连接 PowerPoint 放映（页码同步）",
                        static_cast<float>(panelX), tipsY, SI(10),
                        officeWps_ ? Color{105, 226, 138, 255} : kAccent,
                        panelAlpha);
                tipsY += S(26.0f);
            }

            static const char* const tips[] = {
                "F9  显示 / 隐藏批注层",
                "F10 指针透传模式",
                "Ctrl+Z  撤销",
                "Ctrl+PgUp / PgDn  翻页",
                "Ctrl+Shift+O  复制全部识别文字",
                "Ctrl+Shift+Q  退出程序",
            };

            constexpr size_t tipCount = sizeof(tips) / sizeof(tips[0]);
            for (size_t i = 0; i < tipCount; ++i) {
                addText(tips[i], static_cast<float>(panelX),
                        tipsY + step * static_cast<float>(i), SI(11),
                        kTextSecondary, panelAlpha * 0.95f);
            }

            addText("再次点击当前工具按钮可收起侧边栏",
                    static_cast<float>(panelX), startY + step * 6.0f + S(8.0f),
                    SI(10), kTextTertiary, panelAlpha * 0.95f);
        }
    }

    // -------------------- 图标列 --------------------
    int32_t y = SI(12);
    const int32_t buttonX = columnX + (iconColumn - button) / 2;

    static const Action toolActions[9] = {
        Action::Pen, Action::Highlighter, Action::Eraser,
        Action::Laser, Action::Pointer, Action::Screenshot,
        Action::Ocr, Action::Ai, Action::Document,
    };

    for (int32_t i = 0; i < 9; ++i) {
        const Rect rect{buttonX, y, button, button};
        const Action action = toolActions[i];
        const bool selected = (action == toolAction(tool_)) ||
                              (action == Action::Screenshot && screenshotActive_) ||
                              (action == Action::Document && documentActive_);
        drawToolButton(action, rect, false, selected,
                       iconWhite_[static_cast<size_t>(i)],
                       iconAccent_[static_cast<size_t>(i)],
                       iconDim_[static_cast<size_t>(i)]);
        addRegion(rect, action);
        y += rowStep;
    }

    addLine(static_cast<float>(columnX + SI(10)), static_cast<float>(y + SI(2)),
            static_cast<float>(w - SI(10)), static_cast<float>(y + SI(2)),
            Color{255, 255, 255, 14}, sw1);
    y += SI(10);

    // 撤销 / 清屏（已移除 Redo）
    {
        const Rect undoRect{buttonX, y, button, button};
        drawToolButton(Action::Undo, undoRect, !canUndo_, false, {}, {}, {});
        drawUndoIcon(undoRect, !canUndo_,
                     hoverAction_ == Action::Undo ? hoverProgress_ : 0.0f);
        addRegion(undoRect, Action::Undo);
        y += rowStep;

        const Rect clearRect{buttonX, y, button, button};
        drawToolButton(Action::Clear, clearRect, false, false,
                       utilityWhite_[0], utilityAccent_[0], utilityDim_[0]);
        addRegion(clearRect, Action::Clear);
        y += rowStep;
    }

    addLine(static_cast<float>(columnX + SI(10)), static_cast<float>(y + SI(2)),
            static_cast<float>(w - SI(10)), static_cast<float>(y + SI(2)),
            Color{255, 255, 255, 14}, sw1);
    y += SI(10);

    // 设置
    {
        const Rect rect{buttonX, y, button, button};
        const bool selected = (panelMode_ == PanelMode::Settings);
        drawToolButton(Action::Settings, rect, false, selected,
                       settingsIconWhite_, settingsIconAccent_, settingsIconDim_);
        addRegion(rect, Action::Settings);
        y += rowStep;
    }

    // 翻页：back / forward 图标
    {
        const int32_t rowY = y + SI(2);
        const int32_t smallButton = SI(20);
        const int32_t labelWidth = SI(24);
        const int32_t iconSize = SI(18);
        const int32_t rowHeight = SI(30);

        const Rect prevRect{columnX + SI(2), rowY, smallButton, rowHeight};
        const Rect labelRect{columnX + SI(22), rowY, labelWidth, rowHeight};
        const Rect nextRect{columnX + SI(46), rowY, smallButton, rowHeight};

        for (const auto& item : {std::make_pair(prevRect, Action::PrevPage),
                                 std::make_pair(nextRect, Action::NextPage)}) {
            const Action action = item.second;
            const float hover = (hoverAction_ == action) ? hoverProgress_ : 0.0f;
            if (hover > 0.02f) {
                addRect(item.first.x, item.first.y, item.first.w, item.first.h,
                        S(8.0f), Color{255, 255, 255,
                                       static_cast<uint8_t>(16 * hover)},
                        Color{0, 0, 0, 0}, 0);
            }
            const std::string& icon =
                (action == Action::PrevPage) ? utilityWhite_[2] : utilityWhite_[3];
            addSvgIcon(icon,
                       static_cast<float>(item.first.x + (item.first.w - iconSize) / 2),
                       static_cast<float>(item.first.y + (item.first.h - iconSize) / 2),
                       static_cast<float>(iconSize), 1.0f);
            addRegion(item.first, action);
        }

        char pageText[32];
        std::snprintf(pageText, sizeof(pageText), "%d/%d", pageIndex_ + 1, pageCount_);
        const Color pageColor = officeActive_
            ? (officeWps_ ? Color{105, 226, 138, 255} : kAccent)
            : kTextSecondary;
        addText(pageText, static_cast<float>(labelRect.x + SI(2)),
                static_cast<float>(rowY + SI(19)), SI(10), pageColor, 1.0f);

        y += SI(38);
    }

    // 新建页：add 图标
    {
        const int32_t rowHeight = SI(28);
        const int32_t iconSize = SI(18);
        const Rect rect{buttonX, y, button, rowHeight};
        const float hover = (hoverAction_ == Action::AddPage) ? hoverProgress_ : 0.0f;
        addRect(rect.x, rect.y, rect.w, rect.h, S(9.0f),
                Color{255, 255, 255, static_cast<uint8_t>(12 + 14 * hover)},
                Color{255, 255, 255, 18}, sw1);
        addSvgIcon(utilityWhite_[1],
                   static_cast<float>(rect.x + (rect.w - iconSize) / 2),
                   static_cast<float>(rect.y + (rect.h - iconSize) / 2),
                   static_cast<float>(iconSize), 1.0f);
        addRegion(rect, Action::AddPage);
        y += SI(34);
    }

    // 退出：quit 图标
    {
        const Rect rect{buttonX, y, button, button};
        drawToolButton(Action::Close, rect, false, false,
                       utilityWhite_[4], utilityAccent_[4], utilityDim_[4]);
        addRegion(rect, Action::Close);
    }

    if (groupId_ != 0) {
        canvas().updateShape(groupId_, [&](Shape& shape) {
            shape.data = std::move(group);
            shape.base.visible = true;
        });
    }
}

} // namespace annotation
