// ============================================================================
// FluentFeedback.cpp — 教学提示 / 气泡 / 对话框 / 菜单 / 命令条
// ============================================================================

#include "FluentFeedback.h"

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
constexpr int kKeyEnter = 0x0D;
constexpr int kKeyEscape = 0x1B;

} // namespace

// ===========================================================================
// TeachingTip
// ===========================================================================
TeachingTip::TeachingTip(std::string title, std::string message, bool closable)
    : title_(std::move(title)), message_(std::move(message)), closable_(closable) {
    Shape background = makeRRectShape(Rect{0, 0, 1, 1}, 8.0f,
                                      theme().overlay, theme().strokeStrong, 1);
    backgroundId_ = canvas().addShape(std::move(background));

    Shape icon = makeCircleShape(0, 0, 16.0f, withOpacity(theme().accent, 0.18f),
                                 theme().accent, 1);
    iconId_ = canvas().addShape(std::move(icon));

    Shape glyph = makePlusShape(0, 0, 10, theme().accent, 1);
    iconGlyphId_ = canvas().addShape(std::move(glyph));

    Shape titleShape = makeTextShape(title_, 14, theme().textPrimary);
    titleId_ = canvas().addShape(std::move(titleShape));

    Shape messageShape = makeTextShape(message_, 12, theme().textSecondary);
    messageId_ = canvas().addShape(std::move(messageShape));

    Shape closeBackground = makeRRectShape(Rect{0, 0, 1, 1}, 4.0f, Color::nullColor());
    closeBackgroundId_ = canvas().addShape(std::move(closeBackground));

    Shape closeGlyph = makeXShape(0, 0, 9, theme().textSecondary, 1);
    closeGlyphId_ = canvas().addShape(std::move(closeGlyph));
}

void TeachingTip::setTitle(std::string title) {
    if (title == title_) return;
    title_ = std::move(title);
    titleMeasured_ = Rect::null();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void TeachingTip::setMessage(std::string message) {
    if (message == message_) return;
    message_ = std::move(message);
    messageMeasured_ = Rect::null();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

Vec2i TeachingTip::onMeasure(const Vec2i& available) const {
    (void)available;
    titleMeasured_ = measureString(title_, 14);
    messageMeasured_ = message_.empty() ? Rect::null() : measureString(message_, 12);
    const int32_t content = std::max(titleMeasured_.w, messageMeasured_.w);
    return Vec2i{std::max(280, content + 112), 108};
}

void TeachingTip::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, width, height, 8.0f, 8.0f};
        shape.base.fill = theme().overlay;
        shape.base.stroke = theme().strokeStrong;
        shape.base.strokeWidth = 1;
        shape.base.visible = true;
    });

    const int32_t iconCenterX = 36;
    const int32_t iconCenterY = 38;
    canvas().updateShape(iconId_, [&](Shape& shape) {
        shape.data = CircleShape{iconCenterX, iconCenterY, 16.0f};
        shape.base.fill = withOpacity(theme().accent, 0.18f);
        shape.base.stroke = theme().accent;
        shape.base.strokeWidth = 1;
        shape.base.visible = true;
    });

    canvas().updateShape(iconGlyphId_, [&](Shape& shape) {
        shape = makePlusShape(iconCenterX, iconCenterY, 10, theme().accent, 1);
        shape.base.visible = true;
    });

    const int32_t textMax = std::max(0, width - 64 - 24);
    const std::string shownTitle = ellipsizeText(canvas(), title_, 14, textMax);
    const std::string shownMessage = ellipsizeText(canvas(), message_, 12, textMax);

    const Rect titleSize = measureString(shownTitle, 14);
    const Rect messageSize = message_.empty() ? Rect::null() : measureString(shownMessage, 12);
    titleMeasured_ = titleSize;
    messageMeasured_ = messageSize;

    const int32_t textX = 64;
    const int32_t titleBaseline = message_.empty() ? height / 2 - titleSize.y
                                                    : (height - titleSize.h - 22) / 2 - titleSize.y;
    canvas().updateShape(titleId_, [&](Shape& shape) {
        setShapeText(shape, shownTitle, 14, theme().textPrimary, textX, titleBaseline);
        shape.base.visible = !title_.empty();
    });
    canvas().updateShape(messageId_, [&](Shape& shape) {
        setShapeText(shape, shownMessage, 12, theme().textSecondary, textX,
                titleBaseline + titleSize.h + 6 - messageSize.y);
        shape.base.visible = !message_.empty();
    });

    if (!closable_) {
        canvas().updateShape(closeBackgroundId_, [](Shape& shape) { shape.base.visible = false; });
        canvas().updateShape(closeGlyphId_, [](Shape& shape) { shape.base.visible = false; });
    } else {
        const int32_t buttonX = width - 40;
        const int32_t buttonY = 8;
        canvas().updateShape(closeBackgroundId_, [&](Shape& shape) {
            shape.data = RRectShape{buttonX, buttonY, 28, 28, 4.0f, 4.0f};
            shape.base.fill = closeHover_ ? theme().subtleHover : Color::nullColor();
            shape.base.visible = closeHover_;
        });
        canvas().updateShape(closeGlyphId_, [&](Shape& shape) {
            shape = makeXShape(buttonX + 10, buttonY + 10, 8, theme().textSecondary, 1);
            shape.base.visible = true;
        });
    }
}

