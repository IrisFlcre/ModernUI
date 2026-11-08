#include "Panel.h"

namespace demo {

Panel::Panel(Color fill, Color stroke, float radius)
    : fill_(fill), stroke_(stroke), radius_(radius) {
    // 构造函数：初始化子 Canvas（基类完成）并创建几何体 —— 相对坐标的圆角矩形
    modern_ui::Shape shape;
    shape.base.fill = fill_;
    shape.base.stroke = stroke_;
    shape.base.strokeWidth = strokeWidth_;
    shape.base.visible = fill_.a > 0 || (stroke_.a > 0 && strokeWidth_ > 0);
    shape.data = modern_ui::RRectShape{0, 0, 1, 1, radius_, radius_};
    backgroundId_ = canvas().addShape(std::move(shape));
}

Panel::~Panel() {
    // 析构函数：删除几何体前的准备 —— 几何体留在子 Canvas 中由基类统一清理，
    // 这里只重置自身状态，避免背景尺寸停留在旧 frame 上。
    lastFrame_ = Rect::null();
}

void Panel::setFill(Color fill) {
    fill_ = fill;
    writeGeometry();
}

void Panel::setStroke(Color stroke, int32_t width) {
    stroke_ = stroke;
    strokeWidth_ = width;
    writeGeometry();
}

void Panel::setRadius(float radius) {
    radius_ = radius;
    writeGeometry();
}

void Panel::onLayout(const Rect& frame) {
    lastFrame_ = frame;
    // 背景铺满自身 frame（依然是控件自己的相对坐标，不关心屏幕绝对位置）
    writeGeometry();
}

void Panel::writeGeometry() {
    const Rect frame = lastFrame_;
    canvas().updateShape(backgroundId_, [&](modern_ui::Shape& shape) {
        shape.data = modern_ui::RRectShape{0, 0, frame.w, frame.h, radius_, radius_};
        shape.base.fill = fill_;
        shape.base.stroke = stroke_;
        shape.base.strokeWidth = strokeWidth_;
        shape.base.visible = fill_.a > 0 || (stroke_.a > 0 && strokeWidth_ > 0);
    });
}

} // namespace demo
