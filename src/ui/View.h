// ============================================================================
// View.h — 视图 / UI 根容器（排版驱动 + 合成 + 事件分发）
// ============================================================================
//
// View 是控件树的根，自身也是一个 ParentWidget：
//   - 持有画布宽度 / 高度，根 Widget 的子 Canvas 就是最终交给后端的 Canvas；
//   - layoutNow() 用 Flexbox 算出整棵树的 frame；
//   - syncImages() 按后序把"子 Canvas 被标脏"的控件镜像成 Group 写进父 Canvas，
//     未标脏的控件完全不处理（不做几何体逐项比较）；
//   - handleEvent() 命中测试 → 捕获 → 冒泡，并把鼠标坐标换算成控件局部坐标。
//
// 控件与渲染后端完全隔离：View 只向 Canvas 暴露 IRenderer，控件只操作子 Canvas。
// ============================================================================

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Canvas.h"
#include "Event.h"
#include "StyleEngine.h"
#include "Widget.h"

namespace modern_ui {

class View : public ParentWidget {
public:
    View(int32_t width, int32_t height);
    ~View() override;

    View(const View&) = delete;
    View& operator=(const View&) = delete;

    // ==================== 尺寸 / 后端 ====================
    void resize(int32_t width, int32_t height);
    int32_t width() const  { return canvas_.width(); }
    int32_t height() const { return canvas_.height(); }

    // 渲染后端只挂在根 Canvas 上；控件子 Canvas 永远不接触后端
    void setRenderer(IRenderer* renderer);
    IRenderer* renderer() const { return canvas_.renderer(); }

    // 文本度量注入（控件子 Canvas 无后端，必须由外部把度量能力下发）
    using TextMeasurer = Canvas::TextMetricProvider;
    void setTextMetricProvider(TextMeasurer provider);
    const TextMeasurer& textMetricProvider() const { return textProvider_; }

    // ==================== 样式 ====================
    // 全局 CSS 级联引擎：控件挂载 / id / class 变化时自动套用
    StyleEngine& styleEngine() { return styleEngine_; }
    const StyleEngine& styleEngine() const { return styleEngine_; }

    // 加载 .css 后立即刷新整棵控件树（样式 + 排版）
    bool loadStyleSheetText(const std::string& css, std::string* error = nullptr);
    bool loadStyleSheetFile(const std::string& path, std::string* error = nullptr);
    void refreshStyles();

    // ==================== 布局 / 绘制 ====================
    bool needsLayout() const { return layoutDirty_; }
    // 是否有任何"子 Canvas 内容 / 布局 / 待绘制区域"变化
    bool isDirty() const;
    bool needsDraw() const { return isDirty(); }

    void layoutNow();
    void layoutIfNeeded() { if (layoutDirty_) layoutNow(); }

    // 先排版（如需要）→ 合成脏控件 → 根 Canvas 绘制
    void drawFrame();

    // 动画帧：递归下发 onTick(dtSeconds)，随后按需重新排版。
    // 返回 true 表示本帧有可见变化（调用方据此决定是否 drawFrame）。
    bool tick(float dtSeconds);

    // ==================== 事件 ====================
    // 鼠标坐标以 View 左上角为原点；返回 true 表示事件被消费。
    // Resize 不在这里处理（由宿主调用 resize()，便于和窗口尺寸同步）。
    bool handleEvent(const Event& ev);

    Widget* focusedWidget() const { return focusedWidget_; }
    Widget* pressedWidget() const { return pressedWidget_; }
    Widget* hoveredWidget() const { return hoveredWidget_; }

    // ==================== 合成（内部 / Widget 生命周期使用）====================
    void attachSubtree(Widget& widget);
    void detachSubtree(Widget& widget);
    size_t mirrorCount() const;

    // 延迟移除控件：允许在 View::tick 的控件遍历过程中安全摘除控件
    // （例如 ComboBox 在其 onTick 中关闭根级浮出层）。实际移除在遍历结束后执行。
    void deferRemoveWidget(Widget* widget);

    // 脏子树标记：控件子 Canvas 标脏时由 Widget 回调（只标记祖先链，O(深度)）
    void noteSubtreeDirty(Widget& widget);

private:
    static void applyFrames(Widget& node, int32_t originX, int32_t originY);
    static void clearLayoutDirty(Widget& node);

    Widget* hitTestTree(Widget& node, const Vec2i& point) const;
    bool dispatchMouse(Widget* target, const Event& ev, const Vec2i& absPoint);
    void updateHover(Widget* target);
    void setPressed(Widget* target);
    void setFocus(Widget* target);
    Widget* nextFocusCandidate(Widget* from, int direction) const;
    static void collectFocusable(const Widget& node, std::vector<Widget*>& out);

    // === 合成：子 Canvas → 父 Canvas（逐几何体镜像 + 增量更新）===
    void syncImages();
    void syncWidgetPostOrder(Widget& widget);
    // 把 source 这个控件的子 Canvas 增量合成到它的父 Canvas
    void composeWidgetCanvas(Widget& widget);
    // 控件位置 / 裁剪变化：只更新父 Canvas 中镜像 Group 的 scrollOffset / clipRect
    void updatePlacement(Widget& widget);
    static Shape makeShapeWrapper(const Widget& widget, const Shape& sourceShape);
    static void rewriteWrapper(Shape& wrapper, const Widget& widget, const Shape& sourceShape);
    static void normalizeIds(Shape& shape);

    // 只给与视口相交的子树下发 onTick（离屏动画不做无用功）
    static void tickTree(Widget& node, float dtSeconds, const Rect& viewport);
    void processPendingRemovals();

    TextMeasurer textProvider_;
    StyleEngine styleEngine_;
    std::vector<Widget*> pendingRemovals_;
    Widget* hoveredWidget_ = nullptr;
    Widget* pressedWidget_ = nullptr;
    Widget* focusedWidget_ = nullptr;
};

} // namespace modern_ui