bool TeachingTip::onEvent(const Event& ev) {
    if (!enabled()) return false;

    const auto inClose = [&](int32_t x, int32_t y) {
        return closable_ && x >= frameWidth() - 40 && x < frameWidth() - 12 &&
               y >= 8 && y < 36;
    };

    switch (ev.type) {
    case EventType::MouseMove: {
        const bool hover = inClose(ev.x, ev.y);
        if (hover != closeHover_) {
            closeHover_ = hover;
            if (laidOut()) updateVisuals();
        }
        return false;
    }
    case EventType::MouseDown:
        return true;
    case EventType::MouseUp:
        if (clicked() && inClose(ev.x, ev.y)) {
            if (onClose) onClose();
            return true;
        }
        return true;
    default:
        return false;
    }
}

void TeachingTip::onStateChanged() {
    if (!hovered() && closeHover_) {
        closeHover_ = false;
        if (laidOut()) updateVisuals();
    }
}

// ===========================================================================
// ToolTip
// ===========================================================================
ToolTip::ToolTip(std::string text) : text_(std::move(text)) {
    Shape background = makeRRectShape(Rect{0, 0, 1, 1}, 6.0f,
                                      theme().overlay, theme().strokeStrong, 1);
    backgroundId_ = canvas().addShape(std::move(background));

    Shape label = makeTextShape(text_, 12, theme().textPrimary);
    textId_ = canvas().addShape(std::move(label));
}

void ToolTip::setText(std::string text) {
    if (text == text_) return;
    text_ = std::move(text);
    measured_ = Rect::null();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

Vec2i ToolTip::onMeasure(const Vec2i& available) const {
    (void)available;
    measured_ = measureString(text_, 12);
    return Vec2i{std::max(28, measured_.w + 20), 28};
}

void ToolTip::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();
    if (measured_.isNull()) measured_ = measureString(text_, 12);

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, width, height, 6.0f, 6.0f};
        shape.base.fill = theme().overlay;
        shape.base.stroke = theme().strokeStrong;
        shape.base.strokeWidth = 1;
        shape.base.visible = true;
    });

    const int32_t x = std::max(0, (width - measured_.w) / 2);
    canvas().updateShape(textId_, [&](Shape& shape) {
        setShapeText(shape, text_, 12, theme().textPrimary, x,
                baselineForCenter(measured_, height));
    });
}

// ===========================================================================
// ContentDialog
// ===========================================================================
ContentDialog::ContentDialog(std::string title, std::string message,
                             std::string primaryText, std::string secondaryText)
    : title_(std::move(title)), message_(std::move(message)),
      primaryText_(std::move(primaryText)), secondaryText_(std::move(secondaryText)) {
    Shape background = makeRRectShape(Rect{0, 0, 1, 1}, 8.0f,
                                      theme().overlay, theme().strokeStrong, 1);
    backgroundId_ = canvas().addShape(std::move(background));

    Shape titleShape = makeTextShape(title_, 20, theme().textPrimary);
    titleId_ = canvas().addShape(std::move(titleShape));

    Shape messageShape = makeTextShape(message_, 14, theme().textSecondary);
    messageId_ = canvas().addShape(std::move(messageShape));

    Shape primaryBackground = makeRRectShape(Rect{0, 0, 1, 1}, theme().radiusControl,
                                              theme().accent);
    primaryBackgroundId_ = canvas().addShape(std::move(primaryBackground));

    Shape primaryTextShape = makeTextShape(primaryText_, 14, theme().textOnAccent);
    primaryTextId_ = canvas().addShape(std::move(primaryTextShape));

    Shape secondaryBackground = makeRRectShape(Rect{0, 0, 1, 1}, theme().radiusControl,
                                                theme().controlFill,
                                                theme().stroke, 1);
    secondaryBackgroundId_ = canvas().addShape(std::move(secondaryBackground));

    Shape secondaryTextShape = makeTextShape(secondaryText_, 14, theme().textPrimary);
    secondaryTextId_ = canvas().addShape(std::move(secondaryTextShape));

    Shape close = makeXShape(0, 0, 10, theme().textSecondary, 1);
    closeId_ = canvas().addShape(std::move(close));
}

