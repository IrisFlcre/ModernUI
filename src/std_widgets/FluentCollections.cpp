// ============================================================================
// FluentCollections.cpp — 布局 / 集合 / 导航控件实现
// ============================================================================

#include "FluentCollections.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace modern_ui {
namespace fluent {
namespace {

constexpr int kKeyLeft = 0x25;
constexpr int kKeyUp = 0x26;
constexpr int kKeyRight = 0x27;
constexpr int kKeyDown = 0x28;
constexpr int kKeyHome = 0x24;
constexpr int kKeyEnd = 0x23;
constexpr int kKeyPageUp = 0x21;
constexpr int kKeyPageDown = 0x22;
constexpr int kKeySpace = 0x20;
constexpr int kKeyEnter = 0x0D;

std::string firstChar(const std::string& text) {
    if (text.empty()) return {};
    const unsigned char first = static_cast<unsigned char>(text.front());
    size_t length = 1;
    if ((first & 0xE0u) == 0xC0u) length = 2;
    else if ((first & 0xF0u) == 0xE0u) length = 3;
    else if ((first & 0xF8u) == 0xF0u) length = 4;
    return text.substr(0, std::min(length, text.size()));
}

int32_t scrollbarThumbHeight(int32_t trackHeight, float viewport, float content) {
    if (content <= viewport || content <= 0.0f) return 0;
    const float ratio = viewport / content;
    return std::max(24, std::min(trackHeight, static_cast<int32_t>(std::lround(trackHeight * ratio))));
}

} // namespace

// ===========================================================================
// StackPanel
// ===========================================================================
StackPanel::StackPanel(FlexDirection direction, int32_t spacing, int32_t padding)
    : ParentWidget() {
    style().direction = direction;
    style().gap = std::max(0, spacing);
    style().crossGap = std::max(0, spacing);
    style().padding = Edges{std::max(0, padding)};
}

// ===========================================================================
// Card
// ===========================================================================
Card::Card() {
    style().padding = Edges{16};
    Shape background = makeRRectShape(Rect{0, 0, 1, 1}, theme().radiusCard,
                                      theme().card, theme().stroke, 1);
    backgroundId_ = canvas().addShape(std::move(background));
}

Card::Card(Color fill, Color stroke, float radius, int32_t padding)
    : useThemeFill_(false), useThemeStroke_(false),
      fill_(fill), stroke_(stroke), radius_(radius) {
    style().padding = Edges{std::max(0, padding)};
    Shape background = makeRRectShape(Rect{0, 0, 1, 1},
                                      radius_ >= 0.0f ? radius_ : theme().radiusCard,
                                      fill_, stroke_, 1);
    backgroundId_ = canvas().addShape(std::move(background));
}

void Card::setFill(Color fill) {
    useThemeFill_ = false;
    fill_ = fill;
    if (laidOut()) updateVisuals();
}

void Card::setStroke(Color stroke) {
    useThemeStroke_ = false;
    stroke_ = stroke;
    if (laidOut()) updateVisuals();
}

void Card::setRadius(float radius) {
    radius_ = radius;
    if (laidOut()) updateVisuals();
}

void Card::setPadding(int32_t padding) {
    style().padding = Edges{std::max(0, padding)};
    invalidateLayout();
}

void Card::updateVisuals() {
    if (!laidOut()) return;
    const Color fill = useThemeFill_ ? theme().card : fill_;
    const Color stroke = useThemeStroke_ ? theme().stroke : stroke_;
    const float radius = radius_ >= 0.0f ? radius_ : theme().radiusCard;

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, frameWidth(), frameHeight(), radius, radius};
        shape.base.fill = fill;
        shape.base.stroke = stroke;
        shape.base.strokeWidth = 1;
        shape.base.visible = true;
    });
}

// ===========================================================================
// AcrylicPanel
// ===========================================================================
AcrylicPanel::AcrylicPanel(Color tint, float radius)
    : tint_(tint), radius_(radius) {
    style().padding = Edges{16};
    Shape background = makeRRectShape(Rect{0, 0, 1, 1}, radius_, theme().layer,
                                      theme().stroke, 1);
    backgroundId_ = canvas().addShape(std::move(background));
}

void AcrylicPanel::setTint(Color tint) {
    tint_ = tint;
    if (laidOut()) updateVisuals();
}

void AcrylicPanel::setRadius(float radius) {
    radius_ = radius;
    if (laidOut()) updateVisuals();
}

void AcrylicPanel::updateVisuals() {
    if (!laidOut()) return;

    const Color tint = tint_.a > 0 ? tint_ : theme().accent;
    LinearGradient gradient;
    gradient.from = Vec2f{0.0f, 0.0f};
    gradient.to = Vec2f{static_cast<float>(frameWidth()), static_cast<float>(frameHeight())};
    gradient.spread = SpreadMethod::Pad;
    gradient.stops.push_back(GradientStop{0.0f, withOpacity(mixColor(theme().layerAlt, tint, 0.16f), 0.94f)});
    gradient.stops.push_back(GradientStop{0.52f, withOpacity(mixColor(theme().layer, tint, 0.08f), 0.90f)});
    gradient.stops.push_back(GradientStop{1.0f, withOpacity(mixColor(theme().cardAlt, tint, 0.03f), 0.88f)});

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, frameWidth(), frameHeight(), radius_, radius_};
        shape.base.fill = Paint::linear(gradient);
        shape.base.stroke = theme().stroke;
        shape.base.strokeWidth = 1;
        shape.base.visible = true;
    });
}

// ===========================================================================
// Chip
// ===========================================================================
Chip::Chip(std::string text, bool selected)
    : text_(std::move(text)), selected_(selected) {
    Shape background = makeRRectShape(Rect{0, 0, 1, 28}, 14.0f,
                                      theme().controlFill, theme().stroke, 1);
    backgroundId_ = canvas().addShape(std::move(background));

    Shape label = makeTextShape(text_, fontSize_, theme().textPrimary);
    textId_ = canvas().addShape(std::move(label));

    Shape closeBackground = makeCircleShape(0, 0, 8.0f, Color::nullColor());
    closeBackground.base.visible = false;
    closeBackgroundId_ = canvas().addShape(std::move(closeBackground));

    Shape closeGlyph = makeXShape(0, 0, 6, theme().textSecondary, 1);
    closeGlyph.base.visible = false;
    closeGlyphId_ = canvas().addShape(std::move(closeGlyph));
}

