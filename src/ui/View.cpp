// ============================================================================
// View.cpp — 视图驱动 / 脏控件合成 / 事件分发
// ============================================================================

#include "View.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "Flexbox.h"

namespace modern_ui {
namespace {

constexpr int kKeyTab = 0x09;   // Win32 VK_TAB（Event.h 的 key 使用 VK 码）

Event localizeEvent(const Event& ev, const Widget& widget, const Vec2i& absPoint) {
    Event local = ev;
    local.x = absPoint.x - widget.absoluteFrame().x;
    local.y = absPoint.y - widget.absoluteFrame().y;
    return local;
}

bool pointInsideWidget(const Widget& widget, const Vec2i& absPoint) {
    const Rect& frame = widget.absoluteFrame();
    if (!frame.contains(absPoint)) return false;
    return widget.hitTest(Vec2i{absPoint.x - frame.x, absPoint.y - frame.y});
}

// 增量重写 wrapper 时沿用旧子树的 ID：结构未变时签名保持一致，
// Canvas 无需重新分配 ID / 重建 parentMap（合成热路径）。
void inheritShapeIds(Shape& target, const Shape& previous) {
    target.base.id = previous.base.id;
    if (target.type() != Shape::Type::Group || previous.type() != Shape::Type::Group) return;

    auto& dst = std::get<GroupData>(target.data);
    const auto& src = std::get<GroupData>(previous.data);
    if (dst.children.size() != src.children.size()) return;

    for (size_t i = 0; i < dst.children.size(); ++i) {
        inheritShapeIds(dst.children[i], src.children[i]);
    }
}

} // namespace

View::View(int32_t width, int32_t height) {
    canvas_.resize(width, height);
    frame_ = Rect{0, 0, canvas_.width(), canvas_.height()};
    absoluteFrame_ = frame_;
    layoutDirty_ = true;
}

View::~View() {
    // 在 View 自身成员（styleEngine_ / textProvider_ …）析构前先销毁控件树：
    // 控件析构会回调 detachSubtree，此时 View 仍然完整可用。
    children_.clear();
    canvas_.clear();
}

// ============================================================================
// 尺寸 / 后端 / 文本度量
// ============================================================================

void View::resize(int32_t width, int32_t height) {
    canvas_.resize(width, height);
    layoutDirty_ = true;
}

void View::setRenderer(IRenderer* renderer) {
    canvas_.setRenderer(renderer);
}

void View::setTextMetricProvider(TextMeasurer provider) {
    textProvider_ = std::move(provider);
    canvas_.setTextMetricProvider(textProvider_);

    std::vector<Widget*> stack;
    for (const auto& child : children_) stack.push_back(child.get());
    while (!stack.empty()) {
        Widget* widget = stack.back();
        stack.pop_back();
        widget->canvas_.setTextMetricProvider(textProvider_);
        for (const auto& child : widget->children_) stack.push_back(child.get());
    }

    // 文本度量变化会改变 Label 等控件的固有尺寸 → 需要重新排版
    invalidateLayout();
}

// ============================================================================
// 样式
// ============================================================================

bool View::loadStyleSheetText(const std::string& css, std::string* error) {
    if (!styleEngine_.loadText(css, error)) return false;
    refreshStyles();
    return true;
}

bool View::loadStyleSheetFile(const std::string& path, std::string* error) {
    if (!styleEngine_.loadFile(path, error)) return false;
    refreshStyles();
    return true;
}

void View::refreshStyles() {
    std::function<void(Widget&)> walk = [&](Widget& node) {
        node.refreshStyle();
        for (const auto& child : node.children_) walk(*child);
    };

    for (const auto& child : children_) walk(*child);
    // 样式可能改变尺寸（control-height / font-size 等）→ 重排一次
    invalidateLayout();
    canvas_.markDirtyAll();
}

// ============================================================================
// 布局
// ============================================================================

void View::layoutNow() {
    if (!layoutDirty_) return;

    frame_ = Rect{0, 0, canvas_.width(), canvas_.height()};
    absoluteFrame_ = frame_;

    Flexbox::layout(*this);
    onLayout(frame_);          // View 自身也有几何体（渲染 Canvas 的宿主）
    applyFrames(*this, 0, 0);
    clearLayoutDirty(*this);
    layoutDirty_ = false;
}

void View::applyFrames(Widget& node, int32_t originX, int32_t originY) {
    for (auto& childPtr : node.children_) {
        Widget& child = *childPtr;
        const Rect local = child.frame_;
        const Rect absolute{originX + local.x, originY + local.y, local.w, local.h};
        const bool moved = !(absolute == child.absoluteFrame_);
        const bool needsCallback = moved || child.layoutDirty_;

        child.absoluteFrame_ = absolute;
        if (needsCallback) {
            // 几何体在控件子 Canvas 内，按最终 frame 对齐
            child.onLayout(local);
        }
        if (moved && child.view_) {
            // 控件位置 / 尺寸变化只影响合成位置，不改变子 Canvas 里的几何体
            child.view_->updatePlacement(child);
        }
        child.layoutDirty_ = false;

        applyFrames(child, absolute.x, absolute.y);
    }
}

void View::clearLayoutDirty(Widget& node) {
    node.layoutDirty_ = false;
    for (auto& child : node.children_) clearLayoutDirty(*child);
}

// ============================================================================
// 脏状态 / 绘制
// ============================================================================

void View::noteSubtreeDirty(Widget& widget) {
    // 不变式：某个控件被标记 ⇒ 它的祖先链也都被标记。
    // 因此遇到已标记的祖先即可停止，标脏开销正比于"新增脏分支的深度"。
    if (widget.subtreeDirty_) return;
    for (Widget* node = &widget; node; node = node->parent_) {
        if (node->subtreeDirty_) break;
        node->subtreeDirty_ = true;
    }
}

bool View::isDirty() const {
    // O(1)：控件子 Canvas 的变化会通过回调把 subtreeDirty_ 传播到根，
    // 不再需要每帧递归遍历整棵控件树。
    return layoutDirty_ || subtreeDirty_ || canvas_.isDirty();
}

void View::drawFrame() {
    processPendingRemovals();
    layoutIfNeeded();
    syncImages();

    if (canvas_.needsRedraw() || canvas_.isDirty()) {
        canvas_.drawFrame();
    }
    // 根 Canvas 的跨帧标记在绘制完成后消费；帧内脏区域已由 drawFrame 处理
    canvas_.consumeDirty();
    contentDirty_ = false;
    subtreeDirty_ = false;
}

void View::tickTree(Widget& node, float dtSeconds, const Rect& viewport) {
    node.onTick(dtSeconds);
    for (auto& child : node.children_) {
        // 离屏子树跳过动画：滚动进入视口后自动继续，避免长列表里
        // 所有 spinner / shimmer 在后台持续标脏。
        if (!child->absoluteFrame().intersects(viewport)) continue;
        tickTree(*child, dtSeconds, viewport);
    }
}

bool View::tick(float dtSeconds) {
    if (dtSeconds <= 0.0f) return isDirty();
    const Rect viewport{0, 0, canvas_.width(), canvas_.height()};
    tickTree(*this, dtSeconds, viewport);
    processPendingRemovals();
    layoutIfNeeded();
    return isDirty();
}

void View::deferRemoveWidget(Widget* widget) {
    if (!widget) return;
    for (Widget* pending : pendingRemovals_) {
        if (pending == widget) return;
    }
    pendingRemovals_.push_back(widget);
}

void View::processPendingRemovals() {
    if (pendingRemovals_.empty()) return;

    std::vector<Widget*> pending;
    pending.swap(pendingRemovals_);

    for (Widget* widget : pending) {
        if (!widget || !widget->parent()) continue;
        if (auto* parent = dynamic_cast<ParentWidget*>(widget->parent())) {
            parent->removeChild(widget);
        }
    }
}

// ============================================================================
// 合成：子 Canvas → 父 Canvas（逐几何体镜像 + 增量更新）
// ============================================================================
//
// 每个控件子 Canvas 的第 i 个顶层几何体，在父 Canvas 中对应一个 wrapper Group：
//   wrapper.scrollOffset = 控件在父容器中的位置（frame 左上角）
//   wrapper.clipRect     = 控件 frame（父容器局部坐标）
//   wrapper.children     = 该几何体的副本（保持局部坐标 / 内部 Group 结构）
// 因此绝对坐标 = 几何体局部坐标 + wrapper.scrollOffset + 各级祖先偏移。
//
// isDirty 只表示"子 Canvas 里有脏几何体"。这里进入子 Canvas 后：
//   - structureDirty / 镜像数量不符 → 重建 wrapper 列表；
//   - 否则 forEachDirtyShape() 逐几何体更新对应 wrapper，未脏的几何体不碰。

void View::syncImages() {
    for (auto& child : children_) {
        if (child->view_ == this && child->subtreeDirty_) syncWidgetPostOrder(*child);
    }
}

void View::syncWidgetPostOrder(Widget& widget) {
    // 只进入有脏分支的子树：干净控件（以及整块干净的子树）完全不参与合成
    if (!widget.subtreeDirty_) return;

    for (auto& child : widget.children_) {
        if (child->view_ == this && child->subtreeDirty_) syncWidgetPostOrder(*child);
    }

    if (widget.canvas_.isDirty()) {
        composeWidgetCanvas(widget);
        widget.canvas_.consumeDirty();
    }
    widget.contentDirty_ = false;
    widget.subtreeDirty_ = false;
}

void View::composeWidgetCanvas(Widget& widget) {
    if (!widget.parent_) return;

    Canvas& source = widget.canvas_;
    Canvas& target = widget.parent_->canvas_;

    bool needRebuild = source.structureDirty() ||
                       widget.foldIds_.size() != source.shapeCount();

    if (!needRebuild) {
        for (uint32_t id : widget.foldIds_) {
            if (target.getShape(id) == nullptr) {
                needRebuild = true;
                break;
            }
        }
    }

    if (needRebuild) {
        for (uint32_t id : widget.foldIds_) target.removeShape(id);
        widget.foldIds_.clear();
        widget.foldIds_.reserve(source.shapeCount());

        for (const Shape& shape : source.shapes()) {
            widget.foldIds_.push_back(target.addShape(makeShapeWrapper(widget, shape)));
        }
        return;
    }

    // 逐几何体增量更新：未脏的几何体完全不动
    source.forEachDirtyShape([&](size_t index, const Shape& shape) {
        if (index >= widget.foldIds_.size()) return;
        const uint32_t id = widget.foldIds_[index];
        target.updateShape(id, [&widget, &shape](Shape& wrapper) {
            rewriteWrapper(wrapper, widget, shape);   // 保留 wrapper 根 id
        });
    });
}

void View::updatePlacement(Widget& widget) {
    if (!widget.parent_ || widget.foldIds_.empty()) return;

    Canvas& target = widget.parent_->canvas_;
    for (uint32_t id : widget.foldIds_) {
        target.updateShape(id, [&widget](Shape& wrapper) {
            auto* group = std::get_if<GroupData>(&wrapper.data);
            if (!group) return;

            // 位置：frame 左上角（不是中心）
            group->scrollOffset = Vec2i{widget.frame_.x, widget.frame_.y};
            wrapper.base.opacity = widget.cssOpacity();
            if (widget.style_.clip) {
                group->clipRect = widget.frame_;
            } else {
                group->clipRect.reset();
            }
        });
    }
}

Shape View::makeShapeWrapper(const Widget& widget, const Shape& sourceShape) {
    Shape wrapper;
    GroupData group;
    group.scrollOffset = Vec2i{widget.frame_.x, widget.frame_.y};
    if (widget.style_.clip) group.clipRect = widget.frame_;

    Shape copy = sourceShape;
    normalizeIds(copy);
    group.children.push_back(std::move(copy));
    wrapper.data = std::move(group);
    wrapper.base.opacity = widget.cssOpacity();
    return wrapper;
}

void View::rewriteWrapper(Shape& wrapper, const Widget& widget, const Shape& sourceShape) {
    const GroupData* previous = std::get_if<GroupData>(&wrapper.data);

    GroupData group;
    group.scrollOffset = Vec2i{widget.frame_.x, widget.frame_.y};
    if (widget.style_.clip) group.clipRect = widget.frame_;

    Shape copy = sourceShape;
    normalizeIds(copy);
    // 结构与上次一致时继承旧 ID，避免每帧重新分配 ID / 重建映射。
    if (previous && previous->children.size() == 1) {
        inheritShapeIds(copy, previous->children.front());
    }
    group.children.push_back(std::move(copy));
    wrapper.data = std::move(group);
    wrapper.base.opacity = widget.cssOpacity();
}

void View::normalizeIds(Shape& shape) {
    shape.base.id = 0;
    if (shape.type() == Shape::Type::Group) {
        auto& group = std::get<GroupData>(shape.data);
        for (auto& child : group.children) normalizeIds(child);
    }
}

size_t View::mirrorCount() const {
    size_t count = 0;
    std::vector<const Widget*> stack;
    for (const auto& child : children_) stack.push_back(child.get());
    while (!stack.empty()) {
        const Widget* widget = stack.back();
        stack.pop_back();
        count += widget->foldIds_.size();
        for (const auto& child : widget->children_) stack.push_back(child.get());
    }
    return count;
}

// ============================================================================
// 挂载 / 摘除
// ============================================================================

void View::attachSubtree(Widget& widget) {
    widget.view_ = this;
    widget.foldIds_.clear();
    // 新挂载 / 重新挂载：强制重建镜像（位置可能相同，但 wrapper 已不存在）
    widget.canvas_.markDirtyAll();
    widget.canvas_.setTextMetricProvider(textProvider_);
    // 挂载后立即套用默认样式 + 全局 CSS（.ui 的 id / class 在挂载前已设置）
    widget.refreshStyle();

    for (auto& child : widget.children_) attachSubtree(*child);
}

void View::detachSubtree(Widget& widget) {
    // 子控件可能正被 hover / pressed / focus 引用；摘除整棵子树前先解除这些指针，
    // 否则组合式控件在运行期重建子项时会留下悬空指针。
    auto insideSubtree = [&widget](Widget* target) {
        for (Widget* node = target; node; node = node->parent_) {
            if (node == &widget) return true;
        }
        return false;
    };
    if (hoveredWidget_ && insideSubtree(hoveredWidget_)) hoveredWidget_ = nullptr;
    if (pressedWidget_ && insideSubtree(pressedWidget_)) pressedWidget_ = nullptr;
    if (focusedWidget_ && insideSubtree(focusedWidget_)) focusedWidget_ = nullptr;

    // 后序：先摘子控件在各自父 Canvas 中的 wrapper，再摘自己的
    std::function<void(Widget&)> detachRecursive = [&](Widget& node) {
        for (auto& child : node.children_) detachRecursive(*child);

        if (node.parent_) {
            Canvas& target = node.parent_->canvas_;
            for (uint32_t id : node.foldIds_) target.removeShape(id);
        }
        node.foldIds_.clear();
        node.view_ = nullptr;
        node.contentDirty_ = false;
        node.subtreeDirty_ = false;
        // 解除 provider（可能捕获了外部 renderer）
        node.canvas_.setTextMetricProvider(Canvas::TextMetricProvider{});
    };

    detachRecursive(widget);
    widget.parent_ = nullptr;
}

// ============================================================================
// 命中测试 / 事件
// ============================================================================

Widget* View::hitTestTree(Widget& node, const Vec2i& point) const {
    // 从后往前 = 叠放顺序上层优先
    for (auto it = node.children_.rbegin(); it != node.children_.rend(); ++it) {
        Widget& child = **it;
        if (!pointInsideWidget(child, point)) continue;

        if (Widget* deeper = hitTestTree(child, point)) return deeper;
        return &child;
    }
    return nullptr;
}

bool View::dispatchMouse(Widget* target, const Event& ev, const Vec2i& absPoint) {
    for (Widget* node = target; node; node = node->parent_) {
        if (node->onEvent(localizeEvent(ev, *node, absPoint))) return true;
    }
    return false;
}

void View::updateHover(Widget* target) {
    if (hoveredWidget_ == target) return;
    if (hoveredWidget_) {
        hoveredWidget_->hovered_ = false;
        hoveredWidget_->onStateChanged();
    }
    hoveredWidget_ = target;
    if (hoveredWidget_) {
        hoveredWidget_->hovered_ = true;
        hoveredWidget_->onStateChanged();
    }
}

void View::setPressed(Widget* target) {
    if (pressedWidget_ == target) return;
    if (pressedWidget_) {
        pressedWidget_->pressed_ = false;
        pressedWidget_->onStateChanged();
    }
    pressedWidget_ = target;
    if (pressedWidget_) {
        pressedWidget_->pressed_ = true;
        pressedWidget_->onStateChanged();
    }
}

void View::setFocus(Widget* target) {
    if (focusedWidget_ == target) return;
    if (focusedWidget_) {
        focusedWidget_->focused_ = false;
        focusedWidget_->onStateChanged();
    }
    focusedWidget_ = target;
    if (focusedWidget_) {
        focusedWidget_->focused_ = true;
        focusedWidget_->onStateChanged();
    }
}

void View::collectFocusable(const Widget& node, std::vector<Widget*>& out) {
    for (const auto& child : node.children_) {
        if (child->acceptsFocus()) out.push_back(child.get());
        collectFocusable(*child, out);
    }
}

Widget* View::nextFocusCandidate(Widget* from, int direction) const {
    std::vector<Widget*> focusable;
    collectFocusable(*this, focusable);
    if (focusable.empty()) return nullptr;

    const auto it = std::find(focusable.begin(), focusable.end(), from);
    if (it == focusable.end()) return direction > 0 ? focusable.front() : focusable.back();

    const ptrdiff_t count = static_cast<ptrdiff_t>(focusable.size());
    const ptrdiff_t index = static_cast<ptrdiff_t>(std::distance(focusable.begin(), it));
    ptrdiff_t next = (index + direction) % count;
    if (next < 0) next += count;
    return focusable[static_cast<size_t>(next)];
}

bool View::handleEvent(const Event& ev) {
    layoutIfNeeded();

    const Vec2i point{ev.x, ev.y};

    switch (ev.type) {
    case EventType::MouseLeave:
        updateHover(nullptr);
        return false;

    case EventType::MouseMove: {
        Widget* target = hitTestTree(*this, point);
        updateHover(target);
        if (pressedWidget_) {
            // 按压捕获：拖动滑杆时鼠标移出控件仍持续收到 Move / Up
            return dispatchMouse(pressedWidget_, ev, point);
        }
        return target ? dispatchMouse(target, ev, point) : false;
    }

    case EventType::MouseDown: {
        Widget* target = hitTestTree(*this, point);
        updateHover(target);
        setPressed(target);

        if (target && target->acceptsFocus()) {
            setFocus(target);
        } else {
            setFocus(nullptr);
        }

        return target ? dispatchMouse(target, ev, point) : false;
    }

    case EventType::MouseUp: {
        Widget* target = pressedWidget_ ? pressedWidget_ : hitTestTree(*this, point);
        const bool inside = target && pointInsideWidget(*target, point);

        setPressed(nullptr);
        updateHover(hitTestTree(*this, point));

        if (!target) return false;
        if (inside) target->clicked_ = true;
        const bool consumed = dispatchMouse(target, ev, point);
        target->clicked_ = false;
        return consumed;
    }

    case EventType::MouseWheel: {
        Widget* target = hitTestTree(*this, point);
        return target ? dispatchMouse(target, ev, point) : false;
    }

    case EventType::KeyDown: {
        if (ev.key == kKeyTab) {
            const int direction = (ev.modifiers & ModifierShift) ? -1 : 1;
            setFocus(nextFocusCandidate(focusedWidget_, direction));
            return true;
        }
        if (!focusedWidget_) return false;
        Event local = ev;
        local.x = 0;
        local.y = 0;
        for (Widget* node = focusedWidget_; node; node = node->parent_) {
            if (node->onEvent(local)) return true;
        }
        return false;
    }

    case EventType::KeyUp:
    case EventType::Char: {
        if (!focusedWidget_) return false;
        Event local = ev;
        local.x = 0;
        local.y = 0;
        for (Widget* node = focusedWidget_; node; node = node->parent_) {
            if (node->onEvent(local)) return true;
        }
        return false;
    }

    default:
        return false;
    }
}

} // namespace modern_ui