void ContentDialog::setTitle(std::string title) {
    title_ = std::move(title);
    if (laidOut()) updateVisuals();
}

void ContentDialog::setMessage(std::string message) {
    message_ = std::move(message);
    if (laidOut()) updateVisuals();
}

void ContentDialog::setPrimaryText(std::string text) {
    primaryText_ = std::move(text);
    if (laidOut()) updateVisuals();
}

void ContentDialog::setSecondaryText(std::string text) {
    secondaryText_ = std::move(text);
    if (laidOut()) updateVisuals();
}

Vec2i ContentDialog::onMeasure(const Vec2i& available) const {
    (void)available;
    return Vec2i{400, 208};
}

void ContentDialog::updateVisuals() {
    if (!laidOut()) return;
    const int32_t width = frameWidth();
    const int32_t height = frameHeight();

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, width, height, 8.0f, 8.0f};
        shape.base.fill = theme().overlay;
        shape.base.stroke = theme().strokeStrong;
        shape.base.strokeWidth = 1;
        shape.base.visible = true;
    });

    const int32_t textX = 24;
    const int32_t textMax = std::max(0, width - 48 - 48);
    const std::string shownTitle = ellipsizeText(canvas(), title_, 20, textMax);
    const std::string shownMessage = ellipsizeText(canvas(), message_, 14, textMax);
    const Rect titleSize = measureString(shownTitle, 20);
    const Rect messageSize = measureString(shownMessage, 14);
    canvas().updateShape(titleId_, [&](Shape& shape) {
        setShapeText(shape, shownTitle, 20, theme().textPrimary, textX,
                baselineForCenter(titleSize, 72) + 12);
        shape.base.visible = !title_.empty();
    });
    canvas().updateShape(messageId_, [&](Shape& shape) {
        setShapeText(shape, shownMessage, 14, theme().textSecondary, textX,
                baselineForCenter(titleSize, 72) + 12 + titleSize.h + 8 - messageSize.y);
        shape.base.visible = !message_.empty();
    });

    closeRect_ = Rect{width - 48, 14, 32, 32};
    canvas().updateShape(closeId_, [&](Shape& shape) {
        shape = makeXShape(closeRect_.x + 11, closeRect_.y + 11, 10,
                           theme().textSecondary, 1);
        shape.base.visible = true;
    });

    const int32_t buttonHeight = 32;
    const int32_t buttonY = height - 24 - buttonHeight;
    const int32_t primaryWidth = std::max(72, measureString(primaryText_, 14).w + 32);
    const int32_t primaryX = width - 24 - primaryWidth;
    primaryRect_ = Rect{primaryX, buttonY, primaryWidth, buttonHeight};

    const bool hasSecondary = !secondaryText_.empty();
    const int32_t secondaryWidth = hasSecondary
        ? std::max(72, measureString(secondaryText_, 14).w + 32) : 0;
    const int32_t secondaryX = primaryX - 8 - secondaryWidth;
    secondaryRect_ = Rect{secondaryX, buttonY, secondaryWidth, buttonHeight};

    const Color primaryFill = pressedButton_ == 1
        ? theme().accentPressed
        : (hoverButton_ == 1 ? theme().accentHover : theme().accent);
    canvas().updateShape(primaryBackgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{primaryRect_.x, primaryRect_.y,
                                primaryRect_.w, primaryRect_.h,
                                theme().radiusControl, theme().radiusControl};
        shape.base.fill = primaryFill;
        shape.base.visible = true;
    });

    const Rect primaryLabel = measureString(primaryText_, 14);
    canvas().updateShape(primaryTextId_, [&](Shape& shape) {
        setShapeText(shape, primaryText_, 14, theme().textOnAccent,
                primaryRect_.x + (primaryRect_.w - primaryLabel.w) / 2,
                primaryRect_.y + (primaryRect_.h - primaryLabel.h) / 2 - primaryLabel.y);
        shape.base.visible = true;
    });

    if (!hasSecondary) {
        canvas().updateShape(secondaryBackgroundId_, [](Shape& shape) { shape.base.visible = false; });
        canvas().updateShape(secondaryTextId_, [](Shape& shape) { shape.base.visible = false; });
    } else {
        const Color secondaryFill = pressedButton_ == 2
            ? theme().controlFillPressed
            : (hoverButton_ == 2 ? theme().controlFillHover : theme().controlFill);
        canvas().updateShape(secondaryBackgroundId_, [&](Shape& shape) {
            shape.data = RRectShape{secondaryRect_.x, secondaryRect_.y,
                                    secondaryRect_.w, secondaryRect_.h,
                                    theme().radiusControl, theme().radiusControl};
            shape.base.fill = secondaryFill;
            shape.base.stroke = theme().stroke;
            shape.base.strokeWidth = 1;
            shape.base.visible = true;
        });
        const Rect secondaryLabel = measureString(secondaryText_, 14);
        canvas().updateShape(secondaryTextId_, [&](Shape& shape) {
            setShapeText(shape, secondaryText_, 14, theme().textPrimary,
                    secondaryRect_.x + (secondaryRect_.w - secondaryLabel.w) / 2,
                    secondaryRect_.y + (secondaryRect_.h - secondaryLabel.h) / 2 - secondaryLabel.y);
            shape.base.visible = true;
        });
    }
}