void Chip::setText(std::string text) {
    if (text == text_) return;
    text_ = std::move(text);
    measured_ = Rect::null();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void Chip::setSelected(bool selected) {
    if (selected_ == selected) return;
    selected_ = selected;
    if (laidOut()) updateVisuals();
}

void Chip::setClosable(bool closable) {
    if (closable_ == closable) return;
    closable_ = closable;
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

Vec2i Chip::onMeasure(const Vec2i& available) const {
    (void)available;
    measured_ = measureString(text_, fontSize_);
    const int32_t width = 12 + measured_.w + 12 + (closable_ ? 20 : 0);
    return Vec2i{std::max(44, width), 28};
}

void Chip::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (width <= 0 || height <= 0) return;
    if (measured_.isNull()) measured_ = measureString(text_, fontSize_);

    const float radius = static_cast<float>(height) * 0.5f;

    Color fill = enabled()
        ? interactiveFill(theme().controlFill, theme().controlFillHover,
                          theme().controlFillPressed)
        : theme().controlFillDisabled;
    if (selected_) {
        Color accent = enabled()
            ? interactiveFill(theme().accent, theme().accentHover, theme().accentPressed)
            : theme().accentDisabled;
        fill = mixColor(fill, accent, 0.92f);
    }

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, width, height, radius, radius};
        shape.base.fill = fill;
        shape.base.stroke = selected_ ? Paint{theme().accent} : theme().stroke;
        shape.base.strokeWidth = 1;
        shape.base.visible = true;
    });

    const Color textColor = !enabled() ? theme().textDisabled
                          : selected_  ? theme().textOnAccent
                                       : theme().textPrimary;
    const int32_t textX = 12;
    const int32_t baseline = baselineForCenter(measured_, height);
    canvas().updateShape(textId_, [&](Shape& shape) {
        setShapeText(shape, text_, fontSize_, textColor, textX, baseline);
    });

    if (!closable_) {
        canvas().updateShape(closeBackgroundId_, [&](Shape& shape) { shape.base.visible = false; });
        canvas().updateShape(closeGlyphId_, [&](Shape& shape) { shape.base.visible = false; });
        return;
    }

    const int32_t closeSize = 16;
    const int32_t closeX = width - closeSize - 6;
    const int32_t closeY = (height - closeSize) / 2;
    const Color closeFill = closeHover_
        ? (selected_ ? withOpacity(theme().textOnAccent, 0.20f) : theme().subtleHover)
        : Color::nullColor();
    canvas().updateShape(closeBackgroundId_, [&](Shape& shape) {
        shape.data = CircleShape{closeX + closeSize / 2, closeY + closeSize / 2,
                                 static_cast<float>(closeSize) * 0.5f};
        shape.base.fill = closeFill;
        shape.base.stroke = Paint{};
        shape.base.visible = closeFill.a > 0;
    });
    canvas().updateShape(closeGlyphId_, [&](Shape& shape) {
        const int32_t size = 6;
        shape = makeXShape(closeX + (closeSize - size) / 2,
                           closeY + (closeSize - size) / 2, size,
                           selected_ ? theme().textOnAccent : theme().textSecondary, 1);
        shape.base.visible = true;
    });
}

bool Chip::closeHit(int32_t x, int32_t y) const {
    if (!closable_) return false;
    const int32_t closeSize = 20;
    const int32_t closeX = frameWidth() - closeSize - 2;
    return x >= closeX && x < frameWidth() && y >= (frameHeight() - closeSize) / 2 &&
           y < (frameHeight() + closeSize) / 2;
}

bool Chip::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseMove: {
        const bool hover = closeHit(ev.x, ev.y);
        if (hover != closeHover_) {
            closeHover_ = hover;
            if (laidOut()) updateVisuals();
        }
        return false;
    }

    case EventType::MouseDown:
        return true;

    case EventType::MouseUp:
        if (!clicked()) return false;
        if (closeHit(ev.x, ev.y)) {
            if (onClose) onClose();
            return true;
        }
        if (selectable_) activate();
        return true;

    case EventType::KeyDown:
        if (ev.key == kKeySpace || ev.key == kKeyEnter) {
            activate();
            return true;
        }
        return FluentControl::onEvent(ev);

    default:
        return FluentControl::onEvent(ev);
    }
}

void Chip::onStateChanged() {
    if (!hovered() && closeHover_) {
        closeHover_ = false;
        if (laidOut()) updateVisuals();
    }
}

void Chip::activate() {
    if (!enabled() || !selectable_) return;
    selected_ = !selected_;
    if (onSelectedChanged) onSelectedChanged(selected_);
    if (onClick) onClick();
}

// ===========================================================================
// Expander
// ===========================================================================
Expander::Expander(std::string header, bool expanded)
    : header_(std::move(header)), expanded_(expanded) {
    style().direction = FlexDirection::Column;
    style().alignItems = AlignItems::Stretch;
    style().padding = Edges{headerHeight_, 16, expanded_ ? 12 : 0, 16};

    Shape background = makeRRectShape(Rect{0, 0, 1, 1}, theme().radiusCard,
                                      theme().card, theme().stroke, 1);
    backgroundId_ = canvas().addShape(std::move(background));

    Shape hover = makeRRectShape(Rect{0, 0, 1, 1}, theme().radiusCard,
                                 Color::nullColor());
    hover.base.visible = false;
    headerHoverId_ = canvas().addShape(std::move(hover));

    Shape title = makeTextShape(header_, 14, theme().textPrimary);
    titleId_ = canvas().addShape(std::move(title));

    Shape chevron = makeChevronRightShape(0, 0, 6, 10, theme().textSecondary, 2);
    chevronId_ = canvas().addShape(std::move(chevron));

    Shape divider = makeRectShape(Rect{0, 0, 1, 1}, theme().divider);
    divider.base.visible = expanded_;
    dividerId_ = canvas().addShape(std::move(divider));
}

void Expander::setHeader(std::string header) {
    if (header == header_) return;
    header_ = std::move(header);
    headerMeasured_ = Rect::null();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void Expander::setContent(std::unique_ptr<Widget> content) {
    clearChildren();
    if (content) addChild(std::move(content));
    invalidateLayout();
}

Widget* Expander::content() const {
    return children().empty() ? nullptr : children().front().get();
}

void Expander::setExpanded(bool expanded) {
    if (expanded_ == expanded) return;
    expanded_ = expanded;
    style().padding = Edges{headerHeight_, 16, expanded_ ? 12 : 0, 16};
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void Expander::toggle() {
    if (!enabled()) return;
    setExpanded(!expanded_);
    if (onExpandedChanged) onExpandedChanged(expanded_);
}

Vec2i Expander::onMeasure(const Vec2i& available) const {
    int32_t contentHeight = 0;
    if (expanded_ && !children().empty()) {
        const int32_t contentWidth = std::max(0, available.x - 32);
        contentHeight = std::max(0, children().front()->onMeasure(
            Vec2i{contentWidth, std::max(0, available.y - headerHeight_)}).y);
    }

    int32_t width = available.x > 0 ? available.x : 240;
    return Vec2i{width, headerHeight_ + (expanded_ ? contentHeight + 12 : 0)};
}

void Expander::updateVisuals() {
    if (!laidOut()) return;

    const int32_t width = frameWidth();
    const int32_t height = frameHeight();

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, width, height, theme().radiusCard, theme().radiusCard};
        shape.base.fill = theme().card;
        shape.base.stroke = theme().stroke;
        shape.base.strokeWidth = 1;
        shape.base.visible = true;
    });

    canvas().updateShape(headerHoverId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, width, headerHeight_, theme().radiusCard, theme().radiusCard};
        shape.base.fill = theme().subtleHover;
        shape.base.visible = headerHover_ && enabled();
    });

    if (headerMeasured_.isNull()) headerMeasured_ = measureString(header_, 14);
    const int32_t baseline = baselineForCenter(headerMeasured_, headerHeight_);
    canvas().updateShape(titleId_, [&](Shape& shape) {
        setShapeText(shape, header_, 14,
                enabled() ? theme().textPrimary : theme().textDisabled, 40, baseline);
    });

    canvas().updateShape(chevronId_, [&](Shape& shape) {
        const int32_t centerY = headerHeight_ / 2;
        if (expanded_) {
            shape = makeChevronShape(14, centerY - 3, 10, 6,
                                     true, theme().textSecondary, 2);
        } else {
            shape = makeChevronRightShape(16, centerY - 5, 6, 10,
                                          theme().textSecondary, 2);
        }
        shape.base.visible = true;
    });

    canvas().updateShape(dividerId_, [&](Shape& shape) {
        shape.data = RectShape{16, headerHeight_ - 1, std::max(0, width - 32), 1};
        shape.base.fill = theme().divider;
        shape.base.visible = expanded_;
    });
}

