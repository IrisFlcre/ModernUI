// ============================================================================
// CameraToolbar.cpp
// ============================================================================

#include "CameraToolbar.h"

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

namespace camerapro {

namespace {

// 基准尺寸（未缩放）；最终 × uiScale（默认 1.5）
constexpr int32_t kCollapsedWidth = 68;
constexpr int32_t kExpandedWidth = 304;
constexpr int32_t kBaseHeight = 680;
constexpr int32_t kIconColumn = 68;
constexpr int32_t kButtonSize = 44;
constexpr int32_t kRowStep = 46;

const Color kTextPrimary{240, 243, 248, 255};
const Color kTextSecondary{170, 176, 190, 255};
const Color kTextTertiary{122, 128, 142, 255};
const Color kAccent{96, 205, 255, 255};

const char* const kIconFiles[8] = {
    "brush.svg",
    "Highlighter.svg",
    "editor-eraser.svg",
    "laser.svg",
    "hand-pan.svg",
    "camera-snapshot.svg",
    "camera-tune.svg",
    "scan_type.svg",
};

// 新图标尚未下载时的回退文件，保证界面依然可用。
const char* const kIconFallbacks[8] = {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    "085-pointer.svg",
    "screenshot.svg",
    "icon-config.svg",
    nullptr,
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
    "钢笔", "荧光笔", "橡皮擦", "激光笔", "拖动画布", "拍照", "画面调整", "文字识别",
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

// 把 SVG path 的 fill / stroke 染色。支持"填充型"和"描边型"两种图标。
std::string tintSvg(const std::string& markup, const Color& color) {
    if (markup.empty()) return markup;

    const std::string fillAttr = "fill=\"" + colorHex(color) + "\"";
    const std::string strokeAttr = "stroke=\"" + colorHex(color) + "\"";
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

        const bool hasFill = tag.find("fill=") != std::string::npos;
        const bool fillNone = tag.find("fill=\"none\"") != std::string::npos;
        const bool hasStroke = tag.find("stroke=") != std::string::npos;

        auto writeAttribute = [&](const char* name, const std::string& value) {
            const std::string pattern = std::string(name) + "=\"";
            const size_t existing = tag.find(pattern);
            if (existing != std::string::npos) {
                const size_t valueStart = existing + pattern.size();
                const size_t valueEnd = tag.find('"', valueStart);
                if (valueEnd != std::string::npos) {
                    tag.replace(existing, valueEnd - existing + 1, value);
                }
                return;
            }
            const size_t insertAt = (tag.size() > 2 && tag[tag.size() - 2] == '/')
                ? tag.size() - 2
                : tag.size() - 1;
            tag.insert(insertAt, " " + value);
        };

        if (!fillNone) writeAttribute("fill", fillAttr);
        if (hasStroke || (!hasFill && !fillNone)) {
            writeAttribute("stroke", strokeAttr);
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

// 按 Unicode 码点截断 UTF-8。
std::string utf8Preview(const std::string& text, std::size_t maxChars) {
    std::size_t count = 0;
    std::size_t index = 0;
    while (index < text.size() && count < maxChars) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t step = 1;
        if (lead >= 0xF0) step = 4;
        else if (lead >= 0xE0) step = 3;
        else if (lead >= 0xC0) step = 2;
        index += step;
        ++count;
    }
    if (index < text.size()) return text.substr(0, index) + "…";
    return text;
}

} // namespace

// ============================================================================
// 构造 / 布局
// ============================================================================

CameraToolbar::CameraToolbar() {
    clip(true);
    canvas().setBackgroundColor(Color{0, 0, 0, 0});
    groupId_ = canvas().group().add();
}

void CameraToolbar::loadIcons(const std::string& directory) {
    for (int32_t i = 0; i < 8; ++i) {
        std::string markup = readFile(joinPath(directory, kIconFiles[i]));
        if (markup.empty() && kIconFallbacks[i]) {
            markup = readFile(joinPath(directory, kIconFallbacks[i]));
        }
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

void CameraToolbar::setScreenSize(int32_t width, int32_t height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (screenWidth_ == width && screenHeight_ == height) return;
    screenWidth_ = width;
    screenHeight_ = height;
    updateFrameGeometry();
    visualDirty_ = true;
}

int32_t CameraToolbar::scaledInt(float value) const {
    return static_cast<int32_t>(std::lround(value * uiScale_));
}

Vec2i CameraToolbar::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{scaledInt(kCollapsedWidth), scaledInt(kBaseHeight)};
}

void CameraToolbar::onLayout(const Rect& frame) {
    const int32_t w = std::max(1, frame.w);
    const int32_t h = std::max(1, frame.h);
    if (canvas().width() != w || canvas().height() != h) {
        canvas().resize(w, h);
    }
    layoutValid_ = true;
    rebuildVisuals();
}

void CameraToolbar::updateFrameGeometry() {
    // 目标 150%；屏幕高度不足时按可用高度等比收缩。
    const float desiredScale = 1.5f;
    const float available = std::max(320.0f, static_cast<float>(screenHeight_) - 24.0f);
    uiScale_ = std::min(desiredScale, available / static_cast<float>(kBaseHeight));
    uiScale_ = std::max(0.72f, uiScale_);

    const int32_t targetWidth = scaledInt(
        static_cast<float>(kCollapsedWidth) +
        static_cast<float>(kExpandedWidth - kCollapsedWidth) *
            easeOutCubic(expandProgress_));
    const int32_t height = scaledInt(kBaseHeight);
    const int32_t x = chromeVisible_
        ? screenWidth_ - scaledInt(18.0f) - targetWidth
        : screenWidth_ + scaledInt(64.0f);
    const int32_t y = std::max(scaledInt(8.0f), (screenHeight_ - height) / 2);

    if (frame().w != targetWidth || frame().h != height) {
        size(targetWidth, height);
    }
    absolute(x, y);
}

void CameraToolbar::onStateChanged() {
    if (!hovered()) {
        hoverAction_ = Action::None;
        hoverColor_ = -1;
        hoverProgress_ = 0.0f;
        visualDirty_ = true;
    }
}

void CameraToolbar::onTick(float dtSeconds) {
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

void CameraToolbar::setTool(Tool tool) {
    if (tool_ == tool) return;
    tool_ = tool;
    setPanelForTool();
    visualDirty_ = true;
}

void CameraToolbar::setColor(const Color& color) {
    if (color_.r == color.r && color_.g == color.g &&
        color_.b == color.b && color_.a == color.a) {
        return;
    }
    color_ = color;
    visualDirty_ = true;
}

void CameraToolbar::setWidth(float width) {
    const float clamped = clampf(width, 1.0f, 40.0f);
    if (std::fabs(clamped - width_) < 0.05f) return;
    width_ = clamped;
    visualDirty_ = true;
}

void CameraToolbar::setInkParameters(float smoothing01, float penTip01, float taper01) {
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

void CameraToolbar::setHistoryState(bool canUndo) {
    if (canUndo_ == canUndo) return;
    canUndo_ = canUndo;
    visualDirty_ = true;
}

void CameraToolbar::setPageInfo(int32_t index, int32_t count) {
    const int32_t safeIndex = std::max(0, index);
    const int32_t safeCount = std::max(1, count);
    if (pageIndex_ == safeIndex && pageCount_ == safeCount) return;
    pageIndex_ = safeIndex;
    pageCount_ = safeCount;
    visualDirty_ = true;
}

void CameraToolbar::setZoom(float zoom) {
    const float clamped = clampf(zoom, 0.5f, 8.0f);
    if (std::fabs(clamped - zoom_) < 0.005f) return;
    zoom_ = clamped;
    visualDirty_ = true;
}

void CameraToolbar::setFrameRate(int32_t fps) {
    const int32_t clamped = std::max(5, std::min(30, fps));
    if (frameRate_ == clamped) return;
    frameRate_ = clamped;
    visualDirty_ = true;
}

void CameraToolbar::setCameraParams(const PostParams& params) {
    const bool changed =
        std::fabs(params.brightness - cameraParams_.brightness) > 0.002f ||
        std::fabs(params.contrast - cameraParams_.contrast) > 0.002f ||
        std::fabs(params.saturation - cameraParams_.saturation) > 0.002f ||
        std::fabs(params.sharpness - cameraParams_.sharpness) > 0.002f ||
        params.autoEnhance != cameraParams_.autoEnhance ||
        params.mirror != cameraParams_.mirror;
    cameraParams_ = params;
    if (changed) visualDirty_ = true;
}

void CameraToolbar::setFreeze(bool frozen) {
    if (freeze_ == frozen) return;
    freeze_ = frozen;
    visualDirty_ = true;
}

void CameraToolbar::setCameraInfo(const std::string& name, bool connected) {
    if (cameraName_ == name && cameraConnected_ == connected) return;
    cameraName_ = name;
    cameraConnected_ = connected;
    visualDirty_ = true;
}

void CameraToolbar::setChromeVisible(bool visible) {
    if (chromeVisible_ == visible) return;
    chromeVisible_ = visible;
    updateFrameGeometry();
    visualDirty_ = true;
}

void CameraToolbar::setPanelForTool() {
    switch (tool_) {
        case Tool::Pen:
        case Tool::Highlighter:
            panelMode_ = PanelMode::Brush;
            break;
        default:
            panelMode_ = PanelMode::None;
            break;
    }
}

const char* CameraToolbar::toolName() const {
    if (panelMode_ == PanelMode::Camera) return "画面调整";
    if (panelMode_ == PanelMode::InkSettings) return "笔迹参数";
    switch (tool_) {
        case Tool::Pen: return kToolNames[0];
        case Tool::Highlighter: return kToolNames[1];
        case Tool::Eraser: return kToolNames[2];
        case Tool::Laser: return kToolNames[3];
        case Tool::Pan: return kToolNames[4];
        case Tool::Ocr: return kToolNames[7];
    }
    return "";
}

const char* CameraToolbar::toolShortcut() const {
    if (panelMode_ == PanelMode::Camera) {
        return "亮度 / 对比度 / 饱和度 / 锐度";
    }
    if (panelMode_ == PanelMode::InkSettings) {
        return "平滑 / 笔锋 / 起收笔";
    }
    switch (tool_) {
        case Tool::Pen: return "贝塞尔平滑 + 速度笔锋";
        case Tool::Highlighter: return "半透明荧光标注";
        case Tool::Eraser: return "像素擦除（不可撤销）";
        case Tool::Laser: return "授课激光指示";
        case Tool::Pan: return "拖动平移画面 · 滚轮缩放";
        case Tool::Ocr: return "框选画面文字 → 高亮 / 点击复制";
    }
    return "";
}

// ============================================================================
// 交互
// ============================================================================

bool CameraToolbar::isToolAction(Action action, Tool tool) const {
    switch (tool) {
        case Tool::Pen: return action == Action::Pen;
        case Tool::Highlighter: return action == Action::Highlighter;
        case Tool::Eraser: return action == Action::Eraser;
        case Tool::Laser: return action == Action::Laser;
        case Tool::Pan: return action == Action::Pan;
        case Tool::Ocr: return action == Action::Ocr;
    }
    return false;
}

CameraToolbar::Action CameraToolbar::actionAt(const Vec2i& point) const {
    for (const HitRegion& region : buttonRegions_) {
        if (region.rect.contains(point)) return region.action;
    }
    return Action::None;
}

int32_t CameraToolbar::colorAt(const Vec2i& point) const {
    for (size_t i = 0; i < colorSwatches_.size(); ++i) {
        if (colorSwatches_[i].contains(point)) return static_cast<int32_t>(i);
    }
    return -1;
}

int32_t CameraToolbar::sliderAt(const Vec2i& point) const {
    if (!widthSliderRect_.isNull() &&
        widthSliderRect_.inflate(0, scaledInt(12.0f)).contains(point)) {
        return 0;
    }
    for (size_t i = 0; i < inkSliderRects_.size(); ++i) {
        if (!inkSliderRects_[i].isNull() &&
            inkSliderRects_[i].inflate(0, scaledInt(12.0f)).contains(point)) {
            return static_cast<int32_t>(i) + 1;
        }
    }
    for (size_t i = 0; i < cameraSliderRects_.size(); ++i) {
        if (!cameraSliderRects_[i].isNull() &&
            cameraSliderRects_[i].inflate(0, scaledInt(12.0f)).contains(point)) {
            return static_cast<int32_t>(i) + 4;
        }
    }
    return -1;
}

void CameraToolbar::setSliderValue(int32_t slider, int32_t localX) {
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

    if (slider >= 1 && slider <= 3) {
        const int32_t index = slider - 1;
        const Rect& track = inkSliderRects_[static_cast<size_t>(index)];
        if (track.isNull() || track.w <= 0) return;
        const float t = clampf(
            static_cast<float>(localX - track.x) /
                static_cast<float>(track.w), 0.0f, 1.0f);

        if (index == 0) smoothing01_ = t;
        else if (index == 1) penTip01_ = t;
        else taper01_ = t;

        visualDirty_ = true;
        if (onInkParametersChanged) {
            onInkParametersChanged(smoothing01_, penTip01_, taper01_);
        }
        return;
    }

    if (slider >= 4 && slider <= 8) {
        const int32_t index = slider - 4;
        const Rect& track = cameraSliderRects_[static_cast<size_t>(index)];
        if (track.isNull() || track.w <= 0) return;
        const float t = clampf(
            static_cast<float>(localX - track.x) /
                static_cast<float>(track.w), 0.0f, 1.0f);

        if (index == 0) cameraParams_.brightness = t;
        else if (index == 1) cameraParams_.contrast = t;
        else if (index == 2) cameraParams_.saturation = t;
        else if (index == 3) cameraParams_.sharpness = t;
        else {
            const int32_t fps = 5 + static_cast<int32_t>(
                std::lround(t * 25.0f));
            if (frameRate_ == fps) return;
            frameRate_ = fps;
            visualDirty_ = true;
            if (onFrameRateChanged) onFrameRateChanged(frameRate_);
            return;
        }

        visualDirty_ = true;
        if (onCameraParamsChanged) onCameraParamsChanged(cameraParams_);
    }
}

void CameraToolbar::activate(Action action) {
    switch (action) {
        case Action::Pen:
            setTool(Tool::Pen);
            if (onToolChanged) onToolChanged(Tool::Pen);
            break;
        case Action::Highlighter:
            setTool(Tool::Highlighter);
            if (onToolChanged) onToolChanged(Tool::Highlighter);
            break;
        case Action::Eraser:
            setTool(Tool::Eraser);
            if (onToolChanged) onToolChanged(Tool::Eraser);
            break;
        case Action::Laser:
            setTool(Tool::Laser);
            if (onToolChanged) onToolChanged(Tool::Laser);
            break;
        case Action::Pan:
            setTool(Tool::Pan);
            if (onToolChanged) onToolChanged(Tool::Pan);
            break;
        case Action::Ocr:
            setTool(Tool::Ocr);
            if (onToolChanged) onToolChanged(Tool::Ocr);
            break;
        case Action::Snapshot:
            if (onSnapshot) onSnapshot();
            break;
        case Action::CameraPanel:
            panelMode_ = (panelMode_ == PanelMode::Camera) ? PanelMode::None
                                                           : PanelMode::Camera;
            break;
        case Action::Undo: if (onUndo) onUndo(); break;
        case Action::Clear: if (onClear) onClear(); break;
        case Action::InkSettings:
            panelMode_ = (panelMode_ == PanelMode::InkSettings)
                ? PanelMode::None : PanelMode::InkSettings;
            break;
        case Action::ZoomOut: if (onZoomOut) onZoomOut(); break;
        case Action::ZoomIn: if (onZoomIn) onZoomIn(); break;
        case Action::Fit: if (onFit) onFit(); break;
        case Action::AutoEnhance:
            cameraParams_.autoEnhance = !cameraParams_.autoEnhance;
            if (onCameraParamsChanged) onCameraParamsChanged(cameraParams_);
            break;
        case Action::Mirror:
            cameraParams_.mirror = !cameraParams_.mirror;
            if (onCameraParamsChanged) onCameraParamsChanged(cameraParams_);
            break;
        case Action::Freeze:
            freeze_ = !freeze_;
            if (onFreezeChanged) onFreezeChanged(freeze_);
            break;
        case Action::SwitchCamera: if (onSwitchCamera) onSwitchCamera(); break;
        case Action::ResetCamera:
            cameraParams_ = PostParams{};
            if (onCameraParamsChanged) onCameraParamsChanged(cameraParams_);
            break;
        case Action::AddPage: if (onAddPage) onAddPage(); break;
        case Action::PrevPage: if (onPrevPage) onPrevPage(); break;
        case Action::NextPage: if (onNextPage) onNextPage(); break;
        case Action::Close: if (onClose) onClose(); break;
        default: break;
    }
    visualDirty_ = true;
}

bool CameraToolbar::onEvent(const modern_ui::Event& ev) {
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
                draggingSlider_ = -1;
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

void CameraToolbar::rebuildVisuals() {
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
    for (Rect& rect : inkSliderRects_) rect = Rect::null();
    for (Rect& rect : cameraSliderRects_) rect = Rect::null();

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
        buttonRegions_.push_back(HitRegion{rect, action});
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

        auto drawSlider = [&](Rect* target, float x, float labelBaseline,
                              const char* label, const char* valueText,
                              float value) {
            addText(label, x, labelBaseline, SI(12), kTextSecondary, panelAlpha);
            addText(valueText, static_cast<float>(panelRight - SI(44)),
                    labelBaseline, SI(12), kAccent, panelAlpha);

            const int32_t trackX = static_cast<int32_t>(x);
            const int32_t trackY = static_cast<int32_t>(
                std::lround(labelBaseline + S(12.0f)));
            const int32_t trackW = panelRight - panelX;
            const int32_t trackH = SI(6);
            const int32_t fillW = static_cast<int32_t>(std::lround(
                static_cast<float>(trackW) * clampf(value, 0.0f, 1.0f)));

            addRect(trackX, trackY, trackW, trackH, S(3.0f),
                    Color{255, 255, 255, 22}, Color{0, 0, 0, 0}, 0);
            addRect(trackX, trackY, std::max(SI(2), fillW), trackH, S(3.0f),
                    alphaOf(kAccent, static_cast<uint8_t>(230 * panelAlpha)),
                    Color{0, 0, 0, 0}, 0);
            addCircle(static_cast<float>(trackX + fillW),
                      static_cast<float>(trackY + trackH / 2), S(7.0f),
                      Color{250, 252, 255, 255}, Color{96, 205, 255, 255}, sw2);

            if (target) *target = Rect{trackX, trackY, trackW, trackH};
        };

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
            char valueText[32];
            std::snprintf(valueText, sizeof(valueText), "%.0f px", width_);
            drawSlider(&widthSliderRect_, static_cast<float>(panelX),
                       sliderY, highlighter ? "荧光笔宽" : "笔宽",
                       valueText, (width_ - 1.0f) / 39.0f);

            addText("速度模拟笔锋 · 书写越快越细",
                    static_cast<float>(panelX), sliderY + S(52.0f), SI(10),
                    kTextTertiary, panelAlpha * 0.95f);
        } else if (panelMode_ == PanelMode::InkSettings) {
            const float startY = S(78.0f);
            const float step = S(56.0f);

            char text0[16], text1[16], text2[16];
            std::snprintf(text0, sizeof(text0), "%.0f%%", smoothing01_ * 100.0f);
            std::snprintf(text1, sizeof(text1), "%.0f%%", penTip01_ * 100.0f);
            std::snprintf(text2, sizeof(text2), "%.0f%%", taper01_ * 100.0f);

            drawSlider(&inkSliderRects_[0], static_cast<float>(panelX),
                       startY, "EMA 平滑", text0, smoothing01_);
            drawSlider(&inkSliderRects_[1], static_cast<float>(panelX),
                       startY + step, "笔锋强度", text1, penTip01_);
            drawSlider(&inkSliderRects_[2], static_cast<float>(panelX),
                       startY + step * 2.0f, "起收笔锥化", text2, taper01_);

            addText("平滑越大越稳，跟手延迟略增",
                    static_cast<float>(panelX), startY + step * 3.0f, SI(10),
                    kTextTertiary, panelAlpha * 0.95f);
            addText("笔锋 = 速度对笔宽的影响",
                    static_cast<float>(panelX), startY + step * 3.0f + S(16.0f),
                    SI(10), kTextTertiary, panelAlpha * 0.95f);
        } else if (panelMode_ == PanelMode::Camera) {
            const float startY = S(78.0f);
            const float step = S(46.0f);

            char text0[20], text1[20], text2[20], text3[20], text4[20];
            std::snprintf(text0, sizeof(text0), "%+d%%",
                          static_cast<int>(std::lround(
                              (cameraParams_.brightness - 0.5f) * 200.0f)));
            std::snprintf(text1, sizeof(text1), "×%.2f",
                          0.5f + cameraParams_.contrast * 1.3f);
            std::snprintf(text2, sizeof(text2), "×%.2f",
                          cameraParams_.saturation * 2.0f);
            std::snprintf(text3, sizeof(text3), "%.0f%%",
                          cameraParams_.sharpness * 100.0f);
            std::snprintf(text4, sizeof(text4), "%d fps", frameRate_);

            drawSlider(&cameraSliderRects_[0], static_cast<float>(panelX),
                       startY, "亮度", text0, cameraParams_.brightness);
            drawSlider(&cameraSliderRects_[1], static_cast<float>(panelX),
                       startY + step, "对比度", text1, cameraParams_.contrast);
            drawSlider(&cameraSliderRects_[2], static_cast<float>(panelX),
                       startY + step * 2.0f, "饱和度", text2,
                       cameraParams_.saturation);
            drawSlider(&cameraSliderRects_[3], static_cast<float>(panelX),
                       startY + step * 3.0f, "锐度", text3,
                       cameraParams_.sharpness);
            drawSlider(&cameraSliderRects_[4], static_cast<float>(panelX),
                       startY + step * 4.0f, "采集帧率", text4,
                       static_cast<float>(frameRate_ - 5) / 25.0f);

            // 开关 Chip：自动增强 / 镜像 / 冻结
            const int32_t chipY = SI(310);
            const int32_t chipW = SI(64);
            const int32_t chipH = SI(28);
            const int32_t gap = SI(7);

            auto drawChip = [&](int32_t index, Action action,
                                const char* label, bool active) {
                const int32_t x = panelX + index * (chipW + gap);
                const Rect rect{x, chipY, chipW, chipH};
                const float hover = (hoverAction_ == action) ? hoverProgress_ : 0.0f;
                const Color fill = active
                    ? Color{96, 205, 255, static_cast<uint8_t>(46 + hover * 18)}
                    : Color{255, 255, 255, static_cast<uint8_t>(14 + hover * 10)};
                const Color stroke = active ? Color{96, 205, 255, 120}
                                            : Color{255, 255, 255, 26};
                addRect(rect.x, rect.y, rect.w, rect.h, S(10.0f),
                        fill, stroke, sw1);
                addText(label, static_cast<float>(rect.x + SI(11)),
                        static_cast<float>(rect.y + SI(19)), SI(10),
                        active ? Color{230, 246, 255, 255} : kTextSecondary,
                        panelAlpha);
                buttonRegions_.push_back(HitRegion{rect, action});
            };

            drawChip(0, Action::AutoEnhance, "自动增强",
                     cameraParams_.autoEnhance);
            drawChip(1, Action::Mirror, "镜像", cameraParams_.mirror);
            drawChip(2, Action::Freeze, "冻结", freeze_);

            // 切换摄像头 / 恢复默认
            const int32_t buttonY = SI(350);
            const int32_t halfW = SI(99);
            const int32_t halfH = SI(30);
            const Rect switchRect{panelX, buttonY, halfW, halfH};
            const Rect resetRect{panelX + halfW + SI(8), buttonY, halfW, halfH};

            auto drawTextButton = [&](const Rect& rect, Action action,
                                      const char* label) {
                const float hover = (hoverAction_ == action) ? hoverProgress_ : 0.0f;
                addRect(rect.x, rect.y, rect.w, rect.h, S(9.0f),
                        Color{255, 255, 255, static_cast<uint8_t>(12 + hover * 16)},
                        Color{255, 255, 255, 22}, sw1);
                addText(label, static_cast<float>(rect.x + SI(10)),
                        static_cast<float>(rect.y + SI(20)), SI(10),
                        kTextSecondary, panelAlpha);
                buttonRegions_.push_back(HitRegion{rect, action});
            };

            drawTextButton(switchRect, Action::SwitchCamera, "切换摄像头");
            drawTextButton(resetRect, Action::ResetCamera, "恢复默认");

            const std::string cameraText = cameraConnected_
                ? "设备：" + utf8Preview(cameraName_, 18)
                : "设备：未连接";
            addText(cameraText, static_cast<float>(panelX), S(404.0f), SI(9),
                    cameraConnected_ ? Color{150, 230, 180, 255}
                                     : Color{255, 130, 120, 255},
                    panelAlpha);

            addText("滚轮缩放 · 中键拖动画面",
                    static_cast<float>(panelX), S(428.0f), SI(9),
                    kTextTertiary, panelAlpha * 0.95f);
            addText("Ctrl+0 适应屏幕 · F10 冻结画面",
                    static_cast<float>(panelX), S(444.0f), SI(9),
                    kTextTertiary, panelAlpha * 0.95f);
            addText("拍照自动复制并保存到 图片/CameraPro",
                    static_cast<float>(panelX), S(460.0f), SI(9),
                    kTextTertiary, panelAlpha * 0.95f);
        }
    }

    // -------------------- 图标列 --------------------
    int32_t y = SI(12);
    const int32_t buttonX = columnX + (iconColumn - button) / 2;

    static const Action toolActions[8] = {
        Action::Pen, Action::Highlighter, Action::Eraser, Action::Laser,
        Action::Pan, Action::Snapshot, Action::CameraPanel, Action::Ocr,
    };

    for (int32_t i = 0; i < 8; ++i) {
        const Rect rect{buttonX, y, button, button};
        const Action action = toolActions[i];
        bool selected = false;
        if (i == 6) {
            selected = (panelMode_ == PanelMode::Camera);
        } else if (i != 5) {
            selected = isToolAction(action, tool_);
        }

        drawToolButton(action, rect, false, selected,
                       iconWhite_[static_cast<size_t>(i)],
                       iconAccent_[static_cast<size_t>(i)],
                       iconDim_[static_cast<size_t>(i)]);
        addRegion(rect, action);

        if (i == 6) {
            addCircle(static_cast<float>(rect.x + rect.w - SI(8)),
                      static_cast<float>(rect.y + SI(8)), S(3.0f),
                      cameraConnected_ ? Color{105, 226, 138, 255}
                                       : Color{255, 110, 96, 255},
                      Color{0, 0, 0, 0}, 0);
        }

        y += rowStep;
    }

    addLine(static_cast<float>(columnX + SI(10)), static_cast<float>(y + SI(2)),
            static_cast<float>(w - SI(10)), static_cast<float>(y + SI(2)),
            Color{255, 255, 255, 14}, sw1);
    y += SI(10);

    // 撤销 / 清屏
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

    // 笔迹参数
    {
        const Rect rect{buttonX, y, button, button};
        const bool selected = (panelMode_ == PanelMode::InkSettings);
        drawToolButton(Action::InkSettings, rect, false, selected,
                       settingsIconWhite_, settingsIconAccent_, settingsIconDim_);
        addRegion(rect, Action::InkSettings);
        y += rowStep;
    }

    // 缩放：− / 百分比（点击适应） / +
    {
        const int32_t rowY = y + SI(2);
        const int32_t smallButton = SI(20);
        const int32_t rowHeight = SI(30);

        const Rect outRect{columnX + SI(2), rowY, smallButton, rowHeight};
        const Rect labelRect{columnX + SI(22), rowY, SI(24), rowHeight};
        const Rect inRect{columnX + SI(46), rowY, smallButton, rowHeight};

        auto drawZoomButton = [&](const Rect& rect, Action action, bool plus) {
            const float hover = (hoverAction_ == action) ? hoverProgress_ : 0.0f;
            if (hover > 0.02f) {
                addRect(rect.x, rect.y, rect.w, rect.h, S(8.0f),
                        Color{255, 255, 255, static_cast<uint8_t>(16 * hover)},
                        Color{0, 0, 0, 0}, 0);
            }
            const float cx = static_cast<float>(rect.x + rect.w / 2);
            const float cy = static_cast<float>(rect.y + rect.h / 2);
            addLine(cx - S(5), cy, cx + S(5), cy, Color{210, 218, 232, 255}, sw2);
            if (plus) {
                addLine(cx, cy - S(5), cx, cy + S(5), Color{210, 218, 232, 255}, sw2);
            }
            buttonRegions_.push_back(HitRegion{rect, action});
        };

        drawZoomButton(outRect, Action::ZoomOut, false);
        drawZoomButton(inRect, Action::ZoomIn, true);

        // 百分比标签：点击 = 适应屏幕
        {
            const float hover = (hoverAction_ == Action::Fit) ? hoverProgress_ : 0.0f;
            if (hover > 0.02f) {
                addRect(labelRect.x, labelRect.y, labelRect.w, labelRect.h, S(8.0f),
                        Color{255, 255, 255, static_cast<uint8_t>(16 * hover)},
                        Color{0, 0, 0, 0}, 0);
            }
            char zoomText[16];
            std::snprintf(zoomText, sizeof(zoomText), "%d%%",
                          static_cast<int>(std::lround(zoom_ * 100.0f)));
            addText(zoomText, static_cast<float>(labelRect.x + SI(1)),
                    static_cast<float>(rowY + SI(19)), SI(9), kTextSecondary, 1.0f);
            buttonRegions_.push_back(HitRegion{labelRect, Action::Fit});
        }

        y += SI(36);
    }

    // 翻页
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
        std::snprintf(pageText, sizeof(pageText), "%d/%d",
                      pageIndex_ + 1, pageCount_);
        addText(pageText, static_cast<float>(labelRect.x + SI(2)),
                static_cast<float>(rowY + SI(19)), SI(10), kTextSecondary, 1.0f);

        y += SI(38);
    }

    // 新建页
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

    // 退出
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

} // namespace camerapro