int32_t ContentDialog::hitButton(int32_t x, int32_t y) const {
    if (closeRect_.contains(x, y)) return 3;
    if (primaryRect_.contains(x, y)) return 1;
    if (!secondaryText_.empty() && secondaryRect_.contains(x, y)) return 2;
    return 0;
}

bool ContentDialog::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseMove: {
        const int32_t button = hitButton(ev.x, ev.y);
        if (button != hoverButton_) {
            hoverButton_ = button;
            if (laidOut()) updateVisuals();
        }
        return true;
    }
    case EventType::MouseDown:
        pressedButton_ = hitButton(ev.x, ev.y);
        if (laidOut()) updateVisuals();
        return true;

    case EventType::MouseUp: {
        const int32_t button = pressedButton_;
        pressedButton_ = 0;
        if (laidOut()) updateVisuals();
        if (!clicked() || button == 0) return true;
        if (button == 1) {
            if (onPrimary) onPrimary();
            if (onClosed) onClosed();
        } else if (button == 2) {
            if (onSecondary) onSecondary();
        } else if (button == 3) {
            if (onClosed) onClosed();
        }
        return true;
    }

    case EventType::KeyDown:
        if (ev.key == kKeyEscape) {
            if (onClosed) onClosed();
            return true;
        }
        if (ev.key == kKeyEnter) {
            if (onPrimary) onPrimary();
            if (onClosed) onClosed();
            return true;
        }
        return false;

    default:
        return false;
    }
}

void ContentDialog::onStateChanged() {
    if (!hovered()) {
        hoverButton_ = 0;
        if (laidOut()) updateVisuals();
    }
}

// ===========================================================================
// MenuFlyout
// ===========================================================================
MenuFlyout::MenuFlyout() {
    Shape background = makeRRectShape(Rect{0, 0, 1, 1}, 8.0f,
                                      theme().overlay, theme().strokeStrong, 1);
    backgroundId_ = canvas().addShape(std::move(background));
    rows_.clear();
}