bool Expander::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseMove:
        if (headerHover_ != (ev.y < headerHeight_)) {
            headerHover_ = ev.y < headerHeight_;
            if (laidOut()) updateVisuals();
        }
        return false;

    case EventType::MouseDown:
        return ev.y < headerHeight_;

    case EventType::MouseUp:
        if (ev.y < headerHeight_ && clicked()) {
            toggle();
            return true;
        }
        return false;

    case EventType::KeyDown:
        if (ev.key == kKeySpace || ev.key == kKeyEnter) {
            toggle();
            return true;
        }
        return false;

    default:
        return false;
    }
}

void Expander::onStateChanged() {
    if (!hovered() && headerHover_) {
        headerHover_ = false;
        if (laidOut()) updateVisuals();
    }
}

// ===========================================================================
// ScrollViewer
// ===========================================================================
ScrollViewer::ScrollViewer() {
    style().clip = true;

    Shape track = makeRRectShape(Rect{0, 0, 4, 1}, 2.0f,
                                 withOpacity(theme().textPrimary, 0.08f));
    track.base.visible = false;
    trackId_ = canvas().addShape(std::move(track));

    Shape thumb = makeRRectShape(Rect{0, 0, 4, 1}, 2.0f,
                                 withOpacity(theme().textSecondary, 0.55f));
    thumb.base.visible = false;
    thumbId_ = canvas().addShape(std::move(thumb));
}

