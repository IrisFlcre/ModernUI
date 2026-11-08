// ============================================================================
// Widget.h — 控件接口层（本模块不含任何具体控件）
// ============================================================================
//
// 设计要点：
//   1. 每个控件独占一个子 Canvas（局部坐标系，(0,0) = 控件左上角）。
//      控件只负责"自己子 Canvas 内的相对位置"，不感知自己在屏幕上的绝对位置。
//   2. 控件不能把几何体写进父 Canvas，也不能直接接触渲染后端；
//      组合（合成）由 View 统一负责：View 只复制"子 Canvas 被标脏"的控件。
//   3. 派生控件至少要实现：
//        - 构造函数：初始化子 Canvas 并创建几何体（相对坐标）
//        - 析构函数：删除几何体前的准备（回调解绑 / 几何体离开场景图）
//        - onEvent()：处理事件并修改自己子 Canvas 中的几何体
//      尺寸与位置由 Flexbox 决定，控件只需在 onMeasure()/onLayout() 中
//      声明内容固有尺寸、并根据最终 frame 调整内部几何体。
//
// 用法：
//   View view(800, 600);
//   auto* panel = view.emplaceChild<Panel>();     // ParentWidget::emplaceChild
//   panel->emplaceChild<Button>("OK");
// ============================================================================

#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "Canvas.h"
#include "Event.h"
#include "Layout.h"
#include "StyleSheet.h"

namespace modern_ui {

class View;
class Flexbox;

class Widget {
public:
    Widget();
    virtual ~Widget();

    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    // ==================== 标识（CSS / .ui 选择器）====================
    // 类型名默认由 RTTI 推导（如 "Button"）；.css 标签选择器与 .ui 标签都使用它。
    virtual const char* typeName() const;

    // 以下方法返回自身引用，便于链式书写：
    //   panel->emplaceChild<Button>("OK")->setId("ok").padding(8).size(96, 32);
    Widget& setId(std::string id);
    const std::string& id() const { return id_; }

    // 空格分隔的 class 列表
    Widget& setClassList(const std::string& classes);
    Widget& addClass(const std::string& name);
    Widget& removeClass(const std::string& name);
    bool hasClass(const std::string& name) const;
    const std::vector<std::string>& classes() const { return classes_; }

    // ==================== 样式表 ====================
    // 控件默认样式（派生控件覆盖；通常由主题令牌生成）
    virtual StyleSheet defaultStyleSheet() const;
    // 当前生效样式：默认 → .css 级联 → 手动覆盖 → 内联 style
    const StyleSheet& styleSheet() const { return styleSheet_; }
    // 重新计算样式（挂载 / id 或 class 变化 / CSS 重新加载 / 主题切换时调用）
    void refreshStyle();
    // 追加一次性手动覆盖（Origin 视为 Stylesheet，并在后续刷新中保留）
    Widget& applyStyleSheet(const StyleSheet& overlay);
    Widget& clearStyleSheetOverrides();
    // style="color: red; border-radius: 8px" 内联样式（最高优先级）
    Widget& setInlineStyle(std::string cssText);
    const std::string& inlineStyle() const { return inlineStyle_; }
    // 读取自定义 Entry（StyleSheet::others）
    std::string customStyle(const std::string& key, std::string fallback = {}) const;
    // 控件整体不透明度（CSS opacity；由 View 写入合成 Group，整块淡入淡出）
    float cssOpacity() const;
    // 样式表变化后的回调（派生控件在这里重新派生主题 / 重绘）
    virtual void onStyleChanged() {}

    // ==================== 树结构 ====================
    Widget* parent() const { return parent_; }
    const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }

    // ==================== 子 Canvas ====================
    // 控件几何体的唯一归属地；坐标为相对控件左上角的局部坐标。
    Canvas& canvas() { return canvas_; }
    const Canvas& canvas() const { return canvas_; }

    // ==================== 布局 ====================
    FlexStyle& style() { return style_; }
    const FlexStyle& style() const { return style_; }

    const Rect& frame() const { return frame_; }                  // 相对父容器左上角
    const Rect& absoluteFrame() const { return absoluteFrame_; }  // 相对视图（根）左上角
    int32_t width() const { return frame_.w; }
    int32_t height() const { return frame_.h; }

    // 内容固有尺寸（border-box，含 padding）。available = 父容器最多能给的外框尺寸。
    // 默认实现：按 style() 的容器配置把子控件预排一遍，返回内容包围盒。
    virtual Vec2i onMeasure(const Vec2i& available) const;
    // 排版结果确定后的回调：在这里把子 Canvas 里的几何体对准 frame（相对坐标）。
    virtual void  onLayout(const Rect& frame) { (void)frame; }
    // 事件处理：ev 的鼠标坐标已转成子 Canvas 局部坐标；返回 true 表示已消费（停止冒泡）。
    virtual bool  onEvent(const Event& ev) { (void)ev; return false; }
    // 动画驱动：dt 单位为秒，由 View::tick() 每帧下发；控件在这里推进补间 / Sprite 并标脏。
    virtual void  onTick(float dtSeconds) { (void)dtSeconds; }
    // 交互状态变化（hover / pressed / focused 由 View 维护后回调）
    virtual void  onStateChanged() {}
    // 命中测试：localPoint 为子 Canvas 局部坐标；默认 = 自身矩形
    virtual bool  hitTest(const Vec2i& localPoint) const;
    // 是否接受键盘焦点（被点击时 View 会自动聚焦 / 失焦）
    virtual bool  acceptsFocus() const { return false; }