void MenuFlyout::addItem(std::string label, std::function<void()> action) {
    items_.push_back(Item{std::move(label), std::move(action), false});
    rebuild();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void MenuFlyout::addSeparator() {
    items_.push_back(Item{{}, {}, true});
    rebuild();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void MenuFlyout::clearItems() {
    items_.clear();
    rebuild();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

int32_t MenuFlyout::rowHeight(int32_t index) const {
    if (index < 0 || index >= static_cast<int32_t>(items_.size())) return 0;
    return items_[static_cast<size_t>(index)].separator ? 9 : 32;
}

int32_t MenuFlyout::rowTop(int32_t index) const {
    int32_t y = 8;
    for (int32_t i = 0; i < index && i < static_cast<int32_t>(items_.size()); ++i) {
        y += rowHeight(i);
    }
    return y;
}

void MenuFlyout::rebuild() {
    for (const RowGeometry& row : rows_) {
        if (row.background) canvas().removeShape(row.background);
        if (row.text) canvas().removeShape(row.text);
    }
    rows_.clear();
    rows_.reserve(items_.size());

    for (const Item& item : items_) {
        RowGeometry row;
        if (item.separator) {
            row.background = canvas().addShape(makeRectShape(Rect{0, 0, 1, 1},
                                                             theme().divider));
        } else {
            row.background = canvas().addShape(makeRRectShape(Rect{0, 0, 1, 1}, 4.0f,
                                                              Color::nullColor()));
            row.text = canvas().addShape(makeTextShape(item.label, 14,
                                                        theme().textPrimary));
        }
        rows_.push_back(row);
    }
}

Vec2i MenuFlyout::onMeasure(const Vec2i& available) const {
    (void)available;
    int32_t height = 16;
    for (const Item& item : items_) height += item.separator ? 9 : 32;
    return Vec2i{224, std::max(24, height)};
}

void MenuFlyout::updateVisuals() {
    if (!laidOut()) return;
    if (rows_.size() != items_.size()) rebuild();

    const int32_t width = frameWidth();
    const int32_t height = frameHeight();

    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, width, height, 8.0f, 8.0f};
        shape.base.fill = theme().overlay;
        shape.base.stroke = theme().strokeStrong;
        shape.base.strokeWidth = 1;
        shape.base.visible = true;
    });

    for (size_t i = 0; i < rows_.size(); ++i) {
        const int32_t top = rowTop(static_cast<int32_t>(i));
        const int32_t rowH = rowHeight(static_cast<int32_t>(i));
        if (items_[i].separator) {
            canvas().updateShape(rows_[i].background, [&](Shape& shape) {
                shape.data = RectShape{12, top + 4, std::max(0, width - 24), 1};
                shape.base.fill = theme().divider;
                shape.base.visible = true;
            });
        } else {
            const bool hover = static_cast<int32_t>(i) == hoverIndex_;
            canvas().updateShape(rows_[i].background, [&](Shape& shape) {
                shape.data = RRectShape{4, top, std::max(0, width - 8), rowH, 4.0f, 4.0f};
                shape.base.fill = hover ? theme().subtleHover : Color::nullColor();
                shape.base.visible = hover;
            });
            const Rect measured = measureString(items_[i].label, 14);
            canvas().updateShape(rows_[i].text, [&](Shape& shape) {
                setShapeText(shape, items_[i].label, 14, theme().textPrimary, 12,
                        top + (rowH - measured.h) / 2 - measured.y);
                shape.base.visible = true;
            });
        }
    }
}

int32_t MenuFlyout::rowIndexAt(int32_t localY) const {
    for (size_t i = 0; i < items_.size(); ++i) {
        const int32_t top = rowTop(static_cast<int32_t>(i));
        const int32_t rowH = rowHeight(static_cast<int32_t>(i));
        if (localY >= top && localY < top + rowH) {
            return items_[i].separator ? -1 : static_cast<int32_t>(i);
        }
    }
    return -1;
}

bool MenuFlyout::onEvent(const Event& ev) {
    if (!enabled()) return false;

    switch (ev.type) {
    case EventType::MouseMove: {
        const int32_t index = rowIndexAt(ev.y);
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
            const int32_t index = rowIndexAt(ev.y);
            if (index >= 0 && index < static_cast<int32_t>(items_.size())) {
                auto action = items_[static_cast<size_t>(index)].action;
                if (action) action();
                if (onDismiss) onDismiss();
            }
        }
        return true;

    case EventType::KeyDown: {
        if (ev.key == kKeyEscape) {
            if (onDismiss) onDismiss();
            return true;
        }
        if (ev.key == kKeyEnter) {
            if (hoverIndex_ >= 0 && hoverIndex_ < static_cast<int32_t>(items_.size())) {
                auto action = items_[static_cast<size_t>(hoverIndex_)].action;
                if (action) action();
                if (onDismiss) onDismiss();
            }
            return true;
        }
        if (ev.key == kKeyDown || ev.key == kKeyUp) {
            const int32_t count = static_cast<int32_t>(items_.size());
            if (count <= 0) return true;
            int32_t next = hoverIndex_;
            for (int32_t step = 0; step < count; ++step) {
                next += (ev.key == kKeyDown ? 1 : -1);
                if (next < 0) next = count - 1;
                if (next >= count) next = 0;
                if (!items_[static_cast<size_t>(next)].separator) {
                    hoverIndex_ = next;
                    if (laidOut()) updateVisuals();
                    break;
                }
            }
            return true;
        }
        return false;
    }

    default:
        return false;
    }
}