void ScrollViewer::setContent(std::unique_ptr<Widget> content) {
    if (content_) {
        removeChild(content_);
        content_ = nullptr;
    }

    if (!content) {
        scrollOffset_ = 0.0f;
        contentHeight_ = 0.0f;
        invalidateLayout();
        return;
    }

    content_ = addChild(std::move(content));
    content_->style().position = Position::Absolute;
    content_->style().x = 0;
    content_->style().y = 0;
    content_->style().width = Dimension::percent(100.0f);

    scrollOffset_ = 0.0f;
    contentHeight_ = 0.0f;
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

float ScrollViewer::maxScroll() const {
    return std::max(0.0f, contentHeight_ - static_cast<float>(frameHeight()));
}

void ScrollViewer::scrollTo(float offset) {
    const float next = std::max(0.0f, std::min(offset, maxScroll()));
    if (std::fabs(next - scrollOffset_) < 0.01f) return;
    scrollOffset_ = next;
    applyScrollOffset();
}

void ScrollViewer::scrollBy(float delta) {
    scrollTo(scrollOffset_ + delta);
}

void ScrollViewer::applyScrollOffset() {
    if (!content_) return;
    content_->style().y = -static_cast<int32_t>(std::lround(scrollOffset_));
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

Vec2i ScrollViewer::onMeasure(const Vec2i& available) const {
    return Vec2i{std::max(0, available.x), std::max(0, available.y)};
}

void ScrollViewer::onLayout(const Rect& frame) {
    FluentControl::onLayout(frame);

    if (content_) {
        contentHeight_ = static_cast<float>(content_->frame().h);
        const float maximum = maxScroll();
        if (scrollOffset_ > maximum) {
            scrollOffset_ = maximum;
            content_->style().y = -static_cast<int32_t>(std::lround(scrollOffset_));
        }
    } else {
        contentHeight_ = 0.0f;
    }
    updateVisuals();
}

void ScrollViewer::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (width <= 0 || height <= 0) {
        canvas().updateShape(trackId_, [](Shape& shape) { shape.base.visible = false; });
        canvas().updateShape(thumbId_, [](Shape& shape) { shape.base.visible = false; });
        return;
    }

    const float maximum = maxScroll();
    if (!content_ || maximum <= 0.0f) {
        canvas().updateShape(trackId_, [](Shape& shape) { shape.base.visible = false; });
        canvas().updateShape(thumbId_, [](Shape& shape) { shape.base.visible = false; });
        return;
    }

    const int32_t trackTop = 6;
    const int32_t trackHeight = std::max(1, height - 12);
    const int32_t thumbHeight = scrollbarThumbHeight(trackHeight,
                                                     static_cast<float>(height),
                                                     contentHeight_);
    const float ratio = std::max(0.0f, std::min(1.0f, scrollOffset_ / maximum));
    const int32_t thumbY = trackTop + static_cast<int32_t>(std::lround(
        static_cast<float>(trackHeight - thumbHeight) * ratio));

    canvas().updateShape(trackId_, [&](Shape& shape) {
        shape.data = RRectShape{width - 8, trackTop, 4, trackHeight, 2.0f, 2.0f};
        shape.base.fill = withOpacity(theme().textPrimary, 0.08f);
        shape.base.visible = true;
    });
    canvas().updateShape(thumbId_, [&](Shape& shape) {
        shape.data = RRectShape{width - 8, thumbY, 4, thumbHeight, 2.0f, 2.0f};
        shape.base.fill = withOpacity(theme().textSecondary, hovered() ? 0.8f : 0.55f);
        shape.base.visible = true;
    });
}

bool ScrollViewer::onEvent(const Event& ev) {
    switch (ev.type) {
    case EventType::MouseWheel:
        if (maxScroll() <= 0.0f) return false;
        scrollBy(-ev.wheelDelta * 64.0f);
        return true;

    case EventType::KeyDown: {
        if (!keyboardScrolling_) return false;
        const float page = std::max(40.0f, static_cast<float>(frameHeight()) * 0.85f);
        if (ev.key == kKeyDown) { scrollBy(40.0f); return true; }
        if (ev.key == kKeyUp) { scrollBy(-40.0f); return true; }
        if (ev.key == kKeyPageDown) { scrollBy(page); return true; }
        if (ev.key == kKeyPageUp) { scrollBy(-page); return true; }
        if (ev.key == kKeyHome) { scrollTo(0.0f); return true; }
        if (ev.key == kKeyEnd) { scrollTo(maxScroll()); return true; }
        return false;
    }

    default:
        return false;
    }
}

// ===========================================================================
// ListView
// ===========================================================================
ListView::ListView() {
    Shape track = makeRRectShape(Rect{0, 0, 4, 1}, 2.0f,
                                 withOpacity(theme().textPrimary, 0.08f));
    track.base.visible = false;
    scrollTrackId_ = canvas().addShape(std::move(track));

    Shape thumb = makeRRectShape(Rect{0, 0, 4, 1}, 2.0f,
                                 withOpacity(theme().textSecondary, 0.55f));
    thumb.base.visible = false;
    scrollThumbId_ = canvas().addShape(std::move(thumb));
}

void ListView::setItems(std::vector<std::string> items) {
    items_ = std::move(items);
    if (selectedIndex_ >= static_cast<int32_t>(items_.size())) {
        selectedIndex_ = items_.empty() ? -1 : static_cast<int32_t>(items_.size()) - 1;
    }
    hoverIndex_ = -1;
    scrollOffset_ = 0.0f;
    rebuildRows();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void ListView::addItem(std::string item) {
    items_.push_back(std::move(item));
    rebuildRows();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void ListView::setSelectedIndex(int32_t index) {
    const int32_t next = (index < 0 || index >= static_cast<int32_t>(items_.size())) ? -1 : index;
    if (next == selectedIndex_) return;
    selectedIndex_ = next;
    if (laidOut()) updateVisuals();
}

void ListView::rebuildRows() {
    for (const RowGeometry& row : rows_) {
        canvas().removeShape(row.background);
        canvas().removeShape(row.accentBar);
        canvas().removeShape(row.text);
    }
    rows_.clear();
    rows_.reserve(items_.size());

    for (const std::string& item : items_) {
        (void)item;
        RowGeometry row;
        row.background = canvas().addShape(makeRRectShape(Rect{0, 0, 1, 1}, 4.0f, Color::nullColor()));
        row.accentBar = canvas().addShape(makeRRectShape(Rect{0, 0, 1, 1}, 1.5f, Color::nullColor()));
        row.text = canvas().addShape(makeTextShape({}, 14, theme().textPrimary));
        rows_.push_back(row);
    }
}

Vec2i ListView::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{240, theme().listItemHeight * 6 + 8};
}

float ListView::maxScroll() const {
    const float contentHeight = static_cast<float>(items_.size()) *
                                static_cast<float>(theme().listItemHeight) + 8.0f;
    return std::max(0.0f, contentHeight - static_cast<float>(frameHeight()));
}

int32_t ListView::indexAt(int32_t localY) const {
    if (items_.empty()) return -1;
    const float contentY = static_cast<float>(localY) + scrollOffset_ - 4.0f;
    if (contentY < 0.0f) return -1;
    const int32_t index = static_cast<int32_t>(contentY) / theme().listItemHeight;
    if (index < 0 || index >= static_cast<int32_t>(items_.size())) return -1;

    const float rowTop = 4.0f + static_cast<float>(index * theme().listItemHeight) - scrollOffset_;
    if (static_cast<float>(localY) < rowTop ||
        static_cast<float>(localY) >= rowTop + static_cast<float>(theme().listItemHeight) - 2.0f) {
        return -1;
    }
    return index;
}

void ListView::updateVisuals() {
    if (!laidOut()) return;
    if (rows_.size() != items_.size()) rebuildRows();

    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    const int32_t itemHeight = theme().listItemHeight;

    for (size_t i = 0; i < rows_.size(); ++i) {
        const int32_t top = 4 + static_cast<int32_t>(i) * itemHeight -
                            static_cast<int32_t>(std::lround(scrollOffset_));
        const bool visible = top + itemHeight > 0 && top < height;
        const bool selected = static_cast<int32_t>(i) == selectedIndex_;
        const bool hover = static_cast<int32_t>(i) == hoverIndex_;

        Color fill = Color::nullColor();
        if (selected) fill = withOpacity(theme().accent, 0.16f);
        else if (hover) fill = theme().subtleHover;

        canvas().updateShape(rows_[i].background, [&](Shape& shape) {
            shape.data = RRectShape{4, top, std::max(0, width - 12), itemHeight - 2, 4.0f, 4.0f};
            shape.base.fill = fill;
            shape.base.visible = visible && fill.a > 0;
        });

        canvas().updateShape(rows_[i].accentBar, [&](Shape& shape) {
            shape.data = RRectShape{4, top + 8, 3, itemHeight - 18, 1.5f, 1.5f};
            shape.base.fill = theme().accent;
            shape.base.visible = visible && selected;
        });

        const std::string label = ellipsizeText(canvas(), items_[i], 14,
                                               std::max(0, width - 28));
        const Rect measured = measureString(label, 14);
        const int32_t baseline = top + (itemHeight - 2 - measured.h) / 2 - measured.y;
        canvas().updateShape(rows_[i].text, [&](Shape& shape) {
            setShapeText(shape, label, 14,
                    selected ? theme().textPrimary : theme().textSecondary, 16, baseline);
            shape.base.visible = visible;
        });
    }

    if (maxScroll() <= 0.0f) {
        canvas().updateShape(scrollTrackId_, [](Shape& shape) { shape.base.visible = false; });
        canvas().updateShape(scrollThumbId_, [](Shape& shape) { shape.base.visible = false; });
        return;
    }

    const int32_t trackTop = 6;
    const int32_t trackHeight = std::max(1, height - 12);
    const float contentHeight = static_cast<float>(items_.size()) *
                                static_cast<float>(itemHeight) + 8.0f;
    const int32_t thumbHeight = scrollbarThumbHeight(trackHeight,
                                                     static_cast<float>(height),
                                                     contentHeight);
    const float ratio = std::max(0.0f, std::min(1.0f, scrollOffset_ / maxScroll()));
    const int32_t thumbY = trackTop + static_cast<int32_t>(std::lround(
        static_cast<float>(trackHeight - thumbHeight) * ratio));

    canvas().updateShape(scrollTrackId_, [&](Shape& shape) {
        shape.data = RRectShape{width - 7, trackTop, 4, trackHeight, 2.0f, 2.0f};
        shape.base.fill = withOpacity(theme().textPrimary, 0.08f);
        shape.base.visible = true;
    });
    canvas().updateShape(scrollThumbId_, [&](Shape& shape) {
        shape.data = RRectShape{width - 7, thumbY, 4, thumbHeight, 2.0f, 2.0f};
        shape.base.fill = withOpacity(theme().textSecondary, 0.55f);
        shape.base.visible = true;
    });
}

void ListView::scrollTo(float offset) {
    const float next = std::max(0.0f, std::min(offset, maxScroll()));
    if (std::fabs(next - scrollOffset_) < 0.01f) return;
    scrollOffset_ = next;
    if (laidOut()) updateVisuals();
}

void ListView::ensureVisible(int32_t index) {
    if (index < 0) return;
    const float itemHeight = static_cast<float>(theme().listItemHeight);
    const float top = 4.0f + static_cast<float>(index) * itemHeight;
    const float bottom = top + itemHeight;
    const float viewTop = scrollOffset_;
    const float viewBottom = scrollOffset_ + static_cast<float>(frameHeight());

    if (top < viewTop) scrollTo(top - 4.0f);
    else if (bottom > viewBottom) scrollTo(bottom - static_cast<float>(frameHeight()));
}

bool ListView::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseMove: {
        const int32_t index = indexAt(ev.y);
        if (index != hoverIndex_) {
            hoverIndex_ = index;
            if (laidOut()) updateVisuals();
        }
        return true;
    }
    case EventType::MouseDown:
        return true;

    case EventType::MouseUp: {
        if (clicked()) {
            const int32_t index = indexAt(ev.y);
            if (index >= 0) {
                const bool changed = index != selectedIndex_;
                selectedIndex_ = index;
                if (laidOut()) updateVisuals();
                if (changed && onSelectionChanged) onSelectionChanged(index);
            }
        }
        return true;
    }

    case EventType::MouseWheel:
        if (maxScroll() <= 0.0f) return false;
        scrollTo(scrollOffset_ - ev.wheelDelta * 56.0f);
        return true;

    case EventType::KeyDown: {
        const int32_t count = static_cast<int32_t>(items_.size());
        if (count <= 0) return false;
        int32_t next = selectedIndex_;
        if (ev.key == kKeyDown) next = std::min(count - 1, selectedIndex_ + 1);
        else if (ev.key == kKeyUp) next = std::max(0, selectedIndex_ - 1);
        else if (ev.key == kKeyHome) next = 0;
        else if (ev.key == kKeyEnd) next = count - 1;
        else if (ev.key == kKeyPageDown) next = std::min(count - 1, selectedIndex_ + 6);
        else if (ev.key == kKeyPageUp) next = std::max(0, selectedIndex_ - 6);
        else return false;

        if (next != selectedIndex_) {
            selectedIndex_ = next;
            ensureVisible(next);
            if (laidOut()) updateVisuals();
            if (onSelectionChanged) onSelectionChanged(next);
        }
        return true;
    }

    default:
        return false;
    }
}