    // ==================== 标记 ====================
    // 请求重新排版（尺寸 / 位置可能变化时调用；仅子 Canvas 内容变化无需调用）
    void invalidateLayout();
    bool layoutDirty() const { return layoutDirty_; }

    // ==================== 链式布局语法糖 ====================
    // 全部是 style() 的薄封装（变化时才 invalidateLayout），返回自身引用：
    //   auto* card = panel->emplaceChild<Card>();
    //   card->column().gap(12).padding(16).size(320, 0).grow(1);
    Widget& padding(int32_t all);
    Widget& padding(int32_t vertical, int32_t horizontal);
    Widget& padding(const Edges& edges);
    Widget& margin(int32_t all);
    Widget& margin(int32_t vertical, int32_t horizontal);
    Widget& margin(const Edges& edges);
    Widget& gap(int32_t value);
    Widget& grow(int32_t value = 1);
    Widget& shrink(int32_t value = 1);
    Widget& basis(int32_t pixels);
    Widget& size(int32_t width, int32_t height);
    Widget& minSize(int32_t width, int32_t height);
    Widget& maxSize(int32_t width, int32_t height);
    Widget& row();
    Widget& column();
    Widget& justify(JustifyContent value);
    Widget& align(AlignItems value);
    Widget& alignSelf(AlignSelf value);
    Widget& absolute(int32_t x, int32_t y);
    Widget& clip(bool on = true);

    // ============ 交互状态（由 View 维护，控件在 onEvent 里读取）============
    bool hovered() const { return hovered_; }
    bool pressed() const { return pressed_; }
    bool focused() const { return focused_; }
    // 仅在 MouseUp 且"按下与抬起都在本控件内"时为 true（点击语义）
    bool clicked() const { return clicked_; }
    View* view() const { return view_; }

private:
    friend class View;
    friend class ParentWidget;
    friend class Flexbox;

    View* rootView() const;

    // 把 .css 中出现的布局属性（padding / margin / gap / width / flex …）
    // 写进 FlexStyle；返回是否改动了布局。
    bool applyLayoutStyle();

    Widget* parent_ = nullptr;
    std::vector<std::unique_ptr<Widget>> children_;
    Canvas canvas_{1, 1};
    FlexStyle style_{};

    // ---- 样式 ----
    std::string id_;
    std::vector<std::string> classes_;
    std::string inlineStyle_;
    StyleSheet styleSheet_;
    StyleSheet manualStyle_;

    Rect frame_{};            // 相对父容器
    Rect absoluteFrame_{};    // 相对视图
    bool layoutDirty_ = true;

    bool hovered_ = false;
    bool pressed_ = false;
    bool focused_ = false;
    bool clicked_ = false;

    // ---- 合成状态（View 维护）----
    // foldIds_[i] = 本控件子 Canvas 第 i 个顶层几何体在父 Canvas 中的镜像 Group id。
    // 合成时按"逐几何体"增量更新，而不是整块复制。
    View*    view_ = nullptr;
    std::vector<uint32_t> foldIds_;

    // ---- 脏传播（Canvas 标脏 → 控件 → View）----
    // 让 View 不必每帧遍历整棵控件树来判断 / 合成脏控件：
    //   contentDirty_ 自己的子 Canvas 有变化
    //   subtreeDirty_ 自己或后代有变化（不变式：子控件被标记 ⇒ 祖先链已标记）
    bool contentDirty_ = false;
    bool subtreeDirty_ = false;

    void markContentDirty();   // 由子 Canvas 的标脏回调触发
};

// ============================================================================
// ParentWidget — 可容纳子控件的控件基类（容器控件的扩展点）
// ============================================================================
//
// 子控件的位置 / 尺寸完全由 Flexbox 排版决定并写在子控件的 frame() 上，
// 容器自身不需要（也不应该）手工摆放子控件。
class ParentWidget : public Widget {
public:
    // 添加子控件，所有权转移给父控件；返回裸指针
    Widget* addChild(std::unique_ptr<Widget> child);

    template <typename T, typename... Args>
    T* emplaceChild(Args&&... args) {
        static_assert(std::is_base_of<Widget, T>::value, "T 必须继承自 modern_ui::Widget");
        std::unique_ptr<T> child(new T(std::forward<Args>(args)...));
        T* raw = child.get();
        addChild(std::move(child));
        return raw;
    }

    // 摘除子控件，所有权交还调用者
    std::unique_ptr<Widget> removeChild(Widget* child);
    void clearChildren();
};

} // namespace modern_ui