void MenuFlyout::onStateChanged() {
    if (!hovered() && hoverIndex_ != -1) {
        hoverIndex_ = -1;
        if (laidOut()) updateVisuals();
    }
}

// ===========================================================================
// MenuBar
// ===========================================================================
MenuBar::MenuBar() = default;

int32_t MenuBar::addItem(std::string label) {
    items_.push_back(std::move(label));
    rebuild();
    invalidateLayout();
    if (laidOut()) updateVisuals();
    return static_cast<int32_t>(items_.size()) - 1;
}

void MenuBar::setItems(std::vector<std::string> items) {
    items_ = std::move(items);
    hoverIndex_ = -1;
    rebuild();
    invalidateLayout();
    if (laidOut()) updateVisuals();
}

void MenuBar::rebuild() {
    for (uint32_t id : backgrounds_) canvas().removeShape(id);
    for (uint32_t id : texts_) canvas().removeShape(id);
    backgrounds_.clear();
    texts_.clear();
    itemX_.assign(items_.size(), 0);
    itemWidth_.assign(items_.size(), 0);

    for (const std::string& label : items_) {
        backgrounds_.push_back(canvas().addShape(
            makeRRectShape(Rect{0, 0, 1, 1}, 4.0f, Color::nullColor())));
        texts_.push_back(canvas().addShape(makeTextShape(label, 13,
                                                         theme().textPrimary)));
    }
}

Vec2i MenuBar::onMeasure(const Vec2i& available) const {
    (void)available;
    int32_t width = 8;
    for (const std::string& label : items_) width += measureString(label, 13).w + 24;
    return Vec2i{std::max(40, width), 32};
}

void MenuBar::updateVisuals() {
    if (!laidOut()) return;
    if (texts_.size() != items_.size()) rebuild();

    const int32_t height = frameHeight();
    int32_t x = 4;
    for (size_t i = 0; i < items_.size(); ++i) {
        const Rect measured = measureString(items_[i], 13);
        const int32_t width = measured.w + 24;
        itemX_[i] = x;
        itemWidth_[i] = width;
        const bool hover = static_cast<int32_t>(i) == hoverIndex_;

        canvas().updateShape(backgrounds_[i], [&](Shape& shape) {
            shape.data = RRectShape{x, 4, width, height - 8, 4.0f, 4.0f};
            shape.base.fill = hover ? theme().subtleHover : Color::nullColor();
            shape.base.visible = hover;
        });
        canvas().updateShape(texts_[i], [&](Shape& shape) {
            setShapeText(shape, items_[i], 13,
                    hover ? theme().textPrimary : theme().textSecondary,
                    x + 12, baselineForCenter(measured, height));
            shape.base.visible = true;
        });
        x += width;
    }
}

bool MenuBar::onEvent(const Event& ev) {
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
                    if (onItemClicked) onItemClicked(static_cast<int32_t>(i));
                    return true;
                }
            }
        }
        return true;

    default:
        return false;
    }
}

void MenuBar::onStateChanged() {
    if (!hovered() && hoverIndex_ != -1) {
        hoverIndex_ = -1;
        if (laidOut()) updateVisuals();
    }
}

// ===========================================================================
// CommandBar
// ===========================================================================
CommandBar::CommandBar() {
    style().direction = FlexDirection::Row;
    style().alignItems = AlignItems::Center;
    style().gap = 2;
    style().padding = Edges{4};
    style().clip = true;

    Shape background = makeRRectShape(Rect{0, 0, 1, 1}, 4.0f,
                                      withOpacity(theme().layer, 0.85f),
                                      theme().divider, 1);
    backgroundId_ = canvas().addShape(std::move(background));
}

Button* CommandBar::addCommand(std::string label, std::function<void()> action) {
    auto* button = emplaceChild<Button>(std::move(label), Button::Style::Subtle, 13);
    button->onClick = std::move(action);
    invalidateLayout();
    return button;
}

void CommandBar::updateVisuals() {
    if (!laidOut()) return;
    canvas().updateShape(backgroundId_, [&](Shape& shape) {
        shape.data = RRectShape{0, 0, frameWidth(), frameHeight(), 4.0f, 4.0f};
        shape.base.fill = withOpacity(theme().layer, 0.85f);
        shape.base.stroke = theme().divider;
        shape.base.strokeWidth = 1;
        shape.base.visible = true;
    });
}

} // namespace fluent
} // namespace modern_ui