void ListView::onStateChanged() {
    if (!hovered() && hoverIndex_ != -1) {
        hoverIndex_ = -1;
        if (laidOut()) updateVisuals();
    }
}

// ===========================================================================
// GridView
// ===========================================================================
GridView::GridView() = default;

void GridView::setItems(std::vector<std::string> items) {
    items_ = std::move(items);
    if (selectedIndex_ >= static_cast<int32_t>(items_.size())) {
        selectedIndex_ = items_.empty() ? -1 : static_cast<int32_t>(items_.size()) - 1;
    }
    hoverIndex_ = -1;
    scrollOffset_ = 0.0f;
    rebuildTiles();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void GridView::setSelectedIndex(int32_t index) {
    const int32_t next = (index < 0 || index >= static_cast<int32_t>(items_.size())) ? -1 : index;
    if (next == selectedIndex_) return;
    selectedIndex_ = next;
    if (laidOut()) updateVisuals();
}

void GridView::rebuildTiles() {
    for (const TileGeometry& tile : tiles_) {
        canvas().removeShape(tile.tile);
        canvas().removeShape(tile.icon);
        canvas().removeShape(tile.text);
    }
    tiles_.clear();
    tiles_.reserve(items_.size());

    for (const std::string& label : items_) {
        (void)label;
        TileGeometry tile;
        tile.tile = canvas().addShape(makeRRectShape(Rect{0, 0, 1, 1}, 8.0f,
                                                     theme().layer));
        tile.icon = canvas().addShape(makeCircleShape(0, 0, 16.0f,
                                                      theme().controlFill,
                                                      theme().stroke, 1));
        tile.text = canvas().addShape(makeTextShape({}, 12, theme().textPrimary));
        tiles_.push_back(tile);
    }
}

Vec2i GridView::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{320, 268};
}

int32_t GridView::columnsForWidth(int32_t width) const {
    constexpr int32_t tile = 96;
    constexpr int32_t gap = 8;
    constexpr int32_t padding = 12;
    const int32_t usable = std::max(0, width - padding * 2);
    return std::max(1, (usable + gap) / (tile + gap));
}

int32_t GridView::tileIndexAt(int32_t localX, int32_t localY) const {
    constexpr int32_t tile = 96;
    constexpr int32_t gap = 8;
    constexpr int32_t padding = 12;
    const int32_t columns = columnsForWidth(frameWidth());
    const int32_t x = localX - padding;
    const int32_t y = localY - padding + static_cast<int32_t>(std::lround(scrollOffset_));
    if (x < 0 || y < 0) return -1;
    const int32_t column = x / (tile + gap);
    const int32_t row = y / (tile + gap);
    if (column >= columns) return -1;
    if (x % (tile + gap) >= tile) return -1;
    const int32_t index = row * columns + column;
    if (index < 0 || index >= static_cast<int32_t>(items_.size())) return -1;
    return index;
}

void GridView::updateVisuals() {
    if (!laidOut()) return;
    if (tiles_.size() != items_.size()) rebuildTiles();

    constexpr int32_t tile = 96;
    constexpr int32_t gap = 8;
    constexpr int32_t padding = 12;

    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    const int32_t columns = columnsForWidth(width);
    const int32_t rows = static_cast<int32_t>((items_.size() + static_cast<size_t>(columns) - 1) /
                                              static_cast<size_t>(columns));
    contentHeight_ = static_cast<float>(padding * 2 + rows * tile + std::max(0, rows - 1) * gap);

    const float maximum = std::max(0.0f, contentHeight_ - static_cast<float>(height));
    if (scrollOffset_ > maximum) scrollOffset_ = maximum;

    for (size_t i = 0; i < tiles_.size(); ++i) {
        const int32_t column = static_cast<int32_t>(i) % columns;
        const int32_t row = static_cast<int32_t>(i) / columns;
        const int32_t x = padding + column * (tile + gap);
        const int32_t y = padding + row * (tile + gap) -
                          static_cast<int32_t>(std::lround(scrollOffset_));
        const bool visible = y + tile > 0 && y < height;
        const bool selected = static_cast<int32_t>(i) == selectedIndex_;
        const bool hover = static_cast<int32_t>(i) == hoverIndex_;

        Color fill = theme().layer;
        if (selected) fill = withOpacity(theme().accent, 0.18f);
        else if (hover) fill = theme().layerAlt;

        canvas().updateShape(tiles_[i].tile, [&](Shape& shape) {
            shape.data = RRectShape{x, y, tile, tile, 8.0f, 8.0f};
            shape.base.fill = fill;
            shape.base.stroke = selected ? theme().accent : theme().stroke;
            shape.base.strokeWidth = selected ? 2 : 1;
            shape.base.visible = visible;
        });

        canvas().updateShape(tiles_[i].icon, [&](Shape& shape) {
            shape.data = CircleShape{x + tile / 2, y + 36, 16.0f};
            shape.base.fill = selected ? withOpacity(theme().accent, 0.35f) : theme().controlFill;
            shape.base.stroke = theme().stroke;
            shape.base.strokeWidth = 1;
            shape.base.visible = visible;
        });

        const std::string& label = items_[i];
        const Rect measured = measureString(label, 12);
        canvas().updateShape(tiles_[i].text, [&](Shape& shape) {
            setShapeText(shape, label, 12,
                    selected ? theme().textPrimary : theme().textSecondary,
                    x + std::max(6, (tile - measured.w) / 2), y + 80);
            shape.base.visible = visible;
        });
    }
}

bool GridView::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseMove: {
        const int32_t index = tileIndexAt(ev.x, ev.y);
        if (index != hoverIndex_) {
            hoverIndex_ = index;
            if (laidOut()) updateVisuals();
        }
        return true;
    }
    case EventType::MouseDown:
        return true;

    case EventType::MouseUp:
        if (clicked()) {
            const int32_t index = tileIndexAt(ev.x, ev.y);
            if (index >= 0) {
                const bool changed = index != selectedIndex_;
                selectedIndex_ = index;
                if (laidOut()) updateVisuals();
                if (changed && onSelectionChanged) onSelectionChanged(index);
            }
        }
        return true;

    case EventType::MouseWheel: {
        const float maximum = std::max(0.0f, contentHeight_ - static_cast<float>(frameHeight()));
        if (maximum <= 0.0f) return false;
        const float next = std::max(0.0f, std::min(maximum,
                                                   scrollOffset_ - ev.wheelDelta * 56.0f));
        if (std::fabs(next - scrollOffset_) > 0.01f) {
            scrollOffset_ = next;
            if (laidOut()) updateVisuals();
        }
        return true;
    }

    case EventType::KeyDown: {
        const int32_t count = static_cast<int32_t>(items_.size());
        if (count <= 0) return false;
        const int32_t columns = columnsForWidth(frameWidth());
        int32_t next = selectedIndex_;
        if (ev.key == kKeyRight) next = std::min(count - 1, selectedIndex_ + 1);
        else if (ev.key == kKeyLeft) next = std::max(0, selectedIndex_ - 1);
        else if (ev.key == kKeyDown) next = std::min(count - 1, selectedIndex_ + columns);
        else if (ev.key == kKeyUp) next = std::max(0, selectedIndex_ - columns);
        else if (ev.key == kKeyHome) next = 0;
        else if (ev.key == kKeyEnd) next = count - 1;
        else return false;

        if (next != selectedIndex_) {
            selectedIndex_ = next;
            if (laidOut()) updateVisuals();
            if (onSelectionChanged) onSelectionChanged(next);
        }
        return true;
    }

    default:
        return false;
    }
}

void GridView::onStateChanged() {
    if (!hovered() && hoverIndex_ != -1) {
        hoverIndex_ = -1;
        if (laidOut()) updateVisuals();
    }
}

// ===========================================================================
// NavigationView
// ===========================================================================
NavigationView::NavigationView() {
    style().direction = FlexDirection::Row;
    style().alignItems = AlignItems::Stretch;
    style().padding = Edges{0, 0, 0, paneWidth() + 1};

    Shape pane = makeRectShape(Rect{0, 0, 1, 1}, theme().cardAlt);
    paneBackgroundId_ = canvas().addShape(std::move(pane));

    Shape divider = makeRectShape(Rect{0, 0, 1, 1}, theme().divider);
    paneDividerId_ = canvas().addShape(std::move(divider));

    Shape hamburger = makePathShape(
        {
            {Vec2f{0, 0}, Vec2f{14, 0}},
            {Vec2f{0, 5}, Vec2f{14, 5}},
            {Vec2f{0, 10}, Vec2f{14, 10}}
        },
        theme().textPrimary, 1);
    hamburgerId_ = canvas().addShape(std::move(hamburger));

    Shape title = makeTextShape(paneTitle_, 18, theme().textPrimary);
    titleId_ = canvas().addShape(std::move(title));
}

int32_t NavigationView::paneWidth() const {
    return compact_ ? 52 : 220;
}

void NavigationView::addItem(std::string label) {
    items_.push_back(std::move(label));
    rebuildItems();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void NavigationView::setItems(std::vector<std::string> items) {
    items_ = std::move(items);
    if (selectedIndex_ >= static_cast<int32_t>(items_.size())) {
        selectedIndex_ = items_.empty() ? -1 : 0;
    }
    hoverIndex_ = -1;
    rebuildItems();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void NavigationView::setSelectedIndex(int32_t index) {
    const int32_t next = (index < 0 || index >= static_cast<int32_t>(items_.size())) ? -1 : index;
    if (next == selectedIndex_) return;
    selectedIndex_ = next;
    if (laidOut()) updateVisuals();
}

void NavigationView::setPaneTitle(std::string title) {
    paneTitle_ = std::move(title);
    if (laidOut()) updateVisuals();
}

void NavigationView::setCompact(bool compact) {
    if (compact_ == compact) return;
    compact_ = compact;
    style().padding = Edges{0, 0, 0, paneWidth() + 1};
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void NavigationView::setContent(std::unique_ptr<Widget> content) {
    if (content_) {
        removeChild(content_);
        content_ = nullptr;
    }
    if (!content) {
        invalidateLayout();
        return;
    }

    content_ = addChild(std::move(content));
    content_->style().flexGrow = 1;
    content_->style().alignSelf = AlignSelf::Stretch;
    content_->style().height = Dimension::percent(100.0f);
    invalidateLayout();
}

void NavigationView::rebuildItems() {
    for (const ItemGeometry& item : itemShapes_) {
        canvas().removeShape(item.background);
        canvas().removeShape(item.accentBar);
        canvas().removeShape(item.icon);
        canvas().removeShape(item.iconText);
        canvas().removeShape(item.label);
    }
    itemShapes_.clear();
    itemShapes_.reserve(items_.size());

    for (const std::string& label : items_) {
        (void)label;
        ItemGeometry item;
        item.background = canvas().addShape(makeRRectShape(Rect{0, 0, 1, 1}, 4.0f, Color::nullColor()));
        item.accentBar = canvas().addShape(makeRRectShape(Rect{0, 0, 1, 1}, 1.5f, Color::nullColor()));
        item.icon = canvas().addShape(makeCircleShape(0, 0, 9.0f, theme().controlFill,
                                                      theme().stroke, 1));
        item.iconText = canvas().addShape(makeTextShape({}, 10, theme().textPrimary));
        item.label = canvas().addShape(makeTextShape({}, 14, theme().textPrimary));
        itemShapes_.push_back(item);
    }
}

Vec2i NavigationView::onMeasure(const Vec2i& available) const {
    return Vec2i{std::max(0, available.x), std::max(0, available.y)};
}

void NavigationView::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (width <= 0 || height <= 0) return;

    const int32_t pane = std::min(width, paneWidth());

    canvas().updateShape(paneBackgroundId_, [&](Shape& shape) {
        shape.data = RectShape{0, 0, pane, height};
        shape.base.fill = theme().cardAlt;
        shape.base.stroke = Paint{};
        shape.base.visible = true;
    });
    canvas().updateShape(paneDividerId_, [&](Shape& shape) {
        shape.data = RectShape{pane, 0, 1, height};
        shape.base.fill = theme().divider;
        shape.base.visible = true;
    });

    const int32_t hamburgerX = compact_ ? (pane - 14) / 2 : 16;
    canvas().updateShape(hamburgerId_, [&](Shape& shape) {
        shape = makePathShape(
            {
                {Vec2f{static_cast<float>(hamburgerX), 22},
                 Vec2f{static_cast<float>(hamburgerX + 14), 22}},
                {Vec2f{static_cast<float>(hamburgerX), 27},
                 Vec2f{static_cast<float>(hamburgerX + 14), 27}},
                {Vec2f{static_cast<float>(hamburgerX), 32},
                 Vec2f{static_cast<float>(hamburgerX + 14), 32}}
            },
            theme().textPrimary, 1);
        shape.base.visible = true;
    });

    const Rect titleMeasured = measureString(paneTitle_, 18);
    canvas().updateShape(titleId_, [&](Shape& shape) {
        setShapeText(shape, paneTitle_, 18, theme().textPrimary, 52,
                baselineForCenter(titleMeasured, 52));
        shape.base.visible = !compact_ && pane > 60;
    });

    const int32_t itemTop = 64;
    const int32_t itemHeight = 44;
    for (size_t i = 0; i < itemShapes_.size() && i < items_.size(); ++i) {
        const int32_t top = itemTop + static_cast<int32_t>(i) * itemHeight;
        const bool visible = top + itemHeight <= height && pane >= 52;
        const bool selected = static_cast<int32_t>(i) == selectedIndex_;
        const bool hover = static_cast<int32_t>(i) == hoverIndex_;

        Color fill = Color::nullColor();
        if (selected) fill = withOpacity(theme().accent, 0.16f);
        else if (hover) fill = theme().subtleHover;

        canvas().updateShape(itemShapes_[i].background, [&](Shape& shape) {
            shape.data = RRectShape{8, top + 2, std::max(0, pane - 16), itemHeight - 4, 4.0f, 4.0f};
            shape.base.fill = fill;
            shape.base.visible = visible && fill.a > 0;
        });

        canvas().updateShape(itemShapes_[i].accentBar, [&](Shape& shape) {
            shape.data = RRectShape{8, top + 10, 3, itemHeight - 20, 1.5f, 1.5f};
            shape.base.fill = theme().accent;
            shape.base.visible = visible && selected;
        });

        const int32_t iconCenterX = compact_ ? pane / 2 : 24;
        const int32_t iconCenterY = top + itemHeight / 2;
        canvas().updateShape(itemShapes_[i].icon, [&](Shape& shape) {
            shape.data = CircleShape{iconCenterX, iconCenterY, 9.0f};
            shape.base.fill = selected ? withOpacity(theme().accent, 0.35f) : theme().controlFill;
            shape.base.stroke = theme().stroke;
            shape.base.strokeWidth = 1;
            shape.base.visible = visible;
        });

        const std::string initial = firstChar(items_[i]);
        const Rect iconMeasured = measureString(initial, 10);
        canvas().updateShape(itemShapes_[i].iconText, [&](Shape& shape) {
            setShapeText(shape, initial, 10, theme().textPrimary,
                    iconCenterX - iconMeasured.w / 2,
                    iconCenterY - iconMeasured.h / 2 - iconMeasured.y);
            shape.base.visible = visible;
        });

        const std::string shownLabel = ellipsizeText(canvas(), items_[i], 14,
                                                     std::max(0, pane - 56));
        const Rect labelMeasured = measureString(shownLabel, 14);
        canvas().updateShape(itemShapes_[i].label, [&](Shape& shape) {
            setShapeText(shape, shownLabel, 14, theme().textPrimary, 44,
                    top + (itemHeight - labelMeasured.h) / 2 - labelMeasured.y);
            shape.base.visible = visible && !compact_ && pane > 120;
        });
    }
}

int32_t NavigationView::itemIndexAt(int32_t localY) const {
    const int32_t itemTop = 64;
    const int32_t itemHeight = 44;
    if (localY < itemTop) return -1;
    const int32_t index = (localY - itemTop) / itemHeight;
    if (index < 0 || index >= static_cast<int32_t>(items_.size())) return -1;
    return index;
}

bool NavigationView::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseMove: {
        const int32_t index = ev.x < paneWidth() ? itemIndexAt(ev.y) : -1;
        if (index != hoverIndex_) {
            hoverIndex_ = index;
            if (laidOut()) updateVisuals();
        }
        return false;
    }

    case EventType::MouseDown:
        return ev.x < paneWidth();

    case EventType::MouseUp: {
        if (!clicked()) return false;
        if (ev.x < 48 && ev.y < 48) {
            setCompact(!compact_);
            return true;
        }
        if (ev.x < paneWidth()) {
            const int32_t index = itemIndexAt(ev.y);
            if (index >= 0) {
                const bool changed = index != selectedIndex_;
                selectedIndex_ = index;
                if (laidOut()) updateVisuals();
                if (changed && onSelectionChanged) onSelectionChanged(index);
                return true;
            }
        }
        return false;
    }

    case EventType::KeyDown: {
        const int32_t count = static_cast<int32_t>(items_.size());
        if (count <= 0) return false;
        int32_t next = selectedIndex_;
        if (ev.key == kKeyDown) next = std::min(count - 1, selectedIndex_ + 1);
        else if (ev.key == kKeyUp) next = std::max(0, selectedIndex_ - 1);
        else if (ev.key == kKeyHome) next = 0;
        else if (ev.key == kKeyEnd) next = count - 1;
        else return false;

        if (next != selectedIndex_) {
            selectedIndex_ = next;
            if (laidOut()) updateVisuals();
            if (onSelectionChanged) onSelectionChanged(next);
        }
        return true;
    }

    default:
        return false;
    }
}

void NavigationView::onStateChanged() {
    if (!hovered() && hoverIndex_ != -1) {
        hoverIndex_ = -1;
        if (laidOut()) updateVisuals();
    }
}

// ===========================================================================
// TabView
// ===========================================================================
TabView::TabView() = default;

int32_t TabView::addTab(std::string title) {
    tabs_.push_back(std::move(title));
    rebuildTabs();
    invalidateLayout();
    if (laidOut()) updateVisuals();
    return static_cast<int32_t>(tabs_.size()) - 1;
}

void TabView::setTabs(std::vector<std::string> tabs) {
    tabs_ = std::move(tabs);
    if (selectedIndex_ >= static_cast<int32_t>(tabs_.size())) {
        selectedIndex_ = tabs_.empty() ? -1 : 0;
    }
    hoverIndex_ = -1;
    rebuildTabs();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void TabView::setSelectedIndex(int32_t index) {
    const int32_t next = (index < 0 || index >= static_cast<int32_t>(tabs_.size())) ? -1 : index;
    if (next == selectedIndex_) return;
    selectedIndex_ = next;
    if (laidOut()) updateVisuals();
}

void TabView::closeTab(int32_t index) {
    if (index < 0 || index >= static_cast<int32_t>(tabs_.size())) return;
    tabs_.erase(tabs_.begin() + index);
    if (selectedIndex_ >= static_cast<int32_t>(tabs_.size())) {
        selectedIndex_ = tabs_.empty() ? -1 : static_cast<int32_t>(tabs_.size()) - 1;
    }
    rebuildTabs();
    invalidateLayout();
    if (laidOut()) updateVisuals();
    if (onTabClosed) onTabClosed(index);
}

void TabView::rebuildTabs() {
    for (const TabGeometry& tab : tabShapes_) {
        canvas().removeShape(tab.background);
        canvas().removeShape(tab.indicator);
        canvas().removeShape(tab.text);
    }
    tabShapes_.clear();
    tabShapes_.reserve(tabs_.size());

    for (const std::string& title : tabs_) {
        (void)title;
        TabGeometry tab;
        tab.background = canvas().addShape(makeRRectShape(Rect{0, 0, 1, 1}, 4.0f, Color::nullColor()));
        tab.indicator = canvas().addShape(makeRRectShape(Rect{0, 0, 1, 2}, 1.0f, Color::nullColor()));
        tab.text = canvas().addShape(makeTextShape({}, 13, theme().textSecondary));
        tabShapes_.push_back(tab);
    }
}

Vec2i TabView::onMeasure(const Vec2i& available) const {
    (void)available;
    int32_t width = 8;
    for (const std::string& title : tabs_) {
        width += measureString(title, 13).w + 32;
    }
    return Vec2i{std::max(80, width), 40};
}

void TabView::updateVisuals() {
    if (!laidOut()) return;
    if (tabShapes_.size() != tabs_.size()) rebuildTabs();

    const int32_t height = frameHeight();
    int32_t x = 4;
    tabWidths_.assign(tabs_.size(), 0);

    for (size_t i = 0; i < tabShapes_.size(); ++i) {
        const Rect measured = measureString(tabs_[i], 13);
        const int32_t tabWidth = std::max(48, measured.w + 32);
        tabWidths_[i] = tabWidth;
        const int32_t tabY = 4;
        const int32_t tabHeight = std::max(24, height - 8);
        const bool selected = static_cast<int32_t>(i) == selectedIndex_;
        const bool hover = static_cast<int32_t>(i) == hoverIndex_;

        Color fill = Color::nullColor();
        if (selected) fill = theme().card;
        else if (hover) fill = theme().subtleHover;

        canvas().updateShape(tabShapes_[i].background, [&](Shape& shape) {
            shape.data = RRectShape{x, tabY, tabWidth, tabHeight, 4.0f, 4.0f};
            shape.base.fill = fill;
            shape.base.visible = fill.a > 0;
        });

        canvas().updateShape(tabShapes_[i].indicator, [&](Shape& shape) {
            shape.data = RRectShape{x + 8, std::max(0, height - 3), std::max(0, tabWidth - 16), 2, 1.0f, 1.0f};
            shape.base.fill = theme().accent;
            shape.base.visible = selected;
        });

        canvas().updateShape(tabShapes_[i].text, [&](Shape& shape) {
            setShapeText(shape, tabs_[i], 13,
                    selected ? theme().textPrimary : theme().textSecondary,
                    x + (tabWidth - measured.w) / 2,
                    (height - measured.h) / 2 - measured.y);
            shape.base.visible = true;
        });

        x += tabWidth;
    }
}

bool TabView::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseMove: {
        int32_t hover = -1;
        int32_t x = 4;
        for (size_t i = 0; i < tabWidths_.size(); ++i) {
            if (ev.x >= x && ev.x < x + tabWidths_[i]) { hover = static_cast<int32_t>(i); break; }
            x += tabWidths_[i];
        }
        if (hover != hoverIndex_) {
            hoverIndex_ = hover;
            if (laidOut()) updateVisuals();
        }
        return true;
    }
    case EventType::MouseDown:
        return true;

    case EventType::MouseUp:
        if (clicked()) {
            int32_t x = 4;
            for (size_t i = 0; i < tabWidths_.size(); ++i) {
                if (ev.x >= x && ev.x < x + tabWidths_[i]) {
                    const int32_t index = static_cast<int32_t>(i);
                    if (index != selectedIndex_) {
                        selectedIndex_ = index;
                        if (laidOut()) updateVisuals();
                        if (onTabChanged) onTabChanged(index);
                    }
                    return true;
                }
                x += tabWidths_[i];
            }
        }
        return true;

    case EventType::KeyDown: {
        const int32_t count = static_cast<int32_t>(tabs_.size());
        if (count <= 0) return false;
        int32_t next = selectedIndex_;
        if (ev.key == kKeyRight) next = std::min(count - 1, selectedIndex_ + 1);
        else if (ev.key == kKeyLeft) next = std::max(0, selectedIndex_ - 1);
        else if (ev.key == kKeyHome) next = 0;
        else if (ev.key == kKeyEnd) next = count - 1;
        else return false;

        if (next != selectedIndex_) {
            selectedIndex_ = next;
            if (laidOut()) updateVisuals();
            if (onTabChanged) onTabChanged(next);
        }
        return true;
    }

    default:
        return false;
    }
}

void TabView::onStateChanged() {
    if (!hovered() && hoverIndex_ != -1) {
        hoverIndex_ = -1;
        if (laidOut()) updateVisuals();
    }
}

// ===========================================================================
// BreadcrumbBar
// ===========================================================================
BreadcrumbBar::BreadcrumbBar() = default;

void BreadcrumbBar::setItems(std::vector<std::string> items) {
    items_ = std::move(items);
    if (selectedIndex_ >= static_cast<int32_t>(items_.size())) {
        selectedIndex_ = items_.empty() ? -1 : static_cast<int32_t>(items_.size()) - 1;
    }
    hoverIndex_ = -1;
    rebuild();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void BreadcrumbBar::setSelectedIndex(int32_t index) {
    const int32_t next = (index < 0 || index >= static_cast<int32_t>(items_.size())) ? -1 : index;
    if (next == selectedIndex_) return;
    selectedIndex_ = next;
    if (laidOut()) updateVisuals();
}

void BreadcrumbBar::rebuild() {
    for (uint32_t id : textIds_) canvas().removeShape(id);
    for (uint32_t id : chevronIds_) canvas().removeShape(id);
    textIds_.clear();
    chevronIds_.clear();
    itemX_.assign(items_.size(), 0);
    itemWidth_.assign(items_.size(), 0);

    for (size_t i = 0; i < items_.size(); ++i) {
        textIds_.push_back(canvas().addShape(makeTextShape({}, 12, theme().textSecondary)));
        if (i + 1 < items_.size()) {
            chevronIds_.push_back(canvas().addShape(
                makeChevronRightShape(0, 0, 5, 8, theme().textTertiary, 1)));
        }
    }
}

Vec2i BreadcrumbBar::onMeasure(const Vec2i& available) const {
    (void)available;
    int32_t width = 0;
    for (size_t i = 0; i < items_.size(); ++i) {
        width += measureString(items_[i], 12).w + 8;
        if (i + 1 < items_.size()) width += 14;
    }
    return Vec2i{std::max(0, width), 32};
}

void BreadcrumbBar::updateVisuals() {
    if (!laidOut()) return;

    const int32_t height = frameHeight();
    int32_t x = 0;
    for (size_t i = 0; i < items_.size() && i < textIds_.size(); ++i) {
        const Rect measured = measureString(items_[i], 12);
        itemX_[i] = x;
        itemWidth_[i] = measured.w + 8;
        const bool selected = static_cast<int32_t>(i) == selectedIndex_;
        const bool hover = static_cast<int32_t>(i) == hoverIndex_;

        Color color = theme().textSecondary;
        if (selected) color = theme().textPrimary;
        else if (hover) color = theme().accent;

        canvas().updateShape(textIds_[i], [&](Shape& shape) {
            setShapeText(shape, items_[i], 12, color, x + 4,
                    (height - measured.h) / 2 - measured.y);
            shape.base.visible = true;
        });

        if (i < chevronIds_.size()) {
            const int32_t chevronX = x + itemWidth_[i] + 3;
            canvas().updateShape(chevronIds_[i], [&](Shape& shape) {
                shape = makeChevronRightShape(chevronX, height / 2 - 4, 5, 8,
                                              theme().textTertiary, 1);
                shape.base.visible = true;
            });
            x += itemWidth_[i] + 14;
        } else {
            x += itemWidth_[i];
        }
    }
}

bool BreadcrumbBar::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseMove: {
        int32_t hover = -1;
        for (size_t i = 0; i < itemX_.size(); ++i) {
            if (ev.x >= itemX_[i] && ev.x < itemX_[i] + itemWidth_[i]) {
                hover = static_cast<int32_t>(i);
                break;
            }
        }
        if (hover != hoverIndex_) {
            hoverIndex_ = hover;
            if (laidOut()) updateVisuals();
        }
        return true;
    }
    case EventType::MouseDown:
        return true;

    case EventType::MouseUp:
        if (clicked()) {
            for (size_t i = 0; i < itemX_.size(); ++i) {
                if (ev.x >= itemX_[i] && ev.x < itemX_[i] + itemWidth_[i]) {
                    const int32_t index = static_cast<int32_t>(i);
                    selectedIndex_ = index;
                    if (laidOut()) updateVisuals();
                    if (onItemClicked) onItemClicked(index);
                    return true;
                }
            }
        }
        return true;

    default:
        return false;
    }
}

void BreadcrumbBar::onStateChanged() {
    if (!hovered() && hoverIndex_ != -1) {
        hoverIndex_ = -1;
        if (laidOut()) updateVisuals();
    }
}

} // namespace fluent
} // namespace modern_ui
